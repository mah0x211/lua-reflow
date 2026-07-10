/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_SNIPPET_H
#define REFLOW_SNIPPET_H

#include <stddef.h>
#include "buf.h"

/* 1-based line/column from byte offset (clamp [0,src_len]). */
void offset_to_linecol(const char *src, size_t src_len, size_t offset,
                       long *line, long *col);

/* Build a source snippet with gutter and caret marker. */
int make_snippet(const char *src, size_t src_len, size_t start, size_t end,
                 buf_t *out);

#endif
