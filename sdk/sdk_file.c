/*
 * sdk_file.c - File transfer sub-frame encode / decode.
 */

#include <string.h>
#include <stdint.h>

#include "sdk_file.h"
#include "sdk_frame.h"   /* sdk_crc16 */
#include "log.h"

/* -------------------------------------------------------------------------
 * Portable put / get helpers
 * ---------------------------------------------------------------------- */

static void put_u8 (uint8_t *p, uint8_t  v) { p[0] = v; }
static void put_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v);
}
static void put_u32(uint8_t *p, uint32_t v)
{
	p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
	p[2]=(uint8_t)(v>> 8); p[3]=(uint8_t)(v);
}
static void put_u64(uint8_t *p, uint64_t v)
{
	put_u32(p,     (uint32_t)(v >> 32));
	put_u32(p + 4, (uint32_t)(v      ));
}

static uint8_t  get_u8 (const uint8_t *p) { return p[0]; }
static uint16_t get_u16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}
static uint32_t get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)
		|((uint32_t)p[2]<<8) | (uint32_t)p[3];
}
static uint64_t get_u64(const uint8_t *p)
{
	return ((uint64_t)get_u32(p)<<32)|(uint64_t)get_u32(p+4);
}

#define RSV  SDK_FILE_RESERVED_SIZE

/* -------------------------------------------------------------------------
 * Encode helpers
 * ---------------------------------------------------------------------- */

int sdk_file_encode_read_req(const char *path,
		uint8_t *out, size_t cap, size_t *out_len)
{
	/* flag(1)+reserved(8)+path(256) */
	size_t need = 1 + RSV + SDK_FILE_PATH_SIZE;
	if (cap < need) return -1;
	put_u8(out, SDK_FILE_FLAG_READ_REQ);
	memset(out+1, 0, RSV);
	memset(out+1+RSV, 0, SDK_FILE_PATH_SIZE);
	if (path) strncpy((char *)(out+1+RSV), path, SDK_FILE_PATH_SIZE-1);
	*out_len = need;
	return 0;
}

int sdk_file_encode_read_seq0(uint32_t ret, uint64_t file_len,
		uint16_t crc, uint32_t num_pkg,
		uint8_t *out, size_t cap, size_t *out_len)
{
	/* flag(1)+reserved(8)+ret(4)+file_len(8)+crc(2)+num_pkg(4) */
	size_t need = 1 + RSV + 4 + 8 + 2 + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_READ_SEQ0); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, ret);      p += 4;
	put_u64(p, file_len); p += 8;
	put_u16(p, crc);      p += 2;
	put_u32(p, num_pkg);
	*out_len = need;
	return 0;
}

int sdk_file_encode_read_ack0(uint32_t seq0,
		uint8_t *out, size_t cap, size_t *out_len)
{
	/* flag(1)+reserved(8)+seq0(4) */
	size_t need = 1 + RSV + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_READ_ACK0); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, seq0);
	*out_len = need;
	return 0;
}

int sdk_file_encode_read_seqn(uint32_t ret, const uint8_t *data,
		uint64_t data_len,
		uint8_t *out, size_t cap, size_t *out_len)
{
	/* flag(1)+reserved(8)+ret(4)+data_len(8)+data(N) */
	size_t need = 1 + RSV + 4 + 8 + (size_t)data_len;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_READ_SEQN); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, ret);       p += 4;
	put_u64(p, data_len);  p += 8;
	if (data && data_len) memcpy(p, data, (size_t)data_len);
	*out_len = need;
	return 0;
}

int sdk_file_encode_read_ackn(uint32_t seqn,
		uint8_t *out, size_t cap, size_t *out_len)
{
	size_t need = 1 + RSV + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_READ_ACKN); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, seqn);
	*out_len = need;
	return 0;
}

int sdk_file_encode_write_req(const char *path, uint64_t file_len,
		uint16_t crc, uint32_t num_pkg,
		uint8_t *out, size_t cap, size_t *out_len)
{
	/* flag(1)+reserved(8)+path(256)+file_len(8)+crc(2)+num_pkg(4) */
	size_t need = 1 + RSV + SDK_FILE_PATH_SIZE + 8 + 2 + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_WRITE_REQ); p++;
	memset(p, 0, RSV); p += RSV;
	memset(p, 0, SDK_FILE_PATH_SIZE);
	if (path) strncpy((char *)p, path, SDK_FILE_PATH_SIZE-1);
	p += SDK_FILE_PATH_SIZE;
	put_u64(p, file_len); p += 8;
	put_u16(p, crc);      p += 2;
	put_u32(p, num_pkg);
	*out_len = need;
	return 0;
}

int sdk_file_encode_write_rsp(uint32_t ret_code,
		uint8_t *out, size_t cap, size_t *out_len)
{
	size_t need = 1 + RSV + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_WRITE_RSP); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, ret_code);
	*out_len = need;
	return 0;
}

int sdk_file_encode_write_seqn(const uint8_t *data, uint64_t data_len,
		uint8_t *out, size_t cap, size_t *out_len)
{
	size_t need = 1 + RSV + 8 + (size_t)data_len;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_WRITE_SEQN); p++;
	memset(p, 0, RSV); p += RSV;
	put_u64(p, data_len); p += 8;
	if (data && data_len) memcpy(p, data, (size_t)data_len);
	*out_len = need;
	return 0;
}

int sdk_file_encode_write_ackn(uint32_t ackn,
		uint8_t *out, size_t cap, size_t *out_len)
{
	size_t need = 1 + RSV + 4;
	uint8_t *p  = out;
	if (cap < need) return -1;
	put_u8(p, SDK_FILE_FLAG_WRITE_ACKN); p++;
	memset(p, 0, RSV); p += RSV;
	put_u32(p, ackn);
	*out_len = need;
	return 0;
}

/* -------------------------------------------------------------------------
 * Decode helpers
 * ---------------------------------------------------------------------- */

uint8_t sdk_file_decode_flag(const uint8_t *payload, size_t len)
{
	if (len < 1) return 0xFF;
	return get_u8(payload);
}

int sdk_file_decode_read_req(const uint8_t *p, size_t len,
		sdk_file_read_req_t *out)
{
	size_t need = 1 + RSV + SDK_FILE_PATH_SIZE;
	if (len < need) return -1;
	memcpy(out->path, p + 1 + RSV, SDK_FILE_PATH_SIZE);
	out->path[SDK_FILE_PATH_SIZE-1] = '\0';
	return 0;
}

int sdk_file_decode_read_seq0(const uint8_t *p, size_t len,
		sdk_file_read_seq0_t *out)
{
	size_t need = 1 + RSV + 4 + 8 + 2 + 4;
	const uint8_t *q;
	if (len < need) return -1;
	q = p + 1 + RSV;
	out->ret_code    = get_u32(q); q += 4;
	out->file_len    = get_u64(q); q += 8;
	out->file_crc16  = get_u16(q); q += 2;
	out->num_packages= get_u32(q);
	return 0;
}

int sdk_file_decode_read_ack0(const uint8_t *p, size_t len,
		sdk_file_read_ack0_t *out)
{
	size_t need = 1 + RSV + 4;
	if (len < need) return -1;
	out->seq0_num = get_u32(p + 1 + RSV);
	return 0;
}

int sdk_file_decode_read_seqn(const uint8_t *p, size_t len,
		sdk_file_read_seqn_t *out)
{
	size_t hdr = 1 + RSV + 4 + 8;
	if (len < hdr) return -1;
	out->ret_code = get_u32(p + 1 + RSV);
	out->data_len = get_u64(p + 1 + RSV + 4);
	if (len < hdr + (size_t)out->data_len) return -1;
	out->data = p + hdr;
	return 0;
}

int sdk_file_decode_read_ackn(const uint8_t *p, size_t len,
		sdk_file_read_ackn_t *out)
{
	if (len < 1 + RSV + 4) return -1;
	out->seqn_num = get_u32(p + 1 + RSV);
	return 0;
}

int sdk_file_decode_write_req(const uint8_t *p, size_t len,
		sdk_file_write_req_t *out)
{
	size_t need = 1 + RSV + SDK_FILE_PATH_SIZE + 8 + 2 + 4;
	const uint8_t *q;
	if (len < need) return -1;
	q = p + 1 + RSV;
	memcpy(out->path, q, SDK_FILE_PATH_SIZE);
	out->path[SDK_FILE_PATH_SIZE-1] = '\0';
	q += SDK_FILE_PATH_SIZE;
	out->file_len    = get_u64(q); q += 8;
	out->crc16       = get_u16(q); q += 2;
	out->num_packages= get_u32(q);
	return 0;
}

int sdk_file_decode_write_rsp(const uint8_t *p, size_t len,
		sdk_file_write_rsp_t *out)
{
	if (len < 1 + RSV + 4) return -1;
	out->ret_code = get_u32(p + 1 + RSV);
	return 0;
}

int sdk_file_decode_write_seqn(const uint8_t *p, size_t len,
		sdk_file_write_seqn_t *out)
{
	size_t hdr = 1 + RSV + 8;
	if (len < hdr) return -1;
	out->data_len = get_u64(p + 1 + RSV);
	if (len < hdr + (size_t)out->data_len) return -1;
	out->data = p + hdr;
	return 0;
}

int sdk_file_decode_write_ackn(const uint8_t *p, size_t len,
		sdk_file_write_ackn_t *out)
{
	if (len < 1 + RSV + 4) return -1;
	out->ackn = get_u32(p + 1 + RSV);
	return 0;
}
