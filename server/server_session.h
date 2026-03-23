/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * server_session.h - Per-client session (image tx, cmd rx/tx, file rx/tx).
 */

#ifndef SERVER_SESSION_H
#define SERVER_SESSION_H

#include "../net/net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct server_session server_session_t;

/**
 * server_session_create - Start a session for the given connected socket.
 *
 * Spawns worker threads for image sending, command handling and file handling.
 *
 * @sock: Connected client socket (session takes ownership).
 *
 * Return: session handle, or NULL on failure.
 */
server_session_t *server_session_create(net_socket_t *sock);

/**
 * server_session_destroy - Stop all worker threads and free the session.
 *
 * @sess: Session handle; NULL is safe.
 */
void server_session_destroy(server_session_t *sess);

/**
 * server_session_wait - Block until the session ends (client disconnects).
 *
 * @sess: Session handle.
 */
void server_session_wait(server_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_SESSION_H */
