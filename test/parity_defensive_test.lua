-- Ports test/unit/defensive.test.js of the JavaScript version. The error
-- types are the ones of reflow.error, whose fields are in the meta table.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local cjson = require('cjson')
local reflow = require('reflow')
local reflow_error = require('reflow.error')

function testcase.nil_data_renders_with_empty_globals()
    assert.equal(reflow.render('<p x-text="$.a ?? \'none\'"></p>'),
                 '<p>none</p>')
end

function testcase.null_data_is_rejected()
    -- createEnv(null) defaults to {} in JavaScript; the data of the Lua
    -- version is a table or nil (master-plan), so null is a runtime error
    local out, err = reflow.render('<p></p>', cjson.null)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.equal(err.message.message, 'data must be a table or nil')
end

function testcase.provided_data_is_kept_as_globals()
    assert.equal(reflow.render('<p x-text="$.a"></p>', {
        a = 1,
    }), '<p>1</p>')
end

function testcase.meta_does_not_overwrite_the_error_fields()
    local err = reflow_error.new('op', 'm', nil, {
        op = 'should-not-win',
        message = 'nope',
    })
    assert.equal(err.op, 'op')
    assert.equal(err.message.message, 'm')
    assert.equal(err.meta.message, 'nope')
end

function testcase.cause_is_exposed()
    local orig = errorlib.new('root')
    local err = reflow_error.new('op', 'm', orig)
    assert.rawequal(errorlib.unwrap(err), orig)
end

function testcase.every_error_type_has_its_own_type()
    assert.rawequal(errorlib.typeof(reflow_error.new('op', 'x')),
                    reflow.ReflowError)
    assert.rawequal(errorlib.typeof(reflow_error.new_compile('op', 'x')),
                    reflow.ReflowCompileError)
    assert.rawequal(errorlib.typeof(reflow_error.new_runtime('op', 'x')),
                    reflow.ReflowRuntimeError)
    assert.rawequal(errorlib.typeof(reflow_error.new_include('op', 'x')),
                    reflow.ReflowIncludeError)
end

function testcase.helper_names_are_collected_from_a_unary_operand()
    local out, err = reflow.render('<p x-text="!foo(.x)"></p>')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    assert.match(err.message.message, 'foo', true)
end
