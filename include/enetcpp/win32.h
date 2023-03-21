/**
 @file  win32.h
 @brief ENet Win32 header
*/
#pragma once

#include <stdlib.h>
#include <winsock2.h>

#define ENET_SOCKET_NULL INVALID_SOCKET

#define ENET_API extern

#define ENET_SOCKETSET_EMPTY(sockset) FD_ZERO(&(sockset))
#define ENET_SOCKETSET_ADD(sockset, socket) FD_SET(socket, &(sockset))
#define ENET_SOCKETSET_REMOVE(sockset, socket) FD_CLR(socket, &(sockset))
#define ENET_SOCKETSET_CHECK(sockset, socket) FD_ISSET(socket, &(sockset))

namespace ENet
{

using Socket = SOCKET;

struct Buffer
{
    size_t dataLength;
    void *data;
};

using SocketSet = fd_set;

} // namespace ENet
