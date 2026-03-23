/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * unix_net.h - Linux/Unix-specific TCP network implementation (internal header)
 *
 * Not part of the public API. Include net.h instead.
 * Follows Linux kernel coding style.
 */

#ifndef UNIX_NET_H
#define UNIX_NET_H

#ifdef _WIN32
#error "unix_net.h must not be included on Windows builds."
#endif

#include <sys/socket.h>
#include <netinet/in.h>

#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Internal socket structure (Linux / Unix)
 * -------------------------------------------------------------------- */

struct net_socket {
	int		fd;		  /* POSIX file descriptor               */
	net_addr_t	bound_addr;	  /* Local address (after bind)          */
	net_addr_t	peer_addr;	  /* Remote address (connect / accept)   */
	int		connected;	  /* Non-zero after connect / accept     */
};

/* -----------------------------------------------------------------------
 * Platform function declarations (unix_ prefix)
 * -------------------------------------------------------------------- */

/**
 * unix_net_init - Initialise the Unix network subsystem.
 *
 * No-op on Linux (no equivalent of WSAStartup).
 *
 * Return: NET_OK always.
 */
net_err_t unix_net_init(void);

/**
 * unix_net_cleanup - Release any global resources.
 */
void unix_net_cleanup(void);

/**
 * unix_net_socket_create - Allocate and open a TCP socket.
 *
 * Return: Heap-allocated net_socket_t on success, NULL on failure.
 */
net_socket_t *unix_net_socket_create(void);

/**
 * unix_net_socket_destroy - Shut down fd and free the socket structure.
 *
 * @sock: Handle from unix_net_socket_create(); NULL is safe.
 */
void unix_net_socket_destroy(net_socket_t *sock);

/**
 * unix_net_server_bind - Bind @sock to the local address in @addr.
 *
 * @sock: Open socket handle.
 * @addr: Local address descriptor (ip_str + port).
 *
 * Return: NET_OK, or a negative net_err_t on failure.
 */
net_err_t unix_net_server_bind(net_socket_t *sock, const net_addr_t *addr);

/**
 * unix_net_server_listen - Mark @sock as a passive (listening) socket.
 *
 * @sock:    Bound socket handle.
 * @backlog: Max pending-connection queue depth.
 *
 * Return: NET_OK, or a negative net_err_t on failure.
 */
net_err_t unix_net_server_listen(net_socket_t *sock, int backlog);

/**
 * unix_net_server_accept - Accept one incoming connection on @sock.
 *
 * Blocks until a peer connects.
 *
 * @sock:      Listening socket.
 * @peer_addr: Filled with peer address on success; may be NULL.
 *
 * Return: New connected socket handle, or NULL on failure.
 */
net_socket_t *unix_net_server_accept(net_socket_t *sock,
				     net_addr_t *peer_addr);

/**
 * unix_net_client_connect - Connect @sock to the remote described by @addr.
 *
 * @sock: Open socket handle.
 * @addr: Remote address descriptor (ip_str + port).
 *
 * Return: NET_OK, or a negative net_err_t on failure.
 */
net_err_t unix_net_client_connect(net_socket_t *sock, const net_addr_t *addr);

/**
 * unix_net_send - Send @len bytes from @buf through @sock.
 *
 * @sock:  Connected socket handle.
 * @buf:   Data buffer.
 * @len:   Number of bytes to send.
 * @sent:  Actual bytes sent on success; may be NULL.
 *
 * Return: NET_OK, or a negative net_err_t on failure.
 */
net_err_t unix_net_send(net_socket_t *sock, const void *buf, size_t len,
			size_t *sent);

/**
 * unix_net_recv - Receive up to @buf_len bytes into @buf from @sock.
 *
 * @sock:     Connected socket handle.
 * @buf:      Destination buffer.
 * @buf_len:  Buffer capacity in bytes.
 * @received: Actual bytes received on success; may be NULL.
 *
 * Return: NET_OK, NET_ERR_CLOSED if peer closed, or another negative code.
 */
net_err_t unix_net_recv(net_socket_t *sock, void *buf, size_t buf_len,
			size_t *received);

#ifdef __cplusplus
}
#endif

#endif /* UNIX_NET_H */
