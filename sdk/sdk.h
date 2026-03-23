/**
 * @file sdk.h
 * @brief Client SDK public interface.
 *
 * Usage pattern:
 *
 *   sdk_handle_t *sdk = sdk_create(&cfg);
 *   sdk_connect(sdk, "192.168.1.1", 8080);
 *
 *   // Receive an image
 *   sdk_image_buf_t *img = sdk_recv_image(sdk, 1000);
 *   // ... use img->pixel_data ...
 *   sdk_release_image(sdk, img);
 *
 *   // Send a write command
 *   sdk_cmd_result_t res;
 *   sdk_send_cmd_write(sdk, data, data_len, &res, 2000);
 *
 *   // Send a file to the device
 *   sdk_send_file(sdk, "/remote/path/file.bin", local_buf, local_len, 30000);
 *
 *   // Receive a file from the device
 *   uint8_t *fbuf; size_t flen;
 *   sdk_recv_file(sdk, "/remote/path/data.bin", &fbuf, &flen, 30000);
 *   free(fbuf);
 *
 *   sdk_disconnect(sdk);
 *   sdk_destroy(sdk);
 */

#ifndef SDK_H
#define SDK_H

#include <stdint.h>
#include <stddef.h>
#include "sdk_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */

typedef enum sdk_err {
    SDK_OK              =  0,
    SDK_ERR_PARAM       = -1,
    SDK_ERR_NOMEM       = -2,
    SDK_ERR_NET         = -3,
    SDK_ERR_TIMEOUT     = -4,
    SDK_ERR_PROTO       = -5,   /* frame parse / CRC error              */
    SDK_ERR_REMOTE      = -6,   /* server returned non-zero ret_code    */
    SDK_ERR_IO          = -7,   /* local file I/O error                 */
    SDK_ERR_STATE       = -8,   /* wrong SDK state (e.g. not connected) */
} sdk_err_t;

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

typedef struct sdk_config {
    int image_queue_depth;  /* rotating image queue depth, default 4       */
    int cmd_timeout_ms;     /* default timeout for cmd round-trips, ms     */
    int file_timeout_ms;    /* default timeout for file transfers, ms      */
} sdk_config_t;

#define SDK_CONFIG_DEFAULT { .image_queue_depth = 4,   \
                             .cmd_timeout_ms    = 2000, \
                             .file_timeout_ms   = 30000 }

/* -------------------------------------------------------------------------
 * CMD result returned to the caller
 * ---------------------------------------------------------------------- */

typedef struct sdk_cmd_result {
    uint32_t  ret_code;         /* 0 = success                        */
    uint8_t  *data;             /* valid only for READ_ACK, heap-alloc*/
    size_t    data_len;         /* 0 for write ack                    */
} sdk_cmd_result_t;

/**
 * sdk_cmd_result_free - Free heap memory inside a sdk_cmd_result_t.
 */
void sdk_cmd_result_free(sdk_cmd_result_t *r);

/* -------------------------------------------------------------------------
 * Opaque SDK handle
 * ---------------------------------------------------------------------- */

typedef struct sdk_handle sdk_handle_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * sdk_create - Allocate and initialise the SDK.
 *
 * @cfg: Configuration; NULL uses SDK_CONFIG_DEFAULT.
 *
 * Returns: SDK handle on success, NULL on failure.
 */
sdk_handle_t *sdk_create(const sdk_config_t *cfg);

/**
 * sdk_destroy - Tear down and free the SDK handle.
 *
 * Disconnects if still connected. @h may be NULL.
 */
void sdk_destroy(sdk_handle_t *h);

/**
 * sdk_connect - Establish a TCP connection to the server.
 *
 * @h:    SDK handle.
 * @ip:   Server IP address string.
 * @port: Server port (host byte order).
 *
 * Returns: SDK_OK on success, negative sdk_err_t on failure.
 */
sdk_err_t sdk_connect(sdk_handle_t *h, const char *ip, uint16_t port);

/**
 * sdk_disconnect - Close the TCP connection and stop the receive thread.
 *
 * Safe to call if not connected.
 */
void sdk_disconnect(sdk_handle_t *h);

/* -------------------------------------------------------------------------
 * Image interface
 * ---------------------------------------------------------------------- */

/**
 * sdk_recv_image - Dequeue the oldest received image.
 *
 * Blocks up to @timeout_ms milliseconds.
 * The caller must call sdk_release_image() when done.
 *
 * Returns: Image buffer on success, NULL on timeout.
 */
sdk_image_buf_t *sdk_recv_image(sdk_handle_t *h, int timeout_ms);

/**
 * sdk_release_image - Release an image buffer back to the pool.
 */
void sdk_release_image(sdk_handle_t *h, sdk_image_buf_t *img);

/* -------------------------------------------------------------------------
 * Command interface
 * ---------------------------------------------------------------------- */

/**
 * sdk_send_cmd_write - Send a write command to the server.
 *
 * @h:          SDK handle.
 * @data:       Data to write.
 * @data_len:   Number of bytes.
 * @result:     Output; filled with the server's write-ack.
 * @timeout_ms: How long to wait for the ack (0 = use cfg default).
 *
 * Returns: SDK_OK on success.
 */
sdk_err_t sdk_send_cmd_write(sdk_handle_t *h, const uint8_t *data,
                              size_t data_len, sdk_cmd_result_t *result,
                              int timeout_ms);

/**
 * sdk_send_cmd_read - Send a read command to the server.
 *
 * @h:          SDK handle.
 * @read_len:   Number of bytes to read.
 * @result:     Output; filled with the server's read-ack (including data).
 *              Caller must call sdk_cmd_result_free() on result->data.
 * @timeout_ms: How long to wait for the ack.
 *
 * Returns: SDK_OK on success.
 */
sdk_err_t sdk_send_cmd_read(sdk_handle_t *h, uint64_t read_len,
                             sdk_cmd_result_t *result, int timeout_ms);

/* -------------------------------------------------------------------------
 * File interface
 * ---------------------------------------------------------------------- */

/**
 * sdk_send_file - Upload a local buffer to a path on the device.
 *
 * @h:           SDK handle.
 * @remote_path: Destination path on the device (max 255 chars).
 * @data:        File content.
 * @data_len:    Number of bytes.
 * @timeout_ms:  Per-packet timeout (0 = use cfg default).
 *
 * Returns: SDK_OK on success.
 */
sdk_err_t sdk_send_file(sdk_handle_t *h, const char *remote_path,
                         const uint8_t *data, size_t data_len,
                         int timeout_ms);

/**
 * sdk_recv_file - Download a file from a path on the device.
 *
 * On success, *out_data is a heap-allocated buffer of *out_len bytes.
 * The caller is responsible for free()ing *out_data.
 *
 * @h:           SDK handle.
 * @remote_path: Source path on the device.
 * @out_data:    Receives pointer to allocated file content.
 * @out_len:     Receives length of the content.
 * @timeout_ms:  Per-packet timeout (0 = use cfg default).
 *
 * Returns: SDK_OK on success.
 */
sdk_err_t sdk_recv_file(sdk_handle_t *h, const char *remote_path,
                         uint8_t **out_data, size_t *out_len,
                         int timeout_ms);

/**
 * sdk_strerror - Return a human-readable string for an sdk_err_t code.
 */
const char *sdk_strerror(sdk_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* SDK_H */
