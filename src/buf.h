/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *  MIT license
 */
#ifndef REFLOW_BUF_H
#define REFLOW_BUF_H

#include "arena.h"
#include "checked.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char    *data;
    size_t   len;
    size_t   cap;
    arena_t *arena;
} buf_t;

static inline int buf_init(buf_t *b, arena_t *arena)
{
    b->cap   = 256;
    b->len   = 0;
    b->arena = arena;
    b->data  = (char *)arena_alloc(arena, b->cap);
    return b->data != NULL ? 0 : -1;
}

static inline int buf_ensure(buf_t *b, size_t need)
{
    size_t required;
    size_t with_nul;
    if (!reflow_size_add(b->len, need, &required) ||
        !reflow_size_add(required, 1, &with_nul)) {
        return -1;
    }
    if (with_nul <= b->cap) return 0;

    size_t new_cap;
    if (!reflow_size_grow(b->cap, with_nul, &new_cap)) return -1;
    char *nd = (char *)arena_alloc(b->arena, new_cap);
    if (nd == NULL) return -1;
    if (b->data != NULL && b->len != 0) memcpy(nd, b->data, b->len);
    b->data = nd;
    b->cap  = new_cap;
    return 0;
}

static inline int buf_put(buf_t *b, const char *data, size_t len)
{
    if (buf_ensure(b, len) != 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

static inline int buf_putc(buf_t *b, char c)
{
    if (buf_ensure(b, 1) != 0) return -1;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 0;
}

static inline int buf_printf(buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || buf_ensure(b, (size_t)n) != 0) return -1;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

static inline void buf_free(buf_t *b)
{
    b->data  = NULL;
    b->len   = 0;
    b->cap   = 0;
    b->arena = NULL;
}

#endif /* REFLOW_BUF_H */
