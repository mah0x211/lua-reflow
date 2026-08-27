rockspec_format = "3.0"
package = "reflow"
version = "scm-1"
source = {
    url = "git+https://github.com/mah0x211/lua-reflow.git",
}
description = {
    summary = "An attribute-based, no-eval HTML template engine (Lua port of @mah0x211/reflow).",
    detailed = "Renders x-* attributes into HTML on Lua 5.1, byte-identical to the JS version.",
    homepage = "https://github.com/mah0x211/lua-reflow",
    license = "MIT/X11",
    maintainer = "Masatoshi Fukunaga",
}
dependencies = {
    "lua >= 5.1",
    "errno >= 0.6.0",
}
external_dependencies = {}
build_dependencies = {
    "luarocks-build-hooks >= 0.8.0",
}
test_dependencies = {
    "testcase >= 0.16.0",
    "luacov >= 0.15.0",
    "memlimit >= 0.1.1",
    "newstate >= 0.3.1",
}
build = {
    type = "hooks",
    before_build = {
        "$(extra-vars)",
    },
    extra_variables = {
        CFLAGS = "-Wall -Wno-trigraphs -Wmissing-field-initializers -Wreturn-type -Wmissing-braces -Wparentheses -Wno-switch -Wunused-function -Wunused-label -Wunused-parameter -Wunused-variable -Wunused-value -Wuninitialized -Wunknown-pragmas -Wshadow -Wsign-compare",
    },
    modules = {
        reflow = "lua/reflow.lua",
        ["reflow.error"] = "lua/error.lua",
        ["reflow.pool"] = {
            sources = {
                "src/pool.c",
                "src/pool_lua.c",
            },
            incdirs = {
                "src",
            },
        },
    },
}
test = {
    type = "command",
    command = "testcase test/",
}
