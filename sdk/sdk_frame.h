/**
 * @file sdk_frame.h
 * @brief Outer frame definition and codec for the SDK protocol.
 *
 * All multi-byte integers are in network byte order (big-endian) on the wire.
 * The CRC-16 covers all fields except Header, Tail and the CRC16 field itself.
 *
 * Frame layout:
 *   [Header 2B][Route 2B][Length 8B][Timestamp 8B][Reserved 16B]
 *   [Payload NB][CRC16 2B][Tail 2B]
 *
 * Route field (16 bits):
 *   Bits [15:13] = frame type  (SDK_FRAME_TYPE_*)
 *   Bits [12:0]  = reserved, always 0
 */

#ifndef SDK_FRAME_H
#define SDK_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* -------------------------------------------------------------------------
	 * Wire constants
	 * ---------------------------------------------------------------------- */

#define SDK_FRAME_HEADER        UINT16_C(0x55AA)
#define SDK_FRAME_TAIL          UINT16_C(0xAA55)

#define SDK_FRAME_HEADER_SIZE   2
#define SDK_FRAME_ROUTE_SIZE    2
#define SDK_FRAME_LENGTH_SIZE   8
#define SDK_FRAME_TIMESTAMP_SIZE 8
#define SDK_FRAME_RESERVED_SIZE 16
#define SDK_FRAME_CRC_SIZE      2
#define SDK_FRAME_TAIL_SIZE     2

	/* Offset of each field inside the raw byte stream */
#define SDK_FRAME_OFF_HEADER     0
#define SDK_FRAME_OFF_ROUTE      2
#define SDK_FRAME_OFF_LENGTH     4
#define SDK_FRAME_OFF_TIMESTAMP  12
#define SDK_FRAME_OFF_RESERVED   20
#define SDK_FRAME_OFF_PAYLOAD    36   /* fixed header is 36 bytes */

	/* Minimum frame size: header(2)+route(2)+length(8)+ts(8)+rsv(16)+crc(2)+tail(2) */
#define SDK_FRAME_OVERHEAD       40

	/* CRC covers bytes [route .. payload_end], i.e. everything except
	 * header(2), crc(2) and tail(2).  Start offset for CRC computation: */
#define SDK_FRAME_CRC_START_OFF  SDK_FRAME_OFF_ROUTE

	/* -------------------------------------------------------------------------
	 * Frame types  (stored in bits [15:13] of the Route field)
	 * ---------------------------------------------------------------------- */

	typedef enum sdk_frame_type {
		SDK_FRAME_TYPE_IMAGE = 0x0, /* 000 */
		SDK_FRAME_TYPE_CMD   = 0x1, /* 001 */
		SDK_FRAME_TYPE_FILE  = 0x2, /* 010 */
	} sdk_frame_type_t;

#define SDK_ROUTE_TYPE_SHIFT  13
#define SDK_ROUTE_TYPE_MASK   UINT16_C(0xE000)

	/* -------------------------------------------------------------------------
	 * Decoded outer frame descriptor
	 *
	 * The payload pointer points directly into the receive buffer to avoid
	 * an extra copy. Callers must not free it; it belongs to the rx buffer.
	 * ---------------------------------------------------------------------- */

	typedef struct sdk_frame {
		sdk_frame_type_t  type;
		uint64_t          length;     /* payload length in bytes              */
		uint64_t          timestamp;  /* microseconds since epoch             */
		const uint8_t    *payload;    /* points into the raw receive buffer   */
	} sdk_frame_t;

	/* -------------------------------------------------------------------------
	 * API
	 * ---------------------------------------------------------------------- */

	/**
	 * sdk_crc16 - Compute CRC-16/CCITT-FALSE over @len bytes starting at @data.
	 */
	uint16_t sdk_crc16(const uint8_t *data, size_t len);

	/**
	 * sdk_frame_encode - Serialise a frame into @out_buf.
	 *
	 * @type:      Frame type.
	 * @timestamp: Timestamp in microseconds.
	 * @payload:   Pointer to payload bytes.
	 * @payload_len: Number of payload bytes.
	 * @out_buf:   Destination buffer (must be >= payload_len + SDK_FRAME_OVERHEAD).
	 * @out_len:   On success, set to total bytes written.
	 *
	 * Returns 0 on success, -1 on error.
	 */
	int sdk_frame_encode(sdk_frame_type_t type, uint64_t timestamp,
			const uint8_t *payload, uint64_t payload_len,
			uint8_t *out_buf, size_t *out_len);

	/**
	 * sdk_frame_decode - Parse one complete outer frame from @buf.
	 *
	 * Validates header/tail and CRC-16. On success, frame->payload points
	 * into @buf (zero-copy). @buf must remain valid while the frame is used.
	 *
	 * @buf:     Raw bytes containing exactly one complete frame.
	 * @buf_len: Number of bytes in @buf.
	 * @frame:   Output descriptor.
	 *
	 * Returns 0 on success, -1 on validation failure.
	 */
	int sdk_frame_decode(const uint8_t *buf, size_t buf_len, sdk_frame_t *frame);

	/**
	 * sdk_frame_peek_total_len - Return the total wire length of the frame
	 * whose header starts at @buf, or 0 if @buf_len < SDK_FRAME_OFF_PAYLOAD.
	 *
	 * Used by the receive thread to know how many bytes to wait for before
	 * calling sdk_frame_decode().
	 */
	size_t sdk_frame_peek_total_len(const uint8_t *buf, size_t buf_len);

	/**
	 * sdk_timestamp_us - Return current time in microseconds.
	 */
	uint64_t sdk_timestamp_us(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_FRAME_H */
