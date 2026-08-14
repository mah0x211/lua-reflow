/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_LUA_OWNER_H
#define REFLOW_LUA_OWNER_H

#include <lua.h>

static inline int reflow_lua_absindex(lua_State *L, int index)
{
    return index > 0 || index <= LUA_REGISTRYINDEX
        ? index : lua_gettop(L) + index + 1;
}

/* Attach child to a private table owned by a full userdata. The operation is
 * failure-atomic because owner and child remain rooted on the Lua stack until
 * the reachability edge has been installed. */
static inline void reflow_lua_own(lua_State *L, int owner_index,
                                  int child_index)
{
    owner_index = reflow_lua_absindex(L, owner_index);
    child_index = reflow_lua_absindex(L, child_index);
    lua_newtable(L);
    lua_pushvalue(L, child_index);
    lua_rawseti(L, -2, 1);
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
    lua_setuservalue(L, owner_index);
#else
    lua_setfenv(L, owner_index);
#endif
}

#endif /* REFLOW_LUA_OWNER_H */
