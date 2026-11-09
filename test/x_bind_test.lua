require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local function compile_error(html, helpers)
    local ir, err = compile('page', html, 'x-', helpers or {})
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

local function element_of(html, helpers)
    local ir, err = compile('page', html, 'x-', helpers or {})
    assert.not_nil(ir, err)
    return ir:to_table().root.children[1]
end

function testcase.x_bind_keeps_targets_and_expressions_in_source_order()
    local element = element_of('<p id="s" x-bind:class="ok(@a)" ' ..
                                   'x-bind:data-x.y="$.b" x-bind:ID="1"></p>', {
        ok = true,
    })
    local bind = element.directives.bind
    assert.equal(#bind, 3)
    assert.equal(bind[1].name, 'class')
    assert.equal(bind[1].source, 'ok(@a)')
    assert.equal(bind[1].root.type, 'call')
    assert.equal(bind[2].name, 'data-x.y')
    assert.equal(bind[2].root.type, 'member')
    -- The HTML parser lowercases attribute names, as in the JavaScript
    -- version.
    assert.equal(bind[3].name, 'id')
    assert.equal(bind[3].root.literal_type, 'number')
    assert.equal(element.attrs, {
        {
            name = 'id',
            value = 's',
        },
    })
end

function testcase.x_bind_shares_an_element_with_a_static_attribute()
    local element = element_of('<p class="x" x-bind:class="$.a"></p>')
    assert.equal(element.attrs, {
        {
            name = 'class',
            value = 'x',
        },
    })
    assert.equal(element.directives.bind[1].name, 'class')
end

function testcase.x_bind_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<p x-bind:="$.a"></p>'] = {
            attribute = 'x-bind:',
            message = 'x-bind:: attribute name after "bind:" is required',
        },
        ['<p x-bind:class=""></p>'] = {
            attribute = 'x-bind:class',
            message = 'x-bind:class: value is required',
        },
        ['<p x-bind:class="#"></p>'] = {
            attribute = 'x-bind:class',
            message = 'x-bind:class: expression parse error at position 0: ' ..
                'unexpected character "#"',
        },
        ['<p x-bind:class="nope()"></p>'] = {
            attribute = 'x-bind:class',
            message = 'x-bind:class: unknown helper "nope" \226\128\148 ' ..
                'register it via reflow.new({ helpers = { nope = fn } })',
        },
    }

    for html, expected in pairs(cases) do
        local err = compile_error(html)
        assert.equal(err.message.message, expected.message)
        assert.equal(err.meta.attribute, expected.attribute)
    end
end

function testcase.x_bind_rejects_duplicate_targets_on_the_same_element()
    local err = compile_error('<p x-bind:class="$.a" x-bind:CLASS="$.b"></p>')
    assert.equal(err.message.message, 'duplicate x-bind:class on element')
    assert.equal(err.meta.attribute, 'x-bind:class')
end

function testcase.x_bind_without_a_colon_is_not_a_bind_directive()
    local err = compile_error('<p x-bind="$.a"></p>')
    assert.equal(err.meta.attribute, 'x-bind')
    err = compile_error('<p x-bindx="$.a"></p>')
    assert.equal(err.meta.attribute, 'x-bindx')
end

function testcase.x_bind_shares_an_element_with_content_directives()
    local element = element_of('<p x-text="1" x-bind:title="2"></p>')
    assert.equal(element.directives.text.root.literal_type, 'number')
    assert.equal(element.directives.bind[1].name, 'title')
end
