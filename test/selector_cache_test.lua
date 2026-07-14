require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- ============================================================
-- construction
-- ============================================================

function testcase.default_max_size()
    local c = compiler.selector_cache_new()
    assert.equal(c.max_size(), 128)
    assert.equal(c.size(), 0)
end

function testcase.custom_max_size()
    local c = compiler.selector_cache_new(4)
    assert.equal(c.max_size(), 4)
end

function testcase.rejects_negative_max_size()
    local ok = pcall(compiler.selector_cache_new, -1)
    assert.is_false(ok)
end

-- ============================================================
-- resolve + peek
-- ============================================================

function testcase.resolve_parses_and_caches()
    local c = compiler.selector_cache_new(4)
    -- The stub returns the selector source when a compiled AST is
    -- produced; it is our identity token in tests.
    assert.equal(c.resolve('div'), 'div')
    assert.equal(c.size(), 1)
    assert.equal(c.peek('div'), 'div')
end

function testcase.peek_does_not_promote_recency()
    local c = compiler.selector_cache_new(2)
    c.resolve('a')
    c.resolve('b')
    -- Peek at `a`; should not promote it.
    c.peek('a')
    -- Insert `c`; oldest surviving entry is still `a`, so it evicts.
    c.resolve('c')
    assert.equal(c.peek('a'), nil)
    assert.equal(c.peek('b'), 'b')
    assert.equal(c.peek('c'), 'c')
end

function testcase.resolve_hit_promotes()
    local c = compiler.selector_cache_new(2)
    c.resolve('a')
    c.resolve('b')
    -- Re-resolve `a` — moves it to head.
    c.resolve('a')
    -- Insert `c`: evicts `b` (now oldest), leaves `a` and `c`.
    c.resolve('c')
    assert.equal(c.peek('a'), 'a')
    assert.equal(c.peek('b'), nil)
    assert.equal(c.peek('c'), 'c')
end

function testcase.parse_failure_never_caches()
    local c = compiler.selector_cache_new(4)
    local ok, err = c.resolve(':not(.x)')
    assert.equal(ok, nil)
    assert.equal(err.reason, 'unsupported')
    assert.equal(c.size(), 0)
    assert.equal(c.peek(':not(.x)'), nil)
end

-- ============================================================
-- eviction
-- ============================================================

function testcase.evicts_oldest_when_over_capacity()
    local c = compiler.selector_cache_new(3)
    c.resolve('a'); c.resolve('b'); c.resolve('c')
    assert.equal(c.size(), 3)
    c.resolve('d')  -- should evict `a`
    assert.equal(c.size(), 3)
    assert.equal(c.peek('a'), nil)
    for _, k in ipairs({'b', 'c', 'd'}) do
        assert.equal(c.peek(k), k)
    end
end

function testcase.zero_capacity_disables_cache()
    local c = compiler.selector_cache_new(0)
    -- resolve still works — it parses every time.
    assert.equal(c.resolve('div'), 'div')
    assert.equal(c.size(), 0)
    assert.equal(c.peek('div'), nil)
end

-- ============================================================
-- clear
-- ============================================================

function testcase.clear_empties_cache()
    local c = compiler.selector_cache_new(4)
    c.resolve('a'); c.resolve('b'); c.resolve('c')
    c.clear()
    assert.equal(c.size(), 0)
    assert.equal(c.peek('a'), nil)
end

function testcase.clear_preserves_max_size()
    local c = compiler.selector_cache_new(7)
    c.resolve('a')
    c.clear()
    assert.equal(c.max_size(), 7)
end
