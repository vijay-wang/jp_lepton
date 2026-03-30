/**
 * @file sdk_image.h
 * @brief Image sub-frame parser and rotating queue for the client SDK.
 *
 * Image sub-frame wire layout:
 *   [Width 2B][Height 2B][BPP 1B][PixelFmt 1B][Reserved width*BPP*4 B]
 *   [PixelData width*height*BPP B]
 *
 * The module owns a fixed-size rotating queue of image buffers.
 * When the queue is full, the oldest buffer that is NOT held by the user
 * is evicted and reused. Buffers held by the user (ref-count > 0) are
 * never evicted.
 *
 * Thread safety: all public functions are thread-safe.
 */

#ifndef SDK_IMAGE_H
#define SDK_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Pixel formats
 * ---------------------------------------------------------------------- */

typedef enum sdk_pixel_fmt {
    SDK_PIX_FMT_RGB  = 0x00,
    SDK_PIX_FMT_Y16  = 0x01,
    SDK_PIX_FMT_X16  = 0x02,
    SDK_PIX_FMT_Y8   = 0x03,
} sdk_pixel_fmt_t;

/* -------------------------------------------------------------------------
 * Image buffer
 *
 * Returned to the caller by sdk_image_dequeue(). Must be released with
 * sdk_image_release() when no longer needed.
 * ---------------------------------------------------------------------- */

typedef struct sdk_image_buf {
    uint16_t         width;
    uint16_t         height;
    uint8_t          bpp;           /* bytes per pixel              */
    sdk_pixel_fmt_t  pixel_fmt;
    uint8_t         *pixel_data;    /* width * height * bpp bytes   */
    size_t           pixel_data_len;
    uint64_t         timestamp;     /* from outer frame             */

    /* Private – do not touch */
    int              _slot;         /* index inside the ring        */
    void            *_owner;        /* pointer back to image module */
} sdk_image_buf_t;

/* -------------------------------------------------------------------------
 * Opaque module handle
 * ---------------------------------------------------------------------- */

typedef struct sdk_image_module sdk_image_module_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * sdk_image_module_create - Allocate and initialise the image module.
 *
 * @queue_depth: Maximum number of image buffers in the rotating queue.
 *               Recommended: 4–8.
 *
 * Returns: module handle on success, NULL on failure.
 */
sdk_image_module_t *sdk_image_module_create(int queue_depth);

/**
 * sdk_image_module_destroy - Tear down and free the image module.
 *
 * Blocks until all user-held buffers have been released.
 * @mod: Module handle; NULL is safe.
 */
void sdk_image_module_destroy(sdk_image_module_t *mod);

/**
 * sdk_image_push - Parse an image sub-frame and enqueue it.
 *
 * Called by the receive thread. @payload points into the rx buffer and
 * is valid only for the duration of this call; pixel data is copied out.
 *
 * @mod:         Module handle.
 * @payload:     Raw image sub-frame bytes.
 * @payload_len: Length of @payload.
 * @timestamp:   Timestamp from the outer frame.
 *
 * Returns 0 on success, -1 on parse error.
 */
int sdk_image_push(sdk_image_module_t *mod, const uint8_t *payload,
                   size_t payload_len, uint64_t timestamp);

/**
 * sdk_image_dequeue - Retrieve the oldest available image buffer.
 *
 * Blocks until an image is available or @timeout_ms elapses.
 * The returned buffer is ref-counted; call sdk_image_release() when done.
 *
 * @mod:        Module handle.
 * @timeout_ms: Milliseconds to wait; 0 = non-blocking; -1 = infinite.
 *
 * Returns: Pointer to image buffer, or NULL on timeout / error.
 */
sdk_image_buf_t *sdk_image_dequeue(sdk_image_module_t *mod, int timeout_ms);

/**
 * sdk_image_release - Release a buffer obtained from sdk_image_dequeue().
 *
 * @buf: Buffer to release; NULL is safe.
 */
void sdk_image_release(sdk_image_buf_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* SDK_IMAGE_H */
