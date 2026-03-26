/*
 * sdk_image.c - Image sub-frame parser and rotating queue.
 *
 * Cross-platform: uses pthreads on Linux/macOS and Win32 threads on Windows.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "log.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winsock2.h>
#else
#  include <pthread.h>
#  include <arpa/inet.h>
#endif

#ifndef _WIN32
#  include <time.h>
#endif
#include "sdk_image.h"

/* -------------------------------------------------------------------------
 * Portable get_u16
 * ---------------------------------------------------------------------- */

static uint16_t img_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* -------------------------------------------------------------------------
 * Image sub-frame wire offsets
 *  [Width 2B][Height 2B][BPP 1B][PixelFmt 1B][Reserved width*BPP*4 B]
 *  [PixelData width*height*BPP B]
 * ---------------------------------------------------------------------- */

#define IMG_OFF_WIDTH     0
#define IMG_OFF_HEIGHT    2
#define IMG_OFF_BPP       4
#define IMG_OFF_PIXFMT    5
#define IMG_HDR_FIXED     6    /* bytes before the reserved field */

/* -------------------------------------------------------------------------
 * Internal slot
 * ---------------------------------------------------------------------- */

typedef struct img_slot {
    sdk_image_buf_t  buf;
    int              in_use;   /* 1 = valid image stored          */
    int              ref;      /* user hold count                 */
} img_slot_t;

/* -------------------------------------------------------------------------
 * Module
 * ---------------------------------------------------------------------- */

struct sdk_image_module {
    img_slot_t  *slots;
    int          depth;

    /* write head (next slot to fill) */
    int          write_idx;
    /* read head (oldest valid slot) */
    int          read_idx;
    int          count;        /* number of valid frames in queue */

#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE  cond;
#else
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
#endif
};

/* -------------------------------------------------------------------------
 * Lock / unlock wrappers
 * ---------------------------------------------------------------------- */

static void mod_lock(sdk_image_module_t *m)
{
#ifdef _WIN32
    EnterCriticalSection(&m->lock);
#else
    pthread_mutex_lock(&m->lock);
#endif
}

static void mod_unlock(sdk_image_module_t *m)
{
#ifdef _WIN32
    LeaveCriticalSection(&m->lock);
#else
    pthread_mutex_unlock(&m->lock);
#endif
}

static void mod_signal(sdk_image_module_t *m)
{
#ifdef _WIN32
    WakeConditionVariable(&m->cond);
#else
    pthread_cond_signal(&m->cond);
#endif
}

/* Wait with timeout_ms (-1 = infinite, 0 = try once).
 * Returns 0 if signalled, -1 on timeout. */
static int mod_wait(sdk_image_module_t *m, int timeout_ms)
{
#ifdef _WIN32
    DWORD ms = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!SleepConditionVariableCS(&m->cond, &m->lock, ms))
        return -1;
    return 0;
#else
    if (timeout_ms < 0) {
        pthread_cond_wait(&m->cond, &m->lock);
        return 0;
    }
    if (timeout_ms == 0) {
        /* non-blocking: caller checks condition itself */
        return -1;
    }
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (pthread_cond_timedwait(&m->cond, &m->lock, &ts) != 0)
            return -1;
    }
    return 0;
#endif
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

sdk_image_module_t *sdk_image_module_create(int queue_depth)
{
    sdk_image_module_t *mod;
    int i;

    if (queue_depth <= 0)
        queue_depth = 4;

    mod = (sdk_image_module_t *)calloc(1, sizeof(*mod));
    if (!mod)
        return NULL;

    mod->slots = (img_slot_t *)calloc((size_t)queue_depth, sizeof(img_slot_t));
    if (!mod->slots) {
        free(mod);
        return NULL;
    }

    mod->depth     = queue_depth;
    mod->write_idx = 0;
    mod->read_idx  = 0;
    mod->count     = 0;

    for (i = 0; i < queue_depth; i++)
        mod->slots[i].buf._slot = i;

#ifdef _WIN32
    InitializeCriticalSection(&mod->lock);
    InitializeConditionVariable(&mod->cond);
#else
    pthread_mutex_init(&mod->lock, NULL);
    pthread_cond_init(&mod->cond, NULL);
#endif

    return mod;
}

void sdk_image_module_destroy(sdk_image_module_t *mod)
{
    int i;

    if (!mod)
        return;

    /* Free pixel data for all slots */
    for (i = 0; i < mod->depth; i++) {
        if (mod->slots[i].buf.pixel_data)
            free(mod->slots[i].buf.pixel_data);
    }

#ifdef _WIN32
    DeleteCriticalSection(&mod->lock);
#else
    pthread_mutex_destroy(&mod->lock);
    pthread_cond_destroy(&mod->cond);
#endif

    free(mod->slots);
    free(mod);
}

int sdk_image_push(sdk_image_module_t *mod, const uint8_t *payload,
                   size_t payload_len, uint64_t timestamp)
{
    uint16_t w, h;
    uint8_t  bpp, fmt;
    size_t   reserved_bytes, pixel_bytes, min_len;
    img_slot_t *slot;
    int         evict_idx;
    int         i;

    /* ---- Parse sub-frame header ---- */
    if (payload_len < IMG_HDR_FIXED)
        return -1;

    w   = img_get_u16(payload + IMG_OFF_WIDTH);
    h   = img_get_u16(payload + IMG_OFF_HEIGHT);
    bpp = payload[IMG_OFF_BPP];
    fmt = payload[IMG_OFF_PIXFMT];

    if (w == 0 || h == 0 || bpp == 0)
        return -1;

    reserved_bytes = (size_t)w * (size_t)bpp * 4;
    pixel_bytes    = (size_t)w * (size_t)h * (size_t)bpp;
    min_len        = IMG_HDR_FIXED + reserved_bytes + pixel_bytes;

    if (payload_len < min_len)
        return -1;

    mod_lock(mod);

    /* ---- Find a slot to write into ---- */
    if (mod->count < mod->depth) {
        /* There is a free slot at write_idx */
        evict_idx = mod->write_idx;
    } else {
        /* Queue is full: evict oldest non-held slot */
        evict_idx = -1;
        for (i = 0; i < mod->depth; i++) {
            int idx = (mod->read_idx + i) % mod->depth;
            if (mod->slots[idx].ref == 0) {
                evict_idx = idx;
                /* Advance read_idx past the evicted slot */
                if (idx == mod->read_idx)
                    mod->read_idx = (mod->read_idx + 1) % mod->depth;
                break;
            }
        }
        if (evict_idx < 0) {
            /* Every slot is user-held, drop this frame */
            mod_unlock(mod);
            pr_err("[image] all slots held, dropping frame\n");
            return 0;
        }
        mod->count--;  /* we're about to reuse one */
    }

    slot = &mod->slots[evict_idx];

    /* Free old pixel data if size differs */
    if (slot->buf.pixel_data && slot->buf.pixel_data_len != pixel_bytes) {
        free(slot->buf.pixel_data);
        slot->buf.pixel_data     = NULL;
        slot->buf.pixel_data_len = 0;
    }

    /* Allocate if needed */
    if (!slot->buf.pixel_data) {
        slot->buf.pixel_data = (uint8_t *)malloc(pixel_bytes);
        if (!slot->buf.pixel_data) {
            mod_unlock(mod);
            return -1;
        }
        slot->buf.pixel_data_len = pixel_bytes;
    }

    /* Copy pixel data (skip reserved bytes) */
    memcpy(slot->buf.pixel_data,
           payload + IMG_HDR_FIXED + reserved_bytes,
           pixel_bytes);

    slot->buf.width      = w;
    slot->buf.height     = h;
    slot->buf.bpp        = bpp;
    slot->buf.pixel_fmt  = (sdk_pixel_fmt_t)fmt;
    slot->buf.timestamp  = timestamp;
    slot->buf._slot      = evict_idx;
    slot->buf._owner     = mod;
    slot->in_use         = 1;
    slot->ref            = 0;

    mod->write_idx = (mod->write_idx + 1) % mod->depth;
    mod->count++;

    mod_signal(mod);
    mod_unlock(mod);
    return 0;
}

sdk_image_buf_t *sdk_image_dequeue(sdk_image_module_t *mod, int timeout_ms)
{
    img_slot_t *slot;

    mod_lock(mod);

    while (mod->count == 0) {
        if (mod_wait(mod, timeout_ms) != 0) {
            mod_unlock(mod);
            return NULL;
        }
    }

    slot = &mod->slots[mod->read_idx];
    slot->ref++;
    mod->read_idx = (mod->read_idx + 1) % mod->depth;
    mod->count--;

    mod_unlock(mod);
    return &slot->buf;
}

void sdk_image_release(sdk_image_buf_t *buf)
{
    sdk_image_module_t *mod;
    img_slot_t         *slot;

    if (!buf)
        return;

    mod  = (sdk_image_module_t *)buf->_owner;
    slot = &mod->slots[buf->_slot];

    mod_lock(mod);
    if (slot->ref > 0)
        slot->ref--;
    mod_unlock(mod);
}
