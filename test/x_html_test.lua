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

function testcase.x_html_keeps_the_expression_in_its_own_slot()
    local ir, err = compile('page',
                            '<div x-html="ok(@body) ?? \'\'" id="a"></div>',
                            'x-', {
        ok = true,
    })
    assert.not_nil(ir, err)
    local element = ir:to_table().root.children[1]
    assert.is_nil(element.directives.text)
    local html = element.directives.html
    assert.equal(html.source, "ok(@body) ?? ''")
    assert.equal(html.root.type, 'binary')
    assert.equal(html.root.op, '??')
    assert.equal(html.root.left.type, 'call')
    assert.equal(html.root.left.callee, 'ok')
    assert.equal(html.root.left.arguments[1].type, 'at')
    assert.equal(element.attrs, {
        {
            name = 'id',
            value = 'a',
        },
    })
end

function testcase.x_html_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<p x-html=""></p>'] = 'x-html: value is required',
        ['<p x-html="#"></p>'] = 'x-html: expression parse error at ' ..
            'position 0: unexpected character "#"',
        ['<p x-html="nope()"></p>'] = 'x-html: unknown helper "nope" ' ..
            '\226\128\148 register it via reflow.new({ helpers = ' ..
            '{ nope = fn } })',
    }

    for html, message in pairs(cases) do
        local err = compile_error(html)
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-html')
    end
end

function testcase.x_html_rejects_duplicates_on_the_same_element()
    local err = compile_error('<p x-html="1" x-html="2"></p>')
    assert.equal(err.message.message, 'duplicate x-html on element')
    assert.equal(err.meta.attribute, 'x-html')
end

function testcase.x_html_and_x_text_conflict_on_the_same_element()
    for _, html in ipairs({
        '<p x-text="1" x-html="2"></p>',
        '<p x-html="2" x-text="1"></p>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message, CONFLICT)
        assert.is_nil(err.meta.attribute)
        assert.equal(err.meta.element, '<p>')
        assert.equal(err.meta.line, 1)
        assert.equal(err.meta.column, 1)
    end
end

function testcase.attribute_errors_precede_the_content_conflict()
    -- The JavaScript version validates combinations after every attribute.
    for _, html in ipairs({
        '<p x-text="1" x-html="#"></p>',
        '<p x-html="#" x-text="1"></p>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message, 'x-html: expression parse error ' ..
                         'at position 0: unexpected character "#"')
    end
end

function testcase.x_html_shares_an_element_with_x_data()
    local ir, err = compile('page', '<p x-html="@v" x-data="v: 1"></p>', 'x-',
                            {})
    assert.not_nil(ir, err)
    local directives = ir:to_table().root.children[1].directives
    assert.equal(directives.html.root.type, 'at')
    assert.equal(directives.data.type, 'object')
end
