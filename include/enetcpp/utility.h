/**
 @file  utility.h
 @brief ENet utility header
*/
#pragma once

namespace ENet
{

template <typename T>
inline T MAX(const T x, const T y)
{
    return x > y ? x : y;
}

template <typename T>
inline T MIN(const T x, const T y)
{
    return x < y ? x : y;
}

template <typename T>
inline T DIFFERENCE(const T x, const T y)
{
    return x < y ? y - x : x - y;
}

} // namespace ENet
