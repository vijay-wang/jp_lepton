/**
 * @file win_net.c
 * @brief Windows-specific TCP network implementation.
 *
 * Implements the WinNet* functions declared in win_net.h.
 * Follows Microsoft C code style:
 *   - PascalCase for functions and parameters
 *   - 4-space indentation
 *   - Braces on the same line for control flow
 *   - Hungarian-style pointer prefix (p)
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "win_net.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static net_err_t WinNetMapWsaError(int WsaErr)
{
    switch (WsaErr) {
    case WSAECONNREFUSED:  return NET_ERR_CONNECT;
    case WSAETIMEDOUT:     return NET_ERR_TIMEOUT;
    case WSAECONNRESET:    /* fall-through */
    case WSAECONNABORTED:  return NET_ERR_CLOSED;
    case WSAEINVAL:        return NET_ERR_PARAM;
    case WSAEADDRINUSE:    return NET_ERR_BIND;
    default:               return NET_ERR_GENERIC;
    }
}

/*
 * WinNetFillSockAddr - Populate a sockaddr from ip_str and port.
 *
 * Tries IPv4 first, then IPv6. Returns TRUE on success.
 */
static BOOL WinNetFillSockAddr(const net_addr_t *pAddr,
                                struct sockaddr_in *pSa4,
                                struct sockaddr_in6 *pSa6,
                                struct sockaddr **ppSa,
                                int *pSaLen)
{
    memset(pSa4, 0, sizeof(*pSa4));
    if (inet_pton(AF_INET, pAddr->ip_str, &pSa4->sin_addr) == 1) {
        pSa4->sin_family = AF_INET;
        pSa4->sin_port   = htons(pAddr->port);
        *ppSa   = (struct sockaddr *)pSa4;
        *pSaLen = sizeof(*pSa4);
        return TRUE;
    }

    memset(pSa6, 0, sizeof(*pSa6));
    if (inet_pton(AF_INET6, pAddr->ip_str, &pSa6->sin6_addr) == 1) {
        pSa6->sin6_family = AF_INET6;
        pSa6->sin6_port   = htons(pAddr->port);
        *ppSa   = (struct sockaddr *)pSa6;
        *pSaLen = sizeof(*pSa6);
        return TRUE;
    }

    return FALSE;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

net_err_t WinNetInit(void)
{
    WSADATA WsaData;
    int     Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0) {
        pr_err("[win_net] WSAStartup failed: %d\n", Result);
        return NET_ERR_INIT;
    }

    if (LOBYTE(WsaData.wVersion) != 2 || HIBYTE(WsaData.wVersion) != 2) {
        pr_err("[win_net] Winsock 2.2 not available.\n");
        WSACleanup();
        return NET_ERR_INIT;
    }

    return NET_OK;
}

void WinNetCleanup(void)
{
    WSACleanup();
}

/* -------------------------------------------------------------------------
 * Socket creation / destruction
 * ---------------------------------------------------------------------- */

net_socket_t *WinNetSocketCreate(void)
{
    net_socket_t *pSock;

    pSock = (net_socket_t *)calloc(1, sizeof(net_socket_t));
    if (pSock == NULL) {
        return NULL;
    }

    pSock->IsConnected = FALSE;

    pSock->Handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pSock->Handle == INVALID_SOCKET) {
        pr_err("[win_net] socket() failed: %d\n", WSAGetLastError());
        free(pSock);
        return NULL;
    }

    return pSock;
}

void WinNetSocketDestroy(net_socket_t *pSock)
{
    if (pSock == NULL) {
        return;
    }

    if (pSock->Handle != INVALID_SOCKET) {
        shutdown(pSock->Handle, SD_BOTH);
        closesocket(pSock->Handle);
        pSock->Handle = INVALID_SOCKET;
    }

    free(pSock);
}

/* -------------------------------------------------------------------------
 * Server-side operations
 * ---------------------------------------------------------------------- */

net_err_t WinNetServerBind(net_socket_t *pSock, const net_addr_t *pAddr)
{
    struct sockaddr_in  Sa4;
    struct sockaddr_in6 Sa6;
    struct sockaddr    *pSa   = NULL;
    int                 SaLen = 0;
    BOOL                ReUse = TRUE;

    if (pSock == NULL || pAddr == NULL) {
        return NET_ERR_PARAM;
    }

    setsockopt(pSock->Handle, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&ReUse, sizeof(ReUse));

    if (!WinNetFillSockAddr(pAddr, &Sa4, &Sa6, &pSa, &SaLen)) {
        return NET_ERR_PARAM;
    }

    if (bind(pSock->Handle, pSa, SaLen) == SOCKET_ERROR) {
        pr_err("[win_net] bind() failed: %d\n", WSAGetLastError());
        return NET_ERR_BIND;
    }

    pSock->BoundAddr = *pAddr;
    return NET_OK;
}

net_err_t WinNetServerListen(net_socket_t *pSock, int Backlog)
{
    if (pSock == NULL) {
        return NET_ERR_PARAM;
    }

    if (listen(pSock->Handle, Backlog) == SOCKET_ERROR) {
        pr_err("[win_net] listen() failed: %d\n", WSAGetLastError());
        return NET_ERR_LISTEN;
    }

    return NET_OK;
}

net_socket_t *WinNetServerAccept(net_socket_t *pSock, net_addr_t *pPeerAddr)
{
    net_socket_t       *pClient;
    struct sockaddr_in  PeerSa;
    int                 PeerSaLen = sizeof(PeerSa);
    SOCKET              ClientHandle;

    if (pSock == NULL) {
        return NULL;
    }

    ClientHandle = accept(pSock->Handle,
                          (struct sockaddr *)&PeerSa, &PeerSaLen);
    if (ClientHandle == INVALID_SOCKET) {
        pr_err("[win_net] accept() failed: %d\n", WSAGetLastError());
        return NULL;
    }

    pClient = (net_socket_t *)calloc(1, sizeof(net_socket_t));
    if (pClient == NULL) {
        closesocket(ClientHandle);
        return NULL;
    }

    pClient->Handle      = ClientHandle;
    pClient->IsConnected = TRUE;

    if (pPeerAddr != NULL) {
        memset(pPeerAddr, 0, sizeof(*pPeerAddr));
        pPeerAddr->port = ntohs(PeerSa.sin_port);
        inet_ntop(AF_INET, &PeerSa.sin_addr,
                  pPeerAddr->ip_str, sizeof(pPeerAddr->ip_str));
        pClient->PeerAddr = *pPeerAddr;
    }

    return pClient;
}

/* -------------------------------------------------------------------------
 * Client-side operations
 * ---------------------------------------------------------------------- */

net_err_t WinNetClientConnect(net_socket_t *pSock, const net_addr_t *pAddr)
{
    struct sockaddr_in  Sa4;
    struct sockaddr_in6 Sa6;
    struct sockaddr    *pSa   = NULL;
    int                 SaLen = 0;
    int                 WsaErr;

    if (pSock == NULL || pAddr == NULL) {
        return NET_ERR_PARAM;
    }

    if (!WinNetFillSockAddr(pAddr, &Sa4, &Sa6, &pSa, &SaLen)) {
        return NET_ERR_PARAM;
    }

    if (connect(pSock->Handle, pSa, SaLen) == SOCKET_ERROR) {
        WsaErr = WSAGetLastError();
        pr_err("[win_net] connect() failed: %d\n", WsaErr);
        return WinNetMapWsaError(WsaErr);
    }

    pSock->PeerAddr    = *pAddr;
    pSock->IsConnected = TRUE;
    return NET_OK;
}

/* -------------------------------------------------------------------------
 * Data transfer
 * ---------------------------------------------------------------------- */

net_err_t WinNetSend(net_socket_t *pSock, const void *pBuf, size_t Len,
                     size_t *pSent)
{
    int SentBytes;
    int WsaErr;

    if (pSock == NULL || pBuf == NULL || Len == 0) {
        return NET_ERR_PARAM;
    }

    if (pSent != NULL) {
        *pSent = 0;
    }

    SentBytes = send(pSock->Handle, (const char *)pBuf, (int)Len, 0);
    if (SentBytes == SOCKET_ERROR) {
        WsaErr = WSAGetLastError();
        pr_err("[win_net] send() failed: %d\n", WsaErr);
        return WinNetMapWsaError(WsaErr);
    }

    if (pSent != NULL) {
        *pSent = (size_t)SentBytes;
    }

    return NET_OK;
}

net_err_t WinNetRecv(net_socket_t *pSock, void *pBuf, size_t BufLen,
                     size_t *pReceived)
{
    int RecvBytes;
    int WsaErr;

    if (pSock == NULL || pBuf == NULL || BufLen == 0) {
        return NET_ERR_PARAM;
    }

    if (pReceived != NULL) {
        *pReceived = 0;
    }

    RecvBytes = recv(pSock->Handle, (char *)pBuf, (int)BufLen, 0);
    if (RecvBytes == 0) {
        return NET_ERR_CLOSED;
    }

    if (RecvBytes == SOCKET_ERROR) {
        WsaErr = WSAGetLastError();
        pr_err("[win_net] recv() failed: %d\n", WsaErr);
        return WinNetMapWsaError(WsaErr);
    }

    if (pReceived != NULL) {
        *pReceived = (size_t)RecvBytes;
    }

    return NET_OK;
}

#endif
