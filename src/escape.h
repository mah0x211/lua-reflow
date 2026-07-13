/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, ... (MIT, same as buf.h)
 */

#ifndef REFLOW_ESCAPE_H
#define REFLOW_ESCAPE_H

#include <stddef.h>
#include "buf.h"

/* escape_text: OWASP 5 chars (& < > " '). */
int escape_text(const char *src, size_t len, buf_t *out);

/* escape_attr: 4 chars (& < > "), double-quoted attribute context. */
int escape_attr(const char *src, size_t len, buf_t *out);

#endif /* REFLOW_ESCAPE_H */
