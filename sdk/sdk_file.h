/**
 * @file sdk_file.h
 * @brief File transfer sub-frame codec and client-side transfer logic.
 *
 * File sub-frame flag values:
 *   SDK_FILE_FLAG_READ_REQ    0x01  client -> server: request to read a file
 *   SDK_FILE_FLAG_READ_SEQ0   0x02  server -> client: file metadata (seq 0)
 *   SDK_FILE_FLAG_READ_ACK0   0x03  client -> server: ack for seq 0
 *   SDK_FILE_FLAG_READ_SEQN   0x04  server -> client: file data chunk
 *   SDK_FILE_FLAG_READ_ACKN   0x05  client -> server: ack for seqN
 *
 *   SDK_FILE_FLAG_WRITE_REQ   0x10  client -> server: request to write a file
 *   SDK_FILE_FLAG_WRITE_RSP   0x11  server -> client: write request response
 *   SDK_FILE_FLAG_WRITE_SEQN  0x12  client -> server: file data chunk
 *   SDK_FILE_FLAG_WRITE_ACKN  0x13  server -> client: ack for seqN
 *
 * All integers are big-endian on the wire.
 */

#ifndef SDK_FILE_H
#define SDK_FILE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define SDK_FILE_FLAG_READ_REQ    UINT8_C(0x01)
#define SDK_FILE_FLAG_READ_SEQ0   UINT8_C(0x02)
#define SDK_FILE_FLAG_READ_ACK0   UINT8_C(0x03)
#define SDK_FILE_FLAG_READ_SEQN   UINT8_C(0x04)
#define SDK_FILE_FLAG_READ_ACKN   UINT8_C(0x05)

#define SDK_FILE_FLAG_WRITE_REQ   UINT8_C(0x10)
#define SDK_FILE_FLAG_WRITE_RSP   UINT8_C(0x11)
#define SDK_FILE_FLAG_WRITE_SEQN  UINT8_C(0x12)
#define SDK_FILE_FLAG_WRITE_ACKN  UINT8_C(0x13)

#define SDK_FILE_PATH_SIZE        256
#define SDK_FILE_RESERVED_SIZE    8
#define SDK_FILE_CHUNK_SIZE       (64 * 1024)   /* 64 KiB per seqN packet */
#define SDK_FILE_MAX_RETRY        3
#define SDK_FILE_TIMEOUT_MS       5000

/* -------------------------------------------------------------------------
 * Decoded sub-frame structs (pointers into rx buffer – do not free)
 * ---------------------------------------------------------------------- */

typedef struct sdk_file_read_req {
    char path[SDK_FILE_PATH_SIZE];
} sdk_file_read_req_t;

typedef struct sdk_file_read_seq0 {
    uint32_t  ret_code;
    uint64_t  file_len;
    uint16_t  file_crc16;
    uint32_t  num_packages;
} sdk_file_read_seq0_t;

typedef struct sdk_file_read_ack0 {
    uint32_t  seq0_num;         /* always 0 */
} sdk_file_read_ack0_t;

typedef struct sdk_file_read_seqn {
    uint32_t        ret_code;
    uint64_t        data_len;
    const uint8_t  *data;       /* points into rx buffer */
} sdk_file_read_seqn_t;

typedef struct sdk_file_read_ackn {
    uint32_t  seqn_num;
} sdk_file_read_ackn_t;

typedef struct sdk_file_write_req {
    char      path[SDK_FILE_PATH_SIZE];
    uint64_t  file_len;
    uint16_t  crc16;
    uint32_t  num_packages;
} sdk_file_write_req_t;

typedef struct sdk_file_write_rsp {
    uint32_t  ret_code;         /* 0 = proceed */
} sdk_file_write_rsp_t;

typedef struct sdk_file_write_seqn {
    uint64_t        data_len;
    const uint8_t  *data;
} sdk_file_write_seqn_t;

typedef struct sdk_file_write_ackn {
    uint32_t  ackn;
} sdk_file_write_ackn_t;

/* -------------------------------------------------------------------------
 * Encode helpers
 * ---------------------------------------------------------------------- */

int sdk_file_encode_read_req  (const char *path,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_read_seq0 (uint32_t ret, uint64_t file_len,
                                uint16_t crc, uint32_t num_pkg,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_read_ack0 (uint32_t seq0,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_read_seqn (uint32_t ret, const uint8_t *data,
                                uint64_t data_len,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_read_ackn (uint32_t seqn,
                                uint8_t *out, size_t cap, size_t *out_len);

int sdk_file_encode_write_req (const char *path, uint64_t file_len,
                                uint16_t crc, uint32_t num_pkg,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_write_rsp (uint32_t ret_code,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_write_seqn(const uint8_t *data, uint64_t data_len,
                                uint8_t *out, size_t cap, size_t *out_len);
int sdk_file_encode_write_ackn(uint32_t ackn,
                                uint8_t *out, size_t cap, size_t *out_len);

/* -------------------------------------------------------------------------
 * Decode helpers
 * ---------------------------------------------------------------------- */

uint8_t sdk_file_decode_flag(const uint8_t *payload, size_t len);

int sdk_file_decode_read_req  (const uint8_t *p, size_t len, sdk_file_read_req_t   *out);
int sdk_file_decode_read_seq0 (const uint8_t *p, size_t len, sdk_file_read_seq0_t  *out);
int sdk_file_decode_read_ack0 (const uint8_t *p, size_t len, sdk_file_read_ack0_t  *out);
int sdk_file_decode_read_seqn (const uint8_t *p, size_t len, sdk_file_read_seqn_t  *out);
int sdk_file_decode_read_ackn (const uint8_t *p, size_t len, sdk_file_read_ackn_t  *out);
int sdk_file_decode_write_req (const uint8_t *p, size_t len, sdk_file_write_req_t  *out);
int sdk_file_decode_write_rsp (const uint8_t *p, size_t len, sdk_file_write_rsp_t  *out);
int sdk_file_decode_write_seqn(const uint8_t *p, size_t len, sdk_file_write_seqn_t *out);
int sdk_file_decode_write_ackn(const uint8_t *p, size_t len, sdk_file_write_ackn_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SDK_FILE_H */
