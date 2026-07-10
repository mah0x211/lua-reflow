require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local reflow = require('reflow')
local compiler = require('reflow.compiler')

function testcase.require_reflow()
    -- the Lua wrapper and C core must load
    assert.equal(type(reflow), 'table')
    assert.equal(type(reflow.new), 'function')
    assert.equal(type(reflow.version), 'function')
    assert.equal(type(compiler), 'table')
end

function testcase.version()
    assert.equal(reflow.version(), '0.0.0-dev')
end

function testcase.yyjson_smoke()
    -- the yyjson amalgamation compiled, linked, and runs
    assert.equal(compiler.yyjson_ok(), true)
end

function testcase.new_instance()
    local r = reflow.new({
        prefix = 'x-',
    })
    assert.equal(type(r), 'table')
    assert.equal(type(r.add_helper), 'function')
end
