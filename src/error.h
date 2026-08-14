/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_ERROR_H
#define REFLOW_ERROR_H

#include <stdbool.h>
#include <stddef.h>

#include "value.h"

typedef struct reflow_error {
    const char              *type;          /* "ReflowCompileError" etc. */
    const char              *message;
    const char              *template_name;
    long                     line;          /* 1-based */
    long                     column;        /* 1-based */
    const char              *snippet;
    const char              *element;       /* reconstructed open tag */
    const char              *directive;
    const char              *attribute;
    const char              *expression;
    const char              *reason;        /* "cycle" | "not_found" | ... */
    const char              *requested;
    const reflow_value      *requested_value;
    const char             **include_stack;
    const size_t            *include_stack_len;
    size_t                   include_stack_count;
    const char              *source;        /* selector source text */
    long                     position;      /* 0-based offset in source */
    bool                     has_position;
    const char              *feature;       /* unsupported feature id */
    struct reflow_error     *cause;         /* chained error (yyjson/helper) */
} reflow_error;

#endif
