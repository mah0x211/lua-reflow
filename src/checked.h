/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_CHECKED_H
#define REFLOW_CHECKED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool reflow_size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

static inline bool reflow_size_mul(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}

static inline bool reflow_size_align(size_t value, size_t alignment,
                                     size_t *out)
{
    size_t mask = alignment - 1;
    if (alignment == 0 || (alignment & mask) != 0 ||
        value > SIZE_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

static inline bool reflow_size_grow(size_t current, size_t required,
                                    size_t *out)
{
    size_t capacity = current == 0 ? 1 : current;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    *out = capacity;
    return true;
}

#endif /* REFLOW_CHECKED_H */
