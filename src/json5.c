/*
 * Copyright (C) 2026 Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <errno.h>
#include <stdint.h>

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <yyjson.h>
#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#include "json5.h"
#include "pool.h"
#include "value.h"

typedef enum json5_frame_type_t {
    JSON5_FRAME_ARRAY = 1,
    JSON5_FRAME_OBJECT
} json5_frame_type_t;

typedef struct json5_frame_t {
    json5_frame_type_t type;
    reflow_value_t *target;
    union {
        yyjson_arr_iter array;
        yyjson_obj_iter object;
    } iterator;
} json5_frame_t;

static void *json5_pool_malloc(void *ctx, size_t size)
{
    return pool_alloc((pool_t *)ctx, size);
}

static void *json5_pool_realloc(void *ctx, void *ptr, size_t old_size,
                                size_t size)
{
    (void)old_size;
    return pool_realloc((pool_t *)ctx, ptr, size);
}

static void json5_pool_free(void *ctx, void *ptr)
{
    (void)pool_free((pool_t *)ctx, ptr);
}

static reflow_value_t *copy_value(ir_t *ir, yyjson_val *source)
{
    if (yyjson_is_null(source)) {
        return reflow_value_new_null(ir);
    } else if (yyjson_is_bool(source)) {
        return reflow_value_new_bool(ir, yyjson_get_bool(source));
    } else if (yyjson_is_num(source)) {
        return reflow_value_new_number(ir, yyjson_get_num(source));
    } else if (yyjson_is_str(source)) {
        return reflow_value_new_string(ir, yyjson_get_str(source),
                                       yyjson_get_len(source));
    } else if (yyjson_is_arr(source)) {
        return reflow_value_new_array(ir);
    } else if (yyjson_is_obj(source)) {
        return reflow_value_new_object(ir);
    }
    errno = EIO;
    return NULL;
}

static int push_frame(pool_t *pool, json5_frame_t **frames, size_t *depth,
                      size_t *capacity, yyjson_val *source,
                      reflow_value_t *target)
{
    json5_frame_t frame = {0};

    if (*depth == *capacity) {
        size_t next_capacity = *capacity == 0 ? 16 : *capacity * 2;
        json5_frame_t *next  = NULL;

        if ((*capacity != 0 && next_capacity < *capacity) ||
            next_capacity > SIZE_MAX / sizeof(**frames)) {
            errno = ENOMEM;
            return -1;
        }
        next = (json5_frame_t *)pool_realloc(pool, *frames,
                                             next_capacity * sizeof(**frames));
        if (next == NULL) {
            return -1;
        }
        *frames   = next;
        *capacity = next_capacity;
    }

    frame.target = target;
    if (yyjson_is_arr(source)) {
        frame.type           = JSON5_FRAME_ARRAY;
        frame.iterator.array = yyjson_arr_iter_with(source);
    } else if (yyjson_is_obj(source)) {
        frame.type            = JSON5_FRAME_OBJECT;
        frame.iterator.object = yyjson_obj_iter_with(source);
    } else {
        errno = EINVAL;
        return -1;
    }
    (*frames)[*depth] = frame;
    (*depth)++;
    return 0;
}

static int copy_tree(pool_t *temporary_pool, ir_t *ir, yyjson_val *source,
                     reflow_value_t **result)
{
    reflow_value_t *root  = copy_value(ir, source);
    json5_frame_t *frames = NULL;
    size_t depth          = 0;
    size_t capacity       = 0;

    if (root == NULL) {
        return -1;
    }
    if (yyjson_is_ctn(source) && push_frame(temporary_pool, &frames, &depth,
                                            &capacity, source, root) != 0) {
        return -1;
    }

    while (depth != 0) {
        json5_frame_t *frame = &frames[depth - 1];
        yyjson_val *child    = NULL;
        yyjson_val *key      = NULL;
        reflow_value_t *copy = NULL;

        if (frame->type == JSON5_FRAME_ARRAY) {
            child = yyjson_arr_iter_next(&frame->iterator.array);
            if (child == NULL) {
                depth--;
                continue;
            }
            copy = copy_value(ir, child);
            if (copy == NULL ||
                reflow_value_array_append(ir, frame->target, copy) != 0) {
                return -1;
            }
        } else {
            key = yyjson_obj_iter_next(&frame->iterator.object);
            if (key == NULL) {
                depth--;
                continue;
            }
            child = yyjson_obj_iter_get_val(key);
            copy  = copy_value(ir, child);
            if (copy == NULL || reflow_value_object_append(
                                    ir, frame->target, yyjson_get_str(key),
                                    yyjson_get_len(key), copy) != 0) {
                return -1;
            }
        }

        if (yyjson_is_ctn(child) && push_frame(temporary_pool, &frames, &depth,
                                               &capacity, child, copy) != 0) {
            return -1;
        }
    }

    *result = root;
    return 0;
}

int json5_parse(pool_t *temporary_pool, ir_t *ir, char *source,
                size_t source_len, reflow_value_t **result,
                json5_error_t *error)
{
    yyjson_alc allocator = {
        .malloc  = json5_pool_malloc,
        .realloc = json5_pool_realloc,
        .free    = json5_pool_free,
        .ctx     = temporary_pool,
    };
    yyjson_read_err read_error = {0};
    yyjson_doc *document       = NULL;
    int status                 = -1;
    int errnum                 = 0;

    if (temporary_pool == NULL || ir == NULL || source == NULL ||
        source_len == 0 || result == NULL || error == NULL) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    *error  = (json5_error_t){0};

    document = yyjson_read_opts(source, source_len, YYJSON_READ_JSON5,
                                &allocator, &read_error);
    if (document == NULL) {
        error->message  = read_error.msg;
        error->position = read_error.pos;
        errno = read_error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ?
                    ENOMEM :
                    EINVAL;
        return -1;
    }

    status =
        copy_tree(temporary_pool, ir, yyjson_doc_get_root(document), result);
    errnum = errno;
    yyjson_doc_free(document);
    errno = errnum;
    return status;
}
