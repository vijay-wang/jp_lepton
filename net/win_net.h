/**
 * @file win_net.h
 * @brief Windows-specific TCP network implementation (internal header).
 *
 * This header is NOT part of the public API. Include net.h instead.
 * Follows Microsoft C code style conventions.
 */

#ifndef WIN_NET_H
#define WIN_NET_H

#ifndef _WIN32
#error "win_net.h may only be included on Windows builds."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "net.h"

#pragma comment(lib, "Ws2_32.lib")

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Internal socket structure (Windows)
 * ---------------------------------------------------------------------- */

struct net_socket {
    SOCKET     Handle;       /* Underlying Winsock socket descriptor  */
    net_addr_t BoundAddr;    /* Local address (after bind)            */
    net_addr_t PeerAddr;     /* Remote address (after connect/accept) */
    BOOL       IsConnected;  /* TRUE after successful connect/accept  */
};

/* -------------------------------------------------------------------------
 * Windows platform function declarations
 * ---------------------------------------------------------------------- */

/**
 * WinNetInit - Initialise Winsock (WSAStartup).
 *
 * Returns: NET_OK on success, NET_ERR_INIT on failure.
 */
net_err_t WinNetInit(void);

/**
 * WinNetCleanup - Call WSACleanup and release Winsock resources.
 */
void WinNetCleanup(void);

/**
 * WinNetSocketCreate - Allocate and open a TCP socket.
 *
 * Returns: Heap-allocated net_socket_t on success, NULL on failure.
 */
net_socket_t *WinNetSocketCreate(void);

/**
 * WinNetSocketDestroy - Close the Winsock handle and free memory.
 *
 * @pSock: Socket handle; passing NULL is safe.
 */
void WinNetSocketDestroy(net_socket_t *pSock);

/**
 * WinNetServerBind - Bind @pSock to the local address in @pAddr.
 */
net_err_t WinNetServerBind(net_socket_t *pSock, const net_addr_t *pAddr);

/**
 * WinNetServerListen - Begin listening on a bound TCP socket.
 *
 * @pSock:   Bound socket.
 * @Backlog: Maximum pending-connection queue depth.
 */
net_err_t WinNetServerListen(net_socket_t *pSock, int Backlog);

/**
 * WinNetServerAccept - Accept one incoming TCP connection.
 *
 * @pSock:     Listening socket.
 * @pPeerAddr: Receives the remote address on success; may be NULL.
 *
 * Returns: New connected socket handle, or NULL on failure.
 */
net_socket_t *WinNetServerAccept(net_socket_t *pSock, net_addr_t *pPeerAddr);

/**
 * WinNetClientConnect - Connect @pSock to the remote address @pAddr.
 */
net_err_t WinNetClientConnect(net_socket_t *pSock, const net_addr_t *pAddr);

/**
 * WinNetSend - Send @Len bytes from @pBuf through @pSock.
 *
 * @pSent: Receives the actual bytes sent; may be NULL.
 */
net_err_t WinNetSend(net_socket_t *pSock, const void *pBuf, size_t Len,
                     size_t *pSent);

/**
 * WinNetRecv - Receive up to @BufLen bytes into @pBuf from @pSock.
 *
 * @pReceived: Receives the actual bytes read; may be NULL.
 */
net_err_t WinNetRecv(net_socket_t *pSock, void *pBuf, size_t BufLen,
                     size_t *pReceived);

#ifdef __cplusplus
}
#endif

#endif /* WIN_NET_H */
