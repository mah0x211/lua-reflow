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

--- @alias reflow.helper fun(...: any): any

--- @class reflow.options
--- @field prefix string
--- @field max_include_depth integer
--- @field selector_cache_size integer
--- @field helpers table<string, reflow.helper>

--- @class reflow
--- @field _state userdata
--- @field _helpers table<string, reflow.helper>
local Reflow = {}
Reflow.__index = Reflow

--- Register a helper function on this instance.
--- @param name string
--- @param fn reflow.helper
--- @return reflow
function Reflow:add_helper(name, fn)
    self._helpers[name] = fn
    compiler.state_add_helper(self._state, name, fn)
    return self
end

--- Compile an HTML template and cache it under `name`.
--- @param name string
--- @param html string
--- @return reflow
function Reflow:compile(name, html)
    compiler.state_compile(self._state, name, html)
    return self
end

--- Render a compiled template against `data`.
--- When `selector` is provided, only the element matching that CSS
--- selector is rendered; a `ReflowSelectorError` is raised when zero
--- or multiple elements match.
--- @param name string
--- @param data string|table|nil    JSON5 string, table, or nil for empty globals
--- @param selector string?         CSS selector for fragment extraction
--- @return string html
function Reflow:render(name, data, selector)
    return compiler.state_render(self._state, name, data, selector)
end

--- Remove one template by name, or every template when name is nil.
--- Returns the list of names actually removed.
--- @param name string?
--- @return string[]
function Reflow:clear(name)
    return compiler.state_clear(self._state, name)
end

--- List every currently registered template name.
--- @return string[]
function Reflow:templates()
    return compiler.state_templates(self._state)
end

--- Create a new reflow instance.
--- @param opts reflow.options? { prefix='x-', max_include_depth=50, selector_cache_size=128, helpers={...} }
--- @return reflow
local function new(opts)
    opts = opts or {}
    local prefix = opts.prefix or 'x-'
    local max_include_depth = opts.max_include_depth or 50
    local selector_cache_size = opts.selector_cache_size or 128
    local self = setmetatable({
        _state = compiler.state_new(prefix, max_include_depth,
                                    selector_cache_size),
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
