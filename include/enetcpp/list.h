/**
 @file  list.h
 @brief ENet list management
*/
#pragma once

#include <stdlib.h>

struct ENetListNode
{
    ENetListNode *next;
    ENetListNode *previous;
};

using ENetListIterator = ENetListNode *;

struct ENetList
{
    ENetListNode sentinel;
};

extern void enet_list_clear(ENetList *list);

extern ENetListIterator enet_list_insert(ENetListIterator position, void *data);
extern void *enet_list_remove(ENetListIterator position);
extern ENetListIterator enet_list_move(ENetListIterator position, void *dataFirst, void *dataLast);

extern size_t enet_list_size(ENetList *list);

#define enet_list_begin(list) ((list)->sentinel.next)
#define enet_list_end(list) (&(list)->sentinel)

#define enet_list_empty(list) (enet_list_begin(list) == enet_list_end(list))

#define enet_list_next(iterator) ((iterator)->next)
#define enet_list_previous(iterator) ((iterator)->previous)

#define enet_list_front(list) ((void *)(list)->sentinel.next)
#define enet_list_back(list) ((void *)(list)->sentinel.previous)
