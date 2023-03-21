/**
 @file  win32.c
 @brief ENet Win32 system specific functions
*/

#ifndef _WIN32
#error "this file should only be used on Windows platforms"
#endif

#include "enetcpp/enetcpp.h"
#include <windows.h>
#include <mmsystem.h>
#include <ws2ipdef.h>

namespace ENet
{

struct Win32Platform final : public Platform
{
    int initialize() override;
    void deinitialize() override;
    uint32_t time_get() override;
    void time_set(uint32_t newTimeBase) override;
    ENet::Socket socket_create(SocketType type) override;
    int socket_bind(ENet::Socket socket, const Address *address) override;
    int socket_get_address(ENet::Socket socket, Address *address) override;
    int socket_listen(ENet::Socket socket, int backlog) override;
    ENet::Socket socket_accept(ENet::Socket socket, Address *address) override;
    int socket_connect(ENet::Socket socket, const Address *address) override;
    int socket_send(ENet::Socket socket, const Address *address, const ENet::Buffer *buffers,
                    size_t bufferCount) override;
    int socket_receive(ENet::Socket socket, Address *address, ENet::Buffer *buffers, size_t bufferCount) override;
    int socket_wait(ENet::Socket socket, uint32_t *condition, uint32_t timeout) override;
    int socket_set_option(ENet::Socket socket, SocketOption option, int value) override;
    int socket_get_option(ENet::Socket socket, SocketOption option, int *value) override;
    int socket_shutdown(ENet::Socket socket, SocketShutdown how) override;
    void socket_destroy(ENet::Socket socket) override;
    int socketset_select(ENet::Socket maxSocket, ENet::SocketSet *readSet, ENet::SocketSet *writeSet,
                         uint32_t timeout) override;
    int address_set_host_ip(Address *address, const char *hostName) override;
    int address_set_host(Address *address, const char *hostName) override;
    int address_get_host_ip(const Address *address, char *hostName, size_t nameLength) override;
    int address_get_host(const Address *address, char *hostName, size_t nameLength) override;
    uint32_t host_random_seed() override;
    uint16_t HOST_TO_NET_16(const uint16_t value) override;
    uint32_t HOST_TO_NET_32(const uint32_t value) override;
    uint16_t NET_TO_HOST_16(const uint16_t value) override;
    uint32_t NET_TO_HOST_32(const uint32_t value) override;
};

Platform &Platform::Get()
{
    static Win32Platform platform;
    return platform;
}

static uint32_t timeBase = 0;

} // namespace ENet

int ENet::Win32Platform::initialize()
{
    WORD versionRequested = MAKEWORD(1, 1);
    WSADATA wsaData;

    if (WSAStartup(versionRequested, &wsaData))
    {
        return -1;
    }

    if (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1)
    {
        WSACleanup();

        return -1;
    }

    timeBeginPeriod(1);

    return 0;
}

void ENet::Win32Platform::deinitialize()
{
    timeEndPeriod(1);

    WSACleanup();
}

uint32_t ENet::Win32Platform::time_get()
{
    return (uint32_t)timeGetTime() - timeBase;
}

void ENet::Win32Platform::time_set(uint32_t newTimeBase)
{
    timeBase = (uint32_t)timeGetTime() - newTimeBase;
}

uint32_t ENet::Win32Platform::host_random_seed()
{
    return (uint32_t)timeGetTime();
}

int ENet::Win32Platform::address_set_host_ip(ENet::Address *address, const char *name)
{
    uint8_t vals[4] = {0, 0, 0, 0};
    int i;

    for (i = 0; i < 4; ++i)
    {
        const char *next = name + 1;
        if (*name != '0')
        {
            long val = strtol(name, (char **)&next, 10);
            if (val < 0 || val > 255 || next == name || next - name > 3)
            {
                return -1;
            }
            vals[i] = (uint8_t)val;
        }

        if (*next != (i < 3 ? '.' : '\0'))
        {
            return -1;
        }
        name = next + 1;
    }

    memcpy(&address->host, vals, sizeof(uint32_t));
    return 0;
}

int ENet::Win32Platform::address_set_host(ENet::Address *address, const char *name)
{
    struct hostent *hostEntry;

    hostEntry = gethostbyname(name);
    if (hostEntry == NULL || hostEntry->h_addrtype != AF_INET)
    {
        return ENet::address_set_host_ip(address, name);
    }

    address->host = *(uint32_t *)hostEntry->h_addr_list[0];

    return 0;
}

int ENet::Win32Platform::address_get_host_ip(const ENet::Address *address, char *name, size_t nameLength)
{
    char *addr = inet_ntoa(*(struct in_addr *)&address->host);
    if (addr == NULL)
    {
        return -1;
    }
    else
    {
        size_t addrLen = strlen(addr);
        if (addrLen >= nameLength)
        {
            return -1;
        }
        memcpy(name, addr, addrLen + 1);
    }
    return 0;
}

int ENet::Win32Platform::address_get_host(const ENet::Address *address, char *name, size_t nameLength)
{
    struct in_addr in;
    struct hostent *hostEntry;

    in.s_addr = address->host;

    hostEntry = gethostbyaddr((char *)&in, sizeof(struct in_addr), AF_INET);
    if (hostEntry == NULL)
    {
        return ENet::address_get_host_ip(address, name, nameLength);
    }
    else
    {
        size_t hostLen = strlen(hostEntry->h_name);
        if (hostLen >= nameLength)
        {
            return -1;
        }
        memcpy(name, hostEntry->h_name, hostLen + 1);
    }

    return 0;
}

int ENet::Win32Platform::socket_bind(ENet::Socket socket, const ENet::Address *address)
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

    return bind(socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == SOCKET_ERROR ? -1 : 0;
}

int ENet::Win32Platform::socket_get_address(ENet::Socket socket, ENet::Address *address)
{
    struct sockaddr_in sin;
    int sinLength = sizeof(struct sockaddr_in);

    if (getsockname(socket, (struct sockaddr *)&sin, &sinLength) == -1)
    {
        return -1;
    }

    address->host = (uint32_t)sin.sin_addr.s_addr;
    address->port = ENet::NET_TO_HOST_16(sin.sin_port);

    return 0;
}

int ENet::Win32Platform::socket_listen(ENet::Socket socket, int backlog)
{
    return listen(socket, backlog < 0 ? SOMAXCONN : backlog) == SOCKET_ERROR ? -1 : 0;
}

ENet::Socket ENet::Win32Platform::socket_create(SocketType type)
{
    return socket(PF_INET, type == ENet::SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
}

int ENet::Win32Platform::socket_set_option(ENet::Socket socket, SocketOption option, int value)
{
    int result = SOCKET_ERROR;
    switch (option)
    {
    case ENet::SOCKOPT_NONBLOCK: {
        u_long nonBlocking = (u_long)value;
        result = ioctlsocket(socket, FIONBIO, &nonBlocking);
        break;
    }

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

    case ENet::SOCKOPT_RCVTIMEO:
        result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_SNDTIMEO:
        result = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_NODELAY:
        result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&value, sizeof(int));
        break;

    case ENet::SOCKOPT_TTL:
        result = setsockopt(socket, IPPROTO_IP, IP_TTL, (char *)&value, sizeof(int));
        break;

    default:
        break;
    }
    return result == SOCKET_ERROR ? -1 : 0;
}

int ENet::Win32Platform::socket_get_option(ENet::Socket socket, SocketOption option, int *value)
{
    int result = SOCKET_ERROR, len;
    switch (option)
    {
    case ENet::SOCKOPT_ERROR:
        len = sizeof(int);
        result = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)value, &len);
        break;

    case ENet::SOCKOPT_TTL:
        len = sizeof(int);
        result = getsockopt(socket, IPPROTO_IP, IP_TTL, (char *)value, &len);
        break;

    default:
        break;
    }
    return result == SOCKET_ERROR ? -1 : 0;
}

int ENet::Win32Platform::socket_connect(ENet::Socket socket, const ENet::Address *address)
{
    struct sockaddr_in sin;
    int result;

    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_port = ENet::HOST_TO_NET_16(address->port);
    sin.sin_addr.s_addr = address->host;

    result = connect(socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        return -1;
    }

    return 0;
}

ENet::Socket ENet::Win32Platform::socket_accept(ENet::Socket socket, ENet::Address *address)
{
    SOCKET result;
    struct sockaddr_in sin;
    int sinLength = sizeof(struct sockaddr_in);

    result = accept(socket, address != NULL ? (struct sockaddr *)&sin : NULL, address != NULL ? &sinLength : NULL);

    if (result == INVALID_SOCKET)
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

int ENet::Win32Platform::socket_shutdown(ENet::Socket socket, SocketShutdown how)
{
    return shutdown(socket, (int)how) == SOCKET_ERROR ? -1 : 0;
}

void ENet::Win32Platform::socket_destroy(ENet::Socket socket)
{
    if (socket != INVALID_SOCKET)
    {
        closesocket(socket);
    }
}

int ENet::Win32Platform::socket_send(ENet::Socket socket, const ENet::Address *address, const ENet::Buffer *buffers,
                                     size_t bufferCount)
{
    struct sockaddr_in sin;
    DWORD sentLength = 0;

    if (address != NULL)
    {
        memset(&sin, 0, sizeof(struct sockaddr_in));

        sin.sin_family = AF_INET;
        sin.sin_port = ENet::HOST_TO_NET_16(address->port);
        sin.sin_addr.s_addr = address->host;
    }

    if (WSASendTo(socket, (LPWSABUF)buffers, (DWORD)bufferCount, &sentLength, 0,
                  address != NULL ? (struct sockaddr *)&sin : NULL, address != NULL ? sizeof(struct sockaddr_in) : 0,
                  NULL, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return 0;
        }

        return -1;
    }

    return (int)sentLength;
}

int ENet::Win32Platform::socket_receive(ENet::Socket socket, ENet::Address *address, ENet::Buffer *buffers,
                                        size_t bufferCount)
{
    INT sinLength = sizeof(struct sockaddr_in);
    DWORD flags = 0, recvLength = 0;
    struct sockaddr_in sin;

    if (WSARecvFrom(socket, (LPWSABUF)buffers, (DWORD)bufferCount, &recvLength, &flags,
                    address != NULL ? (struct sockaddr *)&sin : NULL, address != NULL ? &sinLength : NULL, NULL,
                    NULL) == SOCKET_ERROR)
    {
        switch (WSAGetLastError())
        {
        case WSAEWOULDBLOCK:
        case WSAECONNRESET:
            return 0;
        }

        return -1;
    }

    if (flags & MSG_PARTIAL)
    {
        return -1;
    }

    if (address != NULL)
    {
        address->host = (uint32_t)sin.sin_addr.s_addr;
        address->port = ENet::NET_TO_HOST_16(sin.sin_port);
    }

    return (int)recvLength;
}

int ENet::Win32Platform::socketset_select(ENet::Socket maxSocket, ENet::SocketSet *readSet, ENet::SocketSet *writeSet,
                                          uint32_t timeout)
{
    struct timeval timeVal;

    timeVal.tv_sec = timeout / 1000;
    timeVal.tv_usec = (timeout % 1000) * 1000;

    return select(maxSocket + 1, readSet, writeSet, NULL, &timeVal);
}

int ENet::Win32Platform::socket_wait(ENet::Socket socket, uint32_t *condition, uint32_t timeout)
{
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
}

uint16_t ENet::Win32Platform::HOST_TO_NET_16(const uint16_t value)
{
    return htons(value);
}

uint32_t ENet::Win32Platform::HOST_TO_NET_32(const uint32_t value)
{
    return htonl(value);
}

uint16_t ENet::Win32Platform::NET_TO_HOST_16(const uint16_t value)
{
    return ntohs(value);
}

uint32_t ENet::Win32Platform::NET_TO_HOST_32(const uint32_t value)
{
    return ntohl(value);
}
