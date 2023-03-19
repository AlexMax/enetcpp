/**
 @file  callbacks.h
 @brief ENet callbacks
*/
#pragma once

#include <stdlib.h>

struct ENetCallbacks
{
    void *(ENET_CALLBACK *malloc)(size_t size);
    void(ENET_CALLBACK *free)(void *memory);
    void(ENET_CALLBACK *no_memory)();
};

/** @defgroup callbacks ENet internal callbacks
    @{
    @ingroup private
*/
namespace ENet
{

extern void *enet_malloc(size_t size);
extern void enet_free(void *memory);

} // namespace ENet

/** @} */
