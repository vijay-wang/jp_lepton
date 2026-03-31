/**
 * @file net.h
 * @brief Cross-platform TCP network abstraction layer.
 *
 * Provides a unified API for TCP socket operations across Windows and Linux.
 * Platform-specific details are encapsulated in win_net.c (Windows) and
 * unix_net.c (Linux). This header is the sole public interface.
 */

#ifndef _NET_H
#define _NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* -------------------------------------------------------------------------
	 * Address descriptor
	 * ---------------------------------------------------------------------- */

	typedef struct net_addr {
		char     ip_str[64]; /* Dotted-decimal IPv4 or colon-hex IPv6 */
		uint16_t port;       /* Host byte order                       */
	} net_addr_t;

	/* -------------------------------------------------------------------------
	 * Opaque socket handle
	 * ---------------------------------------------------------------------- */

	typedef struct net_socket net_socket_t;

	/* -------------------------------------------------------------------------
	 * Error codes
	 * ---------------------------------------------------------------------- */

	typedef enum net_err {
		NET_OK          =  0,
		NET_ERR_GENERIC = -1,
		NET_ERR_INIT    = -2,
		NET_ERR_PARAM   = -3,
		NET_ERR_BIND    = -4,
		NET_ERR_LISTEN  = -5,
		NET_ERR_CONNECT = -6,
		NET_ERR_ACCEPT  = -7,
		NET_ERR_SEND    = -8,
		NET_ERR_RECV    = -9,
		NET_ERR_TIMEOUT = -10,
		NET_ERR_CLOSED  = -11,
		NET_ERR_NOMEM   = -12,
	} net_err_t;

	/* -------------------------------------------------------------------------
	 * Lifecycle
	 * ---------------------------------------------------------------------- */

	/**
	 * net_init - Initialise the network subsystem.
	 *
	 * Must be called once before any other net_* function.
	 * On Windows this bootstraps Winsock; on Linux it is a no-op.
	 *
	 * Returns: NET_OK on success, NET_ERR_INIT on failure.
	 */
	net_err_t net_init(void);

	/**
	 * net_cleanup - Tear down the network subsystem.
	 *
	 * Call once when network operations are no longer needed.
	 * All sockets must be closed before calling this function.
	 */
	void net_cleanup(void);

	/* -------------------------------------------------------------------------
	 * Socket creation / destruction
	 * ---------------------------------------------------------------------- */

	/**
	 * net_socket_create - Allocate and return a new TCP socket handle.
	 *
	 * Returns: Pointer to an opaque net_socket_t on success, NULL on failure.
	 */
	net_socket_t *net_socket_create(void);

	/**
	 * net_socket_destroy - Close and free a socket handle.
	 *
	 * @sock: Socket previously returned by net_socket_create().
	 *        Passing NULL is a no-op.
	 */
	void net_socket_destroy(net_socket_t *sock);

	/* -------------------------------------------------------------------------
	 * Server-side operations
	 * ---------------------------------------------------------------------- */

	/**
	 * net_server_bind - Bind a socket to a local address.
	 *
	 * @sock: Socket handle.
	 * @addr: Local address (ip_str + port) to bind to.
	 *
	 * Returns: NET_OK on success, negative net_err_t on failure.
	 */
	net_err_t net_server_bind(net_socket_t *sock, const net_addr_t *addr);

	/**
	 * net_server_listen - Place the socket into the listening state.
	 *
	 * @sock:    Bound socket handle.
	 * @backlog: Maximum number of pending connections.
	 *
	 * Returns: NET_OK on success, negative net_err_t on failure.
	 */
	net_err_t net_server_listen(net_socket_t *sock, int backlog);

	/**
	 * net_server_accept - Accept an incoming connection.
	 *
	 * Blocks until a client connects.
	 *
	 * @sock:      Listening socket handle.
	 * @peer_addr: On success, filled with the remote peer's address.
	 *             May be NULL if the caller does not need this information.
	 *
	 * Returns: New connected socket handle on success, NULL on failure.
	 */
	net_socket_t *net_server_accept(net_socket_t *sock, net_addr_t *peer_addr);

	/* -------------------------------------------------------------------------
	 * Client-side operations
	 * ---------------------------------------------------------------------- */

	/**
	 * net_client_connect - Connect a socket to a remote TCP endpoint.
	 *
	 * Performs a full TCP handshake to addr->ip_str:addr->port.
	 *
	 * @sock: Socket handle.
	 * @addr: Remote address descriptor.
	 *
	 * Returns: NET_OK on success, negative net_err_t on failure.
	 */
	net_err_t net_client_connect(net_socket_t *sock, const net_addr_t *addr);

	/* -------------------------------------------------------------------------
	 * Data transfer
	 * ---------------------------------------------------------------------- */

	/**
	 * net_send - Send data through a connected TCP socket.
	 *
	 * @sock:  Connected socket handle.
	 * @buf:   Pointer to the data buffer.
	 * @len:   Number of bytes to send.
	 * @sent:  On success, set to the number of bytes actually sent.
	 *         May be NULL.
	 *
	 * Returns: NET_OK on success, negative net_err_t on failure.
	 */
	net_err_t net_send(net_socket_t *sock, const void *buf, size_t len,
			size_t *sent);

	/**
	 * net_recv - Receive data from a connected TCP socket.
	 *
	 * Blocks until data is available or the connection is closed.
	 *
	 * @sock:     Connected socket handle.
	 * @buf:      Destination buffer.
	 * @buf_len:  Capacity of @buf in bytes.
	 * @received: On success, set to the number of bytes written into @buf.
	 *            May be NULL.
	 *
	 * Returns: NET_OK on success, NET_ERR_CLOSED if the peer closed the
	 *          connection, or another negative net_err_t on failure.
	 */
	net_err_t net_recv(net_socket_t *sock, void *buf, size_t buf_len,
			size_t *received);

	/* -------------------------------------------------------------------------
	 * Utility
	 * ---------------------------------------------------------------------- */

	/**
	 * net_strerror - Return a human-readable string for a net_err_t code.
	 *
	 * @err: Error code.
	 *
	 * Returns: Pointer to a static, null-terminated string. Never NULL.
	 */
	const char *net_strerror(net_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* _NET_H */
