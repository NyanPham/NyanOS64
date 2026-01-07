#ifndef MATH_H
#define MATH_H

#include <stdint.h>

static inline uint64_t uint64_max(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

static inline int64_t int64_max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static inline uint32_t uint32_max(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static inline int32_t int32_max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static inline uint64_t uint64_min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

static inline int64_t int64_min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static inline uint32_t uint32_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline int32_t int32_min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

#endif