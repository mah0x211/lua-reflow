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

// project
#include "scope.h"

void scope_env_init(scope_env *env, reflow_value *globals)
{
    env->n_frames = 0;
    env->globals  = globals;
}

void scope_push_frame(scope_env *env, scope_frame_kind kind,
                      reflow_value *vars)
{
    if (env->n_frames < SCOPE_MAX_FRAMES) {
        env->frames[env->n_frames].kind = kind;
        env->frames[env->n_frames].vars = vars;
        env->n_frames++;
    }
}

void scope_pop_frame(scope_env *env)
{
    if (env->n_frames > 0)
        env->n_frames--;
}

reflow_value *scope_resolve_dot(scope_env *env,
                                const char *name, size_t name_len)
{
    for (size_t i = env->n_frames; i > 0; i--) {
        scope_frame *f = &env->frames[i - 1];
        reflow_value *v = rv_object_get(f->vars, name, name_len);
        if (v) return v;
    }
    return NULL;
}

reflow_value *scope_resolve_at(scope_env *env,
                               const char *name, size_t name_len)
{
    for (size_t i = env->n_frames; i > 0; i--) {
        scope_frame *f = &env->frames[i - 1];
        if (f->kind != SCOPE_FRAME_DATA) continue;
        reflow_value *v = rv_object_get(f->vars, name, name_len);
        if (v) return v;
    }
    return NULL;
}

reflow_value *scope_resolve_dollar(scope_env *env)
{
    return env->globals;
}
