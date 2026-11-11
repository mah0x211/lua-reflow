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
local setmetatable = setmetatable
local type = type
local new_reflow_error = require('reflow.error').new
local selector = require('reflow.selector')
local compile = selector.compile
local is_compiled = selector.is_compiled

local DEFAULT_MAX_SIZE = 128
local NEW_OPERATION = 'reflow.selector_cache.new'

--- bounded least-recently-used cache of compiled selectors.
--- entries form a doubly linked list from the oldest to the newest entry;
--- `false` marks a missing neighbor so that every field is allocated when
--- its table is created and relinking never allocates.
--- @class reflow.selector_cache
--- @field max integer
--- @field count integer
--- @field entries table<string, table>
--- @field oldest table|false
--- @field newest table|false
local SelectorCache = {}
SelectorCache.__index = SelectorCache
SelectorCache.__metatable = 'reflow.selector_cache'

--- @param v any
--- @return boolean
local function is_non_negative_integer(v)
    return type(v) == 'number' and v >= 0 and v ~= huge and floor(v) == v
end

--- append an entry that is not linked yet at the newest end.
--- @param self reflow.selector_cache
--- @param entry table
local function link_newest(self, entry)
    local newest = self.newest
    entry.prev = newest
    if newest then
        newest.next = entry
    else
        self.oldest = entry
    end
    self.newest = entry
end

--- move a linked entry to the newest end.
--- the entry is not the newest one, so it always has a next entry.
--- @param self reflow.selector_cache
--- @param entry table
local function move_to_newest(self, entry)
    local prev, next = entry.prev, entry.next
    if prev then
        prev.next = next
    else
        self.oldest = next
    end
    next.prev = prev
    entry.next = false
    link_newest(self, entry)
end

--- remove the oldest entry.
--- this is called only when the count exceeds a positive limit, so at least
--- two entries are linked and the oldest one always has a next entry.
--- @param self reflow.selector_cache
local function evict_oldest(self)
    local oldest = self.oldest
    local next = oldest.next
    next.prev = false
    self.oldest = next
    self.entries[oldest.source] = nil
    self.count = self.count - 1
end

--- return the compiled selector for input.
--- a compiled selector is returned as is without touching the cache. a
--- string is looked up and refreshed on a hit; on a miss it is compiled and
--- stored only when the compilation succeeds.
--- @param input any
--- @return userdata? sel reflow.selector
--- @return any err reflow.SelectorError
function SelectorCache:resolve(input)
    if is_compiled(input) then
        return input
    elseif type(input) ~= 'string' or self.max == 0 then
        return compile(input)
    end

    local entry = self.entries[input]
    if entry then
        if entry ~= self.newest then
            move_to_newest(self, entry)
        end
        return entry.selector
    end

    local sel, err = compile(input)
    if not sel then
        return nil, err
    end

    entry = {
        source = input,
        selector = sel,
        prev = false,
        next = false,
    }
    -- registering the entry is the only step that may allocate; it happens
    -- before linking so that a memory error leaves the list unchanged.
    self.entries[input] = entry
    link_newest(self, entry)
    self.count = self.count + 1
    if self.count > self.max then
        evict_oldest(self)
    end
    return sel
end

--- look up an entry without changing its recency.
--- @param source any
--- @return userdata? sel reflow.selector
function SelectorCache:peek(source)
    local entry = self.entries[source]
    if entry then
        return entry.selector
    end
end

--- remove all entries.
function SelectorCache:clear()
    self.entries = {}
    self.oldest = false
    self.newest = false
    self.count = 0
end

--- @return integer count number of live entries
function SelectorCache:size()
    return self.count
end

--- @return integer max maximum number of retained entries
function SelectorCache:max_size()
    return self.max
end

--- create a selector cache.
--- @param max_size integer? maximum number of entries (default 128); 0 disables the cache
--- @return reflow.selector_cache? cache
--- @return any err reflow.Error
local function new(max_size)
    if max_size == nil then
        max_size = DEFAULT_MAX_SIZE
    elseif not is_non_negative_integer(max_size) then
        return nil, new_reflow_error(NEW_OPERATION,
                                     'selector_cache_size must be a non-negative integer')
    end
    return setmetatable({
        max = max_size,
        count = 0,
        entries = {},
        oldest = false,
        newest = false,
    }, SelectorCache)
end

return {
    new = new,
}
