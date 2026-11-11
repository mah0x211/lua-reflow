require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local selector = require('reflow.selector')
local selector_cache = require('reflow.selector_cache')

local function new_cache(max_size)
    local cache, err = selector_cache.new(max_size)
    assert.not_nil(cache, err)
    return cache
end

-- Return the sources from the oldest to the newest entry after checking
-- that the linked list, the entry table and the count agree.
local function order_of(cache)
    local sources = {}
    local prev = false
    local entry = cache.oldest
    while entry do
        assert.rawequal(entry.prev, prev)
        assert.rawequal(cache.entries[entry.source], entry)
        sources[#sources + 1] = entry.source
        prev = entry
        entry = entry.next
    end
    assert.rawequal(cache.newest, prev)
    assert.equal(#sources, cache:size())
    local n = 0
    for _ in pairs(cache.entries) do
        n = n + 1
    end
    assert.equal(n, cache:size())
    return sources
end

local function assert_selector_error(sel, err)
    assert.is_nil(sel)
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
end

-- The following cases port test/selector/cache.test.js of the JavaScript
-- version.

function testcase.default_size_is_128()
    local cache = new_cache()
    assert.equal(cache:max_size(), 128)
    assert.equal(cache:size(), 0)
end

function testcase.parses_on_miss_and_returns_same_selector_on_hit()
    local cache = new_cache(16)
    local a = assert(cache:resolve('#header'))
    local b = assert(cache:resolve('#header'))
    assert.rawequal(a, b)
    assert.is_true(selector.is_compiled(a))
    assert.equal(selector.source(a), '#header')
    assert.equal(cache:size(), 1)
end

function testcase.separate_strings_produce_separate_entries()
    local cache = new_cache(16)
    assert(cache:resolve('#a'))
    assert(cache:resolve('.b'))
    assert(cache:resolve('c'))
    assert.equal(order_of(cache), {
        '#a',
        '.b',
        'c',
    })
end

function testcase.compiled_selector_bypasses_cache()
    local cache = new_cache(16)
    local pre = assert(selector.compile('#header'))
    assert.rawequal(cache:resolve(pre), pre)
    assert.equal(cache:size(), 0)
    -- the source of a compiled selector is not registered either
    assert.is_nil(cache:peek('#header'))
end

function testcase.invalid_selectors_are_not_cached()
    local cache = new_cache(16)
    for _, bad in ipairs({
        '??',
        ':nth-child(2n+1)',
        '::before',
        'a + b',
        '',
    }) do
        assert_selector_error(cache:resolve(bad))
        assert.is_nil(cache:peek(bad))
    end
    assert.equal(cache:size(), 0)
end

function testcase.evicts_oldest_when_size_exceeds_max_size()
    local cache = new_cache(2)
    assert(cache:resolve('#a'))
    assert(cache:resolve('#b'))
    assert.equal(cache:size(), 2)
    assert(cache:resolve('#c'))
    assert.equal(order_of(cache), {
        '#b',
        '#c',
    })
    assert.is_nil(cache:peek('#a'))
    assert.not_nil(cache:peek('#b'))
    assert.not_nil(cache:peek('#c'))
end

function testcase.hit_refreshes_recency()
    local cache = new_cache(2)
    assert(cache:resolve('#a'))
    assert(cache:resolve('#b'))
    assert(cache:resolve('#a'))
    assert.equal(order_of(cache), {
        '#b',
        '#a',
    })
    assert(cache:resolve('#c'))
    assert.not_nil(cache:peek('#a'))
    assert.is_nil(cache:peek('#b'))
    assert.not_nil(cache:peek('#c'))
end

function testcase.size_zero_disables_cache()
    local cache = new_cache(0)
    local a = assert(cache:resolve('#a'))
    local b = assert(cache:resolve('#a'))
    -- each call compiles a new selector
    assert.not_rawequal(a, b)
    assert.equal(cache:size(), 0)
    assert.is_nil(cache:peek('#a'))
    -- a failure is reported in the same way as with an enabled cache
    assert_selector_error(cache:resolve('a + b'))
end

function testcase.new_rejects_negative_or_non_integer_max_size()
    for _, v in ipairs({
        -1,
        1.5,
        '10',
        true,
        {},
        math.huge,
        -math.huge,
        0 / 0,
    }) do
        local cache, err = selector_cache.new(v)
        assert.is_nil(cache)
        assert.rawequal(errorlib.typeof(err), reflow_error.EREFLOW)
        assert.equal(err.op, 'reflow.selector_cache.new')
        assert.match(tostring(err),
                     'selector_cache_size must be a non-negative integer', true)
    end
end

function testcase.new_accepts_integral_numbers()
    assert.equal(new_cache(0):max_size(), 0)
    assert.equal(new_cache(3):max_size(), 3)
    -- an integral float is an integer as in Number.isInteger
    assert.equal(new_cache(2.0):max_size(), 2)
end

function testcase.clear_empties_the_cache()
    local cache = new_cache(4)
    assert(cache:resolve('#a'))
    assert(cache:resolve('#b'))
    assert.equal(cache:size(), 2)
    cache:clear()
    assert.equal(order_of(cache), {})
    assert.is_nil(cache:peek('#a'))
    -- the cache is usable after clearing
    assert(cache:resolve('#c'))
    assert.equal(order_of(cache), {
        '#c',
    })
end

function testcase.peek_does_not_affect_recency()
    local cache = new_cache(2)
    assert(cache:resolve('#a'))
    assert(cache:resolve('#b'))
    assert.not_nil(cache:peek('#a'))
    assert(cache:resolve('#c'))
    assert.is_nil(cache:peek('#a'))
    assert.not_nil(cache:peek('#b'))
end

function testcase.non_string_input_returns_selector_error()
    local cache = new_cache(4)
    for _, v in ipairs({
        42,
        true,
        {},
        function()
        end,
    }) do
        assert_selector_error(cache:resolve(v))
    end
    assert_selector_error(cache:resolve(nil))
    assert.equal(cache:size(), 0)
end

-- Lua specific cases.

function testcase.peek_ignores_unknown_and_non_string_keys()
    local cache = new_cache(4)
    assert(cache:resolve('#a'))
    assert.is_nil(cache:peek('#b'))
    assert.is_nil(cache:peek(42))
    assert.is_nil(cache:peek(nil))
end

function testcase.refreshing_middle_and_newest_entries_keeps_links()
    local cache = new_cache(3)
    assert(cache:resolve('a'))
    assert(cache:resolve('b'))
    assert(cache:resolve('c'))
    -- the newest entry stays in place
    assert(cache:resolve('c'))
    assert.equal(order_of(cache), {
        'a',
        'b',
        'c',
    })
    -- a middle entry moves to the newest end
    assert(cache:resolve('b'))
    assert.equal(order_of(cache), {
        'a',
        'c',
        'b',
    })
    -- the oldest entry moves to the newest end
    assert(cache:resolve('a'))
    assert.equal(order_of(cache), {
        'c',
        'b',
        'a',
    })
end

function testcase.size_one_keeps_only_latest_entry()
    local cache = new_cache(1)
    local a = assert(cache:resolve('a'))
    assert.rawequal(cache:resolve('a'), a)
    assert(cache:resolve('b'))
    assert.equal(order_of(cache), {
        'b',
    })
    assert.not_rawequal(cache:resolve('a'), a)
end

function testcase.failure_does_not_change_order()
    local cache = new_cache(2)
    assert(cache:resolve('a'))
    assert(cache:resolve('b'))
    assert_selector_error(cache:resolve('a + b'))
    assert.equal(order_of(cache), {
        'a',
        'b',
    })
end

function testcase.evicted_and_cleared_selectors_are_collectable()
    local refs = setmetatable({}, {
        __mode = 'v',
    })
    local cache = new_cache(1)
    refs[1] = assert(cache:resolve('a'))
    refs[2] = assert(cache:resolve('b'))
    collectgarbage('collect')
    collectgarbage('collect')
    -- the evicted selector is no longer referenced by the cache
    assert.is_nil(refs[1])
    assert.not_nil(refs[2])

    cache:clear()
    collectgarbage('collect')
    collectgarbage('collect')
    assert.is_nil(refs[2])
end

function testcase.metatable_is_protected()
    local cache = new_cache()
    assert.equal(getmetatable(cache), 'reflow.selector_cache')
    assert.throws(setmetatable, cache, {})
end
