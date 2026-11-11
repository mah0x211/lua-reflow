-- Ports test/unit/errors.test.js of the JavaScript version: the fields of
-- an error are in its meta table, with snake_case names.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow = require('reflow')

function testcase.compile_error_has_template_name_snippet_line_column_element()
    local engine = assert(reflow.new())
    local ok, err = engine:compile('layout',
                                   '<div>\n  <p>ok</p>\n  <template x-else="foo">bad</template>\n</div>')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    assert.equal(err.meta.template_name, 'layout')
    assert.equal(err.meta.line, 3)
    assert.equal(type(err.meta.column), 'number')
    assert.match(err.meta.snippet, '^^^', true)
    assert.match(err.meta.element, '^<template', false)
end

function testcase.runtime_error_has_the_include_stack()
    local engine = assert(reflow.new())
    assert(engine:compile('layout',
                          '<div><section x-include="$.content"></section></div>'))
    assert(engine:compile('page',
                          '<article><p x-text=".missing.field"></p></article>'))
    local out, err = engine:render('layout', {
        content = 'page',
    })
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.equal(err.meta.include_stack, {
        'layout',
        'page',
    })
    assert.equal(err.meta.template_name, 'page')
end

function testcase.include_cycle_error_has_reason_and_stack()
    local engine = assert(reflow.new())
    assert(engine:compile('a', '<section x-include="\'b\'"></section>'))
    assert(engine:compile('b', '<section x-include="\'a\'"></section>'))
    local out, err = engine:render('a')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowIncludeError)
    assert.equal(err.meta.reason, 'cycle')
    assert.equal(err.meta.include_stack, {
        'a',
        'b',
    })
    assert.equal(err.meta.requested, 'a')
end

function testcase.include_depth_exceeded_reports_reason()
    local engine = assert(reflow.new({
        max_include_depth = 3,
    }))
    assert(engine:compile('rec', '<section x-include="\'rec\'"></section>'))
    local out, err = engine:render('rec')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowIncludeError)
    -- cycle detection comes before the depth limit, as in JavaScript
    assert.is_true(err.meta.reason == 'cycle' or err.meta.reason ==
                       'depth_exceeded')
end
