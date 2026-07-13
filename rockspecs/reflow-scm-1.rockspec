rockspec_format = "3.0"
package = "reflow"
version = "scm-1"
source = {
    url = "git+https://github.com/mah0x211/lua-reflow.git",
}
description = {
    summary = "An attribute-based, no-eval HTML template engine (Lua port of @mah0x211/reflow).",
    detailed = "Renders x-* attributes into HTML on Lua 5.1, byte-identical to the JS version. C core (parser/compiler/interpreter) with a thin Lua wrapper.",
    homepage = "https://github.com/mah0x211/lua-reflow",
    license = "MIT/X11",
    maintainer = "Masatoshi Fukunaga",
}
dependencies = {
    "lua >= 5.1",
    "lauxhlib >= 0.6.0",
    "errno >= 0.5.0",
    "error >= 0.14.0",
}
-- external_dependencies must be defined (even if empty) to prevent luarocks
-- from autodetecting dependencies and failing when they are not found.
external_dependencies = {}
build_dependencies = {
    "luarocks-build-hooks >= 0.8.0",
}
build = {
    type = "hooks",
    before_build = {
        "$(extra-vars)",
        "build_lexbor.lua",
    },
    extra_variables = {
        CFLAGS = "-Wall -Wno-trigraphs -Wmissing-field-initializers -Wreturn-type -Wmissing-braces -Wparentheses -Wno-switch -Wunused-function -Wunused-label -Wunused-parameter -Wunused-variable -Wunused-value -Wuninitialized -Wunknown-pragmas -Wshadow -Wsign-compare",
    },
    conditional_variables = {
        REFLOW_COVERAGE = {
            CFLAGS = "--coverage",
            LIBFLAG = "--coverage",
        },
    },
    modules = {
        reflow = "lua/reflow.lua",
        ["reflow.compiler"] = {
            sources = {
                "src/compiler.c",
                "src/value.c",
                "src/escape.c",
                "src/snippet.c",
                "src/error.c",
                "src/arena.c",
                "src/json5.c",
                "src/compile_arena.c",
                "src/scope.c",
                "src/ir.c",
                "src/expr/parse.c",
                "src/expr/eval.c",
                "deps/yyjson/src/yyjson.c",
            },
            incdirs = {
                "deps/yyjson/src",
                "$(DEP_LAUXHLIB_INCDIR)",
                "$(DEP_ERRNO_INCDIR)",
                "$(DEP_ERROR_INCDIR)",
            },
            -- build_lexbor.lua produces liblexbor_static.a before the build.
        },
    },
}
