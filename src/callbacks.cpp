/**
 @file callbacks.c
 @brief ENet callback functions
*/

#include "enetcpp/enetcpp.h"

static ENetCallbacks callbacks = {malloc, free, abort};

int ENet::initialize_with_callbacks(ENetVersion version, const ENetCallbacks *inits)
{
    if (version < ENET_VERSION_CREATE(1, 3, 0))
    {
        return -1;
    }

    if (inits->malloc != NULL || inits->free != NULL)
    {
        if (inits->malloc == NULL || inits->free == NULL)
        {
            return -1;
        }

        callbacks.malloc = inits->malloc;
        callbacks.free = inits->free;
    }

    if (inits->no_memory != NULL)
    {
        callbacks.no_memory = inits->no_memory;
    }

    return ENet::initialize();
}

ENetVersion ENet::linked_version()
{
    return ENET_VERSION;
}

void *ENet::enet_malloc(size_t size)
{
    void *memory = callbacks.malloc(size);

    if (memory == NULL)
    {
        callbacks.no_memory();
    }

    return memory;
}

void ENet::enet_free(void *memory)
{
    callbacks.free(memory);
}
