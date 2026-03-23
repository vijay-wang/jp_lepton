// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * server.c - Main server accept loop.
 *
 * Linux only. Follows Linux kernel coding style.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdatomic.h>

#include "../net/net.h"
#include "server.h"
#include "server_session.h"

/* -----------------------------------------------------------------------
 * Handle structure
 * -------------------------------------------------------------------- */

struct server_handle {
	server_config_t  cfg;
	net_socket_t    *listen_sock;
	atomic_int       stop;
};

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

server_handle_t *
server_create(const server_config_t *cfg)
{
	server_config_t   def = (server_config_t)SERVER_CONFIG_DEFAULT;
	server_handle_t  *srv;

	if (!cfg)
		cfg = &def;

	srv = calloc(1, sizeof(*srv));
	if (!srv)
		return NULL;

	srv->cfg = *cfg;
	atomic_store(&srv->stop, 0);

	if (net_init() != NET_OK) {
		free(srv);
		return NULL;
	}

	return srv;
}

void server_destroy(server_handle_t *srv)
{
	if (!srv)
		return;

	if (srv->listen_sock) {
		net_socket_destroy(srv->listen_sock);
		srv->listen_sock = NULL;
	}

	net_cleanup();
	free(srv);
}

void server_stop(server_handle_t *srv)
{
	if (srv)
		atomic_store(&srv->stop, 1);
}

int server_run(server_handle_t *srv)
{
	net_addr_t addr;
	net_err_t  err;

	if (!srv)
		return -1;

	srv->listen_sock = net_socket_create();
	if (!srv->listen_sock) {
		fprintf(stderr, "[server] socket create failed\n");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	strncpy(addr.ip_str, srv->cfg.bind_ip, sizeof(addr.ip_str) - 1);
	addr.port = srv->cfg.port;

	err = net_server_bind(srv->listen_sock, &addr);
	if (err != NET_OK) {
		fprintf(stderr, "[server] bind failed: %s\n",
			net_strerror(err));
		return -1;
	}

	err = net_server_listen(srv->listen_sock, srv->cfg.backlog);
	if (err != NET_OK) {
		fprintf(stderr, "[server] listen failed: %s\n",
			net_strerror(err));
		return -1;
	}

	printf("[server] Listening on %s:%u\n",
	       srv->cfg.bind_ip, (unsigned)srv->cfg.port);

	while (!atomic_load(&srv->stop)) {
		net_addr_t         peer;
		net_socket_t      *client;
		server_session_t  *sess;

		client = net_server_accept(srv->listen_sock, &peer);
		if (!client) {
			if (!atomic_load(&srv->stop))
				fprintf(stderr, "[server] accept failed\n");
			break;
		}

		printf("[server] Client connected: %s:%u\n",
		       peer.ip_str, (unsigned)peer.port);

		sess = server_session_create(client);
		if (!sess) {
			fprintf(stderr, "[server] session create failed\n");
			net_socket_destroy(client);
			continue;
		}

		/* Block on this client until it disconnects */
		server_session_wait(sess);
		server_session_destroy(sess);

		printf("[server] Client %s:%u disconnected\n",
		       peer.ip_str, (unsigned)peer.port);
	}

	return 0;
}
