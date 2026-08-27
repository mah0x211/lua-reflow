require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errlib = require('error')
local error_type = errlib.type
local reflow_error = require('reflow.error')

function testcase.new_base_error()
    local err = reflow_error.new('load', 'base failed')

    assert.rawequal(errlib.typeof(err), reflow_error.EREFLOW)
    assert.equal(err.op, 'load')
end

function testcase.typed_error_wraps_cause_directly()
    local cause = errlib.new('root cause')
    local meta = {
        template_name = 'page',
        line = 2,
        column = 3,
    }
    local err = reflow_error.new_compile('build_ir', 'compile failed', cause,
                                         meta)

    assert.rawequal(errlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.op, 'build_ir')
    -- typed error wraps the cause directly without an intermediate base error
    assert.rawequal(errlib.unwrap(err), cause)
    -- caller-supplied meta table is retained by reference
    assert.rawequal(err.meta, meta)
end

function testcase.each_constructor_uses_its_own_type()
    assert.rawequal(errlib.typeof(reflow_error.new_compile('op', 'c')),
                    reflow_error.ECOMPILE)
    assert.rawequal(errlib.typeof(reflow_error.new_runtime('op', 'r')),
                    reflow_error.ERUNTIME)
    assert.rawequal(errlib.typeof(reflow_error.new_include('op', 'i')),
                    reflow_error.EINCLUDE)
    assert.rawequal(errlib.typeof(reflow_error.new_selector('op', 's')),
                    reflow_error.ESELECTOR)
end

function testcase.op_appears_in_error_message()
    -- error rendering includes the op inside brackets
    local err = reflow_error.new_runtime('render', 'runtime failed')
    assert.match(tostring(err), '[render] runtime failed')
end

function testcase.arbitrary_cause_is_rejected()
    -- cause must be nil or an error object
    local err = assert.throws(reflow_error.new_runtime, 'render',
                              'runtime failed', {
        original = 'plain lua value',
    })
    assert.match(err, 'error expected')
end

function testcase.meta_is_kept_as_single_table()
    local meta = {
        template_name = 'page',
    }
    local err = reflow_error.new_selector('match', 'selector failed', nil, meta)

    assert.rawequal(err.meta, meta)
    -- err's own message field is not the meta table
    assert(err.message ~= meta)
end

function testcase.types_registered_uniquely()
    -- module registers each type exactly once at load time and exposes them
    assert.rawequal(reflow_error.EREFLOW, error_type.get('reflow.Error'))
    assert.rawequal(reflow_error.ECOMPILE, error_type.get('reflow.CompileError'))
    assert.rawequal(reflow_error.ERUNTIME, error_type.get('reflow.RuntimeError'))
    assert.rawequal(reflow_error.EINCLUDE, error_type.get('reflow.IncludeError'))
    assert.rawequal(reflow_error.ESELECTOR,
                    error_type.get('reflow.SelectorError'))
end

function testcase.rejects_invalid_arguments()
    -- non-string op
    local err = assert.throws(reflow_error.new, nil, 'msg')
    assert.match(err, 'op must be string')

    -- non-string message
    err = assert.throws(reflow_error.new, 'op', 123)
    assert.match(err, 'message must be string')

    -- non-table meta
    err = assert.throws(reflow_error.new_compile, 'op', 'msg', nil,
                        'not a table')
    assert.match(err, 'meta must be table')

    -- non-error cause
    err = assert.throws(reflow_error.new_compile, 'op', 'msg', 'not an error')
    assert.match(err, 'error expected')
end

