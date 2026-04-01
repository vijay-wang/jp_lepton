/*
 * sdk_cmd.c - CMD sub-frame encode / decode.
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "log.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#endif

#include "sdk_cmd.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static void put_u8 (uint8_t *p, uint8_t  v) { p[0] = v; }
static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >>  8);
	p[3] = (uint8_t)(v >>  0);
}
static void put_u64(uint8_t *p, uint64_t v)
{
	put_u32(p,     (uint32_t)(v >> 32));
	put_u32(p + 4, (uint32_t)(v));
}

static uint8_t  get_u8 (const uint8_t *p) { return p[0]; }
static uint32_t get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
		| ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
static uint64_t get_u64(const uint8_t *p)
{
	return ((uint64_t)get_u32(p) << 32) | (uint64_t)get_u32(p + 4);
}

/* -------------------------------------------------------------------------
 * Encode
 * ---------------------------------------------------------------------- */

int sdk_cmd_encode_request(uint8_t flag, const uint8_t *data, uint64_t data_len,
		uint8_t *out, size_t out_cap, size_t *out_len)
{
	/* flag(1) + reserved(8) + data_len(8) + data(N) */
	size_t need = 1 + SDK_CMD_RESERVED_SIZE + 8 + (size_t)data_len;
	uint8_t *p  = out;

	if (out_cap < need)
		return -1;

	put_u8(p, flag); p += 1;
	memset(p, 0, SDK_CMD_RESERVED_SIZE);  p += SDK_CMD_RESERVED_SIZE;
	put_u64(p, data_len);                 p += 8;
	if (data && data_len)
		memcpy(p, data, (size_t)data_len);

	*out_len = need;
	return 0;
}

int sdk_cmd_encode_write_ack(uint32_t ret_code,
		uint8_t *out, size_t out_cap, size_t *out_len)
{
	/* flag(1) + reserved(8) + ret_code(4) */
	size_t need = 1 + SDK_CMD_RESERVED_SIZE + 4;
	uint8_t *p  = out;

	if (out_cap < need)
		return -1;

	put_u8(p,  SDK_CMD_FLAG_WRITE_ACK);  p += 1;
	memset(p, 0, SDK_CMD_RESERVED_SIZE); p += SDK_CMD_RESERVED_SIZE;
	put_u32(p, ret_code);

	*out_len = need;
	return 0;
}

int sdk_cmd_encode_ack(uint32_t ret_code, uint8_t cmd_flag,
		const uint8_t *data, uint64_t data_len,
		uint8_t *out, size_t out_cap, size_t *out_len)
{
	/* flag(1) + reserved(8) + ret_code(4) + data_len(8) + data(N) */
	size_t need = 1 + SDK_CMD_RESERVED_SIZE + 4 + 8 + (size_t)data_len;
	uint8_t *p  = out;

	if (out_cap < need)
		return -1;

	put_u8(p, cmd_flag);   p += 1;
	memset(p, 0, SDK_CMD_RESERVED_SIZE); p += SDK_CMD_RESERVED_SIZE;
	put_u32(p, ret_code);                p += 4;
	put_u64(p, data_len);                p += 8;
	if (data && data_len)
		memcpy(p, data, (size_t)data_len);

	*out_len = need;
	return 0;
}

int sdk_cmd_encode_read_ack(uint32_t ret_code,
		const uint8_t *data, uint64_t data_len,
		uint8_t *out, size_t out_cap, size_t *out_len)
{
	/* flag(1) + reserved(8) + ret_code(4) + data_len(8) + data(N) */
	size_t need = 1 + SDK_CMD_RESERVED_SIZE + 4 + 8 + (size_t)data_len;
	uint8_t *p  = out;

	if (out_cap < need)
		return -1;

	put_u8(p,  SDK_CMD_FLAG_READ_ACK);   p += 1;
	memset(p, 0, SDK_CMD_RESERVED_SIZE); p += SDK_CMD_RESERVED_SIZE;
	put_u32(p, ret_code);                p += 4;
	put_u64(p, data_len);                p += 8;
	if (data && data_len)
		memcpy(p, data, (size_t)data_len);

	*out_len = need;
	return 0;
}

/* -------------------------------------------------------------------------
 * Decode
 * ---------------------------------------------------------------------- */

uint8_t sdk_cmd_decode_flag(const uint8_t *payload, size_t payload_len)
{
	if (payload_len < 1)
		return 0xFF;
	return get_u8(payload);
}

int sdk_cmd_decode_request(const uint8_t *p, size_t len, sdk_cmd_request_t *out)
{
	/* flag(1)+reserved(8)+data_len(8) */
	size_t hdr = 1 + SDK_CMD_RESERVED_SIZE + 8;

	if (len < hdr)
		return -1;

	out->data_len = get_u64(p + 1 + SDK_CMD_RESERVED_SIZE);
	if (len < hdr + (size_t)out->data_len)
		return -1;
	out->data = p + hdr;
	return 0;
}

int sdk_cmd_decode_ack(const uint8_t *p, size_t len,
		sdk_cmd_ack_t *out)
{
	size_t hdr = 1 + SDK_CMD_RESERVED_SIZE + 4 + 8;

	if (len < hdr)
		return -1;

	out->ret_code = get_u32(p + 1 + SDK_CMD_RESERVED_SIZE);
	out->data_len = get_u64(p + 1 + SDK_CMD_RESERVED_SIZE + 4);

	if (len < hdr + (size_t)out->data_len)
		return -1;

	out->data = p + hdr;
	return 0;
}

void *alloc_ack_buf(size_t data_len, size_t *ack_cap)
{
	/* flag(1) + reserved(8) + ret_code(4) + data_len(8) + data(N) */
	size_t need = 1 + SDK_CMD_RESERVED_SIZE + 4 + 8 + (size_t)data_len;

	*ack_cap = need;
	return malloc(need);
}

void free_ack_buf(void *addr)
{
	free(addr);
	addr = NULL;
}
