--
-- Copyright (C) 2026 Masatoshi Fukunaga
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
-- THE SOFTWARE.
--
-- build_lexbor.lua builds the lexbor submodule as a static library via CMake.
-- It runs as a luarocks-build-hooks `before_build` script, so it takes no
-- command-line arguments (same convention as lua-llsocket's codegen.lua).
-- The resulting liblexbor_static.a is linked into reflow.compiler by the
-- rockspec.

local SRC_DIR = 'deps/lexbor'
local BUILD_DIR = 'deps/lexbor/build'

local CONFIGURE_FLAGS = {
    '-DLEXBOR_BUILD_STATIC=ON',
    '-DLEXBOR_BUILD_SHARED=OFF',
    '-DLEXBOR_BUILD_TESTS=OFF',
    '-DLEXBOR_BUILD_TESTS_CPP=OFF',
    '-DLEXBOR_BUILD_EXAMPLES=OFF',
    '-DLEXBOR_BUILD_UTILS=OFF',
    '-DLEXBOR_BUILD_BENCHMARKS=OFF',
    '-DLEXBOR_INSTALL_HEADERS=OFF',
    '-DLEXBOR_WITHOUT_THREADS=ON',
    '-DCMAKE_BUILD_TYPE=Release',
}

local function run(cmd)
    local res = os.execute(cmd)
    -- Lua 5.1: os.execute returns an implementation-dependent value; on POSIX
    -- it is the exit status (0 == success). Lua 5.2+: true/nil, code.
    if type(res) == 'number' then
        return res == 0
    elseif res == true or res == nil then
        return true
    end
    return false
end

local configure = 'cmake -S ' .. SRC_DIR .. ' -B ' .. BUILD_DIR .. ' '
    .. table.concat(CONFIGURE_FLAGS, ' ')
if not run(configure) then
    error('reflow: lexbor cmake configure failed:\n  ' .. configure)
end

if not run('cmake --build ' .. BUILD_DIR .. ' --config Release --parallel') then
    error('reflow: lexbor cmake build failed')
end

print('reflow: lexbor static library built at ' .. BUILD_DIR)
