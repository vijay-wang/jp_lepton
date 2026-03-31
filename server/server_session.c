// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * server_session.c - Per-client session: receive thread + handler threads
 * for image, cmd and file sub-protocols.
 *
 * Linux only. Follows Linux kernel coding style.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <libgen.h>
#include <sys/ioctl.h>

#include "net.h"
#include "sdk_frame.h"
#include "sdk_image.h"
#include "sdk_cmd.h"
#include "sdk_file.h"
#include "server_session.h"
#include "shmq.h"
#include "log.h"

/* -----------------------------------------------------------------------
 * Receive buffer (mirrors sdk.c rx_buf_t)
 * -------------------------------------------------------------------- */

#define RX_BUF_CAP (4 * 1024 * 1024)

struct rx_buf {
	uint8_t *data;
	size_t   head;
	size_t   tail;
};

static int rxbuf_init(struct rx_buf *b)
{
	b->data = malloc(RX_BUF_CAP);
	if (!b->data)
		return -1;
	b->head = 0;
	b->tail = 0;
	return 0;
}

static void rxbuf_free(struct rx_buf *b)
{
	free(b->data);
	b->data = NULL;
}

static size_t rxbuf_used(const struct rx_buf *b)
{
	return b->tail - b->head;
}

static const uint8_t *rxbuf_ptr(const struct rx_buf *b)
{
	return b->data + b->head;
}

static void rxbuf_consume(struct rx_buf *b, size_t n)
{
	b->head += n;
	if (b->head > RX_BUF_CAP / 2) {
		size_t used = b->tail - b->head;
		memmove(b->data, b->data + b->head, used);
		b->head = 0;
		b->tail = used;
	}
}

static int rxbuf_append(struct rx_buf *b, const uint8_t *src, size_t n)
{
	if (b->tail + n > RX_BUF_CAP)
		return -1;
	memcpy(b->data + b->tail, src, n);
	b->tail += n;
	return 0;
}

/* -----------------------------------------------------------------------
 * Session structure
 * -------------------------------------------------------------------- */

#define FRAME_QUEUE_DEPTH 16

struct queued_frame {
	uint8_t *payload;
	size_t   len;
	sdk_frame_type_t type;
	uint64_t timestamp;
};

struct frame_queue {
	struct queued_frame slots[FRAME_QUEUE_DEPTH];
	int             head;
	int             tail;
	int             count;
	pthread_mutex_t lock;
	pthread_cond_t  cond;
};

struct server_session {
	net_socket_t      *sock;
	atomic_int         running;

	struct frame_queue cmd_queue;
	struct frame_queue file_queue;

	pthread_t          img_thread;
	pthread_t          cmd_thread;
	pthread_t          file_thread;
	pthread_t          rx_thread;

	pthread_mutex_t    send_lock;

	/*
	 * rx-done notification: server_session_wait() waits on done_cond
	 * instead of calling pthread_join(rx_thread), which would conflict
	 * with the later pthread_join inside server_session_destroy().
	 */
	pthread_mutex_t    done_lock;
	pthread_cond_t     done_cond;
	int                rx_done;
};

/* -----------------------------------------------------------------------
 * Frame queue helpers
 * -------------------------------------------------------------------- */

static void fq_init(struct frame_queue *q)
{
	memset(q, 0, sizeof(*q));
	pthread_mutex_init(&q->lock, NULL);
	pthread_cond_init(&q->cond, NULL);
}

static void fq_destroy(struct frame_queue *q)
{
	int i;
	pthread_mutex_lock(&q->lock);
	for (i = 0; i < FRAME_QUEUE_DEPTH; i++)
		free(q->slots[i].payload);
	pthread_mutex_unlock(&q->lock);
	pthread_mutex_destroy(&q->lock);
	pthread_cond_destroy(&q->cond);
}

static void fq_push(struct frame_queue *q, sdk_frame_type_t type,
		    uint8_t *payload, size_t len, uint64_t ts)
{
	pthread_mutex_lock(&q->lock);
	if (q->count < FRAME_QUEUE_DEPTH) {
		q->slots[q->tail].type      = type;
		q->slots[q->tail].payload   = payload;
		q->slots[q->tail].len       = len;
		q->slots[q->tail].timestamp = ts;
		q->tail = (q->tail + 1) % FRAME_QUEUE_DEPTH;
		q->count++;
		pthread_cond_signal(&q->cond);
	} else {
		free(payload);
	}
	pthread_mutex_unlock(&q->lock);
}

static void fq_stop(struct frame_queue *q)
{
	pthread_mutex_lock(&q->lock);
	q->slots[q->tail].payload = NULL;
	q->slots[q->tail].len     = 0;
	q->tail = (q->tail + 1) % FRAME_QUEUE_DEPTH;
	q->count++;
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->lock);
}

static int fq_pop(struct frame_queue *q, struct queued_frame *out,
		  int timeout_ms)
{
	struct timespec ts;
	int ret = 0;

	pthread_mutex_lock(&q->lock);
	for (; q->count == 0; ) {
		if (timeout_ms < 0) {
			pthread_cond_wait(&q->cond, &q->lock);
		} else {
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec  += timeout_ms / 1000;
			ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
			if (ts.tv_nsec >= 1000000000L) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000L;
			}
			if (pthread_cond_timedwait(&q->cond, &q->lock, &ts)
			    != 0) {
				pthread_mutex_unlock(&q->lock);
				return -2;
			}
		}
	}
	*out = q->slots[q->head];
	q->slots[q->head].payload = NULL;  /* prevent fq_destroy double-free */
	q->head = (q->head + 1) % FRAME_QUEUE_DEPTH;
	q->count--;
	if (!out->payload)
		ret = -1;
	pthread_mutex_unlock(&q->lock);
	return ret;
}

/* -----------------------------------------------------------------------
 * Locked send helper
 * -------------------------------------------------------------------- */

static int sess_send_frame(struct server_session *sess,
			   sdk_frame_type_t type,
			   const uint8_t *payload, size_t payload_len)
{
	size_t   frame_size = payload_len + SDK_FRAME_OVERHEAD;
	uint8_t *buf        = malloc(frame_size);
	size_t   frame_len  = 0;
	size_t   sent       = 0;
	net_err_t err;

	if (!buf)
		return -1;

	if (sdk_frame_encode(type, sdk_timestamp_us(),
			     payload, (uint64_t)payload_len,
			     buf, &frame_len) != 0) {
		free(buf);
		return -1;
	}

	pthread_mutex_lock(&sess->send_lock);
	err = net_send(sess->sock, buf, frame_len, &sent);
	pthread_mutex_unlock(&sess->send_lock);

	free(buf);
	return (err == NET_OK && sent == frame_len) ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * Image sender thread  (~10 fps, 64x64 RGB fake frames)
 * -------------------------------------------------------------------- */

static void *img_thread_fn(void *arg)
{
	int ret;
	int shmq_fd;
	void *pool;
	struct shmq_buf_desc desc;
	struct shmq_lookup lk;
	struct shmq_queue_id qid;
	size_t pool_sz;
	struct server_session *sess = (struct server_session *)arg;

	shmq_fd = shmq_open_dev();
	if (shmq_fd <= 0) {
		pr_err("open shmq device failed\n");
		goto open_dev_failed;
	}

	strcpy(lk.name, "lpt_img_shmq");
	ret = ioctl(shmq_fd, SHMQ_IOC_LOOKUP, &lk);
	if (ret != 0) {
		pr_err("lookup %s failed\n", lk.name);
		goto lookup_failed;
	}

	desc.queue_id = lk.queue_id;
	qid.queue_id = lk.queue_id;
	pr_info("lookup %s qid = %d\n", lk.name, lk.queue_id);

	shmq_set_timeout(shmq_fd, lk.queue_id, 500);
	pool = shmq_map_queue(shmq_fd, lk.queue_id, &pool_sz);
	ret = shmq_flush(shmq_fd, &qid);
	if (ret != 0) {
		pr_err("shmq_flush %s failed\n", lk.name);
		goto flush_failed;
	}

	for (; atomic_load(&sess->running); ) {
		ret = ioctl(shmq_fd, SHMQ_IOC_DEQUEUE, &desc);
		if (ret != 0) {
			pr_err("SHMQ_IOC_DEQUEUE failed, errno:%d\n", errno);
			continue;
		}

		if (sess_send_frame(sess, SDK_FRAME_TYPE_IMAGE, pool + desc.offset, desc.data_size) != 0)
			pr_err("sess_send_frame failed\n");

		ret = ioctl(shmq_fd, SHMQ_IOC_RELEASE, &desc);
		if (ret != 0)
			pr_err("SHMQ_IOC_RELEASE failed, errno:%d\n", errno);
	}

	shmq_munmap_queue(pool, pool_sz);

flush_failed:
lookup_failed:
	shmq_close_dev(shmq_fd);
open_dev_failed:
	pr_info("exit img_thread_fn thread\n");
	return NULL;
}

/* -----------------------------------------------------------------------
 * Command handler thread
 * -------------------------------------------------------------------- */

static void *cmd_thread_fn(void *arg)
{
	struct server_session *sess = (struct server_session *)arg;
	int shmq_fd;
	void *in_pool;
	void *out_pool;
	struct shmq_buf_desc in_desc;
	struct shmq_buf_desc out_desc;
	struct shmq_lookup lk;
	size_t in_pool_sz;
	size_t out_pool_sz;
	void *ptr;
	int r;

	shmq_fd = shmq_open_dev();
	if (shmq_fd <= 0) {
		pr_err("open shmq device failed\n");
		goto open_dev_failed;
	}

	/* lookup and mmap lpt_cmd_in */
	strcpy(lk.name, "lpt_cmd_in");
	r = ioctl(shmq_fd, SHMQ_IOC_LOOKUP, &lk);
	if (r != 0) {
		pr_err("lookup %s failed\n", lk.name);
		goto lookup_failed;
	}

	in_desc.queue_id = lk.queue_id;
	shmq_set_timeout(shmq_fd, in_desc.queue_id, 500);
	in_pool = shmq_map_queue(shmq_fd, lk.queue_id, &in_pool_sz);
	pr_info("lookup lpt_cmd_in qid = %d\n", lk.queue_id);

	/* lookup and mmap lpt_cmd_out */
	strcpy(lk.name, "lpt_cmd_out");
	r = ioctl(shmq_fd, SHMQ_IOC_LOOKUP, &lk);
	if (r != 0) {
		pr_err("lookup %s failed\n", lk.name);
		goto lookup_failed;
	}

	out_desc.queue_id = lk.queue_id;
	shmq_set_timeout(shmq_fd, out_desc.queue_id, 500);
	out_pool = shmq_map_queue(shmq_fd, lk.queue_id, &out_pool_sz);
	pr_info("lookup lpt_cmd_out qid = %d\n", lk.queue_id);

	for (; atomic_load(&sess->running); ) {
		struct queued_frame qf;
		uint8_t flag;
		sdk_cmd_request_t request;
		r = fq_pop(&sess->cmd_queue, &qf, -1);
		uint8_t *ack;
		size_t ack_cap;
		size_t ack_len;

		if (r == -1)
			break;
		if (r == -2)
			continue;

		flag = sdk_cmd_decode_flag(qf.payload, qf.len);
		sdk_cmd_decode_request(qf.payload, qf.len, &request);

get_free:
		/* cmd request */
		r = ioctl(shmq_fd, SHMQ_IOC_GET_FREE, &in_desc);
		if (r < 0) {
			pr_err("SHMQ_IOC_GET_FREE failed, errno:%d\n", errno);
			goto get_free;
		}

		ptr = in_pool + in_desc.offset;
		memcpy(ptr, request.data, request.data_len);
		in_desc.data_size = request.data_len;

		ioctl(shmq_fd, SHMQ_IOC_ENQUEUE, &in_desc);

dequeue:
		/* cmd ack */
		r = ioctl(shmq_fd, SHMQ_IOC_DEQUEUE, &out_desc);
		if (r != 0) {
			pr_err("SHMQ_IOC_DEQUEUE failed, errno:%d\n", errno);
			goto dequeue;
		}

		ptr = out_pool + out_desc.offset;

		ack = alloc_ack_buf(out_desc.data_size, &ack_cap);
		sdk_cmd_encode_ack(0, flag, ptr, out_desc.data_size, ack, ack_cap, &ack_len);
		sess_send_frame(sess, SDK_FRAME_TYPE_CMD, ack, ack_len);
		free_ack_buf(ack);

		ioctl(shmq_fd, SHMQ_IOC_RELEASE, &out_desc);
		free(qf.payload);
	}

	shmq_munmap_queue(in_pool, in_pool_sz);

	shmq_munmap_queue(out_pool, out_pool_sz);

lookup_failed:
	shmq_close_dev(shmq_fd);
open_dev_failed:
	pr_info("exit cmd_thread_fn thread\n");
	return NULL;
}

static int dir_of_path_exists(char *path)
{
	if (!path || !*path)
		return 0;

	/*dirname() may modify the string, work on a copy */
	char buf[strlen(path) + 1];
	strcpy(buf, path);

	char *dir = dirname(buf);

	struct stat st;
	if (stat(dir, &st) != 0)
		return 0;

	return S_ISDIR(st.st_mode);
}

/* -----------------------------------------------------------------------
 * File handler thread
 * -------------------------------------------------------------------- */

static void *file_thread_fn(void *arg)
{
	struct server_session *sess = (struct server_session *)arg;

	for ( ; atomic_load(&sess->running); ) {
		struct queued_frame qf;
		uint8_t flag;
		int r = fq_pop(&sess->file_queue, &qf, -1);

		if (r == -1)
			break;

		if (r == -2)
			continue;

		flag = sdk_file_decode_flag(qf.payload, qf.len);

		/* ---- Client wants to READ a file from us ---- */
		if (flag == SDK_FILE_FLAG_READ_REQ) {
			sdk_file_read_req_t  req;
			uint8_t  rsp_buf[1 + SDK_FILE_RESERVED_SIZE
					 + 4 + 8 + 2 + 4];
			size_t   rsp_len  = 0;
			uint32_t num_pkg;
			uint64_t flen;
			uint16_t fcrc;
			uint8_t *fdata    = NULL;
			struct stat st;

			if (sdk_file_decode_read_req(qf.payload, qf.len, &req)
			    != 0) {
				free(qf.payload);
				continue;
			}

			pr_info("[server] File read request: %s\n", req.path);

			if (stat(req.path, &st) == 0 && S_ISREG(st.st_mode)) {
				int fd = open(req.path, O_RDONLY);
				flen   = (uint64_t)st.st_size;
				fdata  = malloc(flen ? (size_t)flen : 1);
				if (fdata && fd >= 0)
					(void)read(fd, fdata, (size_t)flen);

				if (fd >= 0)
					close(fd);
			} else {
				sdk_file_encode_read_seq0(1, 0, 0, 0,
							  rsp_buf, sizeof(rsp_buf),
							  &rsp_len);
				sess_send_frame(sess, SDK_FRAME_TYPE_FILE,
						rsp_buf, rsp_len);
			}

			if (!fdata) {
				free(qf.payload);
				continue;
			}

			fcrc    = sdk_crc16(fdata, (size_t)flen);
			num_pkg = (uint32_t)(
				(flen + SDK_FILE_CHUNK_SIZE - 1)
				/ SDK_FILE_CHUNK_SIZE);
			if (!num_pkg) num_pkg = 1;

			sdk_file_encode_read_seq0(0, flen, fcrc, num_pkg,
						  rsp_buf, sizeof(rsp_buf),
						  &rsp_len);
			sess_send_frame(sess, SDK_FRAME_TYPE_FILE,
					rsp_buf, rsp_len);

			/* Wait for ACK0 */
			{
				struct queued_frame ack0f;
				if (fq_pop(&sess->file_queue, &ack0f,
					   SDK_FILE_TIMEOUT_MS) != 0) {
					free(fdata);
					free(qf.payload);
					continue;
				}
				free(ack0f.payload);
			}

			/* Send seqN */
			{
				uint32_t seq;
				uint8_t *chunk_buf =
					malloc(1 + SDK_FILE_RESERVED_SIZE
					       + 4 + 8 + SDK_FILE_CHUNK_SIZE);
				if (!chunk_buf) {
					free(fdata);
					free(qf.payload);
					continue;
				}

				for (seq = 0; seq < num_pkg; seq++) {
					size_t off = (size_t)seq
						     * SDK_FILE_CHUNK_SIZE;
					size_t csz = (size_t)flen - off;
					size_t cl  = 0;
					int    retry;

					if (csz > SDK_FILE_CHUNK_SIZE)
						csz = SDK_FILE_CHUNK_SIZE;

					sdk_file_encode_read_seqn(
						0, fdata + off,
						(uint64_t)csz,
						chunk_buf,
						1 + SDK_FILE_RESERVED_SIZE
						+ 4 + 8 + csz, &cl);

					for (retry = 0;
					     retry < SDK_FILE_MAX_RETRY;
					     retry++) {
						struct queued_frame af;
						sdk_file_read_ackn_t ak;

						sess_send_frame(
							sess,
							SDK_FRAME_TYPE_FILE,
							chunk_buf, cl);

						if (fq_pop(&sess->file_queue,
							   &af,
							   SDK_FILE_TIMEOUT_MS)
						    != 0)
							continue;

						sdk_file_decode_read_ackn(
							af.payload,
							af.len, &ak);
						free(af.payload);

						if (ak.seqn_num == seq)
							break;
					}

					if (retry == SDK_FILE_MAX_RETRY) {
						pr_err("[server] read: ack "
							"timeout seq %u\n",
							seq);
						break;
					}
				}
				free(chunk_buf);
			}

			free(fdata);

		/* ---- Client wants to WRITE a file to us ---- */
		} else if (flag == SDK_FILE_FLAG_WRITE_REQ) {
			sdk_file_write_req_t wreq;
			uint8_t  rsp_buf[1 + SDK_FILE_RESERVED_SIZE + 4];
			size_t   rsp_len = 0;
			uint8_t *fbuf    = NULL;
			uint32_t seq;

			if (sdk_file_decode_write_req(qf.payload, qf.len,
						      &wreq) != 0) {
				free(qf.payload);
				continue;
			}

			pr_info("[server] File write: %s  %llu bytes\n",
			       wreq.path,
			       (unsigned long long)wreq.file_len);

			if (dir_of_path_exists(wreq.path))
				fbuf = malloc(wreq.file_len
					      ? (size_t)wreq.file_len : 1);

			if (!fbuf ) {
				sdk_file_encode_write_rsp(1, rsp_buf,
							  sizeof(rsp_buf),
							  &rsp_len);
				sess_send_frame(sess, SDK_FRAME_TYPE_FILE,
						rsp_buf, rsp_len);
				free(qf.payload);
				continue;
			}

			sdk_file_encode_write_rsp(0, rsp_buf,
						  sizeof(rsp_buf), &rsp_len);
			sess_send_frame(sess, SDK_FRAME_TYPE_FILE,
					rsp_buf, rsp_len);

			for (seq = 0; seq < wreq.num_packages; seq++) {
				struct queued_frame sf;
				sdk_file_write_seqn_t sn;
				uint8_t  ack_buf[1 + SDK_FILE_RESERVED_SIZE
						 + 4];
				size_t   ack_len = 0;

				if (fq_pop(&sess->file_queue, &sf,
					   SDK_FILE_TIMEOUT_MS) != 0) {
					pr_err( "[server] write: seqN "
						"timeout\n");
					break;
				}

				if (sdk_file_decode_write_seqn(sf.payload,
							       sf.len,
							       &sn) == 0) {
					size_t off = (size_t)seq
						     * SDK_FILE_CHUNK_SIZE;
					if (off + (size_t)sn.data_len
					    <= (size_t)wreq.file_len)
						memcpy(fbuf + off, sn.data,
						       (size_t)sn.data_len);
				}
				free(sf.payload);

				if (seq == (wreq.num_packages - 1)) {
					uint16_t got = sdk_crc16(fbuf,
							(size_t)wreq.file_len);
					if (got == wreq.crc16) {
						FILE *fp = fopen(wreq.path, "wb");
						if (fp) {
							fwrite(fbuf, 1,
									(size_t)wreq.file_len,
									fp);
							fclose(fp);
							pr_info("[server] Saved %s\n",
									wreq.path);
						}
					} else {
						pr_err( "[server] write CRC "
								"mismatch\n");
					}
				}

				sdk_file_encode_write_ackn(seq, ack_buf,
							   sizeof(ack_buf),
							   &ack_len);
				sess_send_frame(sess, SDK_FRAME_TYPE_FILE,
						ack_buf, ack_len);
			}

			free(fbuf);
		}

		free(qf.payload);
	}

	pr_info("exit file_thread_fn thread\n");
	return NULL;
}

/* -----------------------------------------------------------------------
 * Receive thread
 * -------------------------------------------------------------------- */

#define RX_CHUNK 65536

static void *rx_thread_fn(void *arg)
{
	struct server_session *sess = (struct server_session *)arg;
	struct rx_buf          rbuf;
	uint8_t                tmp[RX_CHUNK];

	if (rxbuf_init(&rbuf) != 0) {
		pr_err("[server] rx alloc failed\n");
		goto notify;
	}

	for (; atomic_load(&sess->running); ) {
		size_t    received = 0;
		net_err_t err;

		err = net_recv(sess->sock, tmp, sizeof(tmp), &received);
		if (err == NET_ERR_CLOSED || err != NET_OK)
			break;

		if (rxbuf_append(&rbuf, tmp, received) != 0) {
			pr_err("[server] rx buf overflow\n");
			break;
		}

		for (;;) {
			const uint8_t *ptr   = rxbuf_ptr(&rbuf);
			size_t         avail = rxbuf_used(&rbuf);
			size_t         total;
			sdk_frame_t    frame;
			uint8_t       *copy;

			if (avail < SDK_FRAME_OVERHEAD)
				break;

			if (ptr[0] != 0x55 || ptr[1] != 0xAA) {
				size_t i;
				for (i = 1; i < avail - 1; i++) {
					if (ptr[i] == 0x55 &&
					    ptr[i+1] == 0xAA) {
						rxbuf_consume(&rbuf, i);
						break;
					}
				}
				break;
			}

			total = sdk_frame_peek_total_len(ptr, avail);
			if (!total || avail < total)
				break;

			if (sdk_frame_decode(ptr, total, &frame) != 0) {
				rxbuf_consume(&rbuf, total);
				continue;
			}

			copy = malloc(frame.length
				      ? (size_t)frame.length : 1);
			if (copy) {
				memcpy(copy, frame.payload,
				       (size_t)frame.length);

				if (frame.type == SDK_FRAME_TYPE_CMD)
					fq_push(&sess->cmd_queue, frame.type,
						copy, (size_t)frame.length,
						frame.timestamp);
				else if (frame.type == SDK_FRAME_TYPE_FILE)
					fq_push(&sess->file_queue, frame.type,
						copy, (size_t)frame.length,
						frame.timestamp);
				else
					free(copy);
			}

			rxbuf_consume(&rbuf, total);
		}
	}

	rxbuf_free(&rbuf);

notify:
	atomic_store(&sess->running, 0);

	fq_stop(&sess->cmd_queue);
	fq_stop(&sess->file_queue);

	/* Signal server_session_wait() */
	pthread_mutex_lock(&sess->done_lock);
	sess->rx_done = 1;
	pthread_cond_signal(&sess->done_cond);
	pthread_mutex_unlock(&sess->done_lock);
	pr_info("exit rx_thread_fn thread\n");

	return NULL;
}

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

server_session_t *
server_session_create(net_socket_t *sock)
{
	server_session_t *sess = calloc(1, sizeof(*sess));

	if (!sess)
		return NULL;

	sess->sock   = sock;
	sess->rx_done = 0;
	atomic_store(&sess->running, 1);

	fq_init(&sess->cmd_queue);
	fq_init(&sess->file_queue);
	pthread_mutex_init(&sess->send_lock, NULL);
	pthread_mutex_init(&sess->done_lock, NULL);
	pthread_cond_init (&sess->done_cond, NULL);

	if (pthread_create(&sess->rx_thread,   NULL, rx_thread_fn,   sess) ||
	    pthread_create(&sess->img_thread,  NULL, img_thread_fn,  sess) ||
	    pthread_create(&sess->cmd_thread,  NULL, cmd_thread_fn,  sess) ||
	    pthread_create(&sess->file_thread, NULL, file_thread_fn, sess)) {
		server_session_destroy(sess);
		return NULL;
	}

	return sess;
}

void server_session_destroy(server_session_t *sess)
{
	if (!sess)
		return;

	atomic_store(&sess->running, 0);

	net_socket_destroy(sess->sock);
	sess->sock = NULL;

	fq_stop(&sess->cmd_queue);
	fq_stop(&sess->file_queue);

	pthread_join(sess->rx_thread,   NULL);
	pthread_join(sess->img_thread,  NULL);
	pthread_join(sess->cmd_thread,  NULL);
	pthread_join(sess->file_thread, NULL);

	fq_destroy(&sess->cmd_queue);
	fq_destroy(&sess->file_queue);
	pthread_mutex_destroy(&sess->send_lock);
	pthread_mutex_destroy(&sess->done_lock);
	pthread_cond_destroy (&sess->done_cond);

	free(sess);
}

/*
 * server_session_wait - Block until the rx thread signals completion.
 *
 * Uses the done_cond condvar instead of pthread_join so that
 * server_session_destroy() can still call pthread_join(rx_thread)
 * without hitting a double-join / double-free.
 */
void server_session_wait(server_session_t *sess)
{
	if (!sess)
		return;

	pthread_mutex_lock(&sess->done_lock);
	for (; !sess->rx_done; )
		pthread_cond_wait(&sess->done_cond, &sess->done_lock);
	pthread_mutex_unlock(&sess->done_lock);
}
