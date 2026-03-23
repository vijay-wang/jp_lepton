/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * server.h - Device server main interface (Linux only).
 *
 * The server accepts one TCP client at a time and runs three handler
 * threads: image sender, command handler, and file handler.
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------- */

typedef struct server_config {
	const char *bind_ip;    /* IP to listen on, e.g. "0.0.0.0"  */
	uint16_t    port;       /* TCP port                          */
	int         backlog;    /* listen() backlog                  */
} server_config_t;

#define SERVER_CONFIG_DEFAULT { .bind_ip = "0.0.0.0", \
                                .port    = 8080,        \
                                .backlog = 4 }

/* -----------------------------------------------------------------------
 * Opaque handle
 * -------------------------------------------------------------------- */

typedef struct server_handle server_handle_t;

/* -----------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------- */

/**
 * server_create - Allocate and initialise the server.
 *
 * @cfg: Configuration; NULL uses SERVER_CONFIG_DEFAULT.
 *
 * Return: handle on success, NULL on failure.
 */
server_handle_t *server_create(const server_config_t *cfg);

/**
 * server_destroy - Stop the server and free resources.
 *
 * @srv: Handle; NULL is safe.
 */
void server_destroy(server_handle_t *srv);

/**
 * server_run - Enter the accept loop (blocks until server_stop() is called).
 *
 * @srv: Handle.
 *
 * Return: 0 on clean shutdown, -1 on error.
 */
int server_run(server_handle_t *srv);

/**
 * server_stop - Signal the server to stop accepting new clients.
 *
 * Safe to call from a signal handler.
 *
 * @srv: Handle.
 */
void server_stop(server_handle_t *srv);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_H */
