require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errno = require('errno')
local newstate = require('newstate')
local arena = require('reflow.arena')

local PAGE = arena.page_size()

local function assert_errno(err, errtype, op)
    assert.equal(type(err), 'table')
    assert.rawequal(err.type, errtype)
    assert.equal(err.op, op)
end

function testcase.new_returns_arena_userdata()
    local a = assert(arena.new())
    assert.equal(type(a), 'userdata')
    assert.equal(a:retained(), 0)
    assert.greater(a:chunk_size(), 0)
    assert.throws(function()
        arena.new(-1)
    end)
end

function testcase.chunk_userdata_is_a_multiple_of_the_page_size()
    for _, size in ipairs({
        1,
        PAGE,
        PAGE * 3 + 1,
    }) do
        local a = assert(arena.new(size))
        assert(a:alloc(1))
        assert.equal(a:retained() % PAGE, 0)
        assert.greater_or_equal(a:retained(), size)
        assert.less(a:retained() - size, PAGE)
    end
end

function testcase.alloc_cuts_blocks_from_one_chunk_until_it_is_full()
    local a = assert(arena.new(PAGE))
    local blocks = {}
    local n = math.floor(a:chunk_size() / 16)
    for i = 1, n do
        blocks[i] = assert(a:alloc(16))
        a:write(blocks[i], 0, string.format('%016d', i))
    end
    assert.equal(a:retained(), PAGE)
    for i = 1, n do
        assert.equal(a:read(blocks[i], 0, 16), string.format('%016d', i))
    end
    -- the next block takes a new chunk
    assert(a:alloc(16))
    assert.equal(a:retained(), PAGE * 2)
end

function testcase.alloc_aligns_blocks()
    local a = assert(arena.new())
    assert(a:alloc(1))
    assert.is_true(a:is_aligned(assert(a:alloc(8, 8)), 8))
    assert(a:alloc(3))
    assert.is_true(a:is_aligned(assert(a:alloc(16)), 8))
    local mem, err = a:alloc(8, 3)
    assert.is_nil(mem)
    assert_errno(err, errno.EINVAL, 'reflow.arena.alloc')
    mem, err = a:alloc(8, 4096)
    assert.is_nil(mem)
    assert_errno(err, errno.EINVAL, 'reflow.arena.alloc')
end

function testcase.large_requests_get_blocks_of_their_own()
    local a = assert(arena.new(PAGE))
    local size = math.floor(a:chunk_size() / 4) + 1
    local mem = assert(a:alloc(size))
    assert.equal(a:retained() % PAGE, 0)
    assert.greater(a:retained(), size)
    local big = assert(a:large_alloc(PAGE * 2))
    local retained = a:retained()
    a:write(big, PAGE * 2 - 4, 'tail')
    assert.equal(a:read(big, PAGE * 2 - 4, 4), 'tail')
    a:large_free(big)
    assert.less(a:retained(), retained)
    assert.is_true(a:is_aligned(mem, 8))
end

function testcase.reset_keeps_chunks_and_drops_large_allocations()
    local a = assert(arena.new(PAGE))
    local first = assert(a:alloc(16))
    assert(a:large_alloc(PAGE * 2))
    assert.greater(a:retained(), PAGE)
    a:reset()
    assert.equal(a:retained(), PAGE)
    -- the kept chunk is cut again from its start
    assert.equal(assert(a:alloc(16)), first)
    assert.equal(a:retained(), PAGE)
end

function testcase.released_chunks_are_reused()
    local a = assert(arena.new(PAGE))
    local chunk = assert(a:chunk_acquire())
    assert.is_true(a:is_aligned(chunk, 8))
    a:write(chunk, a:chunk_size() - 4, 'last')
    a:chunk_release(chunk)
    assert.equal(assert(a:chunk_acquire()), chunk)
    assert.equal(a:retained(), PAGE)
end

function testcase.compact_drops_released_chunks_down_to_the_size()
    local a = assert(arena.new(PAGE))
    local chunks = {}
    for i = 1, 3 do
        chunks[i] = assert(a:chunk_acquire())
    end
    a:chunk_release(chunks[2])
    a:chunk_release(chunks[3])
    -- a chunk in use is never dropped
    assert.equal(a:compact(0), PAGE)
    for i = 2, 3 do
        chunks[i] = assert(a:chunk_acquire())
    end
    for i = 1, 3 do
        a:chunk_release(chunks[i])
    end
    -- stops once the retained size is not above the size
    assert.equal(a:compact(PAGE * 2), PAGE * 2)
    assert.equal(a:compact(PAGE * 2 - 1), PAGE)
    assert.equal(a:compact(0), 0)
end

function testcase.out_of_memory_returns_enomem_and_keeps_the_arena_usable()
    local ok, failed, recovered = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local a = require('reflow.arena').new()
        local mem = assert(a:alloc(16))
        a:write(mem, 0, 'abcdefgh')
        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 8192)
        local big, err = a:large_alloc(1024 * 1024)
        local chunk, cerr = a:chunk_acquire()
        memlimit.maxsize(0)
        local failed = big == nil and err.op == 'reflow.arena.large_alloc' and
                       chunk == nil and cerr.op == 'reflow.arena.chunk_acquire'
        return failed, a:read(mem, 0, 8) == 'abcdefgh' and
                       a:large_alloc(1024 * 1024) ~= nil
    ]])
    assert.is_true(ok, failed)
    assert.is_true(failed)
    assert.is_true(recovered)
end

function testcase.cutting_a_kept_chunk_does_not_allocate_lua_memory()
    -- Preparing a protected call must not allocate: on Lua 5.1 and LuaJIT,
    -- lua_pushcfunction would create a closure every time.  It is measured
    -- in a new state, where the coverage hook does not allocate, and with
    -- the JIT compiler of LuaJIT off, whose traces are collectable objects.
    local ok, grown = newstate.new():dostring([[
        if jit then
            jit.off()
        end
        local a = require('reflow.arena').new()
        assert(a:alloc(16))
        collectgarbage('collect')
        collectgarbage('stop')
        local before = collectgarbage('count')
        for _ = 1, 1000 do
            a:reset()
            a:alloc(16)
        end
        return collectgarbage('count') - before
    ]])
    assert.is_true(ok, grown)
    assert.equal(grown, 0)
end

function testcase.arena_and_its_chunks_are_collected_when_unreachable()
    local ok, held, released = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local arena = require('reflow.arena')
        local refs = setmetatable({}, { __mode = 'v' })
        collectgarbage('collect')
        local before = memlimit.used()
        do
            local a = arena.new()
            assert(a:large_alloc(1024 * 1024))
            assert(a:alloc(16))
            refs[1] = a
        end
        local held = memlimit.used() - before
        collectgarbage('collect')
        collectgarbage('collect')
        return held, refs[1] == nil and memlimit.used() - before < 64 * 1024
    ]])
    assert.is_true(ok, held)
    assert.greater(held, 1024 * 1024)
    assert.is_true(released)
end
