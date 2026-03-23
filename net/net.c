/*
 * net.c - Cross-platform TCP network API implementation.
 *
 * Dispatches each public net_* call to the appropriate platform backend:
 *   WinNet*      on Windows  (win_net.c)
 *   unix_net_*   on Linux    (unix_net.c)
 *
 * No platform-specific headers are included here.
 */

#include <stddef.h>
#include "net.h"

#ifdef _WIN32
#  include "win_net.h"
#  define PLATFORM_INIT()               WinNetInit()
#  define PLATFORM_CLEANUP()            WinNetCleanup()
#  define PLATFORM_SOCKET_CREATE()      WinNetSocketCreate()
#  define PLATFORM_SOCKET_DESTROY(s)    WinNetSocketDestroy(s)
#  define PLATFORM_SERVER_BIND(s, a)    WinNetServerBind(s, a)
#  define PLATFORM_SERVER_LISTEN(s, b)  WinNetServerListen(s, b)
#  define PLATFORM_SERVER_ACCEPT(s, p)  WinNetServerAccept(s, p)
#  define PLATFORM_CLIENT_CONNECT(s, a) WinNetClientConnect(s, a)
#  define PLATFORM_SEND(s, b, l, t)     WinNetSend(s, b, l, t)
#  define PLATFORM_RECV(s, b, l, r)     WinNetRecv(s, b, l, r)
#else
#  include "unix_net.h"
#  define PLATFORM_INIT()               unix_net_init()
#  define PLATFORM_CLEANUP()            unix_net_cleanup()
#  define PLATFORM_SOCKET_CREATE()      unix_net_socket_create()
#  define PLATFORM_SOCKET_DESTROY(s)    unix_net_socket_destroy(s)
#  define PLATFORM_SERVER_BIND(s, a)    unix_net_server_bind(s, a)
#  define PLATFORM_SERVER_LISTEN(s, b)  unix_net_server_listen(s, b)
#  define PLATFORM_SERVER_ACCEPT(s, p)  unix_net_server_accept(s, p)
#  define PLATFORM_CLIENT_CONNECT(s, a) unix_net_client_connect(s, a)
#  define PLATFORM_SEND(s, b, l, t)     unix_net_send(s, b, l, t)
#  define PLATFORM_RECV(s, b, l, r)     unix_net_recv(s, b, l, r)
#endif

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

net_err_t net_init(void)
{
    return PLATFORM_INIT();
}

void net_cleanup(void)
{
    PLATFORM_CLEANUP();
}

/* -------------------------------------------------------------------------
 * Socket creation / destruction
 * ---------------------------------------------------------------------- */

net_socket_t *net_socket_create(void)
{
    return PLATFORM_SOCKET_CREATE();
}

void net_socket_destroy(net_socket_t *sock)
{
    PLATFORM_SOCKET_DESTROY(sock);
}

/* -------------------------------------------------------------------------
 * Server-side operations
 * ---------------------------------------------------------------------- */

net_err_t net_server_bind(net_socket_t *sock, const net_addr_t *addr)
{
    return PLATFORM_SERVER_BIND(sock, addr);
}

net_err_t net_server_listen(net_socket_t *sock, int backlog)
{
    return PLATFORM_SERVER_LISTEN(sock, backlog);
}

net_socket_t *net_server_accept(net_socket_t *sock, net_addr_t *peer_addr)
{
    return PLATFORM_SERVER_ACCEPT(sock, peer_addr);
}

/* -------------------------------------------------------------------------
 * Client-side operations
 * ---------------------------------------------------------------------- */

net_err_t net_client_connect(net_socket_t *sock, const net_addr_t *addr)
{
    return PLATFORM_CLIENT_CONNECT(sock, addr);
}

/* -------------------------------------------------------------------------
 * Data transfer
 * ---------------------------------------------------------------------- */

net_err_t net_send(net_socket_t *sock, const void *buf, size_t len,
                   size_t *sent)
{
    return PLATFORM_SEND(sock, buf, len, sent);
}

net_err_t net_recv(net_socket_t *sock, void *buf, size_t buf_len,
                   size_t *received)
{
    return PLATFORM_RECV(sock, buf, buf_len, received);
}

/* -------------------------------------------------------------------------
 * Utility
 * ---------------------------------------------------------------------- */

const char *net_strerror(net_err_t err)
{
    switch (err) {
    case NET_OK:          return "Success";
    case NET_ERR_GENERIC: return "Generic error";
    case NET_ERR_INIT:    return "Subsystem initialisation failed";
    case NET_ERR_PARAM:   return "Invalid parameter";
    case NET_ERR_BIND:    return "Bind failed";
    case NET_ERR_LISTEN:  return "Listen failed";
    case NET_ERR_CONNECT: return "Connect failed";
    case NET_ERR_ACCEPT:  return "Accept failed";
    case NET_ERR_SEND:    return "Send failed";
    case NET_ERR_RECV:    return "Receive failed";
    case NET_ERR_TIMEOUT: return "Operation timed out";
    case NET_ERR_CLOSED:  return "Connection closed by peer";
    case NET_ERR_NOMEM:   return "Out of memory";
    default:              return "Unknown error";
    }
}
