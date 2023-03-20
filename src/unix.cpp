/**
 @file  unix.c
 @brief ENet Unix system specific functions
*/

#ifndef __unix__
#error "this file should only be used on UNIX-like platforms"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "enetcpp/enetcpp.h"

namespace ENet
{

struct UNIXPlatform final : public Platform
{
    int initialize() override;
    void deinitialize() override;
    uint32_t time_get() override;
    void time_set(uint32_t newTimeBase) override;
    ENetSocket socket_create(ENet::SocketType type) override;
    int socket_bind(ENetSocket socket, const ENet::Address *address) override;
    int socket_get_address(ENetSocket socket, ENet::Address *address) override;
    int socket_listen(ENetSocket socket, int backlog) override;
    ENetSocket socket_accept(ENetSocket socket, ENet::Address *address) override;
    int socket_connect(ENetSocket socket, const ENet::Address *address) override;
    int socket_send(ENetSocket socket, const ENet::Address *address, const ENetBuffer *buffers,
                    size_t bufferCount) override;
    int socket_receive(ENetSocket socket, ENet::Address *address, ENetBuffer *buffers, size_t bufferCount) override;
    int socket_wait(ENetSocket socket, uint32_t *condition, uint32_t timeout) override;
    int socket_set_option(ENetSocket socket, ENet::SocketOption option, int value) override;
    int socket_get_option(ENetSocket socket, ENet::SocketOption option, int *value) override;
    int socket_shutdown(ENetSocket socket, ENet::SocketShutdown how) override;
    void socket_destroy(ENetSocket socket) override;
    int socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet, ENetSocketSet *writeSet,
                         uint32_t timeout) override;
    int address_set_host_ip(ENet::Address *address, const char *hostName) override;
    int address_set_host(ENet::Address *address, const char *hostName) override;
    int address_get_host_ip(const ENet::Address *address, char *hostName, size_t nameLength) override;
    int address_get_host(const ENet::Address *address, char *hostName, size_t nameLength) override;
    uint32_t host_random_seed() override;
    uint16_t HOST_TO_NET_16(const uint16_t value) override;
    uint32_t HOST_TO_NET_32(const uint32_t value) override;
    uint16_t NET_TO_HOST_16(const uint16_t value) override;
    uint32_t NET_TO_HOST_32(const uint32_t value) override;
};

Platform &Platform::Get()
{
    static UNIXPlatform platform;
    return platform;
}

#ifdef __APPLE__
#ifdef HAS_POLL
#undef HAS_POLL
#endif
#ifndef HAS_FCNTL
#define HAS_FCNTL 1
#endif
#ifndef HAS_INET_PTON
#define HAS_INET_PTON 1
#endif
#ifndef HAS_INET_NTOP
#define HAS_INET_NTOP 1
#endif
#ifndef HAS_MSGHDR_FLAGS
#define HAS_MSGHDR_FLAGS 1
#endif
#ifndef HAS_SOCKLEN_T
#define HAS_SOCKLEN_T 1
#endif
#ifndef HAS_GETADDRINFO
#define HAS_GETADDRINFO 1
#endif
#ifndef HAS_GETNAMEINFO
#define HAS_GETNAMEINFO 1
#endif
#endif

#ifdef HAS_FCNTL
#include <fcntl.h>
#endif

#ifdef HAS_POLL
#include <poll.h>
#endif

#if !defined(HAS_SOCKLEN_T) && !defined(__socklen_t_defined)
typedef int socklen_t;
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static uint32_t timeBase = 0;

int UNIXPlatform::initialize()
{
    return 0;
}

void UNIXPlatform::deinitialize()
{
}

uint32_t UNIXPlatform::host_random_seed()
{
    return (uint32_t)time(NULL);
}

uint32_t UNIXPlatform::time_get()
{
    struct timeval timeVal;

    gettimeofday(&timeVal, NULL);

    return timeVal.tv_sec * 1000 + timeVal.tv_usec / 1000 - timeBase;
}

void UNIXPlatform::time_set(uint32_t newTimeBase)
{
    struct timeval timeVal;

    gettimeofday(&timeVal, NULL);

    timeBase = timeVal.tv_sec * 1000 + timeVal.tv_usec / 1000 - newTimeBase;
}

int UNIXPlatform::address_set_host_ip(ENet::Address *address, const char *name)
{
#ifdef HAS_INET_PTON
    if (!inet_pton(AF_INET, name, &address->host))
#else
    if (!inet_aton(name, (struct in_addr *)&address->host))
#endif
        return -1;

    return 0;
}

int UNIXPlatform::address_set_host(ENet::Address *address, const char *name)
{
#ifdef HAS_GETADDRINFO
    struct addrinfo hints, *resultList = NULL, *result = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (getaddrinfo(name, NULL, NULL, &resultList) != 0)
    {
        return -1;
    }

    for (result = resultList; result != NULL; result = result->ai_next)
    {
        if (result->ai_family == AF_INET && result->ai_addr != NULL && result->ai_addrlen >= sizeof(struct sockaddr_in))
        {
            struct sockaddr_in *sin = (struct sockaddr_in *)result->ai_addr;

            address->host = sin->sin_addr.s_addr;

            freeaddrinfo(resultList);

            return 0;
        }
    }

    if (resultList != NULL)
    {
        freeaddrinfo(resultList);
    }
#else
    struct hostent *hostEntry = NULL;
#ifdef HAS_GETHOSTBYNAME_R
    struct hostent hostData;
    char buffer[2048];
    int errnum;

#if defined(linux) || defined(__linux) || defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
    defined(__DragonFly__) || defined(__GNU__)
    gethostbyname_r(name, &hostData, buffer, sizeof(buffer), &hostEntry, &errnum);
#else
    hostEntry = gethostbyname_r(name, &hostData, buffer, sizeof(buffer), &errnum);
#endif
#else
    hostEntry = gethostbyname(name);
#endif

    if (hostEntry != NULL && hostEntry->h_addrtype == AF_INET)
    {
        address->host = *(uint32_t *)hostEntry->h_addr_list[0];

        return 0;
    }
#endif

    return ENet::address_set_host_ip(address, name);
}

int UNIXPlatform::address_get_host_ip(const ENet::Address *address, char *name, size_t nameLength)
{
#ifdef HAS_INET_NTOP
    if (inet_ntop(AF_INET, &address->host, name, nameLength) == NULL)
#else
    char *addr = inet_ntoa(*(struct in_addr *)&address->host);
    if (addr != NULL)
    {
        size_t addrLen = strlen(addr);
        if (addrLen >= nameLength)
        {
            return -1;
        }
        memcpy(name, addr, addrLen + 1);
    }
    else
#endif
        return -1;
    return 0;
}

int UNIXPlatform::address_get_host(const ENet::Address *address, char *name, size_t nameLength)
{
#ifdef HAS_GETNAMEINFO
    struct sockaddr_in sin;
    int err;

    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_port = ENet::HOST_TO_NET_16(address->port);
    sin.sin_addr.s_addr = address->host;

    err = getnameinfo((struct sockaddr *)&sin, sizeof(sin), name, nameLength, NULL, 0, NI_NAMEREQD);
    if (!err)
    {
        if (name != NULL && nameLength > 0 && !memchr(name, '\0', nameLength))
        {
            return -1;
        }
        return 0;
    }
    if (err != EAI_NONAME)
    {
        return -1;
    }
#else
    struct in_addr in;
    struct hostent *hostEntry = NULL;
#ifdef HAS_GETHOSTBYADDR_R
    struct hostent hostData;
    char buffer[2048];
    int errnum;

    in.s_addr = address->host;

#if defined(linux) || defined(__linux) || defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
    defined(__DragonFly__) || defined(__GNU__)
    gethostbyaddr_r((char *)&in, sizeof(struct in_addr), AF_INET, &hostData, buffer, sizeof(buffer), &hostEntry,
                    &errnum);
#else
    hostEntry =
        gethostbyaddr_r((char *)&in, sizeof(struct in_addr), AF_INET, &hostData, buffer, sizeof(buffer), &errnum);
#endif
#else
    in.s_addr = address->host;

    hostEntry = gethostbyaddr((char *)&in, sizeof(struct in_addr), AF_INET);
#endif

    if (hostEntry != NULL)
    {
        size_t hostLen = strlen(hostEntry->h_name);
        if (hostLen >= nameLength)
        {
            return -1;
        }
        memcpy(name, hostEntry->h_name, hostLen + 1);
        return 0;
    }
#endif

    return ENet::address_get_host_ip(address, name, nameLength);
}

int UNIXPlatform::socket_bind(ENetSocket socket, const ENet::Address *address)
{
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;

    if (address != NULL)
    {
        sin.sin_port = ENet::HOST_TO_NET_16(address->port);
        sin.sin_addr.s_addr = address->host;
    }
    else
    {
        sin.sin_port = 0;
        sin.sin_addr.s_addr = INADDR_ANY;
    }

    return bind(socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
}

int UNIXPlatform::socket_get_address(ENetSocket socket, ENet::Address *address)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof(struct sockaddr_in);

    if (getsockname(socket, (struct sockaddr *)&sin, &sinLength) == -1)
    {
        return -1;
    }

    address->host = (uint32_t)sin.sin_addr.s_addr;
    address->port = ENet::NET_TO_HOST_16(sin.sin_port);

    return 0;
}

int UNIXPlatform::socket_listen(ENetSocket socket, int backlog)
{
    return listen(socket, backlog < 0 ? SOMAXCONN : backlog);
}

ENetSocket UNIXPlatform::socket_create(ENet::SocketType type)
{
    return socket(PF_INET, type == ENet::SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
}

int UNIXPlatform::socket_set_option(ENetSocket socket, ENet::SocketOption option, int value)
{
    int result = -1;
    switch (option)
    {
    case ENet::SOCKOPT_NONBLOCK:
#ifdef HAS_FCNTL
        result = fcntl(socket, F_SETFL, (value ? O_NONBLOCK : 0) | (fcntl(socket, F_GETFL) & ~O_NONBLOCK));
#else
        result = ioctl(socket, FIONBIO, &value);
#endif
        break;

    case ENet::SOCKOPT_BROADCAST:
        result = setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_REUSEADDR:
        result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_RCVBUF:
        result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_SNDBUF:
        result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_RCVTIMEO: {
        struct timeval timeVal;
        timeVal.tv_sec = value / 1000;
        timeVal.tv_usec = (value % 1000) * 1000;
        result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeVal, sizeof(struct timeval));
        break;
    }

    case ENet::SOCKOPT_SNDTIMEO: {
        struct timeval timeVal;
        timeVal.tv_sec = value / 1000;
        timeVal.tv_usec = (value % 1000) * 1000;
        result = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeVal, sizeof(struct timeval));
        break;
    }

    case ENet::SOCKOPT_NODELAY:
        result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_TTL:
        result = setsockopt(socket, IPPROTO_IP, IP_TTL, (char *)&value, sizeof(int));
        break;

    default:
        break;
    }
    return result == -1 ? -1 : 0;
}

int UNIXPlatform::socket_get_option(ENetSocket socket, ENet::SocketOption option, int *value)
{
    int result = -1;
    socklen_t len;
    switch (option)
    {
    case ENet::SOCKOPT_ERROR:
        len = sizeof(int);
        result = getsockopt(socket, SOL_SOCKET, SO_ERROR, value, &len);
        break;

    case ENet::SOCKOPT_TTL:
        len = sizeof(int);
        result = getsockopt(socket, IPPROTO_IP, IP_TTL, (char *)value, &len);
        break;

    default:
        break;
    }
    return result == -1 ? -1 : 0;
}

int UNIXPlatform::socket_connect(ENetSocket socket, const ENet::Address *address)
{
    struct sockaddr_in sin;
    int result;

    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_port = ENet::HOST_TO_NET_16(address->port);
    sin.sin_addr.s_addr = address->host;

    result = connect(socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
    if (result == -1 && errno == EINPROGRESS)
    {
        return 0;
    }

    return result;
}

ENetSocket UNIXPlatform::socket_accept(ENetSocket socket, ENet::Address *address)
{
    int result;
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof(struct sockaddr_in);

    result = accept(socket, address != NULL ? (struct sockaddr *)&sin : NULL, address != NULL ? &sinLength : NULL);

    if (result == -1)
    {
        return ENET_SOCKET_NULL;
    }

    if (address != NULL)
    {
        address->host = (uint32_t)sin.sin_addr.s_addr;
        address->port = ENet::NET_TO_HOST_16(sin.sin_port);
    }

    return result;
}

int UNIXPlatform::socket_shutdown(ENetSocket socket, ENet::SocketShutdown how)
{
    return shutdown(socket, (int)how);
}

void UNIXPlatform::socket_destroy(ENetSocket socket)
{
    if (socket != -1)
    {
        close(socket);
    }
}

int UNIXPlatform::socket_send(ENetSocket socket, const ENet::Address *address, const ENetBuffer *buffers,
                              size_t bufferCount)
{
    struct msghdr msgHdr;
    struct sockaddr_in sin;
    int sentLength;

    memset(&msgHdr, 0, sizeof(struct msghdr));

    if (address != NULL)
    {
        memset(&sin, 0, sizeof(struct sockaddr_in));

        sin.sin_family = AF_INET;
        sin.sin_port = ENet::HOST_TO_NET_16(address->port);
        sin.sin_addr.s_addr = address->host;

        msgHdr.msg_name = &sin;
        msgHdr.msg_namelen = sizeof(struct sockaddr_in);
    }

    msgHdr.msg_iov = (struct iovec *)buffers;
    msgHdr.msg_iovlen = bufferCount;

    sentLength = sendmsg(socket, &msgHdr, MSG_NOSIGNAL);

    if (sentLength == -1)
    {
        if (errno == EWOULDBLOCK)
        {
            return 0;
        }

        return -1;
    }

    return sentLength;
}

int UNIXPlatform::socket_receive(ENetSocket socket, ENet::Address *address, ENetBuffer *buffers, size_t bufferCount)
{
    struct msghdr msgHdr;
    struct sockaddr_in sin;
    int recvLength;

    memset(&msgHdr, 0, sizeof(struct msghdr));

    if (address != NULL)
    {
        msgHdr.msg_name = &sin;
        msgHdr.msg_namelen = sizeof(struct sockaddr_in);
    }

    msgHdr.msg_iov = (struct iovec *)buffers;
    msgHdr.msg_iovlen = bufferCount;

    recvLength = recvmsg(socket, &msgHdr, MSG_NOSIGNAL);

    if (recvLength == -1)
    {
        if (errno == EWOULDBLOCK)
        {
            return 0;
        }

        return -1;
    }

#ifdef HAS_MSGHDR_FLAGS
    if (msgHdr.msg_flags & MSG_TRUNC)
    {
        return -1;
    }
#endif

    if (address != NULL)
    {
        address->host = (uint32_t)sin.sin_addr.s_addr;
        address->port = ENet::NET_TO_HOST_16(sin.sin_port);
    }

    return recvLength;
}

int UNIXPlatform::socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet, ENetSocketSet *writeSet,
                                   uint32_t timeout)
{
    struct timeval timeVal;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    return select(maxSocket + 1, readSet, writeSet, NULL, &timeVal);
}

int UNIXPlatform::socket_wait(ENetSocket socket, uint32_t *condition, uint32_t timeout)
{
#ifdef HAS_POLL
    struct pollfd pollSocket;
    int pollCount;

    pollSocket.fd = socket;
    pollSocket.events = 0;

    if (*condition & ENet::SOCKET_WAIT_SEND)
    {
        pollSocket.events |= POLLOUT;
    }

    if (*condition & ENet::SOCKET_WAIT_RECEIVE)
    {
        pollSocket.events |= POLLIN;
    }

    pollCount = poll(&pollSocket, 1, timeout);

    if (pollCount < 0)
    {
        if (errno == EINTR && *condition & ENet::SOCKET_WAIT_INTERRUPT)
        {
            *condition = ENet::SOCKET_WAIT_INTERRUPT;

            return 0;
        }

        return -1;
    }

    *condition = ENet::SOCKET_WAIT_NONE;

    if (pollCount == 0)
    {
        return 0;
    }

    if (pollSocket.revents & POLLOUT)
    {
        *condition |= ENet::SOCKET_WAIT_SEND;
    }

    if (pollSocket.revents & POLLIN)
    {
        *condition |= ENet::SOCKET_WAIT_RECEIVE;
    }

    return 0;
#else
    fd_set readSet, writeSet;
    struct timeval timeVal;
    int selectCount;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);

    if (*condition & ENet::SOCKET_WAIT_SEND)
    {
        FD_SET(socket, &writeSet);
    }

    if (*condition & ENet::SOCKET_WAIT_RECEIVE)
    {
        FD_SET(socket, &readSet);
    }

    selectCount = select(socket + 1, &readSet, &writeSet, NULL, &timeVal);

    if (selectCount < 0)
    {
        if (errno == EINTR && *condition & ENet::SOCKET_WAIT_INTERRUPT)
        {
            *condition = ENet::SOCKET_WAIT_INTERRUPT;

            return 0;
        }

        return -1;
    }

    *condition = ENet::SOCKET_WAIT_NONE;

    if (selectCount == 0)
    {
        return 0;
    }

    if (FD_ISSET(socket, &writeSet))
    {
        *condition |= ENet::SOCKET_WAIT_SEND;
    }

    if (FD_ISSET(socket, &readSet))
    {
        *condition |= ENet::SOCKET_WAIT_RECEIVE;
    }

    return 0;
#endif
}

uint16_t UNIXPlatform::HOST_TO_NET_16(const uint16_t value)
{
    return htons(value);
}

uint32_t UNIXPlatform::HOST_TO_NET_32(const uint32_t value)
{
    return htonl(value);
}

uint16_t UNIXPlatform::NET_TO_HOST_16(const uint16_t value)
{
    return ntohs(value);
}

uint32_t UNIXPlatform::NET_TO_HOST_32(const uint32_t value)
{
    return ntohl(value);
}

} // namespace ENet
