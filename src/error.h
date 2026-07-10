/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_ERROR_H
#define REFLOW_ERROR_H

#include <stddef.h>

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
    struct reflow_error     *cause;         /* chained error (yyjson/helper) */
} reflow_error;

/* Allocate a zero-initialized reflow_error with the given type and message. */
reflow_error *reflow_error_new(const char *type, const char *message);

/* Free a reflow_error and its cause chain (does not free string fields). */
void reflow_error_free(reflow_error *err);

#endif
