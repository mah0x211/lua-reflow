require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local newstate = require('newstate')

local HELPERS = {
    choose = true,
    make = true,
    ok = true,
}

-- Attribute values reach the parser unescaped, so an expression that
-- contains a double quote is placed in a single-quoted attribute.
local function text_attribute(source)
    if source:find('"', 1, true) then
        assert(not source:find("'", 1, true), source)
        return "x-text='" .. source .. "'"
    end
    return 'x-text="' .. source .. '"'
end

local function compile_text(source, helpers)
    local html = '<p ' .. text_attribute(source) .. '></p>'
    return compile('page', html, 'x-', helpers or HELPERS)
end

local function parse(source)
    local ir, err = compile_text(source)
    assert.not_nil(ir, err)
    local text = ir:to_table().root.children[1].directives.text
    assert.equal(text.source, source)
    return text.root
end

local function assert_number_value(node, expected)
    assert.equal(node.literal_type, 'number')
    assert.is_true(node.value == expected)
end

local function compile_error(source, helpers)
    local ir, err = compile_text(source, helpers)
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

function testcase.x_text_keeps_the_expression_source_and_node_ranges()
    local root = parse('  ok(1)  ')
    assert.equal(root.type, 'call')
    assert.equal(root.offset, 2)
    assert.equal(root.length, 5)
    assert.equal(root.source, 'ok(1)')
    assert.equal(root.arguments[1].offset, 5)
    assert.equal(root.arguments[1].source, '1')
end

function testcase.x_text_preserves_primitive_literal_values()
    local cases = {
        {
            source = 'true',
            literal_type = 'boolean',
            value = true,
        },
        {
            source = 'false',
            literal_type = 'boolean',
            value = false,
        },
        {
            source = [=['line\n\t\r\\\'\`\0']=],
            literal_type = 'string',
            value = "line\n\t\r\\'`\0",
        },
        {
            source = [=["quote\"d"]=],
            literal_type = 'string',
            value = 'quote"d',
        },
    }

    for _, case in ipairs(cases) do
        local root = parse(case.source)
        assert.equal(root.type, 'literal')
        assert.equal(root.literal_type, case.literal_type)
        assert.equal(root.value, case.value)
        assert.equal(root.offset, 0)
        assert.equal(root.length, #case.source)
    end

    assert_number_value(parse('-12.5e2'), -1250)

    local null = parse('null')
    assert.equal(null.type, 'literal')
    assert.equal(null.literal_type, 'null')
    assert.is_nil(null.value)
end

function testcase.x_text_builds_scope_and_member_chains()
    local root = parse('$.user?.profile.name')
    assert.equal(root.type, 'member')
    assert.equal(root.property, 'name')
    assert.is_false(root.optional)
    assert.equal(root.object.type, 'member')
    assert.equal(root.object.property, 'profile')
    assert.is_true(root.object.optional)
    assert.equal(root.object.object.type, 'member')
    assert.equal(root.object.object.property, 'user')
    assert.equal(root.object.object.object.type, 'dollar')

    root = parse('@scope.value')
    assert.equal(root.object.type, 'at')
    assert.equal(root.object.name, 'scope')

    root = parse('.item')
    assert.equal(root.type, 'dot')
    assert.equal(root.name, 'item')
end

function testcase.x_text_applies_operator_precedence_and_associativity()
    local root = parse('!$.active == false && @fallback || .other ?? choose(1)')
    assert.equal(root.type, 'binary')
    assert.equal(root.op, '??')
    assert.equal(root.left.op, '||')
    assert.equal(root.left.left.op, '&&')
    assert.equal(root.left.left.left.op, '==')
    assert.equal(root.left.left.left.left.type, 'unary')
    assert.equal(root.left.left.left.left.op, '!')
    assert.equal(root.right.type, 'call')
    assert.equal(root.right.callee, 'choose')
    assert.equal(#root.right.arguments, 1)

    root = parse('$.a ?? $.b ?? $.c')
    assert.equal(root.op, '??')
    assert.equal(root.left.op, '??')
    assert.equal(root.right.type, 'member')

    root = parse('$.first ? 1 : $.second ? 2 : 3')
    assert.equal(root.type, 'ternary')
    assert.equal(root.alternate.type, 'ternary')

    local comparisons = {
        '==',
        '!=',
        '<=',
        '>=',
        '<',
        '>',
    }
    for _, op in ipairs(comparisons) do
        local source = '1 ' .. op .. ' 2'
        root = parse(source)
        assert.equal(root.type, 'binary')
        assert.equal(root.op, op)
        assert.equal(root.source, source)
    end
end

function testcase.x_text_builds_calls_arrays_and_object_entries()
    local root = parse([[make({ key: [1, 'two',], [$.meta.name]: true, }, [])]])
    assert.equal(root.type, 'call')
    assert.equal(root.callee, 'make')
    assert.equal(#root.arguments, 2)

    local object = root.arguments[1]
    assert.equal(object.type, 'object')
    assert.equal(#object.entries, 2)
    assert.is_false(object.entries[1].computed)
    assert.equal(object.entries[1].key.value, 'key')
    assert.equal(object.entries[1].value.type, 'array')
    assert.equal(#object.entries[1].value.items, 2)
    assert.is_true(object.entries[2].computed)
    assert.equal(object.entries[2].key.type, 'member')
    assert.equal(object.entries[2].value.value, true)
    assert.equal(root.arguments[2].type, 'array')
    assert.equal(#root.arguments[2].items, 0)
end

function testcase.x_text_reports_parse_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['bare_identifier'] = 'x-text: expression parse error at position 15: ' ..
            'bare identifier "bare_identifier" is not a valid expression ' ..
            '(helpers must be called: "bare_identifier(...)")',
        ['$'] = 'x-text: expression parse error at position 1: ' ..
            '"$" must be followed by ".<identifier>"',
        ['@'] = 'x-text: expression parse error at position 1: ' ..
            '"@" must be followed by an identifier',
        ['.'] = 'x-text: expression parse error at position 0: ' ..
            '"." must be followed by an identifier',
        ['.a.b()'] = 'x-text: expression parse error at position 4: ' ..
            'unexpected token',
        ['1 < 2 < 3'] = 'x-text: expression parse error at position 6: ' ..
            'unexpected token',
        ['true ? 1'] = 'x-text: expression parse error at position 8: ' ..
            'expected ":"',
        ['ok(1,)'] = 'x-text: expression parse error at position 5: ' ..
            'unexpected character ")"',
        ['[1,,2]'] = 'x-text: expression parse error at position 3: ' ..
            'unexpected character ","',
        ['{ key 1 }'] = 'x-text: expression parse error at position 6: ' ..
            'expected ":"',
        ['{ [ok()]: 1 }'] = 'x-text: expression parse error at position 3: ' ..
            'computed object key must be a string, number, or scope ' ..
            'reference (helpers and operators are not allowed here)',
        ['{ [$.key || .fallback]: 1 }'] = 'x-text: expression parse error ' ..
            'at position 9: expected "]"',
        ["'unterminated"] = 'x-text: expression parse error at position 13: ' ..
            'unterminated string literal',
        ["'\\x'"] = 'x-text: expression parse error at position 3: ' ..
            'invalid escape sequence "\\x"',
        ['1e'] = 'x-text: expression parse error at position 2: ' ..
            'invalid number literal "1e"',
        ['#'] = 'x-text: expression parse error at position 0: ' ..
            'unexpected character "#"',
        ['$.a?.'] = 'x-text: expression parse error at position 5: ' ..
            'expected identifier after "?."',
        ['(1'] = 'x-text: expression parse error at position 2: ' ..
            'expected ")"',
        ['!'] = 'x-text: expression parse error at position 1: ' ..
            'unexpected end of expression',
        ['{ a: 1'] = 'x-text: expression parse error at position 6: ' ..
            'expected "}"',
        ['[1'] = 'x-text: expression parse error at position 2: ' ..
            'expected "]"',
        ['{ [@]: 1 }'] = 'x-text: expression parse error at position 4: ' ..
            '"@" must be followed by an identifier',
        ['{ [x]: 1 }'] = 'x-text: expression parse error at position 3: ' ..
            'computed object key must be a string, number, or scope ' ..
            'reference (helpers and operators are not allowed here)',
        ["'日本' 1"] = 'x-text: expression parse error at position 5: ' ..
            'unexpected token',
        ["'日本\\é'"] = 'x-text: expression parse error at position 5: ' ..
            'invalid escape sequence "\\é"',
    }

    for source, message in pairs(cases) do
        local err = compile_error(source)
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-text')
    end
end

function testcase.x_text_reports_the_first_unknown_helper_in_source_order()
    local err = compile_error('ok(first(), second())')
    assert.equal(err.message.message,
                 'x-text: unknown helper "first" \226\128\148 register it via ' ..
                     'reflow.new({ helpers = { first = fn } })')

    err = compile_error('outer(inner())', {})
    assert.match(err.message.message, 'unknown helper "outer"')

    assert.not_nil(compile_text('ok(choose(make()))'))
end

function testcase.x_text_requires_a_non_blank_value()
    for _, source in ipairs({
        '',
        '   ',
        '\t\n',
        '\194\160',
        '\227\128\128',
    }) do
        local err = compile_error(source)
        assert.equal(err.message.message, 'x-text: value is required')
    end

    local ir, err = compile('page', '<p x-text></p>', 'x-', HELPERS)
    assert.is_nil(ir)
    assert.equal(err.message.message, 'x-text: value is required')
end

function testcase.x_text_rejects_duplicates_on_the_same_element()
    local ir, err = compile('page', '<p x-text="ok(1)" x-text="ok(2)"></p>',
                            'x-', HELPERS)
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.message.message, 'duplicate x-text on element')
    assert.equal(err.meta.attribute, 'x-text')
end

function testcase.x_text_errors_carry_source_context()
    local html = table.concat({
        '<main>',
        '  <p x-text="#"></p>',
        '</main>',
    }, '\n')
    local ir, err = compile('context', html, 'x-', {})
    assert.is_nil(ir)
    assert.equal(err.meta.template_name, 'context')
    assert.equal(err.meta.attribute, 'x-text')
    assert.equal(err.meta.line, 2)
    assert.equal(err.meta.column, 3)
    assert.match(err.meta.snippet, '<p x-text="#">')
    assert.equal(err.meta.element, '<p>')

    ir, err = compile('prefix', '<p r-text="#"></p>', 'r-', {})
    assert.is_nil(ir)
    assert.equal(err.message.message,
                 'r-text: expression parse error at position 0: ' ..
                     'unexpected character "#"')
end

function testcase.x_text_shares_an_element_with_x_data()
    local ir, err = compile('page',
                            '<p x-data="v: 1" x-text="@v.name" id="a"></p>',
                            'x-', {})
    assert.not_nil(ir, err)
    local element = ir:to_table().root.children[1]
    assert.equal(element.directives.data.type, 'object')
    assert.equal(element.directives.text.root.type, 'member')
    assert.equal(element.attrs, {
        {
            name = 'id',
            value = 'a',
        },
    })
end

function testcase.x_text_parses_deep_expressions_without_c_recursion()
    local depth = 2000
    local root = parse(string.rep('!', depth) .. 'true')
    for _ = 1, depth do
        assert.equal(root.type, 'unary')
        root = root.argument
    end
    assert.equal(root.literal_type, 'boolean')

    root = parse(string.rep('[', depth) .. '0' .. string.rep(']', depth))
    for _ = 1, depth do
        assert.equal(root.type, 'array')
        assert.equal(#root.items, 1)
        root = root.items[1]
    end
    assert_number_value(root, 0)

    root = parse(string.rep('ok(', depth) .. '1' .. string.rep(')', depth))
    for _ = 1, depth do
        assert.equal(root.type, 'call')
        root = root.arguments[1]
    end
    assert_number_value(root, 1)
end

function testcase.x_text_keeps_other_directives_rejected()
    local ir, err = compile('page', '<p x-break-if="ok(1)"></p>', 'x-', HELPERS)
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.meta.attribute, 'x-break-if')
end

function testcase.x_text_oom_returns_no_ir_and_allows_a_later_compile()
    local state = assert(newstate.new())
    local ok, failed, typed, recovered = state:dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local html = '<p x-text="' .. string.rep('[', 4096) .. '0' ..
                         string.rep(']', 4096) .. '"></p>'

        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 256 * 1024)
        local result, err = compile('oom', html, 'x-', {})
        memlimit.maxsize(0)
        collectgarbage('collect')
        collectgarbage('collect')

        local retry = compile('retry', '<p x-text="1"></p>', 'x-', {})
        return result == nil, errorlib.typeof(err) == errors.ECOMPILE,
               type(retry) == 'userdata'
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.is_true(typed)
    assert.is_true(recovered)
end

function testcase.x_text_errors_locate_the_position_in_lua_terms()
    local err = compile_error("'日本' #")
    assert.equal(err.message.message, 'x-text: expression parse error at ' ..
                     'position 5: unexpected token')
    local location = err.meta.expression
    assert.equal(location, {
        source = "'日本' #",
        byte = 10,
        char = 6,
    })
    assert.equal(location.source:sub(location.byte, location.byte), '#')

    err = compile_error('ok(nope())')
    assert.equal(err.meta.expression, {
        source = 'ok(nope())',
        byte = 4,
        char = 4,
    })

    err = compile_error('')
    assert.is_nil(err.meta.expression)
end
