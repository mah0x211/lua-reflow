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
local compiler = require('reflow.compiler')

--- @alias reflow.helper fun(name: string, fn: fun(...: any): any)

--- @class reflow.options
--- @field prefix string
--- @field max_include_depth integer
--- @field helpers table<string, reflow.helper>

--- @class reflow
--- @field _prefix string
--- @field _max_include_depth integer
--- @field _helpers table<string, reflow.helper>
local Reflow = {}
Reflow.__index = Reflow

--- Add a helper function to the reflow instance.
--- @param name string
--- @param fn reflow.helper
--- @return reflow
function Reflow:add_helper(name, fn)
    self._helpers[name] = fn
    return self
end

--- Create a new reflow instance.
--- @param opts reflow.options? { prefix='x-', max_include_depth=50, helpers={...} }
--- @return reflow
local function new(opts)
    opts = opts or {}
    local self = setmetatable({
        _prefix = opts.prefix or 'x-',
        _max_include_depth = opts.max_include_depth or 50,
        _helpers = {},
    }, Reflow)
    for name, fn in pairs(opts.helpers or {}) do
        self:add_helper(name, fn)
    end
    return self
end

--- Get the version of the reflow compiler.
--- @return string
local function version()
    return compiler.version()
end

return {
    version = version,
    new = new,
}
