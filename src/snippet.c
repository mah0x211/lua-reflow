/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
// project
#include "snippet.h"
// system
#include <stdio.h>

void offset_to_linecol(const char *src, size_t src_len, size_t offset,
                       long *line, long *col)
{
    if (offset > src_len) {
        offset = src_len;
    }
    long l = 1;
    long last_nl = -1;
    for (size_t i = 0; i < offset; i++) {
        if (src[i] == '\n') {
            l++;
            last_nl = (long)i;
        }
    }
    *line = l;
    *col  = (long)offset - last_nl;
}

int make_snippet(const char *src, size_t src_len, size_t start, size_t end,
                 buf_t *out)
{
    /* clamp */
    if (start > src_len) start = src_len;
    if (end > src_len) end = src_len;
    if (end < start) end = start;

    long sl, sc, el, ec;
    offset_to_linecol(src, src_len, start, &sl, &sc);
    offset_to_linecol(src, src_len, end, &el, &ec);

    /* count lines for gutter width */
    long max_line = el;
    int gw = 1;
    for (long t = max_line; t >= 10; t /= 10) gw++;

    /* iterate lines */
    size_t pos = 0;
    long line_no = 1;
    long ctx = 2;
    long first = sl - ctx;
    if (first < 1) first = 1;
    long last = el + ctx;

    while (pos <= src_len && line_no <= last) {
        /* find end of this line */
        size_t lstart = pos;
        while (pos < src_len && src[pos] != '\n') pos++;
        size_t lend = pos;
        if (pos < src_len) pos++; /* skip \n */

        if (line_no >= first) {
            buf_printf(out, "%*ld | ", gw, line_no);
            buf_put(out, src + lstart, lend - lstart);
            buf_putc(out, '\n');

            /* caret on start line */
            if (line_no == sl) {
                for (int i = 0; i < gw; i++) buf_putc(out, ' ');
                buf_putc(out, ' ');
                buf_putc(out, '|');
                for (long i = 1; i < sc; i++) buf_putc(out, ' ');
                long caret_end = (sl == el) ? ec : (long)(lend - lstart + 1);
                for (long i = sc; i < caret_end; i++) buf_putc(out, '^');
                buf_putc(out, '\n');
            }
        }
        line_no++;
    }
    return 0;
}
