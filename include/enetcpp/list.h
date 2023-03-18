/**
 @file  list.h
 @brief ENet list management
*/
#pragma once

#include <stdlib.h>

namespace ENet
{

struct ListNode
{
    ListNode *next;
    ListNode *previous;
};

using ListIterator = ListNode *;

struct List
{
    ListNode sentinel;
};

extern void list_clear(List *list);

extern ListIterator list_insert(ListIterator position, void *data);
extern void *list_remove(ListIterator position);
extern ListIterator list_move(ListIterator position, void *dataFirst, void *dataLast);

extern size_t list_size(List *list);

inline ListIterator list_begin(List *list)
{
    return (list)->sentinel.next;
}

inline ListIterator list_end(List *list)
{
    return &(list)->sentinel;
}

inline bool list_empty(List *list)
{
    return list_begin(list) == list_end(list);
}

inline ListIterator list_next(ListIterator iterator)
{
    return iterator->next;
}

inline ListIterator list_previous(ListIterator iterator)
{
    return iterator->previous;
}

inline void *list_front(List *list)
{
    return ((void *)(list)->sentinel.next);
}

inline void *list_back(List *list)
{
    return ((void *)(list)->sentinel.previous);
}

} // namespace ENet
