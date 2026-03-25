#ifndef _SHMQ_H
#define _SHMQ_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SHMQ_DEVICE_NAME        "shmq"
#define SHMQ_MAX_QUEUES         32
#define SHMQ_MAX_BUFFERS        64
#define SHMQ_MIN_BUFFERS        2
#define SHMQ_MAX_BUF_SIZE       (64 * 1024 * 1024)

/* Buffer states */
#define SHMQ_BUF_FREE           0   /* in free_list, writable by any producer	*/
#define SHMQ_BUF_WRITING        1   /* held by a producer via GET_FREE		*/
#define SHMQ_BUF_READY          2   /* in ready_list, readable by any consumer	*/
#define SHMQ_BUF_READING        3   /* held by a consumer via DEQUEUE		*/

/* poll() interest flags – passed to SHMQ_IOC_POLL_INTEREST */
#define SHMQ_POLL_READABLE      (1 << 0)  /* wake me when ready_list non-empty	*/
#define SHMQ_POLL_WRITABLE      (1 << 1)  /* wake me when free_list non-empty	*/

#define SHMQ_IOC_MAGIC          'Q'

/* Structures */

/*
 * Create a new queue.
 * buf_count and buf_size are set by caller.
 * queue_id is filled by kernel on return.
 */
struct shmq_create {
	__u32 buf_count;        /* 2 ~ SHMQ_MAX_BUFFERS, buffers in this queue */
	__u32 buf_size;         /* bytes per buffer, rounded up to page size   */
	char  name[32];         /* optional human-readable label               */
	/* output */
	__u32 queue_id;         /* assigned by kernel                          */
	__u32 _pad;
};

/*
 * Buffer descriptor – used by GET_FREE, ENQUEUE, DEQUEUE, RELEASE, PEEK.
 * Caller sets queue_id (and index for ENQUEUE/RELEASE).
 * Kernel fills the rest on output.
 */
struct shmq_buf_desc {
	__u32 queue_id;
	__u32 index;            /* buffer slot index within this queue		*/
	__u32 state;            /* SHMQ_BUF_* (informational)			*/
	__u32 data_size;        /* valid bytes – set by producer before ENQUEUE	*/
	__u32 flags;            /* user-defined per-frame flags			*/
	__u32 _pad;
	__u64 timestamp;        /* ns since boot, filled by kernel on ENQUEUE	*/
	__u64 sequence;         /* monotonic per-queue enqueue counter		*/
	__u64 offset;           /* byte offset of this buffer inside mmap pool	*/
};

/*
 * Timeout for blocking GET_FREE / DEQUEUE.
 * Each (fd × queue_id) pair has its own timeout value.
 * timeout_ms = 0  →  block forever
 * timeout_ms > 0  →  return -ETIMEDOUT after that many milliseconds
 */
struct shmq_timeout {
	__u32 queue_id;
	__u32 timeout_ms;
};

/*
 * Register poll interest for a (fd × queue_id) pair.
 * Must be called before using poll()/select()/epoll() on this fd.
 * interest: bitmask of SHMQ_POLL_READABLE | SHMQ_POLL_WRITABLE
 *
 * Example: a pure consumer sets SHMQ_POLL_READABLE.
 *          a pure producer sets SHMQ_POLL_WRITABLE.
 *          a process doing both sets both bits.
 */
struct shmq_poll_interest {
	__u32 queue_id;
	__u32 interest;         /* SHMQ_POLL_READABLE | SHMQ_POLL_WRITABLE     */
};

struct shmq_stats {
	__u32 queue_id;
	__u32 buf_count;
	__u32 buf_size;
	__u32 ready_count;
	__u32 free_count;
	__u32 active_fds;       /* number of fds subscribed to this queue      */
	__u32 _pad[2];
	__u64 produced;
	__u64 consumed;
	__u64 dropped;
	char  name[32];
};

struct shmq_queue_info {
	__u32 queue_id;
	__u32 buf_count;
	__u32 buf_size;
	__u32 ready_count;
	__u32 free_count;
	__u32 active_fds;
	char  name[32];
};

struct shmq_list {
	__u32                  count;   /* out: entries filled in infos[] */
	__u32                  total;   /* out: total active queues       */
	struct shmq_queue_info infos[SHMQ_MAX_QUEUES];
};

struct shmq_queue_id {
	__u32 queue_id;
};

struct shmq_mmap_info {
	__u32 queue_id;
	__u32 _pad;
	__u64 base_offset;      /* use as mmap() offset argument */
	__u64 total_size;       /* use as mmap() length argument */
};

/*
 * Look up a queue by name.
 * Any process – even one with no blood relation to the creator – can
 * call LOOKUP with a known name to obtain the queue_id and geometry.
 * Returns -ENOENT if no queue with that name exists.
 *
 * Typical usage pattern (like POSIX shm_open):
 *   creator  : SHMQ_IOC_CREATE  with name="apple"
 *   any other: SHMQ_IOC_LOOKUP  with name="apple"  → gets queue_id
 */
struct shmq_lookup {
	char  name[32];     /* in:  name to search                             */
	__u32 queue_id;     /* out: queue_id of the found queue                */
	__u32 buf_count;    /* out: number of buffers                          */
	__u32 buf_size;     /* out: bytes per buffer (page-aligned)            */
	__u32 _pad;
};

/* ioctl commands */
/*                                  nr   direction  type */
#define SHMQ_IOC_CREATE         _IOWR(SHMQ_IOC_MAGIC,  1, struct shmq_create)
#define SHMQ_IOC_DESTROY        _IOW (SHMQ_IOC_MAGIC,  2, struct shmq_queue_id)
#define SHMQ_IOC_SET_TIMEOUT    _IOW (SHMQ_IOC_MAGIC,  3, struct shmq_timeout)
#define SHMQ_IOC_POLL_INTEREST  _IOW (SHMQ_IOC_MAGIC,  4, struct shmq_poll_interest)
#define SHMQ_IOC_GET_FREE       _IOWR(SHMQ_IOC_MAGIC,  5, struct shmq_buf_desc)
#define SHMQ_IOC_ENQUEUE        _IOW (SHMQ_IOC_MAGIC,  6, struct shmq_buf_desc)
#define SHMQ_IOC_DEQUEUE        _IOWR(SHMQ_IOC_MAGIC,  7, struct shmq_buf_desc)
#define SHMQ_IOC_RELEASE        _IOW (SHMQ_IOC_MAGIC,  8, struct shmq_buf_desc)
#define SHMQ_IOC_PEEK           _IOWR(SHMQ_IOC_MAGIC,  9, struct shmq_buf_desc)
#define SHMQ_IOC_FLUSH          _IOW (SHMQ_IOC_MAGIC, 10, struct shmq_queue_id)
#define SHMQ_IOC_STATS          _IOWR(SHMQ_IOC_MAGIC, 11, struct shmq_stats)
#define SHMQ_IOC_LIST           _IOR (SHMQ_IOC_MAGIC, 12, struct shmq_list)
#define SHMQ_IOC_MMAP_INFO      _IOWR(SHMQ_IOC_MAGIC, 13, struct shmq_mmap_info)

/*
 * Select which queue to map in the next mmap() call.
 * Must be called (with the target queue_id) before every mmap().
 * mmap() offset must be 0.
 */
#define SHMQ_IOC_MMAP_SELECT    _IOW (SHMQ_IOC_MAGIC, 15, __u32)
#define SHMQ_IOC_LOOKUP         _IOWR(SHMQ_IOC_MAGIC, 14, struct shmq_lookup)

int shmq_open_dev(void);

int shmq_close_dev(int fd);

uint32_t shmq_create_queue(int fd, const char *name, uint32_t bufs, uint32_t buf_sz);

void shmq_set_timeout(int fd, uint32_t qid, uint32_t ms);

uint8_t *shmq_map_queue(int fd, uint32_t qid, size_t *out_sz);

int shmq_munmap_queue(void *addr, size_t length);

void shmq_destroy_queue(int fd, uint32_t qid);

#endif /* _SHMQ_H */
