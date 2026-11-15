require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errno = require('errno')
local pool = require('reflow.pool')

local function collect()
    collectgarbage('collect')
    collectgarbage('collect')
end

function testcase.after_each()
    collectgarbage('restart')
end

local function assert_errno(err, errtype, op)
    assert.equal(type(err), 'table')
    assert.rawequal(err.type, errtype)
    assert.equal(err.op, op)
end

function testcase.new_returns_pool_userdata()
    local values = {
        pool.new(0),
    }
    local p = values[1]
    assert.equal(#values, 1)
    assert.equal(type(p), 'userdata')
    assert.equal(type(p.alloc), 'function')
    assert.equal(type(p.aligned_alloc), 'function')
    assert.equal(type(p.calloc), 'function')
    assert.equal(type(p.realloc), 'function')
    assert.equal(type(p.free), 'function')
    assert.equal(type(p.compact), 'function')
    assert.equal(type(p.retained), 'function')
    assert.is_nil(p.used)
    assert.is_nil(p.state)
end

function testcase.new_rejects_negative_capacity()
    local err = assert.throws(function()
        pool.new(-1)
    end)
    assert.match(err, 'capacity must be non-negative')
end

function testcase.pool_keeps_origin_coroutine_only_while_reachable()
    local weak = setmetatable({}, {
        __mode = 'v',
    })
    local p
    local co = coroutine.create(function()
        p = pool.new(0)
        weak.pool = p
        coroutine.yield()
    end)
    weak.thread = co
    assert.is_true(coroutine.resume(co))
    co = nil -- luacheck: ignore
    collect()
    assert.not_nil(weak.thread)
    assert.not_nil(weak.pool)

    p = nil -- luacheck: ignore
    collect()
    assert.is_nil(weak.thread)
    assert.is_nil(weak.pool)
end

function testcase.alloc_zero_size_returns_distinct_freeable_pointers()
    local p = pool.new(0)
    local first = p:alloc(0)
    local second = p:alloc(0)
    assert.equal(type(first), 'userdata')
    assert.equal(type(second), 'userdata')
    assert.not_equal(tostring(first), tostring(second))
    assert.is_true(p:free(first))
    assert.is_true(p:free(second))
end

function testcase.default_and_explicit_alignment()
    local p = pool.new(0)
    local normal = p:alloc(64)
    assert.is_true(p:is_aligned(normal, 8))
    for _, alignment in ipairs({
        8,
        16,
        32,
        64,
    }) do
        local mem = p:aligned_alloc(64, alignment)
        assert.is_true(p:is_aligned(mem, alignment))
        assert.is_true(p:free(mem))
    end
end

function testcase.aligned_alloc_returns_errno_object()
    local p = pool.new(0)
    local ok, err = p:aligned_alloc(64, 3)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.aligned_alloc')
end

function testcase.capacity_limits_only_used_bytes()
    local p = pool.new(64)
    local mem = p:alloc(64)
    local ok, err = p:alloc(1)
    assert.is_nil(ok)
    assert_errno(err, errno.ENOMEM, 'reflow.pool.alloc')
    assert.is_true(p:free(mem))
    assert.not_nil(p:alloc(64))
end

function testcase.free_nil_is_successful_noop()
    local values = {
        pool.new(0):free(nil),
    }
    assert.equal(#values, 1)
    assert.is_true(values[1])
end

function testcase.freed_blocks_are_reused_by_size_class()
    local p = pool.new(0)
    local block = p:alloc(64)
    assert.is_true(p:free(block))
    -- 33 to 64 bytes share a size class
    local reused = p:alloc(40)
    assert.equal(tostring(reused), tostring(block))
    assert.is_true(p:free(reused))
    assert.not_equal(tostring(p:alloc(65)), tostring(block))
end

function testcase.reuse_chain_keeps_multiple_blocks()
    local p = pool.new(0)
    collectgarbage('stop')
    local first = p:alloc(32)
    local second = p:alloc(32)
    local addresses = {
        [tostring(first)] = true,
        [tostring(second)] = true,
    }
    assert.is_true(p:free(first))
    assert.is_true(p:free(second))
    local a = p:alloc(32)
    local b = p:alloc(32)
    assert.is_true(addresses[tostring(a)])
    assert.is_true(addresses[tostring(b)])
    assert.not_equal(tostring(a), tostring(b))
end

function testcase.calloc_zero_initializes_all_bytes()
    local p = pool.new(0)
    local mem = p:calloc(4, 16)
    assert.equal(p:read(mem, 0, 64), string.rep('\0', 64))
end

function testcase.calloc_rejects_overflow_and_capacity_excess()
    local p = pool.new(32)
    local huge = math.floor(2 ^ 30)
    local ok, err = p:calloc(huge, huge)
    assert.is_nil(ok)
    assert_errno(err, errno.ENOMEM, 'reflow.pool.calloc')
    ok, err = p:calloc(33, 1)
    assert.is_nil(ok)
    assert_errno(err, errno.ENOMEM, 'reflow.pool.calloc')
end

function testcase.realloc_nil_allocates_and_zero_frees()
    local p = pool.new(0)
    local mem = p:realloc(nil, 16)
    assert.equal(type(mem), 'userdata')
    local null = p:realloc(mem, 0)
    assert.equal(type(null), 'userdata')
    assert.is_true(p:free(null))
end

function testcase.realloc_same_size_keeps_pointer_and_content()
    local p = pool.new(0)
    local mem = p:alloc(16)
    assert.is_true(p:write(mem, 0, 'abcdefghijklmnop'))
    local resized = p:realloc(mem, 16)
    assert.equal(tostring(resized), tostring(mem))
    assert.equal(p:read(resized, 0, 16), 'abcdefghijklmnop')
end

function testcase.realloc_keeps_the_block_while_it_holds_the_size()
    local p = pool.new(0)
    local mem = p:alloc(20)
    assert.is_true(p:write(mem, 0, 'abcdefghijklmnopqrst'))
    -- 20 bytes take the 32-byte class
    local grown = p:realloc(mem, 32)
    assert.equal(tostring(grown), tostring(mem))
    local shrunk = p:realloc(grown, 8)
    assert.equal(tostring(shrunk), tostring(mem))
    assert.equal(p:read(shrunk, 0, 8), 'abcdefgh')
end

function testcase.realloc_moves_a_block_that_cannot_hold_the_size()
    local p = pool.new(0)
    local mem = p:alloc(16)
    assert.is_true(p:write(mem, 0, 'abcdefghijklmnop'))
    local moved = p:realloc(mem, 4096)
    assert.not_equal(tostring(moved), tostring(mem))
    assert.equal(p:read(moved, 0, 16), 'abcdefghijklmnop')
    assert.is_true(p:is_aligned(moved, 8))
    local large = p:realloc(moved, 64 * 1024)
    assert.equal(p:read(large, 0, 16), 'abcdefghijklmnop')
    local back = p:realloc(large, 16)
    assert.equal(p:read(back, 0, 16), 'abcdefghijklmnop')
end

function testcase.reset_releases_every_block_and_keeps_the_chunks()
    local p = pool.new(4096)
    local first = p:alloc(64)
    for _ = 1, 31 do
        assert.not_nil(p:alloc(64))
    end
    local retained = p:retained()
    p:reset()
    -- the capacity counts no block
    assert.not_nil(p:alloc(4096))
    p:reset()
    -- the chunks are reused from the start, and released by compact
    assert.equal(tostring(p:alloc(64)), tostring(first))
    assert.less_or_equal(p:retained(), retained)
    p:reset()
    assert.equal(p:compact(0), 0)
end

function testcase.reset_releases_large_blocks()
    local p = pool.new(0)
    p:alloc(64)
    local retained = p:retained()
    assert.not_nil(p:alloc(64 * 1024))
    assert.greater(p:retained(), retained)
    p:reset()
    assert.equal(p:retained(), retained)
end

function testcase.empty_chunks_go_back_to_the_arena()
    local p = pool.new(0)
    local blocks = {}
    for i = 1, 4096 do
        blocks[i] = p:alloc(64)
    end
    local retained = p:retained()
    for i = 1, #blocks do
        assert.is_true(p:free(blocks[i]))
    end
    -- the chunks no block uses can be dropped, except the current one
    assert.less(p:compact(0), retained)
    assert.less_or_equal(p:compact(0), retained / 4)
    assert.not_nil(p:alloc(64))
end

function testcase.realloc_failure_preserves_old_block()
    local p = pool.new(16)
    local mem = p:alloc(16)
    assert.is_true(p:write(mem, 0, 'abcdefghijklmnop'))
    local ok, err = p:realloc(mem, 17)
    assert.is_nil(ok)
    assert_errno(err, errno.ENOMEM, 'reflow.pool.realloc')
    assert.equal(p:read(mem, 0, 16), 'abcdefghijklmnop')
    assert.is_true(p:free(mem))
end

function testcase.test_helpers_validate_pointer_range_and_alignment()
    local p = pool.new(0)
    local mem = p:calloc(1, 8)
    local ok, err = p:write(mem, 7, 'xx')
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.write')
    ok, err = p:read(mem, 0, 9)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.read')
    ok, err = p:is_aligned(mem, 3)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.is_aligned')
end
