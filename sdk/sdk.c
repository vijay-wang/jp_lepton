/*
 * sdk.c - Client SDK main entry: lifecycle, send helpers, receive thread,
 *         command and file transfer logic.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

#include "../net/net.h"
#include "sdk.h"
#include "sdk_internal.h"
#include "sdk_frame.h"
#include "sdk_image.h"
#include "sdk_cmd.h"
#include "sdk_file.h"

/* -------------------------------------------------------------------------
 * Receive buffer
 *
 * We maintain a dynamic ring buffer so that partial frames accumulate
 * without extra copies. The design: read into the tail, consume from head.
 * ---------------------------------------------------------------------- */

#define RX_BUF_SIZE (4 * 1024 * 1024)   /* 4 MiB */

typedef struct rx_buf {
    uint8_t *data;
    size_t   head;
    size_t   tail;
    size_t   cap;
} rx_buf_t;

static int rxbuf_init(rx_buf_t *b)
{
    b->data = (uint8_t *)malloc(RX_BUF_SIZE);
    if (!b->data) return -1;
    b->head = 0;
    b->tail = 0;
    b->cap  = RX_BUF_SIZE;
    return 0;
}

static void rxbuf_free(rx_buf_t *b)
{
    free(b->data);
    b->data = NULL;
}

/* Bytes available to read */
static size_t rxbuf_used(const rx_buf_t *b)
{
    return b->tail - b->head;
}

/* Pointer to the contiguous unconsumed region */
static const uint8_t *rxbuf_ptr(const rx_buf_t *b)
{
    return b->data + b->head;
}

/* Advance head (consume bytes) */
static void rxbuf_consume(rx_buf_t *b, size_t n)
{
    b->head += n;
    /* Compact once head passes halfway to avoid realloc */
    if (b->head > b->cap / 2) {
        size_t used = b->tail - b->head;
        memmove(b->data, b->data + b->head, used);
        b->head = 0;
        b->tail = used;
    }
}

/* Append n bytes to tail; returns -1 if no room */
static int rxbuf_append(rx_buf_t *b, const uint8_t *src, size_t n)
{
    if (b->tail + n > b->cap) {
        /* Not enough space even after compaction – shouldn't happen
         * with a 4 MiB buffer and 64 KiB chunks. */
        return -1;
    }
    memcpy(b->data + b->tail, src, n);
    b->tail += n;
    return 0;
}

/* -------------------------------------------------------------------------
 * Pending request helpers
 * ---------------------------------------------------------------------- */

void sdk_pending_init(sdk_pending_req_t *r)
{
    r->state       = SDK_PENDING_NONE;
    r->rsp_payload = NULL;
    r->rsp_len     = 0;
#ifdef _WIN32
    InitializeCriticalSection(&r->lock);
    InitializeConditionVariable(&r->cond);
#else
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->cond, NULL);
#endif
}

void sdk_pending_destroy(sdk_pending_req_t *r)
{
    free(r->rsp_payload);
#ifdef _WIN32
    DeleteCriticalSection(&r->lock);
#else
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->cond);
#endif
}

void sdk_pending_arm(sdk_pending_req_t *r)
{
#ifdef _WIN32
    EnterCriticalSection(&r->lock);
#else
    pthread_mutex_lock(&r->lock);
#endif
    free(r->rsp_payload);
    r->rsp_payload = NULL;
    r->rsp_len     = 0;
    r->state       = SDK_PENDING_WAIT;
#ifdef _WIN32
    LeaveCriticalSection(&r->lock);
#else
    pthread_mutex_unlock(&r->lock);
#endif
}

void sdk_pending_post(sdk_pending_req_t *r, uint8_t *payload, size_t len)
{
#ifdef _WIN32
    EnterCriticalSection(&r->lock);
#else
    pthread_mutex_lock(&r->lock);
#endif
    /* Only deliver if a waiter is actually waiting; otherwise discard. */
    if (r->state == SDK_PENDING_WAIT) {
        free(r->rsp_payload);
        r->rsp_payload = payload;
        r->rsp_len     = len;
        r->state       = SDK_PENDING_DONE;
#ifdef _WIN32
        WakeConditionVariable(&r->cond);
#else
        pthread_cond_signal(&r->cond);
#endif
    } else {
        free(payload);  /* no waiter – discard the response */
    }
#ifdef _WIN32
    LeaveCriticalSection(&r->lock);
#else
    pthread_mutex_unlock(&r->lock);
#endif
}

void sdk_pending_post_err(sdk_pending_req_t *r)
{
#ifdef _WIN32
    EnterCriticalSection(&r->lock);
#else
    pthread_mutex_lock(&r->lock);
#endif
    r->state = SDK_PENDING_ERR;
#ifdef _WIN32
    WakeConditionVariable(&r->cond);
    LeaveCriticalSection(&r->lock);
#else
    pthread_cond_signal(&r->cond);
    pthread_mutex_unlock(&r->lock);
#endif
}

int sdk_pending_wait(sdk_pending_req_t *r, int timeout_ms,
                     uint8_t **payload, size_t *len)
{
    int ret = 0;

#ifdef _WIN32
    EnterCriticalSection(&r->lock);
    while (r->state == SDK_PENDING_WAIT) {
        DWORD ms = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
        if (!SleepConditionVariableCS(&r->cond, &r->lock, ms)) {
            ret = -1;
            break;
        }
    }
#else
    pthread_mutex_lock(&r->lock);
    if (timeout_ms < 0) {
        while (r->state == SDK_PENDING_WAIT)
            pthread_cond_wait(&r->cond, &r->lock);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        while (r->state == SDK_PENDING_WAIT) {
            if (pthread_cond_timedwait(&r->cond, &r->lock, &ts) != 0) {
                ret = -1;
                break;
            }
        }
    }
#endif

    if (ret == 0 && r->state == SDK_PENDING_DONE) {
        *payload       = r->rsp_payload;
        *len           = r->rsp_len;
        r->rsp_payload = NULL;   /* caller owns it now */
        r->rsp_len     = 0;
    } else {
        /* Timed out or error: discard any payload that may have arrived */
        free(r->rsp_payload);
        r->rsp_payload = NULL;
        r->rsp_len     = 0;
        ret = -1;
    }
    r->state = SDK_PENDING_NONE;

#ifdef _WIN32
    LeaveCriticalSection(&r->lock);
#else
    pthread_mutex_unlock(&r->lock);
#endif
    return ret;
}

/* -------------------------------------------------------------------------
 * Frame send helper
 * ---------------------------------------------------------------------- */

int sdk_send_frame(sdk_handle_t *h, sdk_frame_type_t type,
                   const uint8_t *payload, size_t payload_len)
{
    size_t   frame_size = payload_len + SDK_FRAME_OVERHEAD;
    uint8_t *frame_buf  = (uint8_t *)malloc(frame_size);
    size_t   frame_len  = 0;
    size_t   sent       = 0;
    net_err_t err;

    if (!frame_buf)
        return -1;

    if (sdk_frame_encode(type, sdk_timestamp_us(),
                         payload, (uint64_t)payload_len,
                         frame_buf, &frame_len) != 0) {
        free(frame_buf);
        return -1;
    }

    err = net_send(h->sock, frame_buf, frame_len, &sent);
    free(frame_buf);

    return (err == NET_OK && sent == frame_len) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Receive thread
 * ---------------------------------------------------------------------- */

static void rx_dispatch(sdk_handle_t *h, const sdk_frame_t *frame)
{
    switch (frame->type) {
    case SDK_FRAME_TYPE_IMAGE:
        sdk_image_push(h->image_mod,
                       frame->payload, (size_t)frame->length,
                       frame->timestamp);
        break;

    case SDK_FRAME_TYPE_CMD:
    case SDK_FRAME_TYPE_FILE: {
        /* Copy payload for the pending waiter */
        uint8_t *copy = (uint8_t *)malloc((size_t)frame->length);
        if (!copy) {
            sdk_pending_post_err(&h->pending);
            break;
        }
        memcpy(copy, frame->payload, (size_t)frame->length);
        sdk_pending_post(&h->pending, copy, (size_t)frame->length);
        break;
    }

    default:
        fprintf(stderr, "[sdk] Unknown frame type %d, dropping\n", frame->type);
        break;
    }
}

#define RECV_CHUNK 65536

#ifdef _WIN32
static DWORD WINAPI sdk_rx_thread(LPVOID arg)
#else
static void *sdk_rx_thread(void *arg)
#endif
{
    sdk_handle_t *h = (sdk_handle_t *)arg;
    rx_buf_t      rbuf;
    uint8_t       tmp[RECV_CHUNK];

    if (rxbuf_init(&rbuf) != 0) {
        fprintf(stderr, "[sdk] rx thread: failed to alloc recv buffer\n");
        h->rx_running = 0;
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    while (h->rx_running) {
        size_t    received = 0;
        net_err_t err;

        /* Read a chunk into the temp buffer, then append to rbuf */
        err = net_recv(h->sock, tmp, sizeof(tmp), &received);
        if (err == NET_ERR_CLOSED || err != NET_OK) {
            fprintf(stderr, "[sdk] rx: connection closed or error: %d\n", err);
            break;
        }

        if (rxbuf_append(&rbuf, tmp, received) != 0) {
            fprintf(stderr, "[sdk] rx: buffer overflow\n");
            break;
        }

        /* Consume as many complete frames as possible */
        for (;;) {
            size_t avail = rxbuf_used(&rbuf);
            size_t total;
            sdk_frame_t frame;

            if (avail < SDK_FRAME_OVERHEAD)
                break;

            /* Check for correct header first */
            const uint8_t *ptr = rxbuf_ptr(&rbuf);
            if (ptr[0] != 0x55 || ptr[1] != 0xAA) {
                /* Scan for next 0x55AA to resync */
                size_t i;
                for (i = 1; i < avail - 1; i++) {
                    if (ptr[i] == 0x55 && ptr[i+1] == 0xAA) {
                        rxbuf_consume(&rbuf, i);
                        break;
                    }
                }
                break;
            }

            total = sdk_frame_peek_total_len(ptr, avail);
            if (total == 0 || avail < total)
                break;   /* Wait for more data */

            if (sdk_frame_decode(ptr, total, &frame) == 0)
                rx_dispatch(h, &frame);
            else
                fprintf(stderr, "[sdk] rx: frame decode failed (CRC?)\n");

            rxbuf_consume(&rbuf, total);
        }
    }

    rxbuf_free(&rbuf);
    h->rx_running = 0;

    /* Wake any pending waiter so it doesn't block forever */
    sdk_pending_post_err(&h->pending);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

sdk_handle_t *sdk_create(const sdk_config_t *cfg)
{
    sdk_config_t   def = (sdk_config_t)SDK_CONFIG_DEFAULT;
    sdk_handle_t  *h;

    if (!cfg) cfg = &def;

    h = (sdk_handle_t *)calloc(1, sizeof(*h));
    if (!h) return NULL;

    h->cfg      = *cfg;
    h->connected = 0;
    h->rx_running = 0;

    h->image_mod = sdk_image_module_create(cfg->image_queue_depth);
    if (!h->image_mod) {
        free(h);
        return NULL;
    }

    sdk_pending_init(&h->pending);

#ifdef _WIN32
    InitializeCriticalSection(&h->state_lock);
#else
    pthread_mutex_init(&h->state_lock, NULL);
#endif

    if (net_init() != NET_OK) {
        sdk_image_module_destroy(h->image_mod);
        sdk_pending_destroy(&h->pending);
        free(h);
        return NULL;
    }

    return h;
}

void sdk_destroy(sdk_handle_t *h)
{
    if (!h) return;
    sdk_disconnect(h);
    sdk_image_module_destroy(h->image_mod);
    sdk_pending_destroy(&h->pending);
#ifdef _WIN32
    DeleteCriticalSection(&h->state_lock);
#else
    pthread_mutex_destroy(&h->state_lock);
#endif
    net_cleanup();
    free(h);
}

sdk_err_t sdk_connect(sdk_handle_t *h, const char *ip, uint16_t port)
{
    net_addr_t addr;
    net_err_t  err;

    if (!h || !ip) return SDK_ERR_PARAM;

    h->sock = net_socket_create();
    if (!h->sock) return SDK_ERR_NET;

    memset(&addr, 0, sizeof(addr));
    addr.port = port;
    strncpy(addr.ip_str, ip, sizeof(addr.ip_str) - 1);

    err = net_client_connect(h->sock, &addr);
    if (err != NET_OK) {
        net_socket_destroy(h->sock);
        h->sock = NULL;
        return SDK_ERR_NET;
    }

    h->connected  = 1;
    h->rx_running = 1;

#ifdef _WIN32
    h->rx_thread = CreateThread(NULL, 0, sdk_rx_thread, h, 0, NULL);
    if (!h->rx_thread) {
        h->rx_running = 0;
        net_socket_destroy(h->sock);
        h->sock = NULL;
        return SDK_ERR_NET;
    }
#else
    if (pthread_create(&h->rx_thread, NULL, sdk_rx_thread, h) != 0) {
        h->rx_running = 0;
        net_socket_destroy(h->sock);
        h->sock = NULL;
        return SDK_ERR_NET;
    }
#endif

    return SDK_OK;
}

void sdk_disconnect(sdk_handle_t *h)
{
    if (!h || !h->connected) return;

    h->rx_running = 0;
    net_socket_destroy(h->sock);
    h->sock = NULL;

#ifdef _WIN32
    if (h->rx_thread) {
        WaitForSingleObject(h->rx_thread, 3000);
        CloseHandle(h->rx_thread);
        h->rx_thread = NULL;
    }
#else
    pthread_join(h->rx_thread, NULL);
#endif

    h->connected = 0;
}

/* -------------------------------------------------------------------------
 * Image
 * ---------------------------------------------------------------------- */

sdk_image_buf_t *sdk_recv_image(sdk_handle_t *h, int timeout_ms)
{
    if (!h || !h->connected) return NULL;
    return sdk_image_dequeue(h->image_mod, timeout_ms);
}

void sdk_release_image(sdk_handle_t *h, sdk_image_buf_t *img)
{
    (void)h;
    sdk_image_release(img);
}

/* -------------------------------------------------------------------------
 * Command
 * ---------------------------------------------------------------------- */

void sdk_cmd_result_free(sdk_cmd_result_t *r)
{
    if (!r) return;
    free(r->data);
    r->data     = NULL;
    r->data_len = 0;
}

sdk_err_t sdk_send_cmd(sdk_handle_t *h, uint8_t flag, const uint8_t *data,
                              size_t data_len, sdk_cmd_result_t *result, int timeout_ms)
{
    uint8_t  tmp[1 + SDK_CMD_RESERVED_SIZE + 8 + 65536];
    size_t   enc_len = 0;
    uint8_t *rsp     = NULL;
    size_t   rsp_len = 0;
    sdk_cmd_ack_t ack;
    int tms;

    if (!h || !h->connected || !result) return SDK_ERR_PARAM;

    tms = timeout_ms > 0 ? timeout_ms : h->cfg.cmd_timeout_ms;

    if (sdk_cmd_encode_request(flag, data, (uint64_t)data_len,
                             tmp, sizeof(tmp), &enc_len) != 0)
        return SDK_ERR_PARAM;

    sdk_pending_arm(&h->pending);

    if (sdk_send_frame(h, SDK_FRAME_TYPE_CMD, tmp, enc_len) != 0)
        return SDK_ERR_NET;

    if (sdk_pending_wait(&h->pending, tms, &rsp, &rsp_len) != 0)
        return SDK_ERR_TIMEOUT;

    if (sdk_cmd_decode_ack(rsp, rsp_len, &ack) != 0) {
        free(rsp);
        return SDK_ERR_PROTO;
    }

    result->ret_code = ack.ret_code;
    if (ack.ret_code == 0 && ack.data_len > 0) {
        result->data = (uint8_t *)malloc((size_t)ack.data_len);
        if (result->data) {
            memcpy(result->data, ack.data, (size_t)ack.data_len);
            result->data_len = (size_t)ack.data_len;
        }
    } else {
        result->data     = NULL;
        result->data_len = 0;
    }

    free(rsp);
    return SDK_OK;
}

/* -------------------------------------------------------------------------
 * File send (upload to device)
 * ---------------------------------------------------------------------- */

sdk_err_t sdk_send_file(sdk_handle_t *h, const char *remote_path,
                         const uint8_t *data, size_t data_len,
                         int timeout_ms)
{
    uint8_t   tmp[1 + SDK_FILE_RESERVED_SIZE + SDK_FILE_PATH_SIZE + 8 + 2 + 4];
    size_t    enc_len;
    uint16_t  crc16;
    uint32_t  num_pkg;
    uint32_t  seq;
    uint8_t  *rsp     = NULL;
    size_t    rsp_len = 0;
    int       tms;
    int       retry;
    sdk_err_t result  = SDK_OK;

    /* per-chunk send buffer */
    uint8_t  *chunk_buf = NULL;
    size_t    chunk_cap;

    if (!h || !h->connected || !remote_path || (!data && data_len > 0))
        return SDK_ERR_PARAM;

    tms      = timeout_ms > 0 ? timeout_ms : h->cfg.file_timeout_ms;
    crc16    = sdk_crc16(data, data_len);
    num_pkg  = (uint32_t)((data_len + SDK_FILE_CHUNK_SIZE - 1) / SDK_FILE_CHUNK_SIZE);
    if (num_pkg == 0) num_pkg = 1;

    /* ---- WRITE_REQ ---- */
    if (sdk_file_encode_write_req(remote_path, (uint64_t)data_len,
                                  crc16, num_pkg,
                                  tmp, sizeof(tmp), &enc_len) != 0)
        return SDK_ERR_PARAM;

    sdk_pending_arm(&h->pending);

    if (sdk_send_frame(h, SDK_FRAME_TYPE_FILE, tmp, enc_len) != 0)
        return SDK_ERR_NET;

    if (sdk_pending_wait(&h->pending, tms, &rsp, &rsp_len) != 0)
        return SDK_ERR_TIMEOUT;

    {
        sdk_file_write_rsp_t wrsp;
        if (sdk_file_decode_write_rsp(rsp, rsp_len, &wrsp) != 0 ||
            wrsp.ret_code != 0) {
            free(rsp);
            return SDK_ERR_REMOTE;
        }
    }
    free(rsp); rsp = NULL;

    /* ---- seqN loop ---- */
    chunk_cap = 1 + SDK_FILE_RESERVED_SIZE + 8 + SDK_FILE_CHUNK_SIZE;
    chunk_buf = (uint8_t *)malloc(chunk_cap);
    if (!chunk_buf)
        return SDK_ERR_NOMEM;

    for (seq = 0; seq < num_pkg; seq++) {
        size_t offset     = (size_t)seq * SDK_FILE_CHUNK_SIZE;
        size_t chunk_size = data_len - offset;
        if (chunk_size > SDK_FILE_CHUNK_SIZE) chunk_size = SDK_FILE_CHUNK_SIZE;

        for (retry = 0; retry < SDK_FILE_MAX_RETRY; retry++) {
            sdk_file_write_ackn_t wack;

            if (sdk_file_encode_write_seqn(data + offset, (uint64_t)chunk_size,
                                           chunk_buf, chunk_cap, &enc_len) != 0) {
                result = SDK_ERR_PARAM;
                goto done;
            }

            sdk_pending_arm(&h->pending);

            if (sdk_send_frame(h, SDK_FRAME_TYPE_FILE, chunk_buf, enc_len) != 0) {
                result = SDK_ERR_NET;
                goto done;
            }

            if (sdk_pending_wait(&h->pending, tms, &rsp, &rsp_len) != 0)
                continue;  /* timeout – retry */

            if (sdk_file_decode_write_ackn(rsp, rsp_len, &wack) == 0 &&
                wack.ackn == seq) {
                free(rsp); rsp = NULL;
                break;     /* ack received */
            }
            free(rsp); rsp = NULL;
        }

        if (retry == SDK_FILE_MAX_RETRY) {
            fprintf(stderr, "[sdk] send_file: max retries reached at seq %u\n", seq);
            result = SDK_ERR_TIMEOUT;
            goto done;
        }
    }

done:
    free(chunk_buf);
    free(rsp);
    return result;
}

/* -------------------------------------------------------------------------
 * File receive (download from device)
 * ---------------------------------------------------------------------- */

sdk_err_t sdk_recv_file(sdk_handle_t *h, const char *remote_path,
                         uint8_t **out_data, size_t *out_len,
                         int timeout_ms)
{
    uint8_t  tmp[1 + SDK_FILE_RESERVED_SIZE + SDK_FILE_PATH_SIZE];
    size_t   enc_len;
    uint8_t *rsp     = NULL;
    size_t   rsp_len = 0;
    int      tms;
    uint8_t *file_buf = NULL;
    uint64_t file_total;
    uint32_t num_pkg, seq;
    uint16_t expected_crc;
    sdk_err_t result = SDK_OK;

    if (!h || !h->connected || !remote_path || !out_data || !out_len)
        return SDK_ERR_PARAM;

    tms = timeout_ms > 0 ? timeout_ms : h->cfg.file_timeout_ms;

    /* ---- READ_REQ ---- */
    if (sdk_file_encode_read_req(remote_path, tmp, sizeof(tmp), &enc_len) != 0)
        return SDK_ERR_PARAM;

    sdk_pending_arm(&h->pending);

    if (sdk_send_frame(h, SDK_FRAME_TYPE_FILE, tmp, enc_len) != 0)
        return SDK_ERR_NET;

    /* ---- Wait for seq0 ---- */
    if (sdk_pending_wait(&h->pending, tms, &rsp, &rsp_len) != 0)
        return SDK_ERR_TIMEOUT;

    {
        sdk_file_read_seq0_t seq0;
        if (sdk_file_decode_read_seq0(rsp, rsp_len, &seq0) != 0 ||
            seq0.ret_code != 0) {
            free(rsp);
            return SDK_ERR_REMOTE;
        }

        file_total    = seq0.file_len;
        expected_crc  = seq0.file_crc16;
        num_pkg       = seq0.num_packages;
    }
    free(rsp); rsp = NULL;

    file_buf = (uint8_t *)malloc((size_t)file_total ? (size_t)file_total : 1);
    if (!file_buf) return SDK_ERR_NOMEM;

    /* ---- Send ACK0 ---- */
    {
        uint8_t ack0[1 + SDK_FILE_RESERVED_SIZE + 4];
        if (sdk_file_encode_read_ack0(0, ack0, sizeof(ack0), &enc_len) != 0 ||
            sdk_send_frame(h, SDK_FRAME_TYPE_FILE, ack0, enc_len) != 0) {
            result = SDK_ERR_NET;
            goto done;
        }
    }

    /* ---- seqN loop ---- */
    for (seq = 0; seq < num_pkg; seq++) {
        size_t offset = (size_t)seq * SDK_FILE_CHUNK_SIZE;
        uint8_t ackn[1 + SDK_FILE_RESERVED_SIZE + 4];

        sdk_pending_arm(&h->pending);

        if (sdk_pending_wait(&h->pending, tms, &rsp, &rsp_len) != 0) {
            fprintf(stderr, "[sdk] recv_file: timeout waiting for seq %u\n", seq);
            result = SDK_ERR_TIMEOUT;
            goto done;
        }

        {
            sdk_file_read_seqn_t seqn;
            if (sdk_file_decode_read_seqn(rsp, rsp_len, &seqn) != 0 ||
                seqn.ret_code != 0) {
                free(rsp); rsp = NULL;
                result = SDK_ERR_PROTO;
                goto done;
            }

            if (offset + (size_t)seqn.data_len > (size_t)file_total) {
                free(rsp); rsp = NULL;
                result = SDK_ERR_PROTO;
                goto done;
            }

            memcpy(file_buf + offset, seqn.data, (size_t)seqn.data_len);
        }
        free(rsp); rsp = NULL;

        /* Send ackN */
        if (sdk_file_encode_read_ackn(seq, ackn, sizeof(ackn), &enc_len) != 0 ||
            sdk_send_frame(h, SDK_FRAME_TYPE_FILE, ackn, enc_len) != 0) {
            result = SDK_ERR_NET;
            goto done;
        }
    }

    /* CRC verify */
    {
        uint16_t got_crc = sdk_crc16(file_buf, (size_t)file_total);
        if (got_crc != expected_crc) {
            fprintf(stderr, "[sdk] recv_file: CRC mismatch\n");
            result = SDK_ERR_PROTO;
            goto done;
        }
    }

    *out_data = file_buf;
    *out_len  = (size_t)file_total;
    file_buf  = NULL;   /* ownership transferred */

done:
    free(file_buf);
    free(rsp);
    return result;
}

/* -------------------------------------------------------------------------
 * Utility
 * ---------------------------------------------------------------------- */

const char *sdk_strerror(sdk_err_t err)
{
    switch (err) {
    case SDK_OK:          return "Success";
    case SDK_ERR_PARAM:   return "Invalid parameter";
    case SDK_ERR_NOMEM:   return "Out of memory";
    case SDK_ERR_NET:     return "Network error";
    case SDK_ERR_TIMEOUT: return "Timed out";
    case SDK_ERR_PROTO:   return "Protocol error (frame/CRC)";
    case SDK_ERR_REMOTE:  return "Remote device returned error";
    case SDK_ERR_IO:      return "Local I/O error";
    case SDK_ERR_STATE:   return "Invalid SDK state";
    default:              return "Unknown error";
    }
}
