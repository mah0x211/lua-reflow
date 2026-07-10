/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, ... (MIT, same as buf.h)
 */

// project
#include "escape.h"

int escape_text(const char *src, size_t len, buf_t *out)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '&':  if (buf_put(out, "&amp;", 5) != 0) return -1; break;
        case '<':  if (buf_put(out, "&lt;", 4) != 0) return -1; break;
        case '>':  if (buf_put(out, "&gt;", 4) != 0) return -1; break;
        case '"':  if (buf_put(out, "&quot;", 6) != 0) return -1; break;
        case '\'': if (buf_put(out, "&#39;", 5) != 0) return -1; break;
        default:   if (buf_putc(out, (char)c) != 0) return -1; break;
        }
    }
    return 0;
}

int escape_attr(const char *src, size_t len, buf_t *out)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '&': if (buf_put(out, "&amp;", 5) != 0) return -1; break;
        case '<': if (buf_put(out, "&lt;", 4) != 0) return -1; break;
        case '>': if (buf_put(out, "&gt;", 4) != 0) return -1; break;
        case '"': if (buf_put(out, "&quot;", 6) != 0) return -1; break;
        default:  if (buf_putc(out, (char)c) != 0) return -1; break;
        }
    }
    return 0;
}
