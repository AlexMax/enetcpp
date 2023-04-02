/**
 @file  callbacks.h
 @brief ENet callbacks
*/
#pragma once

#include <cstdlib>

namespace ENet
{

struct Callbacks
{
    void *(*malloc)(size_t size);
    void (*free)(void *memory);
    void (*no_memory)();
};

/** @defgroup callbacks ENet internal callbacks
    @{
    @ingroup private
*/

extern void *enet_malloc(size_t size);
extern void enet_free(void *memory);

} // namespace ENet

/** @} */
