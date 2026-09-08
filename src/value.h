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

#ifndef REFLOW_VALUE_H
#define REFLOW_VALUE_H

#include <stddef.h>

struct ir_t;
typedef struct reflow_value_t reflow_value_t;
typedef struct reflow_value_item_t reflow_value_item_t;
typedef struct reflow_value_member_t reflow_value_member_t;

typedef enum reflow_value_type_t {
    REFLOW_VALUE_UNDEFINED = 1,
    REFLOW_VALUE_NULL,
    REFLOW_VALUE_BOOL,
    REFLOW_VALUE_NUMBER,
    REFLOW_VALUE_STRING,
    REFLOW_VALUE_ARRAY,
    REFLOW_VALUE_OBJECT
} reflow_value_type_t;

typedef struct reflow_value_string_t {
    const char *data;
    size_t len;
} reflow_value_string_t;

typedef struct reflow_value_list_t {
    reflow_value_item_t *head;
    reflow_value_item_t *tail;
    size_t count;
} reflow_value_list_t;

typedef struct reflow_value_object_t {
    reflow_value_member_t *head;
    reflow_value_member_t *tail;
    size_t count;
} reflow_value_object_t;

struct reflow_value_t {
    reflow_value_type_t type;
    union {
        int boolean;
        double number;
        reflow_value_string_t string;
        reflow_value_list_t array;
        reflow_value_object_t object;
    } as;
};

struct reflow_value_item_t {
    reflow_value_item_t *next;
    reflow_value_t *value;
};

struct reflow_value_member_t {
    reflow_value_member_t *next;
    reflow_value_string_t key;
    reflow_value_t *value;
};

reflow_value_t *reflow_value_new_undefined(struct ir_t *ir);
reflow_value_t *reflow_value_new_null(struct ir_t *ir);
reflow_value_t *reflow_value_new_bool(struct ir_t *ir, int value);
reflow_value_t *reflow_value_new_number(struct ir_t *ir, double value);
reflow_value_t *reflow_value_new_string(struct ir_t *ir, const char *value,
                                        size_t value_len);
reflow_value_t *reflow_value_new_array(struct ir_t *ir);
reflow_value_t *reflow_value_new_object(struct ir_t *ir);

int reflow_value_array_append(struct ir_t *ir, reflow_value_t *array,
                              reflow_value_t *value);
int reflow_value_object_append(struct ir_t *ir, reflow_value_t *object,
                               const char *key, size_t key_len,
                               reflow_value_t *value);

#endif /* REFLOW_VALUE_H */
