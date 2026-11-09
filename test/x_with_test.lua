require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local HELPERS = {
    ok = true,
}

local function with_attribute(value)
    if value:find('"', 1, true) then
        return "x-with='" .. value .. "'"
    end
    return 'x-with="' .. value .. '"'
end

local function bindings(value)
    local ir, err = compile('page', '<p ' .. with_attribute(value) .. '></p>',
                            'x-', HELPERS)
    assert.not_nil(ir, err)
    return ir:to_table().root.children[1].directives.with
end

local function compile_error(html, prefix)
    local ir, err = compile('page', html, prefix or 'x-', HELPERS)
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

function testcase.x_with_keeps_bindings_in_source_order()
    local with = bindings('a = $.x, b = ok(1, 2), c = { k: [1, 2] }, ')
    assert.equal(#with, 3)
    assert.equal(with[1].name, 'a')
    assert.equal(with[1].source, '$.x')
    assert.equal(with[1].root.type, 'member')
    assert.equal(with[2].name, 'b')
    assert.equal(with[2].source, 'ok(1, 2)')
    assert.equal(#with[2].root.arguments, 2)
    assert.equal(with[3].name, 'c')
    assert.equal(with[3].root.type, 'object')
end

function testcase.x_with_splits_only_at_top_level_commas()
    local with = bindings("a = 'x, y', c = ok([1, 2], { k: 3 })")
    assert.equal(#with, 2)
    assert.equal(with[1].root.value, 'x, y')
    assert.equal(with[2].source, 'ok([1, 2], { k: 3 })')

    with = bindings('b = "q\\"q, r", d = 1')
    assert.equal(#with, 2)
    assert.equal(with[1].root.value, 'q"q, r')
    assert.equal(with[2].name, 'd')

    with = bindings('$a = 1, _b = 2, c$ = 3')
    assert.equal(with[1].name, '$a')
    assert.equal(with[2].name, '_b')
    assert.equal(with[3].name, 'c$')
end

function testcase.x_with_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        [''] = 'x-with: at least one binding is required',
        ['   '] = 'x-with: at least one binding is required',
        [','] = 'x-with: expected binding name at position 0',
        ['1a = 1'] = 'x-with: expected binding name at position 0',
        ['a'] = 'x-with: expected "=" after binding name "a"',
        ['a 1'] = 'x-with: expected "=" after binding name "a"',
        ['a ='] = 'x-with: value expression is required for binding "a"',
        ['a = ,b = 1'] = 'x-with: value expression is required for ' ..
            'binding "a"',
        ['a = 1, a = 2'] = 'x-with: duplicate binding name "a"',
        ['a = )'] = 'x-with: unbalanced ")" in binding "a"',
        ['a = }'] = 'x-with: unbalanced "}" in binding "a"',
        ['a = (1'] = 'x-with: unbalanced brackets in binding "a"',
        ['a = {'] = 'x-with: unbalanced brackets in binding "a"',
        ["a = 'unterminated"] = 'x-with: unterminated string literal in ' ..
            'binding "a"',
        ['a = #'] = 'x-with: expression parse error at position 0: ' ..
            'unexpected character "#" (in binding "a")',
        ['a = 1 2'] = 'x-with: expression parse error at position 2: ' ..
            'unexpected token (in binding "a")',
        ['a = ok(1,)'] = 'x-with: expression parse error at position 5: ' ..
            'unexpected character ")" (in binding "a")',
        ['a = 1,, b = 2'] = 'x-with: expected binding name at position 6',
        ["a = '日本' #"] = 'x-with: expression parse error at position 5: ' ..
            'unexpected token (in binding "a")',
        ['a = nope()'] = 'x-with: unknown helper "nope" \226\128\148 ' ..
            'register it via reflow.new({ helpers = { nope = fn } })',
    }

    for value, message in pairs(cases) do
        local err = compile_error('<p ' .. with_attribute(value) .. '></p>')
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-with')
    end
end

function testcase.x_with_checks_helpers_after_every_binding_parses()
    local err = compile_error('<p x-with="a = nope(), b = #"></p>')
    assert.equal(err.message.message, 'x-with: expression parse error at ' ..
                     'position 0: unexpected character "#" (in binding "b")')
end

function testcase.x_with_uses_a_fixed_head_except_for_helper_errors()
    local err = compile_error('<p r-with="a = #"></p>', 'r-')
    assert.equal(err.message.message, 'x-with: expression parse error at ' ..
                     'position 0: unexpected character "#" (in binding "a")')
    assert.equal(err.meta.attribute, 'r-with')

    err = compile_error('<p r-with="a = nope()"></p>', 'r-')
    assert.equal(err.message.message, 'r-with: unknown helper "nope" ' ..
                     '\226\128\148 register it via reflow.new({ helpers = ' ..
                     '{ nope = fn } })')
end

function testcase.x_with_rejects_duplicates_and_x_data_name_collisions()
    local err = compile_error('<p x-with="a = 1" x-with="b = 2"></p>')
    assert.equal(err.message.message, 'duplicate x-with on element')

    for _, html in ipairs({
        '<p x-data="a: 1, b: 2" x-with="b = 1"></p>',
        '<p x-with="z = 1, b = 1" x-data="b: 2"></p>',
    }) do
        err = compile_error(html)
        assert.equal(err.message.message, 'name "b" declared by both ' ..
                         'x-data and x-with on the same element')
        assert.is_nil(err.meta.attribute)
    end

    err = compile_error('<p x-with="a = #" x-data="a: 1"></p>')
    assert.match(err.message.message, 'x-with: expression parse error')
end

function testcase.x_with_shares_an_element_with_other_directives()
    local ir, err = compile('page', '<p x-data="d: 1" x-with="w = @d" ' ..
                                'x-text="@w"></p>', 'x-', {})
    assert.not_nil(ir, err)
    local directives = ir:to_table().root.children[1].directives
    assert.equal(directives.data.type, 'object')
    assert.equal(directives.with[1].name, 'w')
    assert.equal(directives.text.root.type, 'at')
end

function testcase.x_with_errors_locate_the_position_in_lua_terms()
    local err = compile_error("<p x-with=\"a = '日本' #\"></p>")
    assert.equal(err.meta.expression, {
        source = "'日本' #",
        byte = 10,
        char = 6,
    })

    err = compile_error('<p x-with="a = 1,, b = 2"></p>')
    assert.equal(err.meta.expression, {
        source = 'a = 1,, b = 2',
        byte = 7,
        char = 7,
    })

    err = compile_error('<p x-with="a = 1, b = ok(nope())"></p>')
    assert.equal(err.meta.expression, {
        source = 'ok(nope())',
        byte = 4,
        char = 4,
    })
end
