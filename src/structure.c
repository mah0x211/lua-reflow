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
#include <stddef.h>

#include "ir.h"
#include "ir_internal.h"
#include "reflow_util.h"
#include "structure.h"

/* Whitespace-only text and comments may separate the members of a chain,
 * as in the JavaScript version. */
static int is_ignorable(const ir_node_t *node)
{
    if (node->type == IR_COMMENT) {
        return 1;
    }
    if (node->type == IR_TEXT) {
        const ir_text_t *text = (const ir_text_t *)node;

        return reflow_js_is_blank(text->text.data, text->text.len);
    }
    return 0;
}

static const ir_element_t *as_marked_element(const ir_node_t *node)
{
    const ir_element_t *element = NULL;

    if (node->type != IR_ELEMENT) {
        return NULL;
    }
    element = (const ir_element_t *)node;
    return element->directives != NULL ? element : NULL;
}

static void append_node(ir_node_list_t *list, ir_node_t *node)
{
    node->next = NULL;
    if (list->tail == NULL) {
        list->head = node;
    } else {
        list->tail->next = node;
    }
    list->tail = node;
    list->count++;
}

static int fail(structure_error_t *error, const char *message,
                const ir_element_t *element)
{
    error->message = message;
    error->element = element;
    errno          = EINVAL;
    return -1;
}

/* Collect the chain that starts at head into a chain node. *resume receives
 * the first node after the last chain member, so whitespace and comments
 * that follow the chain are kept in place. */
static int build_chain(ir_t *ir, ir_node_t *head, ir_chain_t **result,
                       ir_node_t **resume, structure_error_t *error)
{
    ir_element_t *first = (ir_element_t *)head;
    ir_chain_t *chain   = NULL;
    ir_node_t *cursor   = head->next;
    int saw_else        = 0;

    chain = ir_new_chain(ir, first->source_start, first->source_end);
    if (chain == NULL ||
        ir_chain_append_branch(ir, chain, first->directives->if_condition,
                               head) != 0) {
        return -1;
    }
    *resume = cursor;
    while (cursor != NULL) {
        ir_node_t *next                             = cursor->next;
        const ir_element_t *element                 = NULL;
        const struct reflow_expression_t *condition = NULL;

        if (is_ignorable(cursor)) {
            cursor = next;
            continue;
        }
        element = as_marked_element(cursor);
        if (element == NULL) {
            break;
        }
        if (element->directives->elseif_condition != NULL) {
            if (saw_else) {
                return fail(error, "x-elseif after x-else is not allowed",
                            element);
            }
            condition = element->directives->elseif_condition;
        } else if (element->directives->has_else) {
            if (saw_else) {
                return fail(error, "multiple x-else in the same chain",
                            element);
            }
            saw_else = 1;
        } else {
            break;
        }
        if (ir_chain_append_branch(ir, chain, condition, cursor) != 0) {
            return -1;
        }
        chain->source_end = element->source_end;
        cursor->next      = NULL;
        cursor            = next;
        *resume           = cursor;
    }
    head->next = NULL;
    *result    = chain;
    return 0;
}

int structure_close_children(ir_t *ir, ir_node_t *parent,
                             structure_error_t *error)
{
    ir_node_list_t *children = ir_node_children(parent);
    ir_node_list_t merged    = {0};
    ir_node_t *node          = NULL;

    if (children == NULL || error == NULL) {
        errno = EINVAL;
        return -1;
    }
    *error = (structure_error_t){0};

    node = children->head;
    while (node != NULL) {
        const ir_element_t *element = as_marked_element(node);
        ir_node_t *next             = node->next;

        if (element != NULL && element->directives->if_condition != NULL) {
            ir_chain_t *chain = NULL;

            if (build_chain(ir, node, &chain, &next, error) != 0) {
                return -1;
            }
            append_node(&merged, &chain->node);
        } else {
            append_node(&merged, node);
        }
        node = next;
    }
    *children = merged;

    for (node = children->head; node != NULL; node = node->next) {
        const ir_element_t *element = as_marked_element(node);

        if (element == NULL) {
            continue;
        }
        if (element->directives->elseif_condition != NULL) {
            return fail(error, "x-elseif has no preceding x-if", element);
        }
        if (element->directives->has_else) {
            return fail(error, "x-else has no preceding x-if / x-elseif",
                        element);
        }
    }
    return 0;
}
