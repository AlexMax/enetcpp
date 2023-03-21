/**
 @file  platform.c
 @brief ENet platform-specific dispatching.
*/

#include "enetcpp/enetcpp.h"

int ENet::initialize()
{
    return ENet::Platform::Get().initialize();
}

void ENet::deinitialize()
{
    ENet::Platform::Get().deinitialize();
}

uint32_t ENet::time_get()
{
    return ENet::Platform::Get().time_get();
}

void ENet::time_set(uint32_t newTimeBase)
{
    return ENet::Platform::Get().time_set(newTimeBase);
}

ENet::Socket ENet::socket_create(ENet::SocketType type)
{
    return ENet::Platform::Get().socket_create(type);
}

int ENet::socket_bind(ENet::Socket socket, const ENet::Address *address)
{
    return ENet::Platform::Get().socket_bind(socket, address);
}

int ENet::socket_get_address(ENet::Socket socket, ENet::Address *address)
{
    return ENet::Platform::Get().socket_get_address(socket, address);
}

int ENet::socket_listen(ENet::Socket socket, int backlog)
{
    return ENet::Platform::Get().socket_listen(socket, backlog);
}

ENet::Socket ENet::socket_accept(ENet::Socket socket, ENet::Address *address)
{
    return ENet::Platform::Get().socket_accept(socket, address);
}

int ENet::socket_connect(ENet::Socket socket, const ENet::Address *address)
{
    return ENet::Platform::Get().socket_connect(socket, address);
}

int ENet::socket_send(ENet::Socket socket, const ENet::Address *address, const ENet::Buffer *buffers,
                      size_t bufferCount)
{
    return ENet::Platform::Get().socket_send(socket, address, buffers, bufferCount);
}

int ENet::socket_receive(ENet::Socket socket, ENet::Address *address, ENet::Buffer *buffers, size_t bufferCount)
{
    return ENet::Platform::Get().socket_receive(socket, address, buffers, bufferCount);
}

int ENet::socket_wait(ENet::Socket socket, uint32_t *condition, uint32_t timeout)
{
    return ENet::Platform::Get().socket_wait(socket, condition, timeout);
}

int ENet::socket_set_option(ENet::Socket socket, ENet::SocketOption option, int value)
{
    return ENet::Platform::Get().socket_set_option(socket, option, value);
}

int ENet::socket_get_option(ENet::Socket socket, ENet::SocketOption option, int *value)
{
    return ENet::Platform::Get().socket_get_option(socket, option, value);
}

int ENet::socket_shutdown(ENet::Socket socket, ENet::SocketShutdown how)
{
    return ENet::Platform::Get().socket_shutdown(socket, how);
}

void ENet::socket_destroy(ENet::Socket socket)
{
    return ENet::Platform::Get().socket_destroy(socket);
}

int ENet::socketset_select(ENet::Socket maxSocket, ENet::SocketSet *readSet, ENet::SocketSet *writeSet,
                           uint32_t timeout)
{
    return ENet::Platform::Get().socketset_select(maxSocket, readSet, writeSet, timeout);
}

int ENet::address_set_host_ip(ENet::Address *address, const char *hostName)
{
    return ENet::Platform::Get().address_set_host_ip(address, hostName);
}

int ENet::address_set_host(ENet::Address *address, const char *hostName)
{
    return ENet::Platform::Get().address_set_host(address, hostName);
}

int ENet::address_get_host_ip(const ENet::Address *address, char *hostName, size_t nameLength)
{
    return ENet::Platform::Get().address_get_host_ip(address, hostName, nameLength);
}

int ENet::address_get_host(const ENet::Address *address, char *hostName, size_t nameLength)
{
    return ENet::Platform::Get().address_get_host(address, hostName, nameLength);
}

uint32_t ENet::host_random_seed()
{
    return ENet::Platform::Get().host_random_seed();
}

uint16_t ENet::HOST_TO_NET_16(const uint16_t value)
{
    return ENet::Platform::Get().HOST_TO_NET_16(value);
}

uint32_t ENet::HOST_TO_NET_32(const uint32_t value)
{
    return ENet::Platform::Get().HOST_TO_NET_32(value);
}

uint16_t ENet::NET_TO_HOST_16(const uint16_t value)
{
    return ENet::Platform::Get().NET_TO_HOST_16(value);
}

uint32_t ENet::NET_TO_HOST_32(const uint32_t value)
{
    return ENet::Platform::Get().NET_TO_HOST_32(value);
}
