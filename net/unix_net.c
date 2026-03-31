// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * unix_net.c - Linux/Unix-specific TCP network implementation
 *
 * Implements the unix_net_* functions declared in unix_net.h.
 * Follows Linux kernel coding style:
 *   - Tabs for indentation (1 tab = 8 spaces display width)
 *   - snake_case for all identifiers
 *   - Opening brace on the same line for non-function blocks
 *   - Function opening brace on its own line
 *   - Pointer asterisk adjacent to the variable name
 */

#ifdef __linux__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "net.h"
#include "unix_net.h"
#include "log.h"

/* -----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static net_err_t errno_to_net_err(int err)
{
	switch (err) {
	case ECONNREFUSED:	return NET_ERR_CONNECT;
	case ETIMEDOUT:		return NET_ERR_TIMEOUT;
	case ECONNRESET:	/* fall-through */
	case EPIPE:		return NET_ERR_CLOSED;
	case EINVAL:		return NET_ERR_PARAM;
	case EADDRINUSE:	return NET_ERR_BIND;
	case ENOMEM:		return NET_ERR_NOMEM;
	default:		return NET_ERR_GENERIC;
	}
}

/*
 * fill_sockaddr - populate a sockaddr_storage from net_addr_t.
 *
 * Tries IPv4 first, then IPv6.
 * Returns 0 on success, -1 if ip_str cannot be parsed.
 */
static int fill_sockaddr(const net_addr_t *addr,
		struct sockaddr_storage *ss,
		socklen_t *ss_len)
{
	struct sockaddr_in  *sa4 = (struct sockaddr_in *)ss;
	struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)ss;

	memset(ss, 0, sizeof(*ss));

	if (inet_pton(AF_INET, addr->ip_str, &sa4->sin_addr) == 1) {
		sa4->sin_family = AF_INET;
		sa4->sin_port   = htons(addr->port);
		*ss_len = (socklen_t)sizeof(*sa4);
		return 0;
	}

	if (inet_pton(AF_INET6, addr->ip_str, &sa6->sin6_addr) == 1) {
		sa6->sin6_family = AF_INET6;
		sa6->sin6_port   = htons(addr->port);
		*ss_len = (socklen_t)sizeof(*sa6);
		return 0;
	}

	return -1;
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------- */

net_err_t unix_net_init(void)
{
	return NET_OK;
}

void unix_net_cleanup(void)
{
}

/* -----------------------------------------------------------------------
 * Socket creation / destruction
 * -------------------------------------------------------------------- */

net_socket_t * unix_net_socket_create(void)
{
	net_socket_t *sock;

	sock = calloc(1, sizeof(*sock));
	if (!sock)
		return NULL;

	sock->fd        = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sock->connected = 0;

	if (sock->fd < 0) {
		pr_err("[unix_net] socket() failed: %s\n",
				strerror(errno));
		free(sock);
		return NULL;
	}

	return sock;
}

void unix_net_socket_destroy(net_socket_t *sock)
{
	if (!sock)
		return;

	if (sock->fd >= 0) {
		shutdown(sock->fd, SHUT_RDWR);
		close(sock->fd);
		sock->fd = -1;
	}

	free(sock);
}

/* -----------------------------------------------------------------------
 * Server-side operations
 * -------------------------------------------------------------------- */

net_err_t unix_net_server_bind(net_socket_t *sock, const net_addr_t *addr)
{
	struct sockaddr_storage ss;
	socklen_t ss_len = 0;
	int opt = 1;

	if (!sock || !addr)
		return NET_ERR_PARAM;

	setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
	setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

	if (fill_sockaddr(addr, &ss, &ss_len) < 0)
		return NET_ERR_PARAM;

	if (bind(sock->fd, (struct sockaddr *)&ss, ss_len) < 0) {
		pr_err("[unix_net] bind() failed: %s\n",
				strerror(errno));
		return NET_ERR_BIND;
	}

	sock->bound_addr = *addr;
	return NET_OK;
}

net_err_t unix_net_server_listen(net_socket_t *sock, int backlog)
{
	if (!sock)
		return NET_ERR_PARAM;

	if (listen(sock->fd, backlog) < 0) {
		pr_err("[unix_net] listen() failed: %s\n",
				strerror(errno));
		return NET_ERR_LISTEN;
	}

	return NET_OK;
}

net_socket_t * unix_net_server_accept(net_socket_t *sock, net_addr_t *peer_addr)
{
	struct sockaddr_storage peer_ss;
	socklen_t peer_len = sizeof(peer_ss);
	net_socket_t *client;
	int client_fd;

	if (!sock)
		return NULL;

	client_fd = accept(sock->fd, (struct sockaddr *)&peer_ss, &peer_len);
	if (client_fd < 0) {
		pr_err("[unix_net] accept() failed: %s\n",
				strerror(errno));
		return NULL;
	}

	client = calloc(1, sizeof(*client));
	if (!client) {
		close(client_fd);
		return NULL;
	}

	client->fd        = client_fd;
	client->connected = 1;

	if (peer_addr) {
		struct sockaddr_in  *sa4;
		struct sockaddr_in6 *sa6;

		memset(peer_addr, 0, sizeof(*peer_addr));

		if (peer_ss.ss_family == AF_INET) {
			sa4 = (struct sockaddr_in *)&peer_ss;
			peer_addr->port = ntohs(sa4->sin_port);
			inet_ntop(AF_INET, &sa4->sin_addr,
					peer_addr->ip_str,
					sizeof(peer_addr->ip_str));
		} else if (peer_ss.ss_family == AF_INET6) {
			sa6 = (struct sockaddr_in6 *)&peer_ss;
			peer_addr->port = ntohs(sa6->sin6_port);
			inet_ntop(AF_INET6, &sa6->sin6_addr,
					peer_addr->ip_str,
					sizeof(peer_addr->ip_str));
		}

		client->peer_addr = *peer_addr;
	}

	return client;
}

/* -----------------------------------------------------------------------
 * Client-side operations
 * -------------------------------------------------------------------- */

net_err_t unix_net_client_connect(net_socket_t *sock, const net_addr_t *addr)
{
	struct sockaddr_storage ss;
	socklen_t ss_len = 0;

	if (!sock || !addr)
		return NET_ERR_PARAM;

	if (fill_sockaddr(addr, &ss, &ss_len) < 0)
		return NET_ERR_PARAM;

	if (connect(sock->fd, (struct sockaddr *)&ss, ss_len) < 0) {
		pr_err("[unix_net] connect() failed: %s\n",
				strerror(errno));
		return errno_to_net_err(errno);
	}

	sock->peer_addr = *addr;
	sock->connected = 1;
	return NET_OK;
}

/* -----------------------------------------------------------------------
 * Data transfer
 * -------------------------------------------------------------------- */

net_err_t unix_net_send(net_socket_t *sock, const void *buf, size_t len, size_t *sent)
{
	ssize_t n;

	if (!sock || !buf || len == 0)
		return NET_ERR_PARAM;

	if (sent)
		*sent = 0;

	n = send(sock->fd, buf, len, 0);
	if (n < 0) {
		pr_err("[unix_net] send() failed: %s\n",
				strerror(errno));
		return errno_to_net_err(errno);
	}

	if (sent)
		*sent = (size_t)n;

	return NET_OK;
}

net_err_t unix_net_recv(net_socket_t *sock, void *buf, size_t buf_len, size_t *received)
{
	ssize_t n;

	if (!sock || !buf || buf_len == 0)
		return NET_ERR_PARAM;

	if (received)
		*received = 0;

	n = recv(sock->fd, buf, buf_len, 0);
	if (n == 0)
		return NET_ERR_CLOSED;

	if (n < 0) {
		pr_err("[unix_net] recv() failed: %s\n",
				strerror(errno));
		return errno_to_net_err(errno);
	}

	if (received)
		*received = (size_t)n;

	return NET_OK;
}

#endif
