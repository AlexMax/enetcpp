/**
 @file  platform.c
 @brief ENet platform-specific dispatching.
*/

#include "enetcpp/enetcpp.h"

int enet_initialize()
{
    return ENet::Platform::Get().initialize();
}

void enet_deinitialize()
{
    ENet::Platform::Get().deinitialize();
}

uint32_t enet_time_get()
{
    return ENet::Platform::Get().time_get();
}

void enet_time_set(uint32_t newTimeBase)
{
    return ENet::Platform::Get().time_set(newTimeBase);
}

ENetSocket enet_socket_create(ENetSocketType type)
{
    return ENet::Platform::Get().socket_create(type);
}

int enet_socket_bind(ENetSocket socket, const ENetAddress *address)
{
    return ENet::Platform::Get().socket_bind(socket, address);
}

int enet_socket_get_address(ENetSocket socket, ENetAddress *address)
{
    return ENet::Platform::Get().socket_get_address(socket, address);
}

int enet_socket_listen(ENetSocket socket, int backlog)
{
    return ENet::Platform::Get().socket_listen(socket, backlog);
}

ENetSocket enet_socket_accept(ENetSocket socket, ENetAddress *address)
{
    return ENet::Platform::Get().socket_accept(socket, address);
}

int enet_socket_connect(ENetSocket socket, const ENetAddress *address)
{
    return ENet::Platform::Get().socket_connect(socket, address);
}

int enet_socket_send(ENetSocket socket, const ENetAddress *address, const ENetBuffer *buffers, size_t bufferCount)
{
    return ENet::Platform::Get().socket_send(socket, address, buffers, bufferCount);
}

int enet_socket_receive(ENetSocket socket, ENetAddress *address, ENetBuffer *buffers, size_t bufferCount)
{
    return ENet::Platform::Get().socket_receive(socket, address, buffers, bufferCount);
}

int enet_socket_wait(ENetSocket socket, uint32_t *condition, uint32_t timeout)
{
    return ENet::Platform::Get().socket_wait(socket, condition, timeout);
}

int enet_socket_set_option(ENetSocket socket, ENetSocketOption option, int value)
{
    return ENet::Platform::Get().socket_set_option(socket, option, value);
}

int enet_socket_get_option(ENetSocket socket, ENetSocketOption option, int *value)
{
    return ENet::Platform::Get().socket_get_option(socket, option, value);
}

int enet_socket_shutdown(ENetSocket socket, ENetSocketShutdown how)
{
    return ENet::Platform::Get().socket_shutdown(socket, how);
}

void enet_socket_destroy(ENetSocket socket)
{
    return ENet::Platform::Get().socket_destroy(socket);
}

int enet_socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet, ENetSocketSet *writeSet, uint32_t timeout)
{
    return ENet::Platform::Get().socketset_select(maxSocket, readSet, writeSet, timeout);
}

int enet_address_set_host_ip(ENetAddress* address, const char* hostName)
{
    return ENet::Platform::Get().address_set_host_ip(address, hostName);
}

int enet_address_set_host(ENetAddress* address, const char* hostName)
{
    return ENet::Platform::Get().address_set_host(address, hostName);
}

int enet_address_get_host_ip(const ENetAddress* address, char* hostName, size_t nameLength)
{
    return ENet::Platform::Get().address_get_host_ip(address, hostName, nameLength);
}

int enet_address_get_host(const ENetAddress* address, char* hostName, size_t nameLength)
{
    return ENet::Platform::Get().address_get_host(address, hostName, nameLength);
}

uint32_t enet_host_random_seed()
{
    return ENet::Platform::Get().host_random_seed();
}
