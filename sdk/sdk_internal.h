/*
 * sdk_internal.h - SDK internal definitions shared between sdk.c
 *                  and the receive thread.
 *
 * Not part of the public API.
 */

#ifndef SDK_INTERNAL_H
#define SDK_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#include "../net/net.h"
#include "sdk.h"
#include "sdk_frame.h"
#include "sdk_image.h"

/* -------------------------------------------------------------------------
 * Pending request slot
 *
 * The send side creates a pending_req, sends the frame, then waits on
 * the condition. The receive thread fills in the response and signals.
 * ---------------------------------------------------------------------- */

#define SDK_PENDING_NONE   0
#define SDK_PENDING_WAIT   1
#define SDK_PENDING_DONE   2
#define SDK_PENDING_ERR    3

typedef struct sdk_pending_req {
	int      state;         /* SDK_PENDING_* */

	/* Response payload – heap-allocated by the rx thread */
	uint8_t *rsp_payload;
	size_t   rsp_len;

#ifdef _WIN32
	CRITICAL_SECTION lock;
	CONDITION_VARIABLE cond;
#else
	pthread_mutex_t  lock;
	pthread_cond_t   cond;
#endif
} sdk_pending_req_t;

/* -------------------------------------------------------------------------
 * SDK handle (full definition)
 * ---------------------------------------------------------------------- */

struct sdk_handle {
	sdk_config_t         cfg;
	net_socket_t        *sock;
	sdk_image_module_t  *image_mod;

	/* Receive thread */
#ifdef _WIN32
	HANDLE               rx_thread;
	CRITICAL_SECTION     state_lock;
#else
	pthread_t            rx_thread;
	pthread_mutex_t      state_lock;
#endif
	int                  rx_running;   /* 1 while rx thread is alive */
	int                  connected;

	/* Single pending request slot (cmd or file, one at a time) */
	sdk_pending_req_t    pending;
};

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

void sdk_pending_init  (sdk_pending_req_t *r);
void sdk_pending_destroy(sdk_pending_req_t *r);

/* Post a response to the pending slot and wake the waiter.
 * Takes ownership of payload (must be heap-allocated). */
void sdk_pending_post(sdk_pending_req_t *r, uint8_t *payload, size_t len);
void sdk_pending_post_err(sdk_pending_req_t *r);

/* Wait up to timeout_ms for a response.
 * On success returns 0 and the caller owns *payload.
 * Returns -1 on timeout or error. */
int  sdk_pending_wait(sdk_pending_req_t *r, int timeout_ms,
		uint8_t **payload, size_t *len);

/* Arm the pending slot before sending a request */
void sdk_pending_arm(sdk_pending_req_t *r);

/* Send one outer frame (thread-safe via sock being one-writer). */
int  sdk_send_frame(sdk_handle_t *h, sdk_frame_type_t type,
		const uint8_t *payload, size_t payload_len);

#endif /* SDK_INTERNAL_H */
