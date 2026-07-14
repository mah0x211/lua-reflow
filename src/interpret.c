/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * IR interpreter: walks a compiled IR tree and emits HTML into an
 * output buffer.
 *
 * Each render function returns a render_result:
 *   RR_OK    : continue rendering siblings.
 *   RR_BREAK : x-break / x-break-if fired; caller unwinds to the
 *              innermost x-for / x-each and continues after it.
 *   RR_ERROR : runtime error; propagate up to the outermost caller.
 *
 * The tree-walk stays balanced under RR_BREAK / RR_ERROR: close tags
 * emit unconditionally in emit_element_with_body, and every push_frame
 * has a matching pop_frame regardless of the return code.
 */

// project
#include "interpret.h"
#include "escape.h"
#include "expr/eval.h"
#include "expr/parse.h"
#include "json5.h"
#include "scope.h"
#include "value.h"
// system
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    RR_OK,
    RR_BREAK,
    RR_ERROR,
} render_result;

typedef struct {
    arena_t      *arena;
    buf_t        *out;
    scope_env     env;
    lua_State    *L;
    int           helpers_ref;
    reflow_error *err;
    /* x-include state. */
    const interpret_include_hooks *hooks;
    const char *include_stack[64];
    size_t      include_stack_len[64];
    int         include_depth;
} render_ctx;

/* HTML5 void elements — never emit an end tag. */
static bool tag_is_void(const char *name)
{
    static const char * const VOID_TAGS[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "source", "track", "wbr", NULL,
    };
    for (int i = 0; VOID_TAGS[i] != NULL; i++) {
        if (strcmp(name, VOID_TAGS[i]) == 0) {
            return true;
        }
    }
    return false;
}

static char g_render_err[512];

static void render_errorf(render_ctx *rc, const char *fmt, ...)
{
    if (rc->err == NULL || rc->err->message != NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_render_err, sizeof(g_render_err), fmt, ap);
    va_end(ap);
    rc->err->type    = "ReflowRuntimeError";
    rc->err->message = g_render_err;
}

/* --- forward declarations --- */
static render_result render_node(render_ctx *rc, const ir_node *node);
static render_result render_children(render_ctx *rc,
                                     struct ir_node * const *children,
                                     size_t n);
static render_result render_element(render_ctx *rc, const ir_node *el);
static render_result render_chain(render_ctx *rc, const ir_node *chain);
static render_result render_for(render_ctx *rc, const ir_node *el);
static render_result render_each(render_ctx *rc, const ir_node *el);

/* --- helper: evaluate an expression, converting errors to RR_ERROR --- */
static render_result eval_expr(render_ctx *rc, const expr_node *n,
                               reflow_value *out)
{
    reflow_error eerr = {0};
    reflow_value v = expr_eval(n, &rc->env, rc->L, rc->helpers_ref,
                               rc->arena, &eerr);
    if (eerr.message != NULL) {
        render_errorf(rc, "%s", eerr.message);
        return RR_ERROR;
    }
    *out = v;
    return RR_OK;
}

/* --- output helpers --- */

/* Emit the opening tag, applying x-bind:<attr> overrides.  The result
 * corresponds one-to-one with the JS renderElementOnce: iterate the raw
 * attrs (respecting order), and additionally emit any x-bind targets that
 * did not appear in the raw attrs. Bind values evaluating to null /
 * undefined omit the attribute entirely; boolean true emits the attribute
 * without a value; false likewise omits it. */
static render_result emit_open_tag(render_ctx *rc, const ir_node *el)
{
    const ir_directives *d = &el->element.directives;
    buf_putc(rc->out, '<');
    buf_put(rc->out, el->element.tag_name, strlen(el->element.tag_name));

    for (size_t i = 0; i < el->element.n_attrs; i++) {
        const ir_attr *a = &el->element.attrs[i];

        /* Is this attribute overridden by x-bind? */
        const ir_bind *bind = NULL;
        for (size_t j = 0; j < d->n_binds; j++) {
            if (strcmp(d->binds[j].attr_name, a->name) == 0) {
                bind = &d->binds[j];
                break;
            }
        }

        if (bind != NULL) {
            reflow_value v;
            if (eval_expr(rc, bind->expr, &v) != RR_OK) return RR_ERROR;
            if (rv_is_nullish(&v)) continue; /* omit */
            if (v.tag == RV_BOOL) {
                if (!v.boolean) continue;
                buf_putc(rc->out, ' ');
                buf_put(rc->out, a->name, strlen(a->name));
                continue;
            }
            char scratch[64];
            size_t n = rv_to_js_string(&v, scratch);
            const char *sptr = scratch;
            /* strings longer than scratch: allocate from arena */
            if (v.tag == RV_STRING) {
                sptr = v.string.data;
                n    = v.string.len;
            }
            buf_putc(rc->out, ' ');
            buf_put(rc->out, a->name, strlen(a->name));
            buf_put(rc->out, "=\"", 2);
            if (escape_attr(sptr, n, rc->out) != 0) return RR_ERROR;
            buf_putc(rc->out, '"');
            continue;
        }

        /* Plain attribute. */
        buf_putc(rc->out, ' ');
        buf_put(rc->out, a->name, strlen(a->name));
        if (a->value != NULL) {
            buf_put(rc->out, "=\"", 2);
            if (escape_attr(a->value, strlen(a->value), rc->out) != 0)
                return RR_ERROR;
            buf_putc(rc->out, '"');
        }
    }

    /* Emit x-bind attributes that were NOT overriding an existing attr. */
    for (size_t j = 0; j < d->n_binds; j++) {
        const ir_bind *bind = &d->binds[j];
        bool seen = false;
        for (size_t i = 0; i < el->element.n_attrs; i++) {
            if (strcmp(el->element.attrs[i].name, bind->attr_name) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        reflow_value v;
        if (eval_expr(rc, bind->expr, &v) != RR_OK) return RR_ERROR;
        if (rv_is_nullish(&v)) continue;
        if (v.tag == RV_BOOL) {
            if (!v.boolean) continue;
            buf_putc(rc->out, ' ');
            buf_put(rc->out, bind->attr_name, strlen(bind->attr_name));
            continue;
        }
        buf_putc(rc->out, ' ');
        buf_put(rc->out, bind->attr_name, strlen(bind->attr_name));
        buf_put(rc->out, "=\"", 2);
        char scratch[64];
        size_t n = rv_to_js_string(&v, scratch);
        const char *sptr = scratch;
        if (v.tag == RV_STRING) {
            sptr = v.string.data;
            n    = v.string.len;
        }
        if (escape_attr(sptr, n, rc->out) != 0) return RR_ERROR;
        buf_putc(rc->out, '"');
    }

    buf_putc(rc->out, '>');
    return RR_OK;
}

static void emit_close_tag(render_ctx *rc, const ir_node *el)
{
    buf_put(rc->out, "</", 2);
    buf_put(rc->out, el->element.tag_name, strlen(el->element.tag_name));
    buf_putc(rc->out, '>');
}

/* Emit content-directive result. Returns RR_OK / RR_ERROR. */
static render_result emit_text_content(render_ctx *rc, const expr_node *e)
{
    reflow_value v;
    if (eval_expr(rc, e, &v) != RR_OK) return RR_ERROR;
    if (rv_is_nullish(&v)) return RR_OK;
    if (v.tag == RV_ARRAY || v.tag == RV_OBJECT) {
        render_errorf(rc, "x-text: value must be primitive, got %s",
                      v.tag == RV_ARRAY ? "array" : "object");
        return RR_ERROR;
    }
    char scratch[64];
    size_t n = rv_to_js_string(&v, scratch);
    const char *sptr = scratch;
    if (v.tag == RV_STRING) {
        sptr = v.string.data;
        n    = v.string.len;
    }
    return escape_text(sptr, n, rc->out) == 0 ? RR_OK : RR_ERROR;
}

static render_result emit_html_content(render_ctx *rc, const expr_node *e)
{
    reflow_value v;
    if (eval_expr(rc, e, &v) != RR_OK) return RR_ERROR;
    if (rv_is_nullish(&v)) return RR_OK;
    if (v.tag != RV_STRING) {
        render_errorf(rc, "x-html: value must be a string, got %s",
                      v.tag == RV_ARRAY ? "array" :
                      v.tag == RV_OBJECT ? "object" :
                      v.tag == RV_NUMBER ? "number" :
                      v.tag == RV_BOOL ? "boolean" : "value");
        return RR_ERROR;
    }
    return buf_put(rc->out, v.string.data, v.string.len) == 0
               ? RR_OK : RR_ERROR;
}

/* --- data / with scope setup --- */

/* Push a data frame carrying the value returned by x-data (parsed JSON5).
 * On success returns RR_OK and the caller must pop_frame after rendering. */
static render_result push_data_scope(render_ctx *rc,
                                     const ir_directives *d)
{
    if (d->data_raw == NULL) {
        return RR_OK;
    }
    reflow_error jerr = {0};
    reflow_value *rv = json5_parse_data(d->data_raw, d->data_raw_len,
                                        rc->arena, &jerr);
    if (rv == NULL) {
        render_errorf(rc, "x-data: %s",
                      jerr.message ? jerr.message : "parse error");
        return RR_ERROR;
    }
    scope_push_frame(&rc->env, SCOPE_FRAME_DATA, rv);
    return RR_OK;
}

/* Push a data frame containing the with-bindings as name→value pairs. */
static render_result push_with_scope(render_ctx *rc,
                                     const ir_directives *d)
{
    if (d->with_bindings == NULL || d->n_with == 0) {
        return RR_OK;
    }
    rv_prop *props = (rv_prop *)arena_alloc(rc->arena,
                                            d->n_with * sizeof(rv_prop));
    if (props == NULL) {
        render_errorf(rc, "out of memory");
        return RR_ERROR;
    }
    for (size_t i = 0; i < d->n_with; i++) {
        props[i].key     = d->with_bindings[i].name;
        props[i].key_len = strlen(d->with_bindings[i].name);
        reflow_value v;
        if (eval_expr(rc, d->with_bindings[i].expr, &v) != RR_OK)
            return RR_ERROR;
        props[i].value = v;
    }
    reflow_value *frame = (reflow_value *)arena_alloc(rc->arena,
                                                      sizeof(reflow_value));
    if (frame == NULL) {
        render_errorf(rc, "out of memory");
        return RR_ERROR;
    }
    frame->tag          = RV_OBJECT;
    frame->object.props = props;
    frame->object.len   = d->n_with;
    frame->object.cap   = d->n_with;
    scope_push_frame(&rc->env, SCOPE_FRAME_DATA, frame);
    return RR_OK;
}

/* --- element rendering --- */

/* Evaluate x-with bindings against the CURRENT scope environment and
 * return them as a fresh reflow_value object suitable for use as the
 * initial data frame of an included template. Returns NULL on error. */
static reflow_value *eval_with_bindings(render_ctx *rc,
                                        const ir_directives *d)
{
    if (d->n_with == 0) return NULL;
    rv_prop *props = (rv_prop *)arena_alloc(rc->arena,
                                            d->n_with * sizeof(rv_prop));
    if (props == NULL) return NULL;
    for (size_t i = 0; i < d->n_with; i++) {
        reflow_value v;
        if (eval_expr(rc, d->with_bindings[i].expr, &v) != RR_OK) {
            return NULL;
        }
        props[i].key     = d->with_bindings[i].name;
        props[i].key_len = strlen(d->with_bindings[i].name);
        props[i].value   = v;
    }
    reflow_value *frame = (reflow_value *)arena_alloc(rc->arena,
                                                      sizeof(reflow_value));
    if (frame == NULL) return NULL;
    frame->tag          = RV_OBJECT;
    frame->object.props = props;
    frame->object.len   = d->n_with;
    frame->object.cap   = d->n_with;
    return frame;
}

/*
 * Render one element instance (without applying iteration).
 * Applies x-data / x-with scopes, evaluates content or renders children,
 * balances open/close tags, and returns RR_BREAK / RR_ERROR to the caller.
 */
static render_result render_element_body(render_ctx *rc, const ir_node *el)
{
    const ir_directives *d = &el->element.directives;
    bool has_include = (d->include_expr != NULL);

    /* x-data pushes a data frame that persists during body render. */
    size_t frames_before_data = rc->env.n_frames;
    if (push_data_scope(rc, d) != RR_OK) return RR_ERROR;

    /* x-with adds another data frame — EXCEPT when combined with x-include,
     * where the bindings become the initial data frame of the included
     * template rather than being visible in the outer scope. */
    if (!has_include && push_with_scope(rc, d) != RR_OK) {
        while (rc->env.n_frames > frames_before_data) scope_pop_frame(&rc->env);
        return RR_ERROR;
    }

    render_result rr = RR_OK;

    /* Handle content directives (mutually exclusive with children). */
    if (d->text_expr != NULL) {
        rr = emit_text_content(rc, d->text_expr);
    } else if (d->html_expr != NULL) {
        rr = emit_html_content(rc, d->html_expr);
    } else if (d->include_expr != NULL) {
        if (rc->hooks == NULL || rc->hooks->get_template == NULL) {
            render_errorf(rc,
                "x-include requires a template registry; use Reflow:render");
            rr = RR_ERROR;
        } else {
            reflow_value nv;
            rr = eval_expr(rc, d->include_expr, &nv);
            if (rr == RR_OK) {
                if (nv.tag != RV_STRING) {
                    render_errorf(rc,
                        "x-include: value must be a string, got %s",
                        nv.tag == RV_NULL ? "null" :
                        nv.tag == RV_UNDEFINED ? "undefined" :
                        nv.tag == RV_NUMBER ? "number" :
                        nv.tag == RV_BOOL ? "boolean" :
                        nv.tag == RV_ARRAY ? "array" :
                        nv.tag == RV_OBJECT ? "object" : "value");
                    rr = RR_ERROR;
                } else if (rc->include_depth >= rc->hooks->max_depth) {
                    render_errorf(rc,
                        "x-include: max include depth (%d) exceeded",
                        rc->hooks->max_depth);
                    rr = RR_ERROR;
                } else {
                    /* Cycle detection. */
                    for (int i = 0; i < rc->include_depth; i++) {
                        if (rc->include_stack_len[i] == nv.string.len &&
                            memcmp(rc->include_stack[i], nv.string.data,
                                   nv.string.len) == 0) {
                            render_errorf(rc,
                                "x-include: include cycle detected on "
                                "\"%.*s\"",
                                (int)nv.string.len, nv.string.data);
                            rr = RR_ERROR;
                            break;
                        }
                    }
                    if (rr == RR_OK) {
                        reflow_error lerr = {0};
                        const ir_node *inc_root =
                            rc->hooks->get_template(rc->hooks->ud,
                                                    nv.string.data,
                                                    nv.string.len,
                                                    &lerr);
                        if (inc_root == NULL) {
                            if (lerr.message != NULL) {
                                render_errorf(rc, "%s", lerr.message);
                            } else {
                                render_errorf(rc,
                                    "x-include: template not found: "
                                    "\"%.*s\"",
                                    (int)nv.string.len, nv.string.data);
                            }
                            rr = RR_ERROR;
                        } else {
                            /* If this element also carries x-with,
                             * evaluate the bindings in the outer env
                             * and hand them to the included template
                             * as its initial data frame. */
                            reflow_value *init_frame = NULL;
                            if (d->n_with > 0) {
                                init_frame = eval_with_bindings(rc, d);
                                if (init_frame == NULL) {
                                    rr = RR_ERROR;
                                }
                            }
                            if (rr == RR_OK) {
                                /* Recurse with fresh scope: globals
                                 * carry through, outer frames are
                                 * hidden; x-with (if any) becomes the
                                 * initial data frame. */
                                size_t saved_n = rc->env.n_frames;
                                rc->env.n_frames = 0;
                                if (init_frame != NULL) {
                                    scope_push_frame(&rc->env,
                                        SCOPE_FRAME_DATA, init_frame);
                                }
                                rc->include_stack[rc->include_depth] =
                                    nv.string.data;
                                rc->include_stack_len[rc->include_depth] =
                                    nv.string.len;
                                rc->include_depth++;
                                rr = render_node(rc, inc_root);
                                rc->include_depth--;
                                rc->env.n_frames = saved_n;
                            }
                        }
                    }
                }
            }
        }
    } else if (d->match != NULL) {
        /* x-match: pick the first matching branch. */
        reflow_value mv;
        rr = eval_expr(rc, d->match->expr, &mv);
        if (rr == RR_OK) {
            const ir_branch *pick = NULL;
            for (size_t i = 0; i < d->match->n_branches; i++) {
                const ir_branch *b = &d->match->branches[i];
                if (b->cond == NULL) {
                    if (pick == NULL) pick = b; /* nocase = fallback */
                    continue;
                }
                reflow_value cv;
                rr = eval_expr(rc, b->cond, &cv);
                if (rr != RR_OK) break;
                if (rv_strict_eq(&mv, &cv)) {
                    pick = b;
                    break;
                }
            }
            if (rr == RR_OK && pick != NULL) {
                rr = render_element(rc, pick->node);
            }
        }
    } else if (el->element.n_children > 0) {
        rr = render_children(rc, el->element.children,
                             el->element.n_children);
    }

    while (rc->env.n_frames > frames_before_data) scope_pop_frame(&rc->env);
    return rr;
}

/*
 * Render an element, wrapping its body between the open and close tags.
 * Invisible K-only markers (x-break / x-break-if alone) emit no tag.
 */
static render_result render_element_once(render_ctx *rc, const ir_node *el)
{
    const ir_directives *d = &el->element.directives;

    /* K-only element: no output, just break signaling. */
    if (el->element.invisible_marker) {
        if (d->break_mark) {
            return RR_BREAK;
        }
        if (d->break_if_expr) {
            reflow_value v;
            if (eval_expr(rc, d->break_if_expr, &v) != RR_OK)
                return RR_ERROR;
            return rv_is_truthy(&v) ? RR_BREAK : RR_OK;
        }
        return RR_OK;
    }

    if (emit_open_tag(rc, el) != RR_OK) return RR_ERROR;

    render_result rr = RR_OK;
    if (!tag_is_void(el->element.tag_name)) {
        rr = render_element_body(rc, el);
        emit_close_tag(rc, el);
    }

    /* Non-invisible x-break / x-break-if on an element with visible tag. */
    if (rr == RR_OK) {
        if (d->break_mark) rr = RR_BREAK;
        else if (d->break_if_expr) {
            reflow_value v;
            if (eval_expr(rc, d->break_if_expr, &v) != RR_OK)
                return RR_ERROR;
            if (rv_is_truthy(&v)) rr = RR_BREAK;
        }
    }
    return rr;
}

/* Dispatch: element may have x-for / x-each iteration. */
static render_result render_element(render_ctx *rc, const ir_node *el)
{
    const ir_directives *d = &el->element.directives;
    if (d->for_spec != NULL) {
        return render_for(rc, el);
    }
    if (d->each_spec != NULL) {
        return render_each(rc, el);
    }
    return render_element_once(rc, el);
}

static render_result render_for(render_ctx *rc, const ir_node *el)
{
    const ir_for_spec *f = el->element.directives.for_spec;
    double i = f->start;
    while (true) {
        bool cont = (f->step > 0) ? (i <= f->stop) : (i >= f->stop);
        if (!cont) break;

        rv_prop props[1];
        props[0].key     = f->var_name;
        props[0].key_len = strlen(f->var_name);
        props[0].value   = rv_number(i);
        reflow_value frame;
        frame.tag          = RV_OBJECT;
        frame.object.props = props;
        frame.object.len   = 1;
        frame.object.cap   = 1;

        scope_push_frame(&rc->env, SCOPE_FRAME_LOOP, &frame);
        render_result rr = render_element_once(rc, el);
        scope_pop_frame(&rc->env);
        if (rr == RR_BREAK) { return RR_OK; }
        if (rr != RR_OK)    { return rr; }
        i += f->step;
    }
    return RR_OK;
}

static render_result render_each(render_ctx *rc, const ir_node *el)
{
    const ir_each_spec *e = el->element.directives.each_spec;
    reflow_value coll;
    if (eval_expr(rc, e->collection, &coll) != RR_OK) return RR_ERROR;

    /* Array iteration: 0-based numeric index. */
    if (coll.tag == RV_ARRAY) {
        for (size_t i = 0; i < coll.array.len; i++) {
            size_t n_frame_props = e->index_name ? 2 : 1;
            rv_prop props[2];
            props[0].key     = e->item_name;
            props[0].key_len = strlen(e->item_name);
            props[0].value   = coll.array.items[i];
            if (e->index_name) {
                props[1].key     = e->index_name;
                props[1].key_len = strlen(e->index_name);
                props[1].value   = rv_number((double)i);
            }
            reflow_value frame;
            frame.tag          = RV_OBJECT;
            frame.object.props = props;
            frame.object.len   = n_frame_props;
            frame.object.cap   = n_frame_props;

            scope_push_frame(&rc->env, SCOPE_FRAME_LOOP, &frame);
            render_result rr = render_element_once(rc, el);
            scope_pop_frame(&rc->env);
            if (rr == RR_BREAK) return RR_OK;
            if (rr != RR_OK)    return rr;
        }
        return RR_OK;
    }
    /* Object iteration: index = property key (string). */
    if (coll.tag == RV_OBJECT) {
        for (size_t i = 0; i < coll.object.len; i++) {
            const rv_prop *p = &coll.object.props[i];
            size_t n_frame_props = e->index_name ? 2 : 1;
            rv_prop props[2];
            props[0].key     = e->item_name;
            props[0].key_len = strlen(e->item_name);
            props[0].value   = p->value;
            if (e->index_name) {
                props[1].key     = e->index_name;
                props[1].key_len = strlen(e->index_name);
                props[1].value   = rv_string(p->key, p->key_len);
            }
            reflow_value frame;
            frame.tag          = RV_OBJECT;
            frame.object.props = props;
            frame.object.len   = n_frame_props;
            frame.object.cap   = n_frame_props;

            scope_push_frame(&rc->env, SCOPE_FRAME_LOOP, &frame);
            render_result rr = render_element_once(rc, el);
            scope_pop_frame(&rc->env);
            if (rr == RR_BREAK) return RR_OK;
            if (rr != RR_OK)    return rr;
        }
        return RR_OK;
    }
    render_errorf(rc, "x-each: collection must be an array or object");
    return RR_ERROR;
}

static render_result render_chain(render_ctx *rc, const ir_node *chain)
{
    for (size_t i = 0; i < chain->chain.n_branches; i++) {
        const ir_branch *b = &chain->chain.branches[i];
        bool take = (b->cond == NULL);
        if (!take) {
            reflow_value v;
            if (eval_expr(rc, b->cond, &v) != RR_OK) return RR_ERROR;
            take = rv_is_truthy(&v);
        }
        if (take) {
            return render_element(rc, b->node);
        }
    }
    return RR_OK;
}

static render_result render_children(render_ctx *rc,
                                     struct ir_node * const *children,
                                     size_t n)
{
    for (size_t i = 0; i < n; i++) {
        render_result rr = render_node(rc, children[i]);
        if (rr != RR_OK) return rr;
    }
    return RR_OK;
}

static render_result render_node(render_ctx *rc, const ir_node *node)
{
    switch (node->type) {
    case IR_ROOT:
        return render_children(rc, node->root.children,
                               node->root.n_children);
    case IR_ELEMENT:
        return render_element(rc, node);
    case IR_CHAIN:
        return render_chain(rc, node);
    case IR_TEXT:
        return buf_put(rc->out, node->text.text, node->text.text_len) == 0
                   ? RR_OK : RR_ERROR;
    case IR_COMMENT:
        buf_put(rc->out, "<!--", 4);
        buf_put(rc->out, node->comment.text, node->comment.text_len);
        buf_put(rc->out, "-->", 3);
        return RR_OK;
    }
    return RR_ERROR;
}

int interpret_render(arena_t *arena,
                     const ir_node *root,
                     reflow_value  *globals,
                     lua_State     *L,
                     int            helpers_ref,
                     const interpret_include_hooks *hooks,
                     buf_t         *out,
                     reflow_error  *err)
{
    render_ctx rc = {
        .arena         = arena,
        .out           = out,
        .L             = L,
        .helpers_ref   = helpers_ref,
        .err           = err,
        .hooks         = hooks,
        .include_depth = 0,
    };
    scope_env_init(&rc.env, globals);

    render_result rr = render_node(&rc, root);
    if (rr == RR_BREAK) {
        render_errorf(&rc, "x-break outside of a loop reached top of tree");
        return -1;
    }
    if (rr == RR_ERROR) {
        return -1;
    }
    return 0;
}
