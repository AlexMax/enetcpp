/**
 @file  unix.h
 @brief ENet Unix header
*/
#pragma once

#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#ifdef MSG_MAXIOVLEN
#define ENET_BUFFER_MAXIMUM MSG_MAXIOVLEN
#endif

#define ENET_SOCKET_NULL -1

#define ENET_API extern

#define ENET_SOCKETSET_EMPTY(sockset) FD_ZERO(&(sockset))
#define ENET_SOCKETSET_ADD(sockset, socket) FD_SET(socket, &(sockset))
#define ENET_SOCKETSET_REMOVE(sockset, socket) FD_CLR(socket, &(sockset))
#define ENET_SOCKETSET_CHECK(sockset, socket) FD_ISSET(socket, &(sockset))

namespace ENet
{

using Socket = int;

struct Buffer
{
    void *data;
    size_t dataLength;
};

using SocketSet = fd_set;

} // namespace ENet
