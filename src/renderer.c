/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */

#include "renderer.h"
#include "checked.h"
#include "compile_arena.h"
#include "expr/eval.h"
#include "expr/node.h"
#include "selector/resolve.h"

#include <string.h>

#define REFLOW_ERROR_MT "reflow.error"

static int reflow_error_tostring(lua_State *L)
{
    lua_getfield(L, 1, "type");
    lua_getfield(L, 1, "message");
    lua_pushfstring(L, "%s: %s",
                    lua_tostring(L, -2) ? lua_tostring(L, -2) : "ReflowError",
                    lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    return 1;
}

void reflow_register_error_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_ERROR_MT)) {
        lua_pushcfunction(L, reflow_error_tostring);
        lua_setfield(L, -2, "__tostring");
    }
    lua_pop(L, 1);
}

static bool eval_positional_from_candidate(void *ud,
                                           size_t index, size_t total,
                                           size_t of_type_index,
                                           size_t of_type_total)
{
    const sel_candidate *cand = (const sel_candidate *)ud;
    for (size_t i = 0; i < cand->n_positional; i++) {
        if (!sel_eval_positional(&cand->positional[i], index, total,
                                 of_type_index, of_type_total)) {
            return false;
        }
    }
    return true;
}

static void record_fragment_matches(frag_search_pub *ctx,
                                    const char *template_name,
                                    size_t template_name_len,
                                    size_t count)
{
    while (count > 0 && ctx->match_count < 2) {
        ctx->match_template_names[ctx->match_count] = template_name;
        ctx->match_template_name_len[ctx->match_count] = template_name_len;
        ctx->match_count++;
        count--;
    }
}

typedef struct include_reach_ud {
    frag_search_pub *ctx;
    const ir_node   *include_el;
    reflow_value    *globals;
    reflow_error    *err;
} include_reach_ud;

static void fragment_error_context(frag_search_pub *ctx, reflow_error *err)
{
    if (err == NULL) return;
    err->include_stack = ctx->stack;
    err->include_stack_len = ctx->stack_len;
    err->include_stack_count = (size_t)ctx->depth;
}

static void fragment_error_requested(frag_search_pub *ctx,
                                     reflow_error *err,
                                     const reflow_value *value)
{
    reflow_value *owned = (reflow_value *)arena_alloc(ctx->rarena,
                                                       sizeof(*owned));
    if (owned != NULL) {
        *owned = *value;
        err->requested_value = owned;
    }
}

static int fragment_eval(frag_search_pub *ctx, scope_env *env,
                         const expr_node *expr, reflow_value *out,
                         reflow_error *err)
{
    reflow_error cause = {0};
    *out = expr_eval(expr, env, ctx->L, ctx->helpers_idx,
                     ctx->rarena, &cause);
    if (cause.message == NULL) return 0;

    err->type = "ReflowRuntimeError";
    err->message = cause.message;
    err->expression = expr->source;
    reflow_error *owned_cause = (reflow_error *)arena_alloc(
        ctx->rarena, sizeof(*owned_cause));
    if (owned_cause != NULL) {
        *owned_cause = cause;
        err->cause = owned_cause;
    }
    fragment_error_context(ctx, err);
    return -1;
}

static int fragment_include_frame(include_reach_ud *rud, scope_env *env,
                                  reflow_value **out_frame)
{
    const ir_directives *d = &rud->include_el->element.directives;
    *out_frame = NULL;
    if (d->n_with == 0) return 0;

    size_t prop_bytes = 0;
    if (!reflow_size_mul(d->n_with, sizeof(rv_prop), &prop_bytes)) {
        rud->err->type = "ReflowRuntimeError";
        rud->err->message = "too many x-with bindings";
        fragment_error_context(rud->ctx, rud->err);
        return -1;
    }
    rv_prop *props = (rv_prop *)arena_alloc(rud->ctx->rarena, prop_bytes);
    if (props == NULL) {
        rud->err->type = "ReflowRuntimeError";
        rud->err->message = "out of memory";
        fragment_error_context(rud->ctx, rud->err);
        return -1;
    }
    for (size_t i = 0; i < d->n_with; i++) {
        reflow_value value;
        if (fragment_eval(rud->ctx, env, d->with_bindings[i].expr,
                          &value, rud->err) != 0) {
            return -1;
        }
        props[i].key = d->with_bindings[i].name;
        props[i].key_len = strlen(d->with_bindings[i].name);
        props[i].value = value;
    }
    reflow_value *frame = (reflow_value *)arena_alloc(
        rud->ctx->rarena, sizeof(*frame));
    if (frame == NULL) {
        rud->err->type = "ReflowRuntimeError";
        rud->err->message = "out of memory";
        fragment_error_context(rud->ctx, rud->err);
        return -1;
    }
    frame->tag = RV_OBJECT;
    frame->object.props = props;
    frame->object.len = d->n_with;
    frame->object.cap = d->n_with;
    *out_frame = frame;
    return 0;
}

static int ensure_include_stack(frag_search_pub *ctx,
                                const char *template_name,
                                size_t template_name_len,
                                reflow_error *err)
{
    if (ctx->stack != NULL) return 0;
    size_t capacity = ctx->max_depth > 0 ? (size_t)ctx->max_depth : 1;
    size_t names_size = 0;
    size_t lengths_size = 0;
    if (!reflow_size_mul(capacity, sizeof(*ctx->stack), &names_size) ||
        !reflow_size_mul(capacity, sizeof(*ctx->stack_len), &lengths_size)) {
        err->type = "ReflowIncludeError";
        err->message = "max include depth is too large";
        err->reason = "depth_exceeded";
        return -1;
    }
    ctx->stack = (const char **)arena_alloc(ctx->rarena, names_size);
    ctx->stack_len = (size_t *)arena_alloc(ctx->rarena, lengths_size);
    if (ctx->stack == NULL || ctx->stack_len == NULL) {
        err->type = "ReflowIncludeError";
        err->message = "max include depth is too large";
        err->reason = "depth_exceeded";
        return -1;
    }
    ctx->stack_capacity = capacity;
    if (ctx->depth == 0 && template_name != NULL &&
        template_name_len != 0 && capacity > 0) {
        ctx->stack[0] = template_name;
        ctx->stack_len[0] = template_name_len;
        ctx->depth = 1;
    }
    return 0;
}

static int include_reach_cb(void *ud, lua_State *L, scope_env *env,
                            reflow_error *err)
{
    include_reach_ud *rud = (include_reach_ud *)ud;
    frag_search_pub *ctx = rud->ctx;
    const ir_directives *directives =
        &rud->include_el->element.directives;
    const expr_node *include_expr = directives->include_expr;
    (void)L;
    (void)err;

    reflow_value target;
    if (fragment_eval(ctx, env, include_expr, &target, rud->err) != 0) {
        return -1;
    }
    if (target.tag != RV_STRING) {
        rud->err->type = "ReflowIncludeError";
        rud->err->message = "x-include value must be a string";
        rud->err->reason = "invalid";
        fragment_error_requested(ctx, rud->err, &target);
        fragment_error_context(ctx, rud->err);
        return -1;
    }
    const char *target_name = target.string.data;
    size_t target_len = target.string.len;

    if (ctx->depth >= ctx->max_depth ||
        (size_t)ctx->depth >= ctx->stack_capacity) {
        rud->err->type = "ReflowIncludeError";
        rud->err->message = "include depth limit exceeded";
        rud->err->reason = "depth_exceeded";
        fragment_error_requested(ctx, rud->err, &target);
        fragment_error_context(ctx, rud->err);
        return -1;
    }
    for (int i = 0; i < ctx->depth; i++) {
        if (ctx->stack_len[i] == target_len &&
            memcmp(ctx->stack[i], target_name, target_len) == 0) {
            rud->err->type = "ReflowIncludeError";
            rud->err->message = "include cycle detected";
            rud->err->reason = "cycle";
            fragment_error_requested(ctx, rud->err, &target);
            fragment_error_context(ctx, rud->err);
            return -1;
        }
    }

    ir_node *target_root = NULL;
    const sel_index *target_index = NULL;
    const char *target_html = NULL;
    size_t target_html_len = 0;
    if (ctx->fetch(ctx->fetch_ud, ctx->L, target_name, target_len,
                   &target_root, &target_index,
                   &target_html, &target_html_len) != 0) {
        rud->err->type = "ReflowIncludeError";
        rud->err->message = "template not found";
        rud->err->reason = "not_found";
        fragment_error_requested(ctx, rud->err, &target);
        fragment_error_context(ctx, rud->err);
        return -1;
    }

    reflow_value *initial_frame = NULL;
    if (fragment_include_frame(rud, env, &initial_frame) != 0) return -1;

    ctx->stack[ctx->depth] = target_name;
    ctx->stack_len[ctx->depth] = target_len;
    ctx->depth++;
    int rc = reflow_frag_search(ctx, target_name, target_len,
                                target_root, target_index,
                                target_html, target_html_len,
                                rud->globals, initial_frame, rud->err);
    ctx->depth--;
    return rc;
}

static int allocate_candidate_work(frag_search_pub *ctx, size_t count,
                                   const ir_node ***seen_parents,
                                   const ir_node ***group_elements,
                                   void ***group_data,
                                   reflow_error *err)
{
    size_t parent_bytes = 0;
    size_t element_bytes = 0;
    size_t data_bytes = 0;
    if (!reflow_size_mul(count, sizeof(**seen_parents), &parent_bytes) ||
        !reflow_size_mul(count, sizeof(**group_elements), &element_bytes) ||
        !reflow_size_mul(count, sizeof(**group_data), &data_bytes)) {
        err->type = "ReflowSelectorError";
        err->message = "too many selector candidates";
        err->reason = "unsupported";
        return -1;
    }
    *seen_parents = (const ir_node **)arena_alloc(ctx->rarena, parent_bytes);
    *group_elements =
        (const ir_node **)arena_alloc(ctx->rarena, element_bytes);
    *group_data = (void **)arena_alloc(ctx->rarena, data_bytes);
    if (*seen_parents == NULL || *group_elements == NULL ||
        *group_data == NULL) {
        err->type = "ReflowSelectorError";
        err->message = "too many selector candidates";
        err->reason = "unsupported";
        return -1;
    }
    return 0;
}

int reflow_frag_search(frag_search_pub *ctx,
                       const char *template_name, size_t template_name_len,
                       ir_node *root, const sel_index *index,
                       const char *html, size_t html_len,
                       reflow_value *globals,
                       reflow_value *initial_frame,
                       reflow_error *err)
{
    (void)template_name_len;
    if (index == NULL) {
        err->type = "ReflowRuntimeError";
        err->message = "template has no selector index";
        return -1;
    }
    if (ensure_include_stack(ctx, template_name, template_name_len, err) != 0) {
        return -1;
    }

    compile_arena *scratch = compile_arena_new(ctx->L, 1024);
    int scratch_pos = lua_gettop(ctx->L);
    sel_candidates *candidates =
        sel_resolve(scratch, ctx->L, index, ctx->sel, err);
    if (candidates == NULL) {
        lua_remove(ctx->L, scratch_pos);
        return -1;
    }

    if (candidates->count > 0 && candidates->items[0].n_positional > 0) {
        const ir_node **seen_parents = NULL;
        const ir_node **group_elements = NULL;
        void **group_data = NULL;
        if (allocate_candidate_work(ctx, candidates->count, &seen_parents,
                                    &group_elements, &group_data, err) != 0) {
            lua_remove(ctx->L, scratch_pos);
            return -1;
        }
        size_t seen_count = 0;
        for (size_t i = 0; i < candidates->count; i++) {
            const ir_node *parent =
                candidates->items[i].element->element.parent;
            bool already_seen = false;
            for (size_t k = 0; k < seen_count; k++) {
                if (seen_parents[k] == parent) {
                    already_seen = true;
                    break;
                }
            }
            if (already_seen) continue;
            seen_parents[seen_count++] = parent;

            size_t group_count = 0;
            for (size_t j = 0; j < candidates->count; j++) {
                if (candidates->items[j].element->element.parent == parent) {
                    group_elements[group_count] = candidates->items[j].element;
                    group_data[group_count] = &candidates->items[j];
                    group_count++;
                }
            }
            struct ir_node * const *root_children = NULL;
            size_t root_child_count = 0;
            if (parent == NULL) {
                root_children = root->root.children;
                root_child_count = root->root.n_children;
            }
            buf_t output;
            if (buf_init(&output, ctx->rarena) != 0) {
                lua_remove(ctx->L, scratch_pos);
                err->type = "ReflowRuntimeError";
                err->message = "out of memory";
                return -1;
            }
            reflow_error fragment_error = {0};
            interpret_fragment_result result =
                interpret_render_fragment_positional(
                    ctx->rarena, parent, root_children, root_child_count,
                    group_elements, group_data, group_count,
                    eval_positional_from_candidate, globals, initial_frame,
                    ctx->L,
                    ctx->helpers_idx, template_name, html, html_len,
                    ctx->hooks, &output, &fragment_error);
            if (result == INTERPRET_FRAG_ERROR) {
                lua_remove(ctx->L, scratch_pos);
                buf_free(&output);
                *err = fragment_error;
                fragment_error_context(ctx, err);
                return -1;
            }
            if (result == INTERPRET_FRAG_OK) {
                if (ctx->match_count == 0) {
                    buf_put(&ctx->first_match, output.data, output.len);
                }
                record_fragment_matches(ctx, template_name,
                                        template_name_len, 1);
            } else if (result == INTERPRET_FRAG_MULTIPLE_MATCHES) {
                record_fragment_matches(ctx, template_name,
                                        template_name_len, 2);
            }
            buf_free(&output);
            if (ctx->match_count > 1) break;
        }
    } else if (candidates->count > 0) {
        for (size_t i = 0; i < candidates->count; i++) {
            const ir_node *target = candidates->items[i].element;
            buf_t output;
            if (buf_init(&output, ctx->rarena) != 0) {
                lua_remove(ctx->L, scratch_pos);
                err->type = "ReflowRuntimeError";
                err->message = "out of memory";
                return -1;
            }
            reflow_error fragment_error = {0};
            interpret_fragment_result result = interpret_render_fragment_at(
                ctx->rarena, target, globals, initial_frame,
                ctx->L, ctx->helpers_idx,
                template_name, html, html_len, ctx->hooks,
                &output, &fragment_error);
            if (result == INTERPRET_FRAG_ERROR) {
                lua_remove(ctx->L, scratch_pos);
                buf_free(&output);
                *err = fragment_error;
                fragment_error_context(ctx, err);
                return -1;
            }
            if (result == INTERPRET_FRAG_OK) {
                if (ctx->match_count == 0) {
                    buf_put(&ctx->first_match, output.data, output.len);
                }
                record_fragment_matches(ctx, template_name,
                                        template_name_len, 1);
            } else if (result == INTERPRET_FRAG_MULTIPLE_MATCHES) {
                record_fragment_matches(ctx, template_name,
                                        template_name_len, 2);
            }
            buf_free(&output);
            if (ctx->match_count > 1) break;
        }
    } else if (index->n_includes > 0) {
        for (size_t i = 0; i < index->n_includes; i++) {
            const ir_node *include_element = index->includes[i];
            include_reach_ud reach = {
                .ctx = ctx,
                .include_el = include_element,
                .globals = globals,
                .err = err,
            };
            size_t reached = 0;
            int rc = interpret_execute_at(
                ctx->rarena, include_element, globals, initial_frame, ctx->L,
                ctx->helpers_idx, template_name, html, html_len, ctx->hooks,
                include_reach_cb, &reach, &reached, err);
            if (rc != 0) {
                lua_remove(ctx->L, scratch_pos);
                return -1;
            }
            if (ctx->match_count > 1) break;
        }
    }

    lua_remove(ctx->L, scratch_pos);
    return 0;
}
