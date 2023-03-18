/**
 @file list.c
 @brief ENet linked list functions
*/
#define ENET_BUILDING_LIB 1
#include "enetcpp/enetcpp.h"

/**
    @defgroup list ENet linked list utility functions
    @ingroup private
    @{
*/
void ENet::list_clear(ENet::List *list)
{
    list->sentinel.next = &list->sentinel;
    list->sentinel.previous = &list->sentinel;
}

ENet::ListIterator ENet::list_insert(ENet::ListIterator position, void *data)
{
    ENet::ListIterator result = (ENet::ListIterator)data;

    result->previous = position->previous;
    result->next = position;

    result->previous->next = result;
    position->previous = result;

    return result;
}

void *ENet::list_remove(ENet::ListIterator position)
{
    position->previous->next = position->next;
    position->next->previous = position->previous;

    return position;
}

ENet::ListIterator ENet::list_move(ENet::ListIterator position, void *dataFirst, void *dataLast)
{
    ENet::ListIterator first = (ENet::ListIterator)dataFirst, last = (ENet::ListIterator)dataLast;

    first->previous->next = last->next;
    last->next->previous = first->previous;

    first->previous = position->previous;
    last->next = position;

    first->previous->next = first;
    position->previous = last;

    return first;
}

size_t ENet::list_size(ENet::List *list)
{
    size_t size = 0;
    ENet::ListIterator position;

    for (position = ENet::list_begin(list); position != ENet::list_end(list); position = ENet::list_next(position))
    {
        ++size;
    }

    return size;
}

/** @} */
