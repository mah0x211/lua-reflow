require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local newstate = require('newstate')

local function run(source)
    local state = assert(newstate.new())
    return state:dostring(source)
end

function testcase.unused_blocks_are_held_only_weakly()
    local ok, before, allocated, after = run([[
        local memlimit = require('memlimit')
        local p = require('reflow.pool').new(0)
        collectgarbage('collect')
        local before = memlimit.used()
        local mem = p:alloc(1024 * 1024)
        local allocated = memlimit.used()
        p:free(mem)
        mem = nil
        collectgarbage('collect')
        collectgarbage('collect')
        return before, allocated, memlimit.used()
    ]])
    assert.is_true(ok)
    assert.greater(allocated - after, 1024 * 1024)
    assert.less(after - before, 4096)
end

function testcase.alloc_oom_returns_enomem_without_changing_old_block()
    local ok, failed, code, op, content = run([[
        local memlimit = require('memlimit')
        local p = require('reflow.pool').new(0)
        local mem = p:alloc(64)
        p:write(mem, 0, 'abcdefgh')
        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 8192)
        local result, err = p:alloc(1024 * 1024)
        return result == nil, err.code, err.op, p:read(mem, 0, 8)
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.equal(code, 12)
    assert.equal(op, 'reflow.pool.alloc')
    assert.equal(content, 'abcdefgh')
end

function testcase.realloc_oom_preserves_old_block_and_ownership()
    local ok, failed, code, op, content, freed = run([[
        local memlimit = require('memlimit')
        local p = require('reflow.pool').new(0)
        local mem = p:alloc(64)
        p:write(mem, 0, 'abcdefgh')
        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 8192)
        local result, err = p:realloc(mem, 1024 * 1024)
        return result == nil, err.code, err.op, p:read(mem, 0, 8),
               p:free(mem)
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.equal(code, 12)
    assert.equal(op, 'reflow.pool.realloc')
    assert.equal(content, 'abcdefgh')
    assert.is_true(freed)
end

function testcase.free_succeeds_when_cache_registration_cannot_be_required()
    local ok, freed = run([[
        local memlimit = require('memlimit')
        local p = require('reflow.pool').new(0)
        local mem = p:alloc(64)
        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 128)
        return p:free(mem)
    ]])
    assert.is_true(ok)
    assert.is_true(freed)
end
