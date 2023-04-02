/**
 @file  time.h
 @brief ENet time constants and macros
*/
#pragma once

#include <cstdint>

namespace ENet
{

constexpr uint32_t TIME_OVERFLOW = 86400000;

inline bool TIME_LESS(const uint32_t a, const uint32_t b)
{
    return a - b >= TIME_OVERFLOW;
}

inline bool TIME_GREATER(const uint32_t a, const uint32_t b)
{
    return b - a >= TIME_OVERFLOW;
}

inline bool TIME_LESS_EQUAL(const uint32_t a, const uint32_t b)
{
    return !TIME_GREATER(a, b);
}

inline bool TIME_GREATER_EQUAL(const uint32_t a, const uint32_t b)
{
    return !TIME_LESS(a, b);
}

inline uint32_t TIME_DIFFERENCE(const uint32_t a, const uint32_t b)
{
    return a - b >= TIME_OVERFLOW ? b - a : a - b;
}

} // namespace ENet
