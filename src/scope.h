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
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef REFLOW_SCOPE_H
#define REFLOW_SCOPE_H

#include <stddef.h>
#include "value.h"

typedef enum {
    SCOPE_FRAME_DATA,   /* from x-data */
    SCOPE_FRAME_LOOP,   /* from x-for / x-each */
} scope_frame_kind;

typedef struct {
    scope_frame_kind  kind;
    reflow_value      *vars;   /* RV_OBJECT (owned by caller / arena) */
} scope_frame;

#define SCOPE_MAX_FRAMES 64

typedef struct {
    scope_frame   frames[SCOPE_MAX_FRAMES];
    size_t        n_frames;
    reflow_value *globals;     /* the $ value (render data) */
} scope_env;

/* Initialize env with the given globals (the $ value). */
void scope_env_init(scope_env *env, reflow_value *globals);

/* Push / pop a frame.  vars must be an RV_OBJECT. */
void scope_push_frame(scope_env *env, scope_frame_kind kind,
                      reflow_value *vars);
void scope_pop_frame(scope_env *env);

/*
 * Resolve .name — scan ALL frames (data + loop) innermost→outermost.
 * Returns pointer to the value, or NULL (undefined) if not found.
 */
reflow_value *scope_resolve_dot(scope_env *env,
                                const char *name, size_t name_len);

/*
 * Resolve @name — scan only data frames, innermost→outermost.
 * Returns pointer to the value, or NULL (undefined) if not found.
 */
reflow_value *scope_resolve_at(scope_env *env,
                               const char *name, size_t name_len);

/* Resolve $ — always returns globals.  May be NULL if not set. */
reflow_value *scope_resolve_dollar(scope_env *env);

#endif /* REFLOW_SCOPE_H */
