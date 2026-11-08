require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local CONFLICT = 'conflicting content directives on same element ' ..
                     '(x-text / x-html / x-include are mutually exclusive)'

local function compile_error(html, helpers)
    local ir, err = compile('page', html, 'x-', helpers or {})
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

function testcase.x_include_keeps_the_expression_in_its_own_slot()
    local ir, err = compile('page',
                            '<div x-include="ok(@name) ?? \'fallback\'"></div>',
                            'x-', {
        ok = true,
    })
    assert.not_nil(ir, err)
    local directives = ir:to_table().root.children[1].directives
    assert.is_nil(directives.text)
    assert.is_nil(directives.html)
    local include = directives.include
    assert.equal(include.source, "ok(@name) ?? 'fallback'")
    assert.equal(include.root.type, 'binary')
    assert.equal(include.root.op, '??')
    assert.equal(include.root.left.callee, 'ok')
    assert.equal(include.root.right.literal_type, 'string')
    assert.equal(include.root.right.value, 'fallback')
end

function testcase.x_include_does_not_resolve_templates_at_compile_time()
    local ir, err = compile('page', '<p x-include="\'missing\'"></p>', 'x-', {})
    assert.not_nil(ir, err)
end

function testcase.x_include_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<p x-include=""></p>'] = 'x-include: value is required',
        ['<p x-include="#"></p>'] = 'x-include: expression parse error at ' ..
            'position 0: unexpected character "#"',
        ['<p x-include="nope()"></p>'] = 'x-include: unknown helper ' ..
            '"nope" \226\128\148 register it via reflow.new({ helpers = ' ..
            '{ nope = fn } })',
    }

    for html, message in pairs(cases) do
        local err = compile_error(html)
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-include')
    end
end

function testcase.x_include_rejects_duplicates_on_the_same_element()
    local err = compile_error('<p x-include="\'a\'" x-include="\'b\'"></p>')
    assert.equal(err.message.message, 'duplicate x-include on element')
    assert.equal(err.meta.attribute, 'x-include')
end

function testcase.x_include_conflicts_with_other_content_directives()
    for _, html in ipairs({
        '<p x-include="\'a\'" x-text="1"></p>',
        '<p x-html="1" x-include="\'a\'"></p>',
        '<p x-text="1" x-html="2" x-include="\'a\'"></p>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message, CONFLICT)
        assert.is_nil(err.meta.attribute)
    end

    local err = compile_error('<p x-include="\'a\'" x-text="#"></p>')
    assert.equal(err.message.message, 'x-text: expression parse error at ' ..
                     'position 0: unexpected character "#"')
end
