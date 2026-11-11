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
--- assign to local
local floor = math.floor
local huge = math.huge
local format = string.format
local ipairs = ipairs
local pairs = pairs
local pcall = pcall
local remove = table.remove
local setmetatable = setmetatable
local tostring = tostring
local type = type
local errors = require('reflow.error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local selector = require('reflow.selector')
local selector_cache = require('reflow.selector_cache')

local DEFAULT_PREFIX = 'x-'
local DEFAULT_MAX_INCLUDE_DEPTH = 50

--- @param v any
--- @return boolean
local function is_non_negative_integer(v)
    return type(v) == 'number' and v >= 0 and v ~= huge and floor(v) == v
end

--- @class reflow.engine
--- @field _prefix string
--- @field _helpers table<string, function>
--- @field _helper_names table<string, boolean>
--- @field _loader function?
--- @field _max_include_depth integer
--- @field _templates table<string, userdata> registered compiled templates
--- @field _order string[] registered names in registration order
--- @field _selector_cache reflow.selector_cache
local Engine = {}
Engine.__index = Engine
Engine.__metatable = 'reflow.engine'

--- compile html and register the template under name.
--- @param name string
--- @param html string
--- @return boolean? ok
--- @return any err
function Engine:compile(name, html)
    if type(name) == 'string' and name ~= '' and type(html) == 'string' and
        self._templates[name] then
        return nil, errors.new_compile('reflow.engine.compile',
                                       format(
                                           'template "%s" already exists; call clear("%s") before recompiling',
                                           name, name), nil, {
            template_name = name,
        })
    end

    local ir, err = compile(name, html, self._prefix, self._helper_names)
    if not ir then
        return nil, err
    end
    self._templates[name] = ir
    self._order[#self._order + 1] = name
    return true
end

--- load html with the loader of the engine and register the template under
--- name. an error raised or returned by the loader is returned as is.
--- @param name string
--- @param path any
--- @return boolean? ok
--- @return any err
function Engine:compile_file(name, path)
    if not self._loader then
        return nil, errors.new('reflow.engine.compile_file',
                               'compile_file: loader is required; provide config.loader in reflow.new')
    end

    local ok, html, err = pcall(self._loader, path)
    if not ok then
        return nil, html
    elseif type(html) ~= 'string' then
        if html == nil and err ~= nil then
            return nil, err
        end
        return nil, errors.new('reflow.engine.compile_file',
                               'loader must return an HTML string')
    end
    return self:compile(name, html)
end

--- render the registered template name, or only the single element
--- matched by selector.
--- @param name string
--- @param data table?
--- @param sel any selector string or compiled selector
--- @return string? html
--- @return any err
function Engine:render(name, data, sel)
    local ir = self._templates[name]
    if not ir then
        return nil, errors.new_runtime('reflow.engine.render', format(
                                           'template not found: "%s"',
                                           tostring(name)), nil, {
            template_name = name,
            reason = 'not_found',
            requested = name,
        })
    end

    if sel ~= nil then
        local err
        sel, err = self._selector_cache:resolve(sel)
        if not sel then
            return nil, err
        end
    end
    return interpret.render(name, ir, data, self._helpers, self._templates,
                            self._max_include_depth, sel)
end

--- remove the template name, or every template when name is nil.
--- @param name string?
--- @return string[] removed names
function Engine:clear(name)
    local order = self._order
    if name == nil then
        self._templates = {}
        self._order = {}
        return order
    end

    for i = 1, #order do
        if order[i] == name then
            remove(order, i)
            self._templates[name] = nil
            return {
                name,
            }
        end
    end
    return {}
end

--- @return string[] names registered names in registration order
function Engine:templates()
    local names = {}
    for i, name in ipairs(self._order) do
        names[i] = name
    end
    return names
end

--- @param message string
--- @return nil
--- @return any err
local function config_error(message)
    return nil, errors.new('reflow.new', message)
end

--- create an engine.
--- @param config table?
--- @return reflow.engine? engine
--- @return any err
local function new(config)
    if config == nil then
        config = {}
    elseif type(config) ~= 'table' then
        return config_error('config must be a table or nil')
    end

    local prefix = config.prefix
    if prefix == nil then
        prefix = DEFAULT_PREFIX
    elseif type(prefix) ~= 'string' then
        return config_error('prefix must be a string')
    end

    local helpers = {}
    local helper_names = {}
    if config.helpers ~= nil then
        if type(config.helpers) ~= 'table' then
            return config_error('helpers must be a table')
        end
        for k, fn in pairs(config.helpers) do
            if type(k) ~= 'string' then
                return config_error('helpers keys must be strings')
            elseif type(fn) ~= 'function' then
                return config_error(format('helper "%s" must be a function', k))
            end
            helpers[k] = fn
            helper_names[k] = true
        end
    end

    if config.loader ~= nil and type(config.loader) ~= 'function' then
        return config_error('loader must be a function')
    end

    local max_include_depth = config.max_include_depth
    if max_include_depth == nil then
        max_include_depth = DEFAULT_MAX_INCLUDE_DEPTH
    elseif not is_non_negative_integer(max_include_depth) then
        return config_error('max_include_depth must be a non-negative integer')
    end

    local cache, err = selector_cache.new(config.selector_cache_size)
    if not cache then
        return nil, err
    end

    return setmetatable({
        _prefix = prefix,
        _helpers = helpers,
        _helper_names = helper_names,
        _loader = config.loader,
        _max_include_depth = max_include_depth,
        _templates = {},
        _order = {},
        _selector_cache = cache,
    }, Engine)
end

--- compile a selector without caching it.
--- @param source string
--- @return userdata? sel reflow.selector
--- @return any err
local function compile_selector(source)
    return selector.compile(source)
end

return {
    new = new,
    compile_selector = compile_selector,
    ReflowError = errors.EREFLOW,
    ReflowCompileError = errors.ECOMPILE,
    ReflowRuntimeError = errors.ERUNTIME,
    ReflowIncludeError = errors.EINCLUDE,
    ReflowSelectorError = errors.ESELECTOR,
}
