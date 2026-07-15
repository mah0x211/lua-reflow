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
local new_template = require('reflow.template')
local new_selector = require('reflow.selector')

--- @alias reflow.helper fun(...: any): any
--- @alias reflow.loader fun(pathname: string): string|nil, table|string|nil

--- @class reflow.options
--- @field prefix string?
--- @field max_include_depth integer?
--- @field selector_cache_size integer?
--- @field helpers table<string, reflow.helper>?
--- @field loader reflow.loader?

--- @class reflow
--- @field _templates table<string, userdata>
--- @field _selectors table<string, userdata>
--- @field _helpers table<string, reflow.helper>
--- @field _prefix string
--- @field _max_depth integer
--- @field _selector_cache_max integer
--- @field _loader reflow.loader
local Reflow = {}
Reflow.__index = Reflow

--- Default loader: read a file's contents from disk.  Returns `content,
--- nil` on success or `nil, err` when the file cannot be opened / read.
--- @param pathname string
--- @return string|nil content, string|nil err
local function default_loader(pathname)
    local f, err = io.open(pathname, 'rb')
    if not f then
        return nil, err
    end
    local content, rerr = f:read('*a')
    f:close()
    if not content then
        return nil, rerr
    end
    return content
end

--- Register a helper function on this instance.
--- @param name string
--- @param fn reflow.helper
--- @return reflow self
function Reflow:add_helper(name, fn)
    assert(type(name) == 'string', 'name must be a string')
    assert(type(fn) == 'function', 'fn must be a function')
    self._helpers[name] = fn
    return self
end

--- Compile an HTML template and cache it under `name`.
--- @param name string
--- @param html string
--- @return reflow|nil self, table|nil err
function Reflow:compile(name, html)
    if type(name) ~= 'string' or name == '' then
        return nil, {
            type = 'ReflowCompileError',
            message = 'template name must be a non-empty string',
        }
    end
    local helper_names = {}
    for h in pairs(self._helpers) do
        helper_names[#helper_names + 1] = h
    end
    local tmpl, err = new_template(html, {
        prefix = self._prefix,
        helpers = helper_names,
    })
    if not tmpl then
        err.templateName = err.templateName or name
        return nil, err
    end
    self._templates[name] = tmpl
    return self
end

--- Compile an HTML template loaded from disk.
--- @param name string
--- @param pathname string
--- @return reflow|nil self, table|string|nil err
function Reflow:compile_file(name, pathname)
    local html, err = self._loader(pathname)
    if not html then
        return nil, err
    end
    return self:compile(name, html)
end

--- Look up (and cache) a compiled selector for `src`.  Accepts either a
--- selector source string or an already-compiled `reflow.selector`
--- userdata; the latter passes through untouched.
local function resolve_selector(self, src)
    if src == nil then return nil, nil end
    if type(src) == 'userdata' then return src, nil end
    if type(src) ~= 'string' then
        return nil, {
            type = 'ReflowSelectorError',
            message = 'selector must be a string, userdata, or nil',
            reason = 'syntax',
        }
    end
    local cached = self._selectors[src]
    if cached then return cached, nil end
    local sel, err = new_selector(src)
    if not sel then return nil, err end
    self._selectors[src] = sel
    return sel, nil
end

--- Render a compiled template against `data`.
--- @param name string
--- @param data string|nil     JSON5 string, or nil for empty globals
--- @param selector string|userdata|nil  CSS selector source, compiled selector, or nil
--- @return string|nil html, table|nil err
function Reflow:render(name, data, selector)
    local tmpl = self._templates[name]
    if not tmpl then
        return nil, {
            type = 'ReflowRuntimeError',
            message = ('template not registered: %q'):format(name),
            reason = 'not_found',
            templateName = name,
            requested = name,
        }
    end
    local sel, serr = resolve_selector(self, selector)
    if selector ~= nil and not sel then
        serr.templateName = serr.templateName or name
        return nil, serr
    end
    local html, err = tmpl:render(data, self._helpers, self._templates,
                                  self._max_depth, sel)
    if err ~= nil then
        -- The template userdata is intentionally anonymous, so it has
        -- no notion of its registered name.  Inject the coordinator's
        -- name here unless the error already carries one (e.g. an
        -- error raised from inside an included template).
        err.templateName = err.templateName or name
    end
    return html, err
end

--- Remove one template by name, or every template when name is nil.
--- Returns the list of names actually removed.
--- @param name string?
--- @return string[]
function Reflow:clear(name)
    if name ~= nil then
        if self._templates[name] then
            self._templates[name] = nil
            return {name}
        end
        return {}
    end
    local removed = {}
    for n in pairs(self._templates) do
        removed[#removed + 1] = n
    end
    self._templates = {}
    return removed
end

--- List every currently registered template name.
--- @return string[]
function Reflow:templates()
    local names = {}
    for n in pairs(self._templates) do
        names[#names + 1] = n
    end
    return names
end

--- Return the reflow.template userdata registered under `name`, or nil
--- when no such template exists.  Useful for callers that want to hold
--- a reference and call methods directly.
--- @param name string
--- @return userdata|nil
function Reflow:template(name)
    return self._templates[name]
end

--- Create a new reflow instance.
--- @param opts reflow.options? { prefix, max_include_depth,
---                                selector_cache_size, helpers, loader }
--- @return reflow
local function new(opts)
    opts = opts or {}
    local prefix = opts.prefix or 'x-'
    local max_include_depth = opts.max_include_depth or 50
    local selector_cache_size = opts.selector_cache_size or 128
    local loader = opts.loader or default_loader
    assert(type(loader) == 'function', 'loader must be a function')
    local self = setmetatable({
        _templates = {},
        _selectors = {},
        _helpers = {},
        _prefix = prefix,
        _max_depth = max_include_depth,
        _selector_cache_max = selector_cache_size,
        _loader = loader,
    }, Reflow)
    for name, fn in pairs(opts.helpers or {}) do
        self:add_helper(name, fn)
    end
    return self
end

--- One-shot render: compile HTML and immediately render it.  Useful
--- when a template is used exactly once and does not need to persist
--- on a Reflow instance.
--- @param html string
--- @param data string|nil
--- @param config table? { helpers, selector, prefix, max_include_depth }
--- @return string|nil html, table|nil err
local function render(html, data, config)
    config = config or {}
    local r = new({
        prefix = config.prefix,
        max_include_depth = config.max_include_depth,
        helpers = config.helpers,
    })
    local _, err = r:compile('__reflow_oneshot__', html)
    if err then return nil, err end
    return r:render('__reflow_oneshot__', data, config.selector)
end

--- One-shot render from a file loaded via config.loader (or the
--- default file-system loader).
--- @param pathname string
--- @param data string|nil
--- @param config table?
--- @return string|nil html, table|nil err
local function render_file(pathname, data, config)
    config = config or {}
    local loader = config.loader or default_loader
    local html, lerr = loader(pathname)
    if not html then return nil, lerr end
    return render(html, data, config)
end

--- Get the version of the reflow compiler.
--- @return string
local function version()
    return compiler.version()
end

return {
    version = version,
    new = new,
    new_selector = new_selector,
    new_template = new_template,
    render = render,
    render_file = render_file,
    default_loader = default_loader,
}
