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
#include <string.h>

#include "ir_internal.h"
#include "pool.h"
#include "value.h"

static reflow_value_t *new_value(ir_t *ir, reflow_value_type_t type)
{
    pool_t *pool          = ir_pool(ir);
    reflow_value_t *value = NULL;

    if (pool == NULL) {
        return NULL;
    }
    value = (reflow_value_t *)pool_calloc(pool, 1, sizeof(*value));
    if (value != NULL) {
        value->type = type;
    }
    return value;
}

static int copy_string(ir_t *ir, const char *data, size_t len,
                       reflow_value_string_t *result)
{
    pool_t *pool = ir_pool(ir);
    char *copy   = NULL;

    if (pool == NULL || result == NULL || (data == NULL && len != 0)) {
        errno = EINVAL;
        return -1;
    } else if (len == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }

    copy = (char *)pool_alloc(pool, len + 1);
    if (copy == NULL) {
        return -1;
    }
    if (len != 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    *result   = (reflow_value_string_t){
        .data = copy,
        .len  = len,
    };
    return 0;
}

reflow_value_t *reflow_value_new_undefined(ir_t *ir)
{
    return new_value(ir, REFLOW_VALUE_UNDEFINED);
}

reflow_value_t *reflow_value_new_null(ir_t *ir)
{
    return new_value(ir, REFLOW_VALUE_NULL);
}

reflow_value_t *reflow_value_new_bool(ir_t *ir, int value)
{
    reflow_value_t *result = new_value(ir, REFLOW_VALUE_BOOL);

    if (result != NULL) {
        result->as.boolean = value != 0;
    }
    return result;
}

reflow_value_t *reflow_value_new_number(ir_t *ir, double value)
{
    reflow_value_t *result = new_value(ir, REFLOW_VALUE_NUMBER);

    if (result != NULL) {
        result->as.number = value;
    }
    return result;
}

reflow_value_t *reflow_value_new_string(ir_t *ir, const char *value,
                                        size_t value_len)
{
    pool_t *pool                 = ir_pool(ir);
    reflow_value_string_t string = {0};
    reflow_value_t *result       = NULL;

    if (pool == NULL || copy_string(ir, value, value_len, &string) != 0) {
        return NULL;
    }
    result = new_value(ir, REFLOW_VALUE_STRING);
    if (result == NULL) {
        (void)pool_free(pool, (void *)string.data);
        return NULL;
    }
    result->as.string = string;
    return result;
}

reflow_value_t *reflow_value_new_array(ir_t *ir)
{
    return new_value(ir, REFLOW_VALUE_ARRAY);
}

reflow_value_t *reflow_value_new_object(ir_t *ir)
{
    return new_value(ir, REFLOW_VALUE_OBJECT);
}

int reflow_value_array_append(ir_t *ir, reflow_value_t *array,
                              reflow_value_t *value)
{
    pool_t *pool              = ir_pool(ir);
    reflow_value_list_t *list = NULL;
    reflow_value_item_t *item = NULL;

    if (pool == NULL || array == NULL || array->type != REFLOW_VALUE_ARRAY ||
        value == NULL) {
        errno = EINVAL;
        return -1;
    }
    list = &array->as.array;
    if (list->count == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }
    item = (reflow_value_item_t *)pool_calloc(pool, 1, sizeof(*item));
    if (item == NULL) {
        return -1;
    }
    item->value = value;
    if (list->tail == NULL) {
        list->head = item;
    } else {
        list->tail->next = item;
    }
    list->tail = item;
    list->count++;
    return 0;
}

int reflow_value_object_append(ir_t *ir, reflow_value_t *object,
                               const char *key, size_t key_len,
                               reflow_value_t *value)
{
    pool_t *pool                   = ir_pool(ir);
    reflow_value_object_t *members = NULL;
    reflow_value_string_t key_copy = {0};
    reflow_value_member_t *member  = NULL;

    if (pool == NULL || object == NULL || object->type != REFLOW_VALUE_OBJECT ||
        value == NULL) {
        errno = EINVAL;
        return -1;
    }
    members = &object->as.object;
    if (members->count == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    } else if (copy_string(ir, key, key_len, &key_copy) != 0) {
        return -1;
    }
    member = (reflow_value_member_t *)pool_calloc(pool, 1, sizeof(*member));
    if (member == NULL) {
        (void)pool_free(pool, (void *)key_copy.data);
        return -1;
    }
    member->key   = key_copy;
    member->value = value;
    if (members->tail == NULL) {
        members->head = member;
    } else {
        members->tail->next = member;
    }
    members->tail = member;
    members->count++;
    return 0;
}
