/**
 @file  list.h
 @brief ENet list management
*/
#pragma once

#include <stdlib.h>

namespace ENet
{

template <typename T>
using ListIterator = T *;

template <typename T>
struct ListNode
{
    T *next;
    T *previous;
};

template <typename T>
struct List
{
    ListNode<T> sentinel;
};

template <typename T>
inline ListIterator<T> list_begin(List<T> *list)
{
    return (ListIterator<T>)((list)->sentinel.next);
}

template <typename T>
inline ListIterator<T> list_end(List<T> *list)
{
    return (ListIterator<T>)&(list)->sentinel;
}

template <typename T>
inline bool list_empty(List<T> *list)
{
    return list_begin<T>(list) == list_end<T>(list);
}

template <typename T>
inline ListIterator<T> list_next(ListIterator<T> iterator)
{
    return iterator->list_node()->next;
}

template <typename T>
inline ListIterator<T> list_previous(ListIterator<T> iterator)
{
    return iterator->list_node()->previous;
}

template <typename T>
inline T *list_front(List<T> *list)
{
    return list->sentinel.next;
}

template <typename T>
inline T *list_back(List<T> *list)
{
    return list->sentinel.previous;
}

template <typename T>
inline void list_clear(List<T> *list)
{
    list->sentinel.next = (T *)&list->sentinel;
    list->sentinel.previous = (T *)&list->sentinel;
}

template <typename T>
inline ListIterator<T> list_insert(ListIterator<T> position, T *data)
{
    ListNode<T> *result = data->list_node();

    result->previous = position->list_node()->previous;
    result->next = position;

    result->previous->list_node()->next = data;
    position->list_node()->previous = data;

    return data;
}

template <typename T>
inline T *list_remove(ListIterator<T> position)
{
    position->list_node()->previous->list_node()->next = position->list_node()->next;
    position->list_node()->next->list_node()->previous = position->list_node()->previous;

    return position;
}

template <typename T>
inline ListIterator<T> list_move(ListIterator<T> position, T *dataFirst, T *dataLast)
{
    ListNode<T> *first = dataFirst->list_node(), *last = dataLast->list_node();

    first->previous->list_node()->next = last->next;
    last->next->list_node()->previous = first->previous;

    first->previous = position->list_node()->previous;
    last->next = position;

    first->previous->list_node()->next = dataFirst;
    position->list_node()->previous = dataLast;

    return dataFirst;
}

template <typename T>
inline size_t list_size(List<T> *list)
{
    size_t size = 0;
    ListIterator<T> position;

    for (position = list_begin<T>(list); position != list_end<T>(list); position = list_next<T>(position))
    {
        ++size;
    }

    return size;
}

} // namespace ENet
