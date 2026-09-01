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

#ifndef REFLOW_UTIL_H
#define REFLOW_UTIL_H

#include <lua.h>

typedef void (*reflow_cleanup_f)(lua_State *L, void *ctx);

/**
 * @brief Invoke a C operation with an immediate, non-throwing cleanup.
 *
 * The operation receives ctx as light userdata in upvalue 1.  The nargs values
 * already on the stack retain their order.  Cleanup runs exactly once after
 * the protected call, whether the operation succeeds or raises, and must not
 * raise or otherwise change the Lua stack.
 *
 * L, operation, and cleanup must be non-NULL.  nargs must describe values
 * already on the stack, and nresults follows the lua_pcall contract.
 *
 * @return A lua_pcall status.  Results, or the original error value, remain on
 *         the stack exactly as they do after lua_pcall.
 */
int reflow_pcall_ex(lua_State *L, lua_CFunction operation, void *ctx, int nargs,
                    int nresults, reflow_cleanup_f cleanup);

#endif /* REFLOW_UTIL_H */
