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

#include "dom_index.h"
#include "ir_internal.h"
#include "pool.h"
#include "reflow_util.h"

#define DOM_INDEX_INITIAL_FRAMES 16
#define DOM_INDEX_INITIAL_SLOTS  16

typedef enum {
    FRAME_CHILDREN = 0,
    FRAME_CHAIN,
    FRAME_MATCH
} frame_kind_t;

/* One level of the walk: a child list, the branches of a chain, or the
 * branches of an x-match element. */
typedef struct {
    frame_kind_t kind;
    ir_node_t *next;
    const ir_branch_t *branch;
    const ir_chain_t *chain;
    const ir_element_t *parent;
    size_t depth;
    size_t branch_index;
} frame_t;

typedef struct {
    pool_t *pool;
    ir_dom_index_t *index;
    frame_t *frames;
    size_t capacity;
    size_t depth;
} build_t;

static size_t hash_bytes(const char *s, size_t len)
{
    /* FNV-1a */
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i      = 0;

    for (i = 0; i < len; i++) {
        hash ^= (unsigned char)s[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static int list_append(pool_t *pool, ir_element_list_t *list,
                       const ir_element_t *element)
{
    if (list->count == list->capacity) {
        size_t capacity          = list->capacity ? list->capacity * 2 : 4;
        const ir_element_t **mem = NULL;

        if (capacity < list->capacity ||
            capacity > SIZE_MAX / sizeof(*list->items)) {
            errno = ENOMEM;
            return -1;
        }
        mem = (const ir_element_t **)pool_realloc(
            pool, (void *)list->items, capacity * sizeof(*list->items));
        if (mem == NULL) {
            return -1;
        }
        list->items    = mem;
        list->capacity = capacity;
    }
    list->items[list->count++] = element;
    return 0;
}

static ir_index_entry_t *axis_lookup(const ir_index_axis_t *axis,
                                     const char *key, size_t key_len,
                                     size_t hash)
{
    ir_index_entry_t *entry = NULL;

    if (axis->slot_count == 0) {
        return NULL;
    }
    for (entry = axis->slots[hash & (axis->slot_count - 1)]; entry != NULL;
         entry = entry->slot_next) {
        if (entry->hash == hash && entry->key.len == key_len &&
            (key_len == 0 || memcmp(entry->key.data, key, key_len) == 0)) {
            return entry;
        }
    }
    return NULL;
}

/* Keep at most one key per slot on average by doubling the slot array. */
static int axis_grow(pool_t *pool, ir_index_axis_t *axis)
{
    size_t slot_count =
        axis->slot_count ? axis->slot_count * 2 : DOM_INDEX_INITIAL_SLOTS;
    ir_index_entry_t **mem  = NULL;
    ir_index_entry_t *entry = NULL;

    if (slot_count < axis->slot_count ||
        slot_count > SIZE_MAX / sizeof(*axis->slots)) {
        errno = ENOMEM;
        return -1;
    }
    mem = (ir_index_entry_t **)pool_calloc(pool, slot_count, sizeof(*mem));
    if (mem == NULL) {
        return -1;
    }
    for (entry = axis->head; entry != NULL; entry = entry->next) {
        size_t slot      = entry->hash & (slot_count - 1);
        entry->slot_next = mem[slot];
        mem[slot]        = entry;
    }
    if (axis->slots != NULL) {
        (void)pool_free(pool, axis->slots);
    }
    axis->slots      = mem;
    axis->slot_count = slot_count;
    return 0;
}

/* key must stay valid as long as the IR; it points into IR strings. */
static int axis_add(pool_t *pool, ir_index_axis_t *axis, const char *key,
                    size_t key_len, const ir_element_t *element)
{
    size_t hash             = hash_bytes(key, key_len);
    ir_index_entry_t *entry = axis_lookup(axis, key, key_len, hash);

    if (entry == NULL) {
        size_t slot = 0;

        if (axis->count >= axis->slot_count && axis_grow(pool, axis) != 0) {
            return -1;
        }
        entry = (ir_index_entry_t *)pool_calloc(pool, 1, sizeof(*entry));
        if (entry == NULL) {
            return -1;
        }
        entry->hash       = hash;
        entry->key        = (ir_string_t){.data = key, .len = key_len};
        slot              = hash & (axis->slot_count - 1);
        entry->slot_next  = axis->slots[slot];
        axis->slots[slot] = entry;
        if (axis->tail == NULL) {
            axis->head = entry;
        } else {
            axis->tail->next = entry;
        }
        axis->tail = entry;
        axis->count++;
    }
    return list_append(pool, &entry->elements, element);
}

static int is_named(const ir_string_t *name, const char *literal, size_t len)
{
    return name->len == len && memcmp(name->data, literal, len) == 0;
}

/* Index the class names of value, split as value.split(/\s+/) splits it in
 * JavaScript; empty names are skipped. */
static int add_classes(build_t *build, const ir_string_t *value,
                       const ir_element_t *element)
{
    const char *s = value->data;
    size_t len    = value->len;
    size_t pos    = 0;

    while (pos < len) {
        size_t start = 0;
        size_t step  = reflow_js_space_len(s, len, pos);

        if (step > 0) {
            pos += step;
            continue;
        }
        start = pos;
        while (pos < len && reflow_js_space_len(s, len, pos) == 0) {
            pos++;
        }
        if (axis_add(build->pool, &build->index->by_class, s + start,
                     pos - start, element) != 0) {
            return -1;
        }
    }
    return 0;
}

static int visit(build_t *build, ir_element_t *element,
                 const ir_element_t *parent, size_t depth,
                 ir_branch_kind_t kind, const ir_chain_t *chain,
                 size_t branch_index)
{
    ir_dom_index_t *index = build->index;
    const ir_attr_t *attr = NULL;

    element->structure = (ir_structure_t){
        .parent       = parent,
        .depth        = depth,
        .order        = index->elements.count,
        .branch_kind  = kind,
        .chain        = chain,
        .branch_index = branch_index,
    };
    if (list_append(build->pool, &index->elements, element) != 0 ||
        axis_add(build->pool, &index->by_tag, element->tag_name.data,
                 element->tag_name.len, element) != 0) {
        return -1;
    }
    for (attr = element->attrs.head; attr != NULL; attr = attr->next) {
        if (axis_add(build->pool, &index->by_attr_name, attr->name.data,
                     attr->name.len, element) != 0) {
            return -1;
        }
        if (attr->value.len == 0) {
            continue;
        }
        if (is_named(&attr->name, "id", 2)) {
            if (axis_add(build->pool, &index->by_id, attr->value.data,
                         attr->value.len, element) != 0) {
                return -1;
            }
        } else if (is_named(&attr->name, "class", 5) &&
                   add_classes(build, &attr->value, element) != 0) {
            return -1;
        }
    }
    if (element->directives != NULL && element->directives->include != NULL &&
        list_append(build->pool, &index->includes, element) != 0) {
        return -1;
    }
    return 0;
}

static int push_frame(build_t *build, frame_t frame)
{
    if (build->depth == build->capacity) {
        size_t capacity = build->capacity * 2;
        frame_t *mem    = NULL;

        if (capacity < build->capacity ||
            capacity > SIZE_MAX / sizeof(*build->frames)) {
            errno = ENOMEM;
            return -1;
        }
        mem = (frame_t *)pool_realloc(build->pool, build->frames,
                                      capacity * sizeof(*mem));
        if (mem == NULL) {
            return -1;
        }
        build->frames   = mem;
        build->capacity = capacity;
    }
    build->frames[build->depth++] = frame;
    return 0;
}

/* Continue the walk below element: into its match branches when it carries
 * x-match, otherwise into its children. */
static int descend(build_t *build, const ir_element_t *element, size_t depth)
{
    if (element->directives != NULL &&
        element->directives->match_condition != NULL) {
        return push_frame(
            build, (frame_t){
                       .kind   = FRAME_MATCH,
                       .branch = element->directives->match_branches.head,
                       .parent = element,
                       .depth  = depth + 1,
                   });
    }
    return push_frame(build, (frame_t){
                                 .kind   = FRAME_CHILDREN,
                                 .next   = element->children.head,
                                 .parent = element,
                                 .depth  = depth + 1,
                             });
}

static int walk(build_t *build, ir_root_t *root)
{
    if (push_frame(build, (frame_t){
                              .kind = FRAME_CHILDREN,
                              .next = root->children.head,
                          }) != 0) {
        return -1;
    }
    while (build->depth > 0) {
        frame_t *frame             = &build->frames[build->depth - 1];
        const ir_element_t *parent = frame->parent;
        size_t depth               = frame->depth;
        ir_branch_kind_t kind      = IR_BRANCH_NONE;
        const ir_chain_t *chain    = NULL;
        size_t branch_index        = 0;
        ir_element_t *element      = NULL;

        if (frame->kind == FRAME_CHILDREN) {
            ir_node_t *node = frame->next;

            if (node == NULL) {
                build->depth--;
                continue;
            }
            frame->next = node->next;
            if (node->type == IR_CHAIN) {
                const ir_chain_t *found = (const ir_chain_t *)node;

                if (push_frame(build, (frame_t){
                                          .kind   = FRAME_CHAIN,
                                          .branch = found->branches.head,
                                          .chain  = found,
                                          .parent = parent,
                                          .depth  = depth,
                                      }) != 0) {
                    return -1;
                }
                continue;
            }
            if (node->type != IR_ELEMENT) {
                continue;
            }
            element = (ir_element_t *)node;
        } else {
            const ir_branch_t *branch = frame->branch;

            if (branch == NULL) {
                build->depth--;
                continue;
            }
            frame->branch = branch->next;
            branch_index  = frame->branch_index++;
            kind =
                frame->kind == FRAME_CHAIN ? IR_BRANCH_CHAIN : IR_BRANCH_MATCH;
            chain   = frame->chain;
            element = (ir_element_t *)branch->node;
        }
        if (visit(build, element, parent, depth, kind, chain, branch_index) !=
                0 ||
            descend(build, element, depth) != 0) {
            return -1;
        }
    }
    return 0;
}

int dom_index_build(ir_t *ir)
{
    build_t build = {0};
    int status    = -1;
    int errnum    = 0;

    if (ir == NULL || ir->root == NULL || ir_pool(ir) == NULL) {
        errno = EINVAL;
        return -1;
    }
    build.pool = ir_pool(ir);
    build.index =
        (ir_dom_index_t *)pool_calloc(build.pool, 1, sizeof(*build.index));
    if (build.index == NULL) {
        return -1;
    }
    build.capacity = DOM_INDEX_INITIAL_FRAMES;
    build.frames =
        (frame_t *)pool_alloc(build.pool, build.capacity * sizeof(frame_t));
    if (build.frames == NULL) {
        return -1;
    }
    status = walk(&build, ir->root);
    errnum = errno;
    (void)pool_free(build.pool, build.frames);
    errno = errnum;
    if (status == 0) {
        ir->index = build.index;
    }
    return status;
}
