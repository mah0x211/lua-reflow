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
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "compat.h"
#include "error.h"
#include "expr_eval.h"
#include "ir.h"
#include "js_number.h"
#include "pool.h"
#include "render_output.h"
#include "scope.h"
#include "selector.h"
#include "selector_exec.h"
#include "snippet.h"
#include "value_lua.h"

#define INTERPRET_OPERATION      "reflow.interpret.render"
#define INTERPRET_INITIAL_FRAMES 16
#define INTERPRET_OOM_MESSAGE    "out of memory while rendering"
#define INTERPRET_INCLUDE_DEPTH  50

/* Stack positions inside the protected body. */
#define INTERPRET_CTX_IDX       1
#define INTERPRET_DATA_IDX      2
#define INTERPRET_HELPERS_IDX   3
#define INTERPRET_TEMPLATES_IDX 4
#define INTERPRET_HISTORY_IDX   5
#define INTERPRET_FRAMES_IDX    6

/* Marks of an element in the selection of a template being searched. */
#define SEL_RELEVANT 0x1 /* holds what is searched for: execute it */
#define SEL_PARENT   0x2 /* the parent of positional candidates */
#define SEL_INCLUDE  0x4 /* an x-include element whose template is searched */

/* The distinct tag names of the positional candidates of one parent. */
typedef struct {
    const ir_string_t **tags;
    size_t ntags;
    size_t capacity;
} sel_parent_t;

/* The selection of a template being searched by a partial render: its
 * static candidates and the marks of the elements to execute, by document
 * order. With no candidate, its x-include elements are searched instead. */
typedef struct {
    const selector_complex_t **candidates;
    unsigned char *marks;
    sel_parent_t *parents;
    sel_parent_t root; /* the top level of the template, as a parent */
    size_t marks_count;
    int search;
} sel_context_t;

/* A captured emission of a positional candidate, judged when its parent is
 * closed and the totals are known. */
typedef struct {
    const selector_complex_t *complex;
    size_t index;
    size_t of_type_index;
    size_t tag;
    char *output;
    size_t len;
} sel_emission_t;

/* The emissions counted under one open parent of positional candidates:
 * all of its children, and those of each candidate tag name. */
typedef struct {
    const sel_parent_t *parent;
    const ir_t *ir;
    size_t total;
    size_t *counts;
    sel_emission_t *emissions;
    size_t nemissions;
    size_t capacity;
} sel_track_t;

/* An emission of a candidate being captured from the output position start.
 * track is NULL for a candidate without positional conditions. */
typedef struct {
    int active;
    const selector_complex_t *complex;
    const ir_t *ir;
    sel_track_t *track;
    size_t index;
    size_t of_type_index;
    size_t tag;
    size_t start;
} sel_capture_t;

typedef enum {
    FRAME_CHILDREN, /* the children of an open element, or of the root */
    FRAME_FOR,      /* the iterations of an x-for element */
    FRAME_EACH,     /* the iterations of an x-each element */
    FRAME_INCLUDE   /* an x-include element whose template is rendering */
} frame_kind_t;

/* A children frame holds an open element (NULL for the root) and the next
 * child to render. A loop frame holds the element it repeats and its
 * position: the next x-for value, or the next x-each array index or, for a
 * hash table, the last key, kept on the Lua stack. scope_depth is restored
 * when the frame is dropped, and a loop frame also restores the Lua stack
 * to stack_base, dropping the collection, the key, and the table of loop
 * variables. An include frame holds the x-include element and the template
 * and scope floor to return to when the included template is done. */
typedef struct {
    frame_kind_t kind;
    const ir_element_t *element;
    const ir_node_t *next;
    size_t scope_depth;
    int stack_base;
    int vars_idx;
    int collection_idx;
    int is_hash;
    int index;
    double value;
    const ir_t *ir;
    size_t floor;
    /* Partial rendering: the counts of a parent of positional candidates,
     * the emission that the frame closes, and the selection to restore when
     * an include frame switched it. */
    sel_track_t *track;
    sel_capture_t capture;
    int switched;
    sel_context_t *context;
} frame_t;

typedef struct {
    lua_State *L;
    const ir_t *ir;
    pool_t *pool;
    render_output_t out;
    frame_t *frames;
    size_t capacity;
    size_t depth;
    scope_t scope;
    expr_eval_t eval;
    /* Where a runtime error was raised, for its meta. */
    const ir_element_t *error_element;
    const reflow_expression_t *error_expression;
    const char *error_directive;
    const ir_bind_t *error_bind;
    /* Set when x-break or x-break-if fired, until the innermost loop ends. */
    int breaking;
    /* The names of the templates being rendered, from the entry, are kept
     * in the history table; include_depth is their number. */
    size_t include_depth;
    lua_Number max_include_depth;
    /* The reason of an include error, which makes the error an include
     * error. */
    const char *error_reason;
    /* Partial rendering. Outside a capture the output is discarded and only
     * what holds a candidate is executed. Two matches stop the render. */
    const selector_t *selector;
    sel_context_t *sel;
    size_t capturing;
    size_t nmatches;
    char *match_output;
    size_t match_len;
    const ir_t *match_irs[2];
    int stopped;
    const selector_pseudo_t *ancestor;
} render_ctx_t;

static const ir_t *to_ir(lua_State *L, int idx);

/* HTML void elements never have an end tag. The list is the one used by the
 * JavaScript version and by Lexbor when the template is compiled. */
static int is_void_element(const ir_string_t *tag)
{
    static const char *const names[] = {
        "area",  "base", "br",   "col",    "embed", "hr",  "img",
        "input", "link", "meta", "source", "track", "wbr", NULL,
    };
    size_t i = 0;

    for (i = 0; names[i] != NULL; i++) {
        if (strlen(names[i]) == tag->len &&
            memcmp(names[i], tag->data, tag->len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int push_frame_value(render_ctx_t *ctx, const frame_t *frame)
{
    if (ctx->depth == ctx->capacity) {
        size_t capacity =
            ctx->capacity ? ctx->capacity * 2 : INTERPRET_INITIAL_FRAMES;
        frame_t *frames = NULL;

        if (capacity > SIZE_MAX / sizeof(*frames)) {
            errno = ENOMEM;
            return -1;
        }
        frames = (frame_t *)pool_realloc(ctx->pool, ctx->frames,
                                         capacity * sizeof(*frames));
        if (frames == NULL) {
            return -1;
        }
        ctx->frames   = frames;
        ctx->capacity = capacity;
    }
    ctx->frames[ctx->depth++] = *frame;
    return 0;
}

static int same_name(const ir_string_t *a, const ir_string_t *b)
{
    return a->len == b->len && memcmp(a->data, b->data, a->len) == 0;
}

/* Return the index of tag among the tags of parent, or SIZE_MAX. */
static size_t sel_tag_index(const sel_parent_t *parent, const ir_string_t *tag)
{
    size_t i = 0;

    for (i = 0; i < parent->ntags; i++) {
        if (same_name(parent->tags[i], tag)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static int sel_add_tag(pool_t *pool, sel_parent_t *parent,
                       const ir_string_t *tag)
{
    if (sel_tag_index(parent, tag) != SIZE_MAX) {
        return 0;
    } else if (parent->ntags == parent->capacity) {
        size_t capacity          = parent->capacity ? parent->capacity * 2 : 2;
        const ir_string_t **tags = NULL;

        if (capacity > SIZE_MAX / sizeof(*tags)) {
            errno = ENOMEM;
            return -1;
        }
        tags = (const ir_string_t **)pool_realloc(pool, (void *)parent->tags,
                                                  capacity * sizeof(*tags));
        if (tags == NULL) {
            return -1;
        }
        parent->tags     = tags;
        parent->capacity = capacity;
    }
    parent->tags[parent->ntags++] = tag;
    return 0;
}

/* Mark element and its structural ancestors as holding what is searched. */
static void sel_mark_relevant(sel_context_t *sel, const ir_element_t *element)
{
    while (element != NULL &&
           !(sel->marks[element->structure.order] & SEL_RELEVANT)) {
        sel->marks[element->structure.order] |= SEL_RELEVANT;
        element = element->structure.parent;
    }
}

static void sel_context_free(pool_t *pool, sel_context_t *sel)
{
    size_t i = 0;

    if (sel == NULL) {
        return;
    }
    if (sel->parents != NULL && sel->marks != NULL) {
        for (i = 0; i < sel->marks_count; i++) {
            (void)pool_free(pool, (void *)sel->parents[i].tags);
        }
    }
    (void)pool_free(pool, (void *)sel->root.tags);
    (void)pool_free(pool, (void *)sel->parents);
    (void)pool_free(pool, (void *)sel->candidates);
    (void)pool_free(pool, sel->marks);
    (void)pool_free(pool, sel);
}

/* Build the selection of ir. A selector whose positional condition is on a
 * part other than the rightmost one is rejected when it matches, as a Lua
 * error noted in the context. */
static int sel_context_new(render_ctx_t *ctx, const ir_t *ir,
                           sel_context_t **out)
{
    pool_t *pool                      = ctx->pool;
    const ir_dom_index_t *index       = ir->index;
    size_t nelements                  = index->elements.count;
    selector_candidates_t cands       = {0};
    const selector_pseudo_t *ancestor = NULL;
    sel_context_t *sel                = NULL;
    size_t i                          = 0;

    if (selector_exec_resolve(pool, index, ctx->selector, &cands, &ancestor) !=
        0) {
        if (errno == EINVAL) {
            ctx->ancestor = ancestor;
            lua_pushliteral(ctx->L, "unsupported selector");
            return lua_error(ctx->L);
        }
        return -1;
    }
    sel = (sel_context_t *)pool_calloc(pool, 1, sizeof(*sel));
    if (sel == NULL) {
        goto failed;
    } else if (cands.count == 0 && index->includes.count == 0) {
        *out = sel;
        return 0;
    }
    sel->marks_count = nelements;
    sel->marks       = (unsigned char *)pool_calloc(pool, nelements, 1);
    if (sel->marks == NULL) {
        goto failed;
    } else if (cands.count == 0) {
        /* Search the templates included from here. */
        sel->search = 1;
        for (i = 0; i < index->includes.count; i++) {
            const ir_element_t *element = index->includes.items[i];

            sel->marks[element->structure.order] |= SEL_INCLUDE;
            sel_mark_relevant(sel, element);
        }
        *out = sel;
        return 0;
    }
    sel->candidates = (const selector_complex_t **)pool_calloc(
        pool, nelements, sizeof(*sel->candidates));
    sel->parents =
        (sel_parent_t *)pool_calloc(pool, nelements, sizeof(*sel->parents));
    if (sel->candidates == NULL || sel->parents == NULL) {
        goto failed;
    }
    for (i = 0; i < cands.count; i++) {
        const ir_element_t *element = cands.items[i].element;
        const ir_element_t *parent  = element->structure.parent;

        sel->candidates[element->structure.order] = cands.items[i].complex;
        sel_mark_relevant(sel, parent);
        if (selector_exec_rightmost(cands.items[i].complex)->npseudos == 0) {
            continue;
        } else if (parent == NULL) {
            if (sel_add_tag(pool, &sel->root, &element->tag_name) != 0) {
                goto failed;
            }
        } else {
            sel->marks[parent->structure.order] |= SEL_PARENT;
            if (sel_add_tag(pool, &sel->parents[parent->structure.order],
                            &element->tag_name) != 0) {
                goto failed;
            }
        }
    }
    (void)pool_free(pool, cands.items);
    *out = sel;
    return 0;

failed:
    (void)pool_free(pool, cands.items);
    sel_context_free(pool, sel);
    errno = ENOMEM;
    return -1;
}

/* Start counting the children of a parent of positional candidates. */
static int sel_track_new(render_ctx_t *ctx, const sel_parent_t *parent,
                         sel_track_t **out)
{
    sel_track_t *track =
        (sel_track_t *)pool_calloc(ctx->pool, 1, sizeof(*track));

    if (track == NULL) {
        return -1;
    }
    track->counts =
        (size_t *)pool_calloc(ctx->pool, parent->ntags, sizeof(*track->counts));
    if (track->counts == NULL) {
        (void)pool_free(ctx->pool, track);
        return -1;
    }
    track->parent = parent;
    track->ir     = ctx->ir;
    *out          = track;
    return 0;
}

/* Return the parent record of the children frame of element (NULL for the
 * top level) when its children are counted by the selection. */
static const sel_parent_t *sel_tracked_parent(const render_ctx_t *ctx,
                                              const ir_element_t *element)
{
    const sel_context_t *sel = ctx->sel;

    if (ctx->selector == NULL || sel == NULL) {
        return NULL;
    } else if (element == NULL) {
        return sel->root.ntags > 0 ? &sel->root : NULL;
    } else if (sel->marks == NULL ||
               !(sel->marks[element->structure.order] & SEL_PARENT)) {
        return NULL;
    }
    return &sel->parents[element->structure.order];
}

static int push_frame(render_ctx_t *ctx, const ir_element_t *element,
                      const ir_node_t *next, size_t scope_depth)
{
    const sel_parent_t *parent = sel_tracked_parent(ctx, element);
    frame_t frame              = {
        .kind        = FRAME_CHILDREN,
        .element     = element,
        .next        = next,
        .scope_depth = scope_depth,
    };

    if (parent != NULL && sel_track_new(ctx, parent, &frame.track) != 0) {
        return -1;
    }
    return push_frame_value(ctx, &frame);
}

#define WRITE_LITERAL(out, s) render_output_write((out), (s), sizeof(s) - 1)

/* Return the 0-based position of the x-bind targeting name, or -1. */
static int find_bind(const ir_bind_list_t *binds, const ir_string_t *name)
{
    const ir_bind_t *bind = NULL;
    int i                 = 0;

    for (bind = binds->head; bind != NULL; bind = bind->next, i++) {
        if (same_name(&bind->name, name)) {
            return i;
        }
    }
    return -1;
}

static int has_attr(const ir_attr_list_t *attrs, const ir_string_t *name)
{
    const ir_attr_t *attr = NULL;

    for (attr = attrs->head; attr != NULL; attr = attr->next) {
        if (same_name(&attr->name, name)) {
            return 1;
        }
    }
    return 0;
}

/* Write a computed attribute from the value at idx: false omits it, true
 * writes the bare name, and a string is written escaped as the value. */
static int write_bound_attr(lua_State *L, render_output_t *out,
                            const ir_string_t *name, int idx)
{
    const char *s = NULL;
    size_t len    = 0;

    if (lua_type(L, idx) == LUA_TBOOLEAN && !lua_toboolean(L, idx)) {
        return 0;
    } else if (WRITE_LITERAL(out, " ") != 0 ||
               render_output_write(out, name->data, name->len) != 0) {
        return -1;
    } else if (lua_type(L, idx) == LUA_TBOOLEAN) {
        return 0;
    }
    s = lua_tolstring(L, idx, &len);
    if (WRITE_LITERAL(out, "=\"") != 0 ||
        render_output_write_attr_text(out, s, len) != 0) {
        return -1;
    }
    return WRITE_LITERAL(out, "\"");
}

/* Write the start tag. The values of the element's x-bind directives are on
 * top of the stack in source order, as left by push_bind_values. A bound
 * attribute replaces every source attribute of the same name in place;
 * the others follow the source attributes in source order. */
static int write_start_tag(lua_State *L, render_output_t *out,
                           const ir_element_t *element)
{
    const ir_bind_list_t *binds =
        element->directives ? &element->directives->bind : NULL;
    int nbinds            = binds ? (int)binds->count : 0;
    int base              = lua_gettop(L) - nbinds;
    const ir_attr_t *attr = NULL;
    const ir_bind_t *bind = NULL;
    int i                 = 0;

    if (WRITE_LITERAL(out, "<") != 0 ||
        render_output_write(out, element->tag_name.data,
                            element->tag_name.len) != 0) {
        return -1;
    }
    for (attr = element->attrs.head; attr != NULL; attr = attr->next) {
        int pos = nbinds ? find_bind(binds, &attr->name) : -1;

        if (pos >= 0) {
            if (write_bound_attr(L, out, &attr->name, base + 1 + pos) != 0) {
                return -1;
            }
        } else if (WRITE_LITERAL(out, " ") != 0 ||
                   render_output_write(out, attr->name.data, attr->name.len) !=
                       0 ||
                   WRITE_LITERAL(out, "=\"") != 0 ||
                   render_output_write_attr_value(out, attr->value.data,
                                                  attr->value.len) != 0 ||
                   WRITE_LITERAL(out, "\"") != 0) {
            return -1;
        }
    }
    for (bind = nbinds ? binds->head : NULL; bind != NULL;
         bind = bind->next, i++) {
        if (!has_attr(&element->attrs, &bind->name) &&
            write_bound_attr(L, out, &bind->name, base + 1 + i) != 0) {
            return -1;
        }
    }
    return WRITE_LITERAL(out, ">");
}

static int write_end_tag(render_output_t *out, const ir_element_t *element)
{
    if (WRITE_LITERAL(out, "</") != 0 ||
        render_output_write(out, element->tag_name.data,
                            element->tag_name.len) != 0) {
        return -1;
    }
    return WRITE_LITERAL(out, ">");
}

/* Evaluate an expression of element, recording both for the meta of an error
 * raised meanwhile. */
static int eval_expression(render_ctx_t *ctx, const ir_element_t *element,
                           const reflow_expression_t *expression)
{
    ctx->error_element    = element;
    ctx->error_expression = expression;
    if (expr_eval(&ctx->eval, expression) != 0) {
        return -1;
    }
    ctx->error_element    = NULL;
    ctx->error_expression = NULL;
    return 0;
}

/* Raise the type error of a directive whose value is on top of the stack.
 * The site of the evaluation is restored for the meta. */
static int raise_type_error(render_ctx_t *ctx, const ir_element_t *element,
                            const reflow_expression_t *expression)
{
    ctx->error_element    = element;
    ctx->error_expression = expression;
    return expr_eval_error(&ctx->eval);
}

/* Write the value of an x-html expression unescaped. nil and null write
 * nothing; any other value that is not a string is an error. */
static int write_html_content(render_ctx_t *ctx, const ir_element_t *element,
                              const reflow_expression_t *expression)
{
    lua_State *L  = ctx->L;
    const char *s = NULL;
    size_t len    = 0;
    int rc        = 0;

    if (eval_expression(ctx, element, expression) != 0) {
        return -1;
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        s  = lua_tolstring(L, -1, &len);
        rc = render_output_write(&ctx->out, s, len);
    } else if (!value_lua_is_nullish(L, -1)) {
        ctx->error_directive = "x-html";
        lua_pushfstring(
            L, "x-html: value must be string, null, or undefined, got %s",
            value_lua_type_name(L, -1));
        return raise_type_error(ctx, element, expression);
    }
    lua_pop(L, 1);
    return rc;
}

/* Write the value of an x-text expression as escaped text. nil and null
 * write nothing; tables, functions, userdata, and threads are errors. */
static int write_text_content(render_ctx_t *ctx, const ir_element_t *element,
                              const reflow_expression_t *expression)
{
    lua_State *L  = ctx->L;
    const char *s = NULL;
    size_t len    = 0;
    int rc        = 0;

    if (eval_expression(ctx, element, expression) != 0) {
        return -1;
    }
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        break;
    case LUA_TBOOLEAN:
        rc = lua_toboolean(L, -1) ? WRITE_LITERAL(&ctx->out, "true") :
                                    WRITE_LITERAL(&ctx->out, "false");
        break;
    case LUA_TNUMBER:
        value_lua_push_number_string(L, -1);
        s  = lua_tolstring(L, -1, &len);
        rc = render_output_write_text(&ctx->out, s, len);
        lua_pop(L, 1);
        break;
    case LUA_TSTRING:
        s  = lua_tolstring(L, -1, &len);
        rc = render_output_write_text(&ctx->out, s, len);
        break;
    default:
        if (value_lua_is_null(L, -1)) {
            break;
        }
        ctx->error_directive = "x-text";
        lua_pushfstring(L, "x-text: value must be primitive, got %s",
                        value_lua_type_name(L, -1));
        return raise_type_error(ctx, element, expression);
    }
    lua_pop(L, 1);
    return rc;
}

/* Evaluate every x-with binding against the current scope, then push them
 * as one data frame, so that the bindings do not see each other. A binding
 * to nil is kept so that it still hides outer bindings of the name. */
static int push_with_frame(render_ctx_t *ctx, const ir_element_t *element)
{
    lua_State *L               = ctx->L;
    const ir_with_list_t *list = &element->directives->with;
    const ir_with_t *with      = NULL;

    lua_createtable(L, 0, list->count > INT_MAX ? 0 : (int)list->count);
    for (with = list->head; with != NULL; with = with->next) {
        if (eval_expression(ctx, element, with->expression) != 0) {
            return -1;
        } else if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            scope_push_undefined(L);
        }
        lua_pushlstring(L, with->name.data, with->name.len);
        lua_insert(L, -2);
        lua_rawset(L, -3);
    }
    return scope_push(&ctx->scope, SCOPE_DATA);
}

/* Evaluate the x-bind directives in source order and leave one value per
 * directive on the stack: false to omit the attribute, true for a bare
 * attribute, or the string value. Numbers are converted as String(n) is in
 * JavaScript; tables, functions, userdata, and threads are errors. */
static int push_bind_values(render_ctx_t *ctx, const ir_element_t *element)
{
    lua_State *L          = ctx->L;
    const ir_bind_t *bind = NULL;

    for (bind = element->directives->bind.head; bind != NULL;
         bind = bind->next) {
        /* The values stay on the stack until the start tag is written.
         * Reserve the room an evaluation starts with, so that running out
         * of stack is reported as too many directives. */
        if (!lua_checkstack(L, LUA_MINSTACK)) {
            ctx->error_element = element;
            lua_pushliteral(L, "too many x-bind directives on one element");
            return expr_eval_error(&ctx->eval);
        } else if (eval_expression(ctx, element, bind->expression) != 0) {
            return -1;
        }
        switch (lua_type(L, -1)) {
        case LUA_TBOOLEAN:
        case LUA_TSTRING:
            break;
        case LUA_TNUMBER:
            value_lua_push_number_string(L, -1);
            lua_replace(L, -2);
            break;
        default:
            if (value_lua_is_nullish(L, -1)) {
                lua_pop(L, 1);
                lua_pushboolean(L, 0);
                break;
            }
            ctx->error_bind = bind;
            lua_pushliteral(L, "x-bind:");
            lua_pushlstring(L, bind->name.data, bind->name.len);
            lua_pushfstring(L, ": value must be primitive, got %s",
                            value_lua_type_name(L, -3));
            lua_concat(L, 3);
            return raise_type_error(ctx, element, bind->expression);
        }
    }
    return 0;
}

/* Select the branch of an x-match element: evaluate the match value, then
 * the x-case values in order until one is the same value (=== in
 * JavaScript, which lua_rawequal gives). An x-nocase is selected when it is
 * reached. *selected is left NULL when nothing matches. */
static int select_match_branch(render_ctx_t *ctx, const ir_element_t *element,
                               const ir_node_t **selected)
{
    lua_State *L               = ctx->L;
    const ir_branch_t *branch  = NULL;
    const ir_directives_t *dir = element->directives;

    *selected = NULL;
    if (eval_expression(ctx, element, dir->match_condition) != 0) {
        return -1;
    }
    for (branch = dir->match_branches.head; branch != NULL;
         branch = branch->next) {
        int same = 0;

        if (branch->condition != NULL) {
            if (eval_expression(ctx, (const ir_element_t *)branch->node,
                                branch->condition) != 0) {
                return -1;
            }
            same = lua_rawequal(L, -1, -2);
            lua_pop(L, 1);
        }
        if (branch->condition == NULL || same) {
            *selected = branch->node;
            break;
        }
    }
    lua_pop(L, 1);
    return 0;
}

/* Raise an include error with its reason; the requested value on top of
 * the stack is kept in the history table for the meta. */
static int raise_include_error(render_ctx_t *ctx, const ir_element_t *element,
                               const char *reason)
{
    lua_State *L = ctx->L;

    lua_pushvalue(L, -2);
    lua_setfield(L, INTERPRET_HISTORY_IDX, "requested");
    ctx->error_reason = reason;
    return raise_type_error(ctx, element, element->directives->include);
}

/* Start rendering the template named by the x-include of element, after its
 * start tag. The template gets a new lexical scope that sees only the
 * x-with frame of the element at with_depth (0 when it has none). The
 * element is closed by render_tree when the template is done. */
static int start_include(render_ctx_t *ctx, const ir_element_t *element,
                         size_t scope_depth, size_t with_depth)
{
    lua_State *L   = ctx->L;
    const ir_t *ir = NULL;
    frame_t frame  = {0};
    size_t i       = 0;

    if (eval_expression(ctx, element, element->directives->include) != 0) {
        return -1;
    } else if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pushfstring(L,
                        "x-include: value must evaluate to a template name "
                        "(string), got %s",
                        value_lua_type_name(L, -1));
        return raise_include_error(ctx, element, "invalid");
    }
    if (lua_type(L, INTERPRET_TEMPLATES_IDX) == LUA_TTABLE) {
        lua_pushvalue(L, -1);
        lua_rawget(L, INTERPRET_TEMPLATES_IDX);
        ir = to_ir(L, -1);
        lua_pop(L, 1);
    }
    if (ir == NULL) {
        lua_pushfstring(L, "template not found: \"%s\"", lua_tostring(L, -1));
        return raise_include_error(ctx, element, "not_found");
    } else if ((lua_Number)ctx->include_depth >= ctx->max_include_depth) {
        char limit[JS_NUMBER_BUFSIZE];

        js_number_format((double)ctx->max_include_depth, limit);
        lua_pushfstring(L, "include depth limit exceeded (%s)", limit);
        return raise_include_error(ctx, element, "depth_exceeded");
    }
    for (i = 1; i <= ctx->include_depth; i++) {
        lua_rawgeti(L, INTERPRET_HISTORY_IDX, (int)i);
        if (lua_rawequal(L, -1, -2)) {
            lua_pop(L, 1);
            lua_pushliteral(L, "include cycle detected");
            return raise_include_error(ctx, element, "cycle");
        }
        lua_pop(L, 1);
    }
    lua_rawseti(L, INTERPRET_HISTORY_IDX, (int)++ctx->include_depth);

    frame = (frame_t){
        .kind        = FRAME_INCLUDE,
        .element     = element,
        .scope_depth = scope_depth,
        .ir          = ctx->ir,
        .floor       = ctx->scope.floor,
    };
    if (ctx->selector != NULL) {
        /* A partial render searches the template only outside a capture;
         * the selection of the including template is restored when the
         * include frame is closed. */
        frame.switched = 1;
        frame.context  = ctx->sel;
    }
    if (push_frame_value(ctx, &frame) != 0) {
        return -1;
    }
    ctx->ir          = ir;
    ctx->scope.floor = ctx->scope.depth;
    if (ctx->selector != NULL) {
        ctx->sel = NULL;
        if (ctx->capturing == 0 && sel_context_new(ctx, ir, &ctx->sel) != 0) {
            return -1;
        }
    }
    if (with_depth > 0 && scope_push_frame_of(&ctx->scope, with_depth) != 0) {
        return -1;
    }
    return push_frame(ctx, NULL, ir->root->children.head, ctx->scope.depth);
}

/* Called when an element has been written and its own frames dropped:
 * x-break, or x-break-if with a truthy value, ends the innermost loop. */
static int finish_element(render_ctx_t *ctx, const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;

    if (directives == NULL) {
        return 0;
    } else if (directives->has_break) {
        ctx->breaking = 1;
    } else if (directives->break_condition != NULL) {
        if (eval_expression(ctx, element, directives->break_condition) != 0) {
            return -1;
        }
        ctx->breaking = value_lua_is_truthy(ctx->L, -1);
        lua_pop(ctx->L, 1);
    }
    return 0;
}

static int is_loop(const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;

    return directives != NULL &&
           (directives->range != NULL || directives->each != NULL);
}

/* Report whether the n-th position from the start (n_from_end unset) or the
 * end of total positions is position. n is a positive integer. */
static int sel_nth_is(double n, int n_from_end, size_t position, size_t total)
{
    size_t nth = 0;

    if (n > (double)total) {
        return 0;
    }
    nth = (size_t)n;
    return position == (n_from_end ? total - nth + 1 : nth);
}

/* Report whether a positional condition holds for an emission at the
 * 1-based position index among total emissions of its parent, and at
 * of_type_index among of_type_total emissions of its tag name, as the
 * JavaScript evalPositional does. */
static int sel_positional_holds(const selector_pseudo_t *pseudo, size_t index,
                                size_t total, size_t of_type_index,
                                size_t of_type_total)
{
    switch (pseudo->kind) {
    case SELECTOR_PSEUDO_FIRST_CHILD:
        return index == 1;
    case SELECTOR_PSEUDO_LAST_CHILD:
        return index == total;
    case SELECTOR_PSEUDO_ONLY_CHILD:
        return total == 1;
    case SELECTOR_PSEUDO_FIRST_OF_TYPE:
        return of_type_index == 1;
    case SELECTOR_PSEUDO_LAST_OF_TYPE:
        return of_type_index == of_type_total;
    case SELECTOR_PSEUDO_ONLY_OF_TYPE:
        return of_type_total == 1;
    case SELECTOR_PSEUDO_NTH_CHILD:
        return sel_nth_is(pseudo->n, 0, index, total);
    case SELECTOR_PSEUDO_NTH_LAST_CHILD:
        return sel_nth_is(pseudo->n, 1, index, total);
    case SELECTOR_PSEUDO_NTH_OF_TYPE:
        return sel_nth_is(pseudo->n, 0, of_type_index, of_type_total);
    case SELECTOR_PSEUDO_NTH_LAST_OF_TYPE:
        break;
    }
    return sel_nth_is(pseudo->n, 1, of_type_index, of_type_total);
}

/* Count a match of the template ir, keeping the output of the first one.
 * The second match stops the render. output is owned by the call. */
static void sel_add_match(render_ctx_t *ctx, const ir_t *ir, char *output,
                          size_t len)
{
    if (ctx->nmatches == 0) {
        ctx->match_output = output;
        ctx->match_len    = len;
    } else {
        (void)pool_free(ctx->pool, output);
    }
    if (ctx->nmatches < 2) {
        ctx->match_irs[ctx->nmatches] = ir;
    }
    if (++ctx->nmatches >= 2) {
        ctx->stopped = 1;
    }
}

/* End a capture: keep the output written since it began as a match, or as
 * an emission to judge when its parent is closed. */
static int sel_finish_capture(render_ctx_t *ctx, const sel_capture_t *capture)
{
    size_t len      = ctx->out.len - capture->start;
    char *output    = (char *)pool_alloc(ctx->pool, len + 1);
    sel_track_t *tr = capture->track;

    if (output == NULL) {
        return -1;
    }
    memcpy(output, ctx->out.data + capture->start, len);
    if (--ctx->capturing == 0) {
        ctx->out.len     = 0;
        ctx->out.discard = 1;
    }
    if (tr == NULL) {
        sel_add_match(ctx, capture->ir, output, len);
        return 0;
    } else if (tr->nemissions == tr->capacity) {
        size_t capacity           = tr->capacity ? tr->capacity * 2 : 4;
        sel_emission_t *emissions = NULL;

        if (capacity > SIZE_MAX / sizeof(*emissions)) {
            errno = ENOMEM;
            return -1;
        }
        emissions = (sel_emission_t *)pool_realloc(
            ctx->pool, tr->emissions, capacity * sizeof(*emissions));
        if (emissions == NULL) {
            return -1;
        }
        tr->emissions = emissions;
        tr->capacity  = capacity;
    }
    tr->emissions[tr->nemissions++] = (sel_emission_t){
        .complex       = capture->complex,
        .index         = capture->index,
        .of_type_index = capture->of_type_index,
        .tag           = capture->tag,
        .output        = output,
        .len           = len,
    };
    return 0;
}

/* Judge the emissions of a closed parent of positional candidates now that
 * its totals are known, in the order they were emitted, and release it. */
static void sel_judge(render_ctx_t *ctx, sel_track_t *track)
{
    size_t i = 0;

    for (i = 0; i < track->nemissions; i++) {
        const sel_emission_t *emission = &track->emissions[i];
        const selector_compound_t *last =
            selector_exec_rightmost(emission->complex);
        int holds = !ctx->stopped;
        size_t j  = 0;

        for (j = 0; holds && j < last->npseudos; j++) {
            holds = sel_positional_holds(&last->pseudos[j], emission->index,
                                         track->total, emission->of_type_index,
                                         track->counts[emission->tag]);
        }
        if (holds) {
            sel_add_match(ctx, track->ir, emission->output, emission->len);
        } else {
            (void)pool_free(ctx->pool, emission->output);
        }
    }
    (void)pool_free(ctx->pool, track->emissions);
    (void)pool_free(ctx->pool, track->counts);
    (void)pool_free(ctx->pool, track);
}

/* Return the counts of the open parent whose children are being rendered,
 * skipping the loop frame of element when it is one of its iterations. */
static sel_track_t *sel_sibling_track(const render_ctx_t *ctx,
                                      const ir_element_t *element)
{
    const frame_t *frame = NULL;

    if (ctx->depth == 0) {
        return NULL;
    }
    frame = &ctx->frames[ctx->depth - 1];
    if (frame->kind != FRAME_CHILDREN && frame->element == element &&
        ctx->depth > 1) {
        frame = &ctx->frames[ctx->depth - 2];
    }
    return frame->kind == FRAME_CHILDREN ? frame->track : NULL;
}

static size_t add_saturated(size_t a, size_t b)
{
    return a > SIZE_MAX - b ? SIZE_MAX : a + b;
}

/* Count n emissions of element under its open parent. */
static void sel_count_emissions(sel_track_t *track, const ir_element_t *element,
                                size_t n)
{
    size_t tag = sel_tag_index(track->parent, &element->tag_name);

    track->total = add_saturated(track->total, n);
    if (tag != SIZE_MAX) {
        track->counts[tag] = add_saturated(track->counts[tag], n);
    }
}

/* Called for each emission of an element in a partial render: count it
 * under its parent, and begin capturing it when it is a candidate. */
static int sel_begin_emission(render_ctx_t *ctx, const ir_element_t *element,
                              sel_capture_t *capture)
{
    const sel_context_t *sel          = ctx->sel;
    sel_track_t *track                = sel_sibling_track(ctx, element);
    const selector_complex_t *complex = NULL;
    size_t tag                        = SIZE_MAX;

    if (track != NULL) {
        tag = sel_tag_index(track->parent, &element->tag_name);
        sel_count_emissions(track, element, 1);
    }
    if (sel != NULL && sel->candidates != NULL) {
        complex = sel->candidates[element->structure.order];
    }
    if (complex == NULL) {
        return 0;
    } else if (selector_exec_rightmost(complex)->npseudos == 0) {
        track = NULL;
    } else if (track == NULL || tag == SIZE_MAX) {
        /* Not emitted under its counted parent: it cannot match. */
        return 0;
    }
    *capture = (sel_capture_t){
        .active        = 1,
        .complex       = complex,
        .ir            = ctx->ir,
        .track         = track,
        .index         = track != NULL ? track->total : 0,
        .of_type_index = track != NULL ? track->counts[tag] : 0,
        .tag           = tag,
        .start         = ctx->out.len,
    };
    ctx->capturing++;
    ctx->out.discard = 0;
    return 0;
}

/* Count the emissions of an element that is not executed: the iterations
 * of its loop, or one. The collection of an x-each is evaluated as when the
 * loop starts. */
static int sel_count_unexecuted(render_ctx_t *ctx, const ir_element_t *element,
                                sel_track_t *track)
{
    lua_State *L                      = ctx->L;
    const ir_directives_t *directives = element->directives;
    size_t n                          = 1;

    if (directives != NULL && directives->range != NULL) {
        const ir_for_t *range = directives->range;
        double span           = range->step > 0 ? range->stop - range->start :
                                                  range->start - range->stop;
        double steps          = floor(span / fabs(range->step));

        /* The steps of a huge range saturate the count. */
        if (span < 0) {
            n = 0;
        } else if (steps >= (double)(SIZE_MAX - 1)) {
            n = SIZE_MAX;
        } else {
            n = (size_t)steps + 1;
        }
    } else if (directives != NULL && directives->each != NULL) {
        if (eval_expression(ctx, element, directives->each->collection) != 0) {
            return -1;
        } else if (lua_type(L, -1) != LUA_TTABLE) {
            ctx->error_directive = "x-each";
            lua_pushfstring(L, "x-each: expected array or object, got %s",
                            value_lua_type_name(L, -1));
            return raise_type_error(ctx, element, directives->each->collection);
        }
        n = 0;
        if (value_lua_raw_length(L, -1) == 0) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                lua_pop(L, 1);
                n++;
            }
        } else {
            while (n < INT_MAX - 1) {
                lua_rawgeti(L, -1, (int)n + 1);
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                lua_pop(L, 1);
                n++;
            }
        }
        lua_pop(L, 1);
    }
    sel_count_emissions(track, element, n);
    return 0;
}

typedef enum {
    SEL_RUN,  /* execute it */
    SEL_SKIP, /* leave it out */
    SEL_COUNT /* count its emissions without executing it */
} sel_action_t;

/* Decide what a partial render does with an element it reaches outside a
 * capture: execute it when it holds what is searched; otherwise count it
 * when it is a sibling under a counted parent, or leave it out, unless a
 * break inside it can change what is emitted. */
static sel_action_t sel_action(const render_ctx_t *ctx,
                               const ir_element_t *element)
{
    const sel_context_t *sel = ctx->sel;
    size_t order             = element->structure.order;

    if (ctx->selector == NULL || ctx->capturing > 0) {
        return SEL_RUN;
    } else if (sel != NULL && sel->marks != NULL &&
               ((sel->marks[order] & SEL_RELEVANT) ||
                (sel->candidates != NULL && sel->candidates[order] != NULL))) {
        return SEL_RUN;
    } else if (sel_sibling_track(ctx, NULL) != NULL) {
        return element->structure.breaks_out ? SEL_RUN : SEL_COUNT;
    }
    return element->structure.breaks_out && !is_loop(element) ? SEL_RUN :
                                                                SEL_SKIP;
}

/* Report whether an x-include element is searched by the partial render. */
static int sel_searches(const render_ctx_t *ctx, const ir_element_t *element)
{
    const sel_context_t *sel = ctx->sel;

    return sel != NULL && sel->search &&
           (sel->marks[element->structure.order] & SEL_INCLUDE);
}

/* Render an element as the JavaScript version does: push its x-data frame,
 * then its x-with frame, select the branch of an x-match, evaluate its
 * x-bind directives, write the start tag and its x-text or x-html content,
 * and open a frame for its children, or for the selected branch alone,
 * unless it has none. */
static int render_element_body(render_ctx_t *ctx, const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;
    size_t scope_depth                = ctx->scope.depth;
    int is_void                       = is_void_element(&element->tag_name);
    const ir_node_t *children         = element->children.head;
    size_t with_depth                 = 0;
    int has_content                   = 0;
    int content_rc                    = 0;
    /* Outside a capture, a partial render writes nothing, so the values of
     * x-bind, x-text, and x-html are not evaluated. */
    int quiet = ctx->selector != NULL && ctx->capturing == 0;

    if (directives != NULL && directives->invisible_marker) {
        /* Only x-break or x-break-if: nothing is written. */
        return finish_element(ctx, element);
    } else if (directives != NULL) {
        if (directives->data_scopes != NULL &&
            (value_lua_push(ctx->L, ctx->pool, directives->data_scopes) != 0 ||
             scope_push(&ctx->scope, SCOPE_DATA) != 0)) {
            return -1;
        } else if (directives->with.count > 0) {
            if (push_with_frame(ctx, element) != 0) {
                return -1;
            }
            with_depth = ctx->scope.depth;
        }
        if (directives->match_condition != NULL &&
            select_match_branch(ctx, element, &children) != 0) {
            return -1;
        } else if (!quiet && push_bind_values(ctx, element) != 0) {
            return -1;
        }
    }
    if (!quiet) {
        if (write_start_tag(ctx->L, &ctx->out, element) != 0) {
            return -1;
        } else if (directives != NULL) {
            lua_pop(ctx->L, (int)directives->bind.count);
        }
    }
    /* x-text, x-html, and x-include replace the children, except on an
     * x-match element, whose content is the selected branch. A void element
     * keeps no end tag even with content, as in the JavaScript version. */
    if (directives != NULL && directives->match_condition == NULL) {
        if (directives->include != NULL &&
            (!quiet || sel_searches(ctx, element))) {
            return start_include(ctx, element, scope_depth, with_depth);
        } else if (directives->include != NULL || quiet) {
            /* The content replaces the children, which are not emitted. */
            has_content = directives->include != NULL ||
                          directives->text != NULL || directives->html != NULL;
        } else if (directives->text != NULL) {
            has_content = 1;
            content_rc  = write_text_content(ctx, element, directives->text);
        } else if (directives->html != NULL) {
            has_content = 1;
            content_rc  = write_html_content(ctx, element, directives->html);
        }
    }
    if (!has_content && !is_void) {
        return push_frame(ctx, element, children, scope_depth);
    } else if (content_rc != 0 ||
               (!is_void && write_end_tag(&ctx->out, element) != 0)) {
        return -1;
    }
    scope_pop_to(&ctx->scope, scope_depth);
    return finish_element(ctx, element);
}

/* Render one emission of an element. A partial render counts it under its
 * parent and captures it when it is a candidate: until the frame that closes
 * it is closed, or now when it opens none. */
static int render_element_once(render_ctx_t *ctx, const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;
    sel_capture_t capture             = {0};
    size_t depth                      = ctx->depth;
    int rc                            = 0;

    if (ctx->selector != NULL &&
        !(directives != NULL && directives->invisible_marker) &&
        sel_begin_emission(ctx, element, &capture) != 0) {
        return -1;
    }
    rc = render_element_body(ctx, element);
    if (rc != 0 || !capture.active) {
        return rc;
    } else if (ctx->depth > depth) {
        ctx->frames[depth].capture = capture;
        return 0;
    }
    return sel_finish_capture(ctx, &capture);
}

/* Start the loop of an x-for or x-each element: evaluate the collection of
 * an x-each outside the element's own frames, and push the frame of loop
 * variables, which is reused for every iteration. The iterations are
 * rendered by render_tree. */
static int start_loop(render_ctx_t *ctx, const ir_element_t *element)
{
    lua_State *L                      = ctx->L;
    const ir_directives_t *directives = element->directives;
    frame_t frame                     = {
        .element     = element,
        .scope_depth = ctx->scope.depth,
        .stack_base  = lua_gettop(L),
    };

    /* The collection, the key, the loop variables, and a copy of them. */
    if (!lua_checkstack(L, 4)) {
        ctx->error_element = element;
        lua_pushliteral(L, "loops are too deeply nested");
        return expr_eval_error(&ctx->eval);
    }
    if (directives->range != NULL) {
        frame.kind  = FRAME_FOR;
        frame.value = directives->range->start;
    } else {
        frame.kind = FRAME_EACH;
        if (eval_expression(ctx, element, directives->each->collection) != 0) {
            return -1;
        } else if (lua_type(L, -1) != LUA_TTABLE) {
            ctx->error_directive = "x-each";
            lua_pushfstring(L, "x-each: expected array or object, got %s",
                            value_lua_type_name(L, -1));
            return raise_type_error(ctx, element, directives->each->collection);
        }
        frame.collection_idx = lua_gettop(L);
        frame.index          = 1;
        frame.is_hash        = value_lua_raw_length(L, -1) == 0;
        if (frame.is_hash) {
            lua_pushnil(L);
        }
    }
    lua_createtable(L, 0, 2);
    frame.vars_idx = lua_gettop(L);
    lua_pushvalue(L, -1);
    if (scope_push(&ctx->scope, SCOPE_LOOP) != 0) {
        return -1;
    }
    return push_frame_value(ctx, &frame);
}

/* Render an element, or start its loop. */
static int render_element(render_ctx_t *ctx, const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;

    if (directives != NULL &&
        (directives->range != NULL || directives->each != NULL)) {
        return start_loop(ctx, element);
    }
    return render_element_once(ctx, element);
}

static void set_loop_variable(lua_State *L, int vars_idx,
                              const ir_string_t *name)
{
    lua_pushlstring(L, name->data, name->len);
    lua_insert(L, -2);
    lua_rawset(L, vars_idx);
}

/* Push an x-for value as a Lua integer when it is one. */
static void push_loop_number(lua_State *L, double value)
{
    if (value >= -9007199254740992.0 && value <= 9007199254740992.0) {
        lua_pushinteger(L, (lua_Integer)value);
    } else {
        lua_pushnumber(L, (lua_Number)value);
    }
}

/* Set the loop variables of the next iteration of the loop frame on top.
 * Return 1 when there is one, or 0 when the loop has ended. */
static int next_iteration(render_ctx_t *ctx, frame_t *frame)
{
    lua_State *L = ctx->L;

    if (frame->kind == FRAME_FOR) {
        const ir_for_t *range = frame->element->directives->range;

        if (range->step > 0 ? frame->value > range->stop :
                              frame->value < range->stop) {
            return 0;
        }
        push_loop_number(L, frame->value);
        set_loop_variable(L, frame->vars_idx, &range->name);
        frame->value += range->step;
        return 1;
    } else {
        const ir_each_t *each = frame->element->directives->each;

        if (frame->is_hash) {
            lua_pushvalue(L, frame->collection_idx + 1);
            if (!lua_next(L, frame->collection_idx)) {
                return 0;
            }
            set_loop_variable(L, frame->vars_idx, &each->item);
            if (each->index.data != NULL) {
                lua_pushvalue(L, -1);
                set_loop_variable(L, frame->vars_idx, &each->index);
            }
            lua_replace(L, frame->collection_idx + 1);
            return 1;
        }
        /* lua_rawgeti takes an int key before Lua 5.3; no array this long
         * fits in memory. */
        if (frame->index == INT_MAX) {
            return 0;
        }
        lua_rawgeti(L, frame->collection_idx, frame->index);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return 0;
        }
        set_loop_variable(L, frame->vars_idx, &each->item);
        if (each->index.data != NULL) {
            lua_pushinteger(L, (lua_Integer)frame->index - 1);
            set_loop_variable(L, frame->vars_idx, &each->index);
        }
        frame->index++;
        return 1;
    }
}

/* Drop the loop frame on top with its scope frame and Lua values. */
static void end_loop(render_ctx_t *ctx)
{
    frame_t *frame = &ctx->frames[ctx->depth - 1];

    scope_pop_to(&ctx->scope, frame->scope_depth);
    lua_settop(ctx->L, frame->stack_base);
    ctx->depth--;
}

/* Render the first branch of an x-if chain whose condition is truthy, or
 * its x-else branch. Conditions are evaluated in the enclosing scope, so a
 * branch's own frames are not visible to them, and the conditions after the
 * selected branch are not evaluated. */
/* Render an element reached as a child, as a partial render decides. */
static int render_child(render_ctx_t *ctx, const ir_element_t *element)
{
    switch (sel_action(ctx, element)) {
    case SEL_RUN:
        return render_element(ctx, element);
    case SEL_COUNT:
        return sel_count_unexecuted(ctx, element, sel_sibling_track(ctx, NULL));
    case SEL_SKIP:
        break;
    }
    return 0;
}

/* Report whether a partial render needs the selected branch of a chain. */
static int sel_chain_needed(const render_ctx_t *ctx, const ir_chain_t *chain)
{
    const ir_branch_t *branch = NULL;

    if (ctx->selector == NULL || ctx->capturing > 0 ||
        sel_sibling_track(ctx, NULL) != NULL) {
        return 1;
    }
    for (branch = chain->branches.head; branch != NULL; branch = branch->next) {
        if (sel_action(ctx, (const ir_element_t *)branch->node) != SEL_SKIP) {
            return 1;
        }
    }
    return 0;
}

static int render_chain(render_ctx_t *ctx, const ir_chain_t *chain)
{
    lua_State *L              = ctx->L;
    const ir_branch_t *branch = NULL;

    if (!sel_chain_needed(ctx, chain)) {
        return 0;
    }
    for (branch = chain->branches.head; branch != NULL; branch = branch->next) {
        const ir_element_t *element = (const ir_element_t *)branch->node;

        if (branch->condition != NULL) {
            int truthy = 0;

            if (eval_expression(ctx, element, branch->condition) != 0) {
                return -1;
            }
            truthy = value_lua_is_truthy(L, -1);
            lua_pop(L, 1);
            if (!truthy) {
                continue;
            }
        }
        return render_child(ctx, element);
    }
    return 0;
}

/* Render one node. An element that may have children opens a new frame. */
static int render_node(render_ctx_t *ctx, const ir_node_t *node)
{
    switch (node->type) {
    case IR_TEXT: {
        const ir_text_t *text = (const ir_text_t *)node;
        return render_output_write(&ctx->out, text->text.data, text->text.len);
    }
    case IR_COMMENT: {
        const ir_comment_t *comment = (const ir_comment_t *)node;
        if (WRITE_LITERAL(&ctx->out, "<!--") != 0 ||
            render_output_write(&ctx->out, comment->text.data,
                                comment->text.len) != 0) {
            return -1;
        }
        return WRITE_LITERAL(&ctx->out, "-->");
    }
    case IR_ELEMENT:
        return render_child(ctx, (const ir_element_t *)node);
    case IR_CHAIN:
        return render_chain(ctx, (const ir_chain_t *)node);
    case IR_ROOT:
        return 0;
    }
    return 0;
}

static int render_tree(render_ctx_t *ctx)
{
    if (push_frame(ctx, NULL, ctx->ir->root->children.head, 0) != 0) {
        return -1;
    }
    while (ctx->depth > 0 && !ctx->stopped) {
        frame_t *frame        = &ctx->frames[ctx->depth - 1];
        const ir_node_t *node = frame->next;

        if (frame->kind == FRAME_INCLUDE) {
            /* The included template is done: return to the including one
             * and close the x-include element. */
            const ir_element_t *element = frame->element;
            size_t scope_depth          = frame->scope_depth;
            sel_capture_t capture       = frame->capture;

            if (frame->switched) {
                sel_context_free(ctx->pool, ctx->sel);
                ctx->sel = frame->context;
            }
            ctx->ir          = frame->ir;
            ctx->scope.floor = frame->floor;
            lua_pushnil(ctx->L);
            lua_rawseti(ctx->L, INTERPRET_HISTORY_IDX,
                        (int)ctx->include_depth--);
            ctx->depth--;
            if (!is_void_element(&element->tag_name) &&
                write_end_tag(&ctx->out, element) != 0) {
                return -1;
            } else if (capture.active &&
                       sel_finish_capture(ctx, &capture) != 0) {
                return -1;
            }
            scope_pop_to(&ctx->scope, scope_depth);
            if (!ctx->breaking && finish_element(ctx, element) != 0) {
                return -1;
            }
            continue;
        } else if (frame->kind != FRAME_CHILDREN) {
            /* A break ends the innermost loop. render_element_once may move
             * the frame array, so the loop position is advanced first. */
            if (ctx->breaking) {
                ctx->breaking = 0;
                end_loop(ctx);
            } else if (!next_iteration(ctx, frame)) {
                end_loop(ctx);
            } else if (render_element_once(ctx, frame->element) != 0) {
                return -1;
            }
            continue;
        } else if (node == NULL || ctx->breaking) {
            /* The element is complete, or a break closes it on its way to
             * the innermost loop. */
            const ir_element_t *element = frame->element;
            size_t scope_depth          = frame->scope_depth;
            sel_capture_t capture       = frame->capture;
            sel_track_t *track          = frame->track;

            ctx->depth--;
            if (element != NULL && write_end_tag(&ctx->out, element) != 0) {
                return -1;
            } else if (track != NULL) {
                sel_judge(ctx, track);
            }
            if (capture.active && sel_finish_capture(ctx, &capture) != 0) {
                return -1;
            }
            scope_pop_to(&ctx->scope, scope_depth);
            if (element != NULL && !ctx->breaking &&
                finish_element(ctx, element) != 0) {
                return -1;
            }
            continue;
        }
        /* render_node may move the frame array, so advance first. */
        frame->next = node->next;
        if (render_node(ctx, node) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Protected body, called with the context, the data, and the helpers:
 * leaves the HTML string on success, or nothing when the pool ran out of
 * memory. Lua errors propagate to the caller's lua_pcall. */
static int render_body(lua_State *L)
{
    render_ctx_t *ctx = (render_ctx_t *)lua_touserdata(L, INTERPRET_CTX_IDX);

    lua_settop(L, INTERPRET_HISTORY_IDX);
    if (lua_isnil(L, INTERPRET_DATA_IDX)) {
        lua_newtable(L);
        lua_replace(L, INTERPRET_DATA_IDX);
    }
    lua_newtable(L);
    scope_init(&ctx->scope, L, ctx->pool, INTERPRET_DATA_IDX,
               INTERPRET_FRAMES_IDX);
    expr_eval_init(&ctx->eval, L, ctx->pool, &ctx->scope,
                   lua_isnil(L, INTERPRET_HELPERS_IDX) ? 0 :
                                                         INTERPRET_HELPERS_IDX);
    if (ctx->selector != NULL) {
        /* A partial render leaves the output of its only match, or false
         * when it has none or more than one. A template with nothing to
         * search is not executed. */
        ctx->out.discard = 1;
        if (sel_context_new(ctx, ctx->ir, &ctx->sel) != 0 ||
            (ctx->sel->marks != NULL && render_tree(ctx) != 0)) {
            return 0;
        } else if (ctx->nmatches != 1) {
            lua_pushboolean(L, 0);
            return 1;
        }
        lua_pushlstring(L, ctx->match_output, ctx->match_len);
        return 1;
    } else if (render_tree(ctx) != 0) {
        return 0;
    }
    lua_pushlstring(L, ctx->out.data == NULL ? "" : ctx->out.data,
                    ctx->out.len);
    return 1;
}

/* Where an error happened, for its meta. */
typedef struct {
    int history_idx;
    const ir_t *ir;
    const ir_element_t *element;
    const reflow_expression_t *expression;
    const char *directive;
    const ir_bind_t *bind;
    const char *reason;
} error_site_t;

static void push_error_meta(lua_State *L, const error_site_t *site)
{
    int depth = (int)value_lua_raw_length(L, site->history_idx);
    int i     = 0;

    /* The template being rendered is the last name of the history. */
    lua_createtable(L, 0, 8);
    lua_rawgeti(L, site->history_idx, depth);
    lua_setfield(L, -2, "template_name");
    lua_createtable(L, depth, 0);
    for (i = 1; i <= depth; i++) {
        lua_rawgeti(L, site->history_idx, i);
        lua_rawseti(L, -2, i);
    }
    lua_setfield(L, -2, "include_stack");
    if (site->element != NULL) {
        const ir_element_t *element       = site->element;
        reflow_source_location_t location = reflow_source_location(
            site->ir->html.data, site->ir->html.len, element->source_start);

        lua_pushinteger(L, (lua_Integer)location.line);
        lua_setfield(L, -2, "line");
        lua_pushinteger(L, (lua_Integer)location.column);
        lua_setfield(L, -2, "column");
        reflow_snippet_push(L, site->ir->html.data, site->ir->html.len,
                            element->source_start, element->source_end, 2);
        lua_setfield(L, -2, "snippet");
        reflow_open_tag_push(L, element);
        lua_setfield(L, -2, "element");
    }
    if (site->expression != NULL) {
        lua_createtable(L, 0, 1);
        lua_pushlstring(L, site->expression->source.data,
                        site->expression->source.len);
        lua_setfield(L, -2, "source");
        lua_setfield(L, -2, "expression");
    }
    if (site->directive != NULL) {
        lua_pushstring(L, site->directive);
        lua_setfield(L, -2, "directive");
    }
    if (site->bind != NULL) {
        lua_pushliteral(L, "x-bind:");
        lua_pushlstring(L, site->bind->name.data, site->bind->name.len);
        lua_concat(L, 2);
        lua_setfield(L, -2, "directive");
        lua_pushlstring(L, site->bind->name.data, site->bind->name.len);
        lua_setfield(L, -2, "attribute");
    }
    if (site->reason != NULL) {
        lua_pushstring(L, site->reason);
        lua_setfield(L, -2, "reason");
        lua_getfield(L, site->history_idx, "requested");
        lua_setfield(L, -2, "requested");
    }
}

/* Return nil and a runtime error, or an include error when the site has a
 * reason. site is NULL when the template name is not known; cause_idx is 0
 * when there is no cause. */
static int return_runtime_error(lua_State *L, const char *message,
                                const error_site_t *site, int cause_idx)
{
    int meta_idx = 0;

    if (site != NULL) {
        push_error_meta(L, site);
        meta_idx = lua_gettop(L);
    }
    if (site != NULL && site->reason != NULL) {
        reflow_error_push_include(L, INTERPRET_OPERATION, message, cause_idx,
                                  meta_idx);
    } else {
        reflow_error_push_runtime(L, INTERPRET_OPERATION, message, cause_idx,
                                  meta_idx);
    }
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

/* Push the message of a normalized cause: its text when it is a string or a
 * number, or a generic message otherwise. */
static const char *push_cause_message(lua_State *L, int cause_idx)
{
    lua_getfield(L, cause_idx, "message");
    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_getfield(L, -1, "message");
        lua_remove(L, -2);
    }
    if (lua_type(L, -1) != LUA_TSTRING && lua_type(L, -1) != LUA_TNUMBER) {
        lua_pop(L, 1);
        lua_pushliteral(L, "failed to render");
    }
    return lua_tostring(L, -1);
}

/* Return the compiled template at idx, or NULL when the value is not one. */
static const ir_t *to_ir(lua_State *L, int idx)
{
    void *ir = lua_touserdata(L, idx);

    if (ir == NULL || !lua_getmetatable(L, idx)) {
        return NULL;
    }
    luaL_getmetatable(L, REFLOW_IR_MT);
    if (!lua_rawequal(L, -1, -2)) {
        ir = NULL;
    }
    lua_pop(L, 2);
    return (const ir_t *)ir;
}

/* Return the compiled selector at idx, or NULL when the value is not one. */
static const selector_t *to_selector(lua_State *L, int idx)
{
    void *sel = lua_touserdata(L, idx);

    if (sel == NULL || !lua_getmetatable(L, idx)) {
        return NULL;
    }
    luaL_getmetatable(L, REFLOW_SELECTOR_MT);
    if (!lua_rawequal(L, -1, -2)) {
        sel = NULL;
    }
    lua_pop(L, 2);
    return (const selector_t *)sel;
}

/* Return nil and a selector error of a partial render: an unsupported
 * positional condition when ancestor is set, or no match or more than one.
 * name_idx is the name of the entry template. */
static int return_selector_error(lua_State *L, const render_ctx_t *ctx,
                                 int name_idx)
{
    const selector_t *sel = ctx->selector;
    int meta_idx          = 0;
    size_t i              = 0;

    lua_createtable(L, 0, 4);
    meta_idx = lua_gettop(L);
    lua_pushlstring(L, sel->source.data, sel->source.len);
    lua_setfield(L, meta_idx, "source");
    if (ctx->ancestor != NULL) {
        const char *name = selector_pseudo_name(ctx->ancestor->kind);

        lua_pushliteral(L, "unsupported");
        lua_setfield(L, meta_idx, "reason");
        lua_pushfstring(L, "pseudo-ancestor:%s", name);
        lua_setfield(L, meta_idx, "feature");
        lua_pushfstring(L,
                        "positional pseudo-class \":%s\" is only supported on "
                        "the rightmost compound selector",
                        name);
    } else {
        lua_pushvalue(L, name_idx);
        lua_setfield(L, meta_idx, "template_name");
        lua_pushstring(L, ctx->nmatches == 0 ? "no_match" : "multiple_matches");
        lua_setfield(L, meta_idx, "reason");
        lua_pushliteral(L, "selector \"");
        lua_pushlstring(L, sel->source.data, sel->source.len);
        if (ctx->nmatches == 0) {
            lua_pushliteral(L, "\" matched no elements in template \"");
            lua_pushvalue(L, name_idx);
            lua_pushliteral(L, "\"");
            lua_concat(L, 5);
        } else {
            lua_createtable(L, 2, 0);
            for (i = 0; i < 2; i++) {
                lua_createtable(L, 0, 1);
                lua_pushlstring(L, ctx->match_irs[i]->name.data,
                                ctx->match_irs[i]->name.len);
                lua_setfield(L, -2, "template_name");
                lua_rawseti(L, -2, (int)i + 1);
            }
            lua_setfield(L, meta_idx, "matches");
            lua_pushfstring(L,
                            "\" matched %d+ elements; the single-fragment "
                            "contract requires exactly one",
                            (int)ctx->nmatches);
            lua_concat(L, 3);
        }
    }
    reflow_error_push_named(L, "new_selector", INTERPRET_OPERATION,
                            lua_tostring(L, -1), 0, meta_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

static int render_lua(lua_State *L)
{
    error_site_t site = {0};
    render_ctx_t ctx  = {0};
    int status        = LUA_OK;
    int base          = 0;

    if (lua_type(L, 1) != LUA_TSTRING || value_lua_raw_length(L, 1) == 0) {
        return return_runtime_error(L, "name must be a non-empty string", NULL,
                                    0);
    }
    /* The history of template names starts with the entry. It is created
     * outside the protected call, so that it still names the template that
     * failed after the call. */
    lua_settop(L, 7);
    lua_createtable(L, 1, 1);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    site.history_idx      = lua_gettop(L);
    ctx.include_depth     = 1;
    ctx.max_include_depth = INTERPRET_INCLUDE_DEPTH;

    ctx.ir = site.ir = to_ir(L, 2);
    if (ctx.ir == NULL) {
        return return_runtime_error(L, "ir must be a compiled template", &site,
                                    0);
    } else if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TTABLE) {
        return return_runtime_error(L, "data must be a table or nil", &site, 0);
    } else if (!lua_isnoneornil(L, 4) && lua_type(L, 4) != LUA_TTABLE) {
        return return_runtime_error(L, "helpers must be a table or nil", &site,
                                    0);
    } else if (!lua_isnil(L, 5) && lua_type(L, 5) != LUA_TTABLE) {
        return return_runtime_error(L, "templates must be a table or nil",
                                    &site, 0);
    } else if (!lua_isnil(L, 6)) {
        lua_Number max = lua_tonumber(L, 6);

        if (lua_type(L, 6) != LUA_TNUMBER || !(max >= 0) || max > floor(max)) {
            return return_runtime_error(
                L, "max_include_depth must be a non-negative integer or nil",
                &site, 0);
        }
        ctx.max_include_depth = max;
    }
    if (!lua_isnil(L, 7) && (ctx.selector = to_selector(L, 7)) == NULL) {
        return return_runtime_error(
            L, "selector must be a compiled selector or nil", &site, 0);
    }
    base = lua_gettop(L);

    lua_pushcfunction(L, render_body);
    ctx.pool = pool_new(L, 0);
    if (ctx.pool == NULL) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, &site, 0);
    }
    /* Keep the pool below the protected call so it stays reachable. */
    lua_insert(L, -2);
    ctx.L        = L;
    ctx.out.pool = ctx.pool;
    lua_pushlightuserdata(L, &ctx);
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    lua_pushvalue(L, 5);
    lua_pushvalue(L, site.history_idx);
    status = lua_pcall(L, 5, 1, 0);
    pool_delete(L, ctx.pool);
    lua_remove(L, base + 1);
    if (status != LUA_OK || lua_isnil(L, -1)) {
        /* Reclaim the render's pool before building the error, so that the
         * error can be created after an out-of-memory failure on Lua 5.1 and
         * LuaJIT, which have no emergency collection. */
        lua_gc(L, LUA_GCCOLLECT, 0);
    }

    if (status == LUA_ERRRUN && ctx.ancestor != NULL) {
        return return_selector_error(L, &ctx, 1);
    } else if (status == LUA_OK && lua_type(L, -1) == LUA_TBOOLEAN) {
        return return_selector_error(L, &ctx, 1);
    } else if (status == LUA_ERRRUN) {
        const char *message = NULL;
        int cause_idx       = 0;

        site.element    = ctx.error_element;
        site.expression = ctx.error_expression;
        site.directive  = ctx.error_directive;
        site.bind       = ctx.error_bind;
        site.reason     = ctx.error_reason;
        site.ir         = ctx.ir;
        if (ctx.eval.raised) {
            /* The renderer raised its own message. */
            message = lua_tostring(L, base + 1);
        } else {
            /* A helper or a metamethod raised a value: keep it as the
             * cause. */
            reflow_error_to_cause(L, base + 1);
            cause_idx = base + 1;
            message   = push_cause_message(L, cause_idx);
        }
        return return_runtime_error(L, message, &site, cause_idx);
    } else if (status != LUA_OK) {
        reflow_error_to_cause(L, base + 1);
        return return_runtime_error(
            L,
            status == LUA_ERRMEM ? INTERPRET_OOM_MESSAGE : "failed to render",
            &site, base + 1);
    } else if (lua_isnil(L, -1)) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, &site, 0);
    }
    return 1;
}

LUALIB_API int luaopen_reflow_interpret(lua_State *L)
{
    struct luaL_Reg funcs[] = {
        {"render", render_lua},
        {NULL,     NULL      },
    };

    reflow_error_require(L, "reflow.error");
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
