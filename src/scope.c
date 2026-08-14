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
#include "checked.h"

#include <string.h>

void scope_env_init(scope_env *env, arena_t *arena, reflow_value *globals)
{
    env->frames = NULL;
    env->n_frames = 0;
    env->frame_capacity = 0;
    env->arena = arena;
    env->globals  = globals;
}

void scope_push_frame(scope_env *env, scope_frame_kind kind,
                      reflow_value *vars)
{
    if (env->n_frames >= env->frame_capacity) {
        size_t required = 0;
        size_t capacity = 0;
        size_t bytes = 0;
        size_t copy_bytes = 0;
        if (!reflow_size_add(env->n_frames, 1, &required) ||
            !reflow_size_grow(env->frame_capacity, required, &capacity) ||
            !reflow_size_mul(capacity, sizeof(scope_frame), &bytes) ||
            !reflow_size_mul(env->n_frames, sizeof(scope_frame),
                             &copy_bytes)) {
            lua_pushliteral(env->arena->L, "scope nesting is too deep");
            lua_error(env->arena->L);
        }
        scope_frame *frames =
            (scope_frame *)arena_alloc(env->arena, bytes);
        if (frames == NULL) {
            lua_pushliteral(env->arena->L, "scope nesting is too deep");
            lua_error(env->arena->L);
        }
        if (copy_bytes != 0) memcpy(frames, env->frames, copy_bytes);
        env->frames = frames;
        env->frame_capacity = capacity;
    }
    env->frames[env->n_frames].kind = kind;
    env->frames[env->n_frames].vars = vars;
    env->n_frames++;
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
