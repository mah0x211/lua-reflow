/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
// project
#include "json5.h"
// depend
#include "yyjson.h"
// system
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- yyjson_val → reflow_value (recursive, arena-owned) --- */

static reflow_value *convert_val(yyjson_val *v, arena_t *arena)
{
    if (v == NULL) return NULL;

    reflow_value *rv = (reflow_value *)arena_alloc(arena, sizeof(reflow_value));
    if (rv == NULL) return NULL;
    memset(rv, 0, sizeof(reflow_value));

    switch (yyjson_get_type(v)) {
    case YYJSON_TYPE_NULL:
        rv->tag = RV_NULL;
        return rv;

    case YYJSON_TYPE_BOOL:
        rv->tag     = RV_BOOL;
        rv->boolean = yyjson_get_bool(v) ? 1 : 0;
        return rv;

    case YYJSON_TYPE_NUM:
        rv->tag    = RV_NUMBER;
        rv->number = yyjson_get_num(v);
        return rv;

    case YYJSON_TYPE_STR: {
        size_t slen    = 0;
        const char *sv = yyjson_get_str(v);
        /* yyjson strings are NUL-terminated; len via get_len */
        slen           = yyjson_get_len(v);
        rv->tag         = RV_STRING;
        char *dst       = (char *)arena_alloc(arena, slen + 1);
        if (dst == NULL) return NULL;
        memcpy(dst, sv, slen);
        dst[slen]       = '\0';
        rv->string.data = dst;
        rv->string.len  = slen;
        return rv;
    }

    case YYJSON_TYPE_ARR: {
        rv->tag = RV_ARRAY;
        size_t idx = 0, max = yyjson_arr_size(v);
        rv->array.items = (reflow_value *)arena_alloc(
            arena, max * sizeof(reflow_value));
        if (rv->array.items == NULL && max > 0) return NULL;
        rv->array.cap = max;
        rv->array.len = 0;

        yyjson_val *elem;
        yyjson_arr_foreach(v, idx, max, elem)
        {
            reflow_value *cv = convert_val(elem, arena);
            if (cv == NULL) return NULL;
            /* store by-value into the array */
            rv->array.items[rv->array.len++] = *cv;
        }
        return rv;
    }

    case YYJSON_TYPE_OBJ: {
        rv->tag = RV_OBJECT;
        size_t max = yyjson_obj_size(v);
        rv->object.props = (rv_prop *)arena_alloc(
            arena, max * sizeof(rv_prop));
        if (rv->object.props == NULL && max > 0) return NULL;
        rv->object.cap = max;
        rv->object.len = 0;

        yyjson_obj_iter iter;
        yyjson_obj_iter_init(v, &iter);
        yyjson_val *key_val;
        while ((key_val = yyjson_obj_iter_next(&iter))) {
            size_t klen     = yyjson_get_len(key_val);
            const char *key = yyjson_get_str(key_val);
            yyjson_val *val = yyjson_obj_iter_get_val(key_val);

            char *kdst = (char *)arena_alloc(arena, klen + 1);
            if (kdst == NULL) return NULL;
            memcpy(kdst, key, klen);
            kdst[klen] = '\0';

            reflow_value *cv = convert_val(val, arena);
            if (cv == NULL) return NULL;

            rv->object.props[rv->object.len].key     = kdst;
            rv->object.props[rv->object.len].key_len = klen;
            rv->object.props[rv->object.len].value   = *cv;
            rv->object.len++;
        }
        return rv;
    }

    default:
        rv->tag = RV_UNDEFINED;
        return rv;
    }
}

/* --- public API --- */

/* Static buffer for yyjson error messages (single-threaded Lua, no leak). */
static char g_json5_err[256];

reflow_value *json5_parse(const char *src, size_t len,
                          arena_t *arena, reflow_error *err)
{
    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_opts((char *)src, len, YYJSON_READ_JSON5,
                                       NULL, &rerr);
    if (doc == NULL) {
        if (err) {
            err->type = "ReflowRuntimeError";
            snprintf(g_json5_err, sizeof(g_json5_err),
                     "failed to parse JSON5: %s (at position %zu)",
                     rerr.msg ? rerr.msg : "unknown error", rerr.pos);
            err->message = g_json5_err;
        }
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    reflow_value *rv = convert_val(root, arena);
    yyjson_doc_free(doc);
    if (rv == NULL && err) {
        err->type    = "ReflowRuntimeError";
        err->message = "out of memory during JSON5 conversion";
    }
    return rv;
}

reflow_value *json5_parse_data(const char *value, size_t len,
                               arena_t *arena, reflow_error *err)
{
    /* Wrap value in "{ ... }" for object-style parsing */
    size_t wrapped_len = len + 4; /* "{ " + value + " }" + NUL handled by buf */
    char *buf = (char *)arena_alloc(arena, wrapped_len + 1);
    if (buf == NULL) {
        if (err) {
            err->type    = "ReflowRuntimeError";
            err->message = "out of memory";
        }
        return NULL;
    }
    buf[0] = '{';
    buf[1] = ' ';
    memcpy(buf + 2, value, len);
    buf[2 + len] = ' ';
    buf[3 + len] = '}';
    buf[4 + len] = '\0';

    /* Use a fresh arena offset snapshot — the wrap buffer is temporary.
     * In practice, the caller passes a large enough arena. */
    return json5_parse(buf, wrapped_len, arena, err);
}
