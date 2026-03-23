/*
 * sdk_frame.c - Outer frame encode / decode and CRC-16.
 *
 * Wire byte order: big-endian (network order) for all multi-byte fields.
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _WIN32
#  include <time.h>
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>   /* htonl / ntohl / htons / ntohs */
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <time.h>
#endif

#include "sdk_frame.h"

/* -------------------------------------------------------------------------
 * Portable 64-bit byte-swap helpers
 * ---------------------------------------------------------------------- */

static uint64_t sdk_hton64(uint64_t v)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return v;
#else
    return (((uint64_t)(uint8_t)(v >>  0)) << 56)
         | (((uint64_t)(uint8_t)(v >>  8)) << 48)
         | (((uint64_t)(uint8_t)(v >> 16)) << 40)
         | (((uint64_t)(uint8_t)(v >> 24)) << 32)
         | (((uint64_t)(uint8_t)(v >> 32)) << 24)
         | (((uint64_t)(uint8_t)(v >> 40)) << 16)
         | (((uint64_t)(uint8_t)(v >> 48)) <<  8)
         | (((uint64_t)(uint8_t)(v >> 56)) <<  0);
#endif
}
#define sdk_ntoh64 sdk_hton64

/* -------------------------------------------------------------------------
 * CRC-16/CCITT-FALSE  (poly=0x1021, init=0xFFFF, no reflect)
 * ---------------------------------------------------------------------- */

uint16_t sdk_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t   i;
    int      bit;

    for (i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U)
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------
 * Timestamp helper
 * ---------------------------------------------------------------------- */

uint64_t sdk_timestamp_us(void)
{
#ifndef _WIN32
#  include <time.h>
#endif

#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ui;
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    /* Windows FILETIME epoch is 1601-01-01; subtract 116444736000000000 to
     * convert to Unix epoch, then divide by 10 for microseconds. */
    return (ui.QuadPart - UINT64_C(116444736000000000)) / 10;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

/* -------------------------------------------------------------------------
 * Write helpers (big-endian)
 * ---------------------------------------------------------------------- */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v >> 0);
}

static void put_u64(uint8_t *p, uint64_t v)
{
    uint64_t n = sdk_hton64(v);
    memcpy(p, &n, 8);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t n;
    memcpy(&n, p, 8);
    return sdk_ntoh64(n);
}

/* -------------------------------------------------------------------------
 * Encode
 * ---------------------------------------------------------------------- */

int sdk_frame_encode(sdk_frame_type_t type, uint64_t timestamp,
                     const uint8_t *payload, uint64_t payload_len,
                     uint8_t *out_buf, size_t *out_len)
{
    size_t   total;
    uint16_t route;
    uint16_t crc;
    uint8_t *p = out_buf;

    total = (size_t)payload_len + SDK_FRAME_OVERHEAD;

    /* Header */
    put_u16(p, SDK_FRAME_HEADER);
    p += 2;

    /* Route */
    route = (uint16_t)(((uint16_t)type & 0x7U) << SDK_ROUTE_TYPE_SHIFT);
    put_u16(p, route);
    p += 2;

    /* Length */
    put_u64(p, payload_len);
    p += 8;

    /* Timestamp */
    put_u64(p, timestamp);
    p += 8;

    /* Reserved */
    memset(p, 0, SDK_FRAME_RESERVED_SIZE);
    p += SDK_FRAME_RESERVED_SIZE;

    /* Payload */
    if (payload && payload_len > 0)
        memcpy(p, payload, (size_t)payload_len);
    p += (size_t)payload_len;

    /* CRC-16: covers Route..end-of-payload */
    crc = sdk_crc16(out_buf + SDK_FRAME_CRC_START_OFF,
                    (size_t)(p - (out_buf + SDK_FRAME_CRC_START_OFF)));
    put_u16(p, crc);
    p += 2;

    /* Tail */
    put_u16(p, SDK_FRAME_TAIL);
    p += 2;

    *out_len = total;
    return 0;
}

/* -------------------------------------------------------------------------
 * Peek total length
 * ---------------------------------------------------------------------- */

size_t sdk_frame_peek_total_len(const uint8_t *buf, size_t buf_len)
{
    uint64_t payload_len;

    if (buf_len < (SDK_FRAME_OFF_LENGTH + SDK_FRAME_LENGTH_SIZE))
        return 0;

    payload_len = get_u64(buf + SDK_FRAME_OFF_LENGTH);
    return (size_t)payload_len + SDK_FRAME_OVERHEAD;
}

/* -------------------------------------------------------------------------
 * Decode
 * ---------------------------------------------------------------------- */

int sdk_frame_decode(const uint8_t *buf, size_t buf_len, sdk_frame_t *frame)
{
    uint16_t header, tail, route, wire_crc, calc_crc;
    uint64_t payload_len;
    size_t   crc_region_len;

    if (buf_len < SDK_FRAME_OVERHEAD)
        return -1;

    /* Header / Tail */
    header = get_u16(buf + SDK_FRAME_OFF_HEADER);
    if (header != SDK_FRAME_HEADER)
        return -1;

    payload_len = get_u64(buf + SDK_FRAME_OFF_LENGTH);

    if (buf_len < (size_t)payload_len + SDK_FRAME_OVERHEAD)
        return -1;

    tail = get_u16(buf + SDK_FRAME_OFF_PAYLOAD + (size_t)payload_len + 2);
    if (tail != SDK_FRAME_TAIL)
        return -1;

    /* CRC validation */
    crc_region_len = (size_t)(
        SDK_FRAME_ROUTE_SIZE +
        SDK_FRAME_LENGTH_SIZE +
        SDK_FRAME_TIMESTAMP_SIZE +
        SDK_FRAME_RESERVED_SIZE +
        (size_t)payload_len);

    calc_crc = sdk_crc16(buf + SDK_FRAME_CRC_START_OFF, crc_region_len);
    wire_crc = get_u16(buf + SDK_FRAME_OFF_PAYLOAD + (size_t)payload_len);

    if (calc_crc != wire_crc)
        return -1;

    /* Fill frame descriptor */
    route             = get_u16(buf + SDK_FRAME_OFF_ROUTE);
    frame->type       = (sdk_frame_type_t)((route & SDK_ROUTE_TYPE_MASK) >>
                                            SDK_ROUTE_TYPE_SHIFT);
    frame->length     = payload_len;
    frame->timestamp  = get_u64(buf + SDK_FRAME_OFF_TIMESTAMP);
    frame->payload    = buf + SDK_FRAME_OFF_PAYLOAD;   /* zero-copy */

    return 0;
}
