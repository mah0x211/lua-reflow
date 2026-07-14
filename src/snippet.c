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

/* Count how many lines the source has (matching JS's source.split('\n')). */
static long count_lines(const char *src, size_t src_len)
{
    long n = 1;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '\n') n++;
    }
    return n;
}

/* Find start / end offsets of the Nth line (1-based). Returns 0 when
 * the line does not exist. */
static int find_line(const char *src, size_t src_len, long line,
                     size_t *out_start, size_t *out_end)
{
    long cur = 1;
    size_t start = 0;
    for (size_t i = 0; i <= src_len; i++) {
        if (cur == line && i == src_len) {
            *out_start = start;
            *out_end   = i;
            return 1;
        }
        if (i == src_len) return 0;
        if (src[i] == '\n') {
            if (cur == line) {
                *out_start = start;
                *out_end   = i;
                return 1;
            }
            cur++;
            start = i + 1;
        }
    }
    return 0;
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

    const long ctx = 2;
    const long total_lines = count_lines(src, src_len);
    long first = sl - ctx;
    if (first < 1) first = 1;
    long last = el + ctx;
    if (last > total_lines) last = total_lines;

    /* gutter width based on largest line number that will be printed */
    int gw = 1;
    for (long t = last; t >= 10; t /= 10) gw++;

    for (long n = first; n <= last; n++) {
        size_t ls = 0, le = 0;
        if (!find_line(src, src_len, n, &ls, &le)) continue;
        if (n > first) buf_putc(out, '\n');
        buf_printf(out, "%*ld | ", gw, n);
        buf_put(out, src + ls, le - ls);

        if (n == sl) {
            long highlight_start = sc - 1;
            long line_len = (long)(le - ls);
            long highlight_len;
            if (sl == el) {
                highlight_len = (long)(end - start);
            } else {
                highlight_len = line_len - highlight_start;
            }
            if (highlight_len < 1) highlight_len = 1;

            buf_putc(out, '\n');
            for (int i = 0; i < gw; i++) buf_putc(out, ' ');
            buf_put(out, " | ", 3);
            for (long i = 0; i < highlight_start; i++) buf_putc(out, ' ');
            for (long i = 0; i < highlight_len; i++) buf_putc(out, '^');
        }
    }
    return 0;
}
