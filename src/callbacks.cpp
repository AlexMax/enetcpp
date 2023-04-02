/**
 @file callbacks.c
 @brief ENet callback functions
*/

#include "enetcpp/enetcpp.h"

static ENet::Callbacks callbacks = {malloc, free, abort};

int ENet::initialize_with_callbacks(ENet::Version version, const ENet::Callbacks *inits)
{
    if (version < ENet::VERSION_CREATE(1, 3, 0))
    {
        return -1;
    }

    if (inits->malloc != nullptr || inits->free != nullptr)
    {
        if (inits->malloc == nullptr || inits->free == nullptr)
        {
            return -1;
        }

        callbacks.malloc = inits->malloc;
        callbacks.free = inits->free;
    }

    if (inits->no_memory != nullptr)
    {
        callbacks.no_memory = inits->no_memory;
    }

    return ENet::initialize();
}

ENet::Version ENet::linked_version()
{
    return ENet::VERSION;
}

void *ENet::enet_malloc(size_t size)
{
    void *memory = callbacks.malloc(size);

    if (memory == nullptr)
    {
        callbacks.no_memory();
    }

    return memory;
}

void ENet::enet_free(void *memory)
{
    callbacks.free(memory);
}
