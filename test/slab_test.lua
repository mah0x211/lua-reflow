require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errno = require('errno')
local newstate = require('newstate')
local slab = require('reflow.slab')

local function assert_errno(err, errtype, op)
    assert.equal(type(err), 'table')
    assert.rawequal(err.type, errtype)
    assert.equal(err.op, op)
end

-- Allocate objects until the slab takes another chunk; return the objects
-- of the first chunk.
local function fill_chunk(s)
    local objects = {
        assert(s:alloc()),
    }
    local retained = s:retained()
    while true do
        local object = assert(s:alloc())
        if s:retained() ~= retained then
            return objects
        end
        objects[#objects + 1] = object
    end
end

function testcase.new_rounds_the_object_size_and_aligns_objects()
    local s = assert(slab.new(1))
    assert.greater_or_equal(s:object_size(), 8)
    assert.equal(s:object_size() % 8, 0)
    for _ = 1, 4 do
        assert.is_true(s:is_aligned(assert(s:alloc()), 8))
    end
    assert.throws(function()
        slab.new(-1)
    end)
end

function testcase.new_rejects_objects_larger_than_a_chunk()
    local s, err = slab.new(1024 * 1024)
    assert.is_nil(s)
    assert_errno(err, errno.EINVAL, 'reflow.slab.new')
end

function testcase.objects_are_distinct_and_keep_their_contents()
    local s = assert(slab.new(16))
    local objects = {}
    for i = 1, 2000 do
        objects[i] = assert(s:alloc())
        s:write(objects[i], 0, string.format('%016d', i))
    end
    for i = 1, #objects do
        assert.equal(s:read(objects[i], 0, 16), string.format('%016d', i))
    end
end

function testcase.freed_objects_are_reused_first()
    local s = assert(slab.new(32))
    local first = assert(s:alloc())
    local second = assert(s:alloc())
    s:free(first)
    s:free(second)
    assert.equal(s:alloc(), second)
    assert.equal(s:alloc(), first)
end

function testcase.a_full_chunk_takes_another()
    local s = assert(slab.new(64))
    assert.equal(s:retained(), 0)
    local objects = fill_chunk(s)
    assert.greater(#objects, 1)
    -- objects of the first chunk did not move
    for i = 1, #objects do
        s:write(objects[i], 0, 'x')
    end
    assert.equal(s:read(objects[1], 0, 1), 'x')
end

function testcase.reset_gives_the_chunks_back_for_reuse()
    local s = assert(slab.new(64))
    local first = fill_chunk(s)[1]
    local retained = s:retained()
    s:reset()
    assert.equal(s:retained(), retained)
    assert.equal(assert(s:alloc()), first)
    assert.equal(s:retained(), retained)
end

function testcase.out_of_memory_returns_enomem_and_keeps_the_slab_usable()
    local ok, failed, kept, recovered = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local s = require('reflow.slab').new(64)
        local first = assert(s:alloc())
        s:write(first, 0, 'abcdefgh')
        collectgarbage('collect')
        local used = memlimit.used()
        local failed = false
        -- a new chunk needs far more than the limit leaves
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 8192)
        for _ = 1, 100000 do
            local object, err = s:alloc()
            if not object then
                failed = err.op == 'reflow.slab.alloc'
                break
            end
        end
        memlimit.maxsize(0)
        return failed, s:read(first, 0, 8) == 'abcdefgh', s:alloc() ~= nil
    ]])
    assert.is_true(ok, failed)
    assert.is_true(failed)
    assert.is_true(kept)
    assert.is_true(recovered)
end

function testcase.reusing_objects_does_not_allocate_lua_memory()
    -- Measured in a new state, where the coverage hook does not allocate,
    -- and with the JIT compiler of LuaJIT off, whose traces are collectable
    -- objects.
    local ok, grown = newstate.new():dostring([[
        if jit then
            jit.off()
        end
        local s = require('reflow.slab').new(64)
        s:free(assert(s:alloc()))
        collectgarbage('collect')
        collectgarbage('stop')
        local before = collectgarbage('count')
        for _ = 1, 1000 do
            s:free(s:alloc())
        end
        return collectgarbage('count') - before
    ]])
    assert.is_true(ok, grown)
    assert.equal(grown, 0)
end
