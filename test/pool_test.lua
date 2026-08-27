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
    assert.equal(type(p.detach), 'function')
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

function testcase.free_rejects_double_and_foreign_pointers()
    local first = pool.new(0)
    local second = pool.new(0)
    local mem = first:alloc(32)
    local ok, err = second:free(mem)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.free')
    assert.is_true(first:free(mem))
    ok, err = first:free(mem)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.free')
end

function testcase.reuse_requires_exact_size_and_alignment()
    local p = pool.new(0)
    collectgarbage('stop')
    local exact = p:aligned_alloc(64, 16)
    local exact_address = tostring(exact)
    assert.is_true(p:free(exact))
    local reused = p:aligned_alloc(64, 16)
    assert.equal(tostring(reused), exact_address)
    assert.is_true(p:free(reused))

    local different_size = p:aligned_alloc(48, 16)
    assert.not_equal(tostring(different_size), exact_address)
    local different_alignment = p:aligned_alloc(64, 32)
    assert.not_equal(tostring(different_alignment), exact_address)
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

function testcase.realloc_grow_and_shrink_preserve_content_and_use_default_alignment()
    local p = pool.new(0)
    collectgarbage('stop')

    local default_grow = p:alloc(32)
    local default_grow_address = tostring(default_grow)
    assert.is_true(p:free(default_grow))

    local mem = p:aligned_alloc(16, 64)
    assert.is_true(p:write(mem, 0, 'abcdefghijklmnop'))
    local grown = p:realloc(mem, 32)
    assert.equal(tostring(grown), default_grow_address)
    assert.equal(p:read(grown, 0, 16), 'abcdefghijklmnop')

    local default_shrink = p:alloc(8)
    local default_shrink_address = tostring(default_shrink)
    assert.is_true(p:free(default_shrink))

    local shrunk = p:realloc(grown, 8)
    assert.equal(tostring(shrunk), default_shrink_address)
    assert.equal(p:read(shrunk, 0, 8), 'abcdefgh')
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

function testcase.realloc_rejects_foreign_pointer()
    local first = pool.new(0)
    local second = pool.new(0)
    local mem = first:alloc(8)
    assert.is_true(first:write(mem, 0, 'abcdefgh'))
    local ok, err = second:realloc(mem, 16)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.realloc')
    assert.equal(first:read(mem, 0, 8), 'abcdefgh')
end

function testcase.test_helpers_validate_pointer_range_and_alignment()
    local first = pool.new(0)
    local second = pool.new(0)
    local mem = first:calloc(1, 8)
    local ok, err = second:read(mem, 0, 1)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.read')
    ok, err = first:write(mem, 7, 'xx')
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.write')
    ok, err = first:is_aligned(mem, 3)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.is_aligned')
end

function testcase.detach_transfers_to_plain_lua_ownership()
    local weak = setmetatable({}, {
        __mode = 'v',
    })
    local detached
    do
        local p = pool.new(0)
        local mem = p:alloc(32)
        detached = p:detach(mem)
        weak.mem = detached
        assert.equal(type(detached), 'userdata')
        assert.equal(detached:size(), 32)
        assert.equal(type(detached:data()), 'userdata')
        assert.is_nil(detached.free)
    end
    collect()
    assert.not_nil(weak.mem)
    assert.equal(detached:size(), 32)
    detached = nil -- luacheck: ignore
    collect()
    assert.is_nil(weak.mem)
end

function testcase.detach_rejects_foreign_and_already_detached_pointer()
    local first = pool.new(0)
    local second = pool.new(0)
    local mem = first:alloc(8)
    local ok, err = second:detach(mem)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.detach')
    local detached = first:detach(mem)
    ok, err = first:detach(mem)
    assert.is_nil(ok)
    assert_errno(err, errno.EINVAL, 'reflow.pool.detach')
    assert.equal(detached:size(), 8)
end
