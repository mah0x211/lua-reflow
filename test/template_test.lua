require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local new_template = require('reflow.template')

-- ============================================================
-- factory
-- ============================================================

function testcase.compile_and_render()
    local t, err = new_template('<p x-text="$.name">?</p>')
    assert(t, err and err.message or 'compile failed')
    local html, rerr = t:render('{name:"Hello"}')
    assert.equal(rerr, nil)
    assert.equal(html, '<p>Hello</p>')
end

function testcase.compile_returns_error_table_on_parse_failure()
    local t, err = new_template('<p x-if="bad(">?</p>')
    assert.equal(t, nil)
    assert.equal(err.type, 'ReflowCompileError')
    assert(err.message:find('x-if'))
end

function testcase.compile_accepts_prefix_option()
    local t = new_template('<p my-text="$.name">?</p>', {prefix = 'my-'})
    assert.equal(t:prefix(), 'my-')
    local html = t:render('{name:"Hi"}')
    assert.equal(html, '<p>Hi</p>')
end

function testcase.compile_accepts_helpers_array()
    -- Helpers are validated at compile time; the render step supplies
    -- the actual callable.
    local t, err = new_template(
        '<p x-text="upper($.name)">?</p>',
        {helpers = {'upper'}})
    assert(t, err and err.message)
    local html = t:render('{name:"hi"}',
        {upper = function(s) return string.upper(s) end})
    assert.equal(html, '<p>HI</p>')
end

function testcase.compile_accepts_helpers_set()
    local t = new_template(
        '<p x-text="upper($.name)">?</p>',
        {helpers = {upper = true}})
    local html = t:render('{name:"hi"}',
        {upper = function(s) return string.upper(s) end})
    assert.equal(html, '<p>HI</p>')
end

function testcase.compile_rejects_unknown_helper()
    local t, err = new_template(
        '<p x-text="unknown($.name)">?</p>',
        {helpers = {'known'}})
    assert.equal(t, nil)
    assert(err.message:find('unknown helper'))
end

-- ============================================================
-- accessors
-- ============================================================

function testcase.html_returns_original_source()
    local src = '<div><p>hi</p></div>'
    local t = new_template(src)
    assert.equal(t:html(), src)
end

function testcase.prefix_defaults_to_x_dash()
    local t = new_template('<p>x</p>')
    assert.equal(t:prefix(), 'x-')
end

-- ============================================================
-- :render dual return
-- ============================================================

function testcase.render_without_data()
    local t = new_template('<p>static</p>')
    local html = t:render()
    assert.equal(html, '<p>static</p>')
end

function testcase.render_nil_data_ok()
    local t = new_template('<p>static</p>')
    local html = t:render(nil)
    assert.equal(html, '<p>static</p>')
end

function testcase.render_returns_error_for_invalid_data()
    local t = new_template('<p x-text="$.x">?</p>')
    local html, err = t:render(42) -- number, not string
    assert.equal(html, nil)
    assert.equal(err.type, 'ReflowRuntimeError')
end

function testcase.render_returns_error_for_bad_json5()
    local t = new_template('<p x-text="$.x">?</p>')
    local html, err = t:render('{unclosed:')
    assert.equal(html, nil)
    assert.equal(err.type, 'ReflowRuntimeError')
end

-- ============================================================
-- :render with x-include across templates
-- ============================================================

function testcase.render_with_x_include_via_templates_table()
    local child = new_template('<p>child text</p>')
    local parent = new_template([[<div><div x-include="'c'"></div></div>]])
    local html = parent:render(nil, nil, {c = child})
    assert.equal(html,
        '<div><div><p>child text</p></div></div>')
end

function testcase.render_x_include_missing_template_errors()
    local parent = new_template([[<div x-include="'missing'"></div>]])
    local html, err = parent:render(nil, nil, {})
    assert.equal(html, nil)
    assert.equal(err.type, 'ReflowIncludeError')
    assert.equal(err.reason, 'not_found')
end

function testcase.render_respects_max_depth()
    local a = new_template([[<div x-include="'b'"></div>]])
    local b = new_template([[<div x-include="'a'"></div>]])
    local html, err = a:render(nil, nil, {a = a, b = b}, 3)
    assert.equal(html, nil)
    -- Either cycle-detected or depth-exceeded depending on which fires
    -- first; both are legitimate outcomes.
    assert.equal(err.type, 'ReflowIncludeError')
    assert(err.reason == 'cycle' or err.reason == 'depth_exceeded')
end

-- ============================================================
-- template userdata is independent (no shared state)
-- ============================================================

function testcase.two_templates_are_independent()
    local a = new_template('<p>A</p>')
    local b = new_template('<p>B</p>')
    assert.equal(a:render(), '<p>A</p>')
    assert.equal(b:render(), '<p>B</p>')
    -- Order-independence: switching order still works.
    assert.equal(b:render(), '<p>B</p>')
    assert.equal(a:render(), '<p>A</p>')
end
