/**
 * @file sdk_cmd.h
 * @brief Command sub-frame encoder/decoder and request/response for the SDK.
 *
 * CMD sub-frame flag values:
 *   SDK_CMD_FLAG_WRITE      0x01  client -> server
 *   SDK_CMD_FLAG_WRITE_ACK  0x02  server -> client
 *   SDK_CMD_FLAG_READ       0x03  client -> server
 *   SDK_CMD_FLAG_READ_ACK   0x04  server -> client
 *
 * Wire layouts:
 *
 *  Write frame:
 *   [flag 1B][reserved 8B][data_len 8B][data NB]
 *
 *  Write-ack frame:
 *   [flag 1B][reserved 8B][ret_code 4B]
 *
 *  Read frame:
 *   [flag 1B][reserved 8B][read_len 8B]
 *
 *  Read-ack frame:
 *   [flag 1B][reserved 8B][ret_code 4B][data_len 8B][data NB]
 */

#ifndef SDK_CMD_H
#define SDK_CMD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * CMD frame flag values
 * ---------------------------------------------------------------------- */

#define SDK_CMD_FLAG_WRITE      UINT8_C(0x01)
#define SDK_CMD_FLAG_WRITE_ACK  UINT8_C(0x02)
#define SDK_CMD_FLAG_READ       UINT8_C(0x03)
#define SDK_CMD_FLAG_READ_ACK   UINT8_C(0x04)

#define SDK_CMD_RESERVED_SIZE   8

/* -------------------------------------------------------------------------
 * Decoded CMD frames
 * ---------------------------------------------------------------------- */

typedef struct sdk_cmd_req {
    uint64_t        data_len;
    const uint8_t  *data;       /* points into rx buffer – do not free */
} sdk_cmd_request_t;

typedef struct sdk_cmd_write {
    uint64_t        data_len;
    const uint8_t  *data;       /* points into rx buffer – do not free */
} sdk_cmd_write_t;

typedef struct sdk_cmd_write_ack {
    uint32_t        ret_code;   /* 0 = success */
} sdk_cmd_write_ack_t;

typedef struct sdk_cmd_read {
    uint64_t        read_len;
    const uint8_t  *data;
} sdk_cmd_read_t;

typedef struct sdk_cmd_read_ack {
    uint32_t        ret_code;
    uint64_t        data_len;
    const uint8_t  *data;       /* points into rx buffer – do not free */
} sdk_cmd_read_ack_t;

/* -------------------------------------------------------------------------
 * Encode helpers (write into caller-supplied buffer)
 * ---------------------------------------------------------------------- */

/**
 * sdk_cmd_encode_write - Serialise a write CMD sub-frame.
 *
 * @data:     Bytes to write.
 * @data_len: Number of bytes.
 * @out:      Output buffer.
 * @out_cap:  Capacity of @out.
 * @out_len:  Bytes written on success.
 *
 * Returns 0 on success, -1 if @out_cap is too small.
 */
int sdk_cmd_encode_write(const uint8_t *data, uint64_t data_len,
                         uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * sdk_cmd_encode_write_ack - Serialise a write-ack CMD sub-frame.
 */
int sdk_cmd_encode_write_ack(uint32_t ret_code,
                              uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * sdk_cmd_encode_read - Serialise a read CMD sub-frame.
 */
int sdk_cmd_encode_read(uint64_t read_len,
                        uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * sdk_cmd_encode_read_ack - Serialise a read-ack CMD sub-frame.
 */
int sdk_cmd_encode_read_ack(uint32_t ret_code,
                             const uint8_t *data, uint64_t data_len,
                             uint8_t *out, size_t out_cap, size_t *out_len);

int sdk_cmd_encode_ack(uint32_t ret_code, uint8_t cmd_flag,
		const uint8_t *data, uint64_t data_len,
		uint8_t *out, size_t out_cap, size_t *out_len);


/* -------------------------------------------------------------------------
 * Decode helpers
 * ---------------------------------------------------------------------- */

/**
 * sdk_cmd_decode_flag - Return the flag byte of a raw CMD payload, or 0xFF.
 */
uint8_t sdk_cmd_decode_flag(const uint8_t *payload, size_t payload_len);

int sdk_cmd_decode_write    (const uint8_t *p, size_t len, sdk_cmd_write_t     *out);
int sdk_cmd_decode_write_ack(const uint8_t *p, size_t len, sdk_cmd_write_ack_t *out);
int sdk_cmd_decode_read     (const uint8_t *p, size_t len, sdk_cmd_read_t      *out);
int sdk_cmd_decode_read_ack (const uint8_t *p, size_t len, sdk_cmd_read_ack_t  *out);
int sdk_cmd_decode_request(const uint8_t *p, size_t len, sdk_cmd_request_t *out);

void free_ack_buf(void *addr);
void *alloc_ack_buf(size_t data_len, size_t *ack_cap);

#ifdef __cplusplus
}
#endif

#endif /* SDK_CMD_H */
