require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local Reflow = require('reflow')

--- Compile-time errors from Reflow:compile carry template location and
--- reconstructed element context.
function testcase.compile_error_shape()
    local r = Reflow.new()
    local self, err = r:compile('main', '<div x-else>bad</div>')
    assert.is_nil(self)
    assert.is_table(err)
    assert.equal(err.type, 'ReflowCompileError')
    assert.equal(err.templateName, 'main')
    assert.equal(err.line, 1)
    assert.equal(err.column, 1)
    assert.equal(err.element, '<div>')
    assert(err.snippet:find('<div x-else>bad</div>', 1, true))
    assert(err.snippet:find('^^^', 1, true))
    assert.equal(tostring(err),
        'ReflowCompileError: x-else has no preceding x-if / x-elseif')
end

function testcase.compile_error_at_inner_element()
    -- The error origin is the <span>, not the enclosing <div>.
    local r = Reflow.new()
    local self, err = r:compile('main',
        '<div><span x-nope="x">y</span></div>')
    assert.is_nil(self)
    assert.equal(err.type, 'ReflowCompileError')
    assert.equal(err.element, '<span>')
    assert.equal(err.column, 6)
    assert.match(err.message, 'unknown directive')
end

function testcase.render_error_shape()
    local r = Reflow.new()
    r:compile('t', '<div><span x-text="$.n"></span></div>')
    local html, err = r:render('t', '{n:{a:1}}')
    assert.is_nil(html)
    assert.is_table(err)
    assert.equal(err.type, 'ReflowRuntimeError')
    assert.equal(err.templateName, 't')
    assert.equal(err.element, '<span>')
    assert.match(err.message, 'must be primitive')
end

function testcase.render_include_not_found_reason()
    local r = Reflow.new()
    r:compile('t', '<div><span x-include="\'missing\'"></span></div>')
    local html, err = r:render('t')
    assert.is_nil(html)
    assert.equal(err.type, 'ReflowIncludeError')
    assert.equal(err.reason, 'not_found')
    assert.equal(err.requested, 'missing')
    assert.equal(err.templateName, 't')
    assert.equal(err.element, '<span>')
    assert.equal(err.directive, 'x-include')
    assert.match(err.message, 'template not found')
end

function testcase.render_missing_template_reason_not_found()
    local r = Reflow.new()
    local html, err = r:render('nope')
    assert.is_nil(html)
    assert.equal(err.type, 'ReflowRuntimeError')
    assert.equal(err.templateName, 'nope')
    assert.equal(err.reason, 'not_found')
end

function testcase.tostring_matches_type_and_message()
    local r = Reflow.new()
    r:compile('t', '<div><span x-text="$.n"></span></div>')
    local html, err = r:render('t', '{n:{a:1}}')
    assert.is_nil(html)
    assert.equal(tostring(err),
        'ReflowRuntimeError: x-text: value must be primitive, got object')
end

--- x-include recursion swaps source context so an error inside the
--- included template reports that template's name and snippet, not the
--- outer caller's.
function testcase.include_error_reports_included_template_name()
    local r = Reflow.new()
    r:compile('inner', '<span x-text="$.n"></span>')
    r:compile('outer', '<div x-include="\'inner\'"></div>')
    -- Force a runtime error inside inner by passing an object for $.n.
    local html, err = r:render('outer', '{n:{a:1}}')
    assert.is_nil(html)
    assert.equal(err.type, 'ReflowRuntimeError')
    assert.equal(err.templateName, 'inner')
    assert.equal(err.element, '<span>')
end
