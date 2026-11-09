require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local HELPERS = {
    ok = true,
}
local EXPECTED = 'x-each: expected "<item>[, <index>] in <collection>", got '

local function each_of(value)
    local ir, err = compile('page', '<p x-each="' .. value .. '"></p>', 'x-',
                            HELPERS)
    assert.not_nil(ir, err)
    return ir:to_table().root.children[1].directives.each
end

local function compile_error(html, prefix)
    local ir, err = compile('page', html, prefix or 'x-', HELPERS)
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

function testcase.x_each_parses_bindings_and_the_collection()
    -- Expected bindings were produced by the JavaScript version.
    local cases = {
        ['item in $.list'] = {
            'item',
            nil,
            'member',
        },
        ['item, i in $.list'] = {
            'item',
            'i',
            'member',
        },
        ['  item ,i   in   $.list  '] = {
            'item',
            'i',
            'member',
        },
        ['item\tin\n$.list'] = {
            'item',
            nil,
            'member',
        },
        ['in in $.list'] = {
            'in',
            nil,
            'member',
        },
        ['item, in in $.list'] = {
            'item',
            'in',
            'member',
        },
        ['$_a9, _b in ok($.x)'] = {
            '$_a9',
            '_b',
            'call',
        },
        ['item in \227\128\128$.list'] = {
            'item',
            nil,
            'member',
        },
        ['item\194\160in\194\160$.list'] = {
            'item',
            nil,
            'member',
        },
        ['\194\160item in $.list\n'] = {
            'item',
            nil,
            'member',
        },
    }

    for value, expected in pairs(cases) do
        local each = each_of(value)
        assert.equal(each.item, expected[1])
        assert.equal(each.index, expected[2])
        assert.equal(each.collection.root.type, expected[3])
    end
    assert.equal(each_of('v, k in ok($.map)').collection.source, 'ok($.map)')
end

function testcase.x_each_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['item in'] = EXPECTED .. '"item in"',
        ['item in '] = EXPECTED .. '"item in "',
        ['item'] = EXPECTED .. '"item"',
        [''] = EXPECTED .. '""',
        ['in $.list'] = EXPECTED .. '"in $.list"',
        ['item of $.list'] = EXPECTED .. '"item of $.list"',
        ['itemin $.list'] = EXPECTED .. '"itemin $.list"',
        ['item in$.list'] = EXPECTED .. '"item in$.list"',
        ['item,in $.list'] = EXPECTED .. '"item,in $.list"',
        ['1item in $.list'] = EXPECTED .. '"1item in $.list"',
        ['item, 1i in $.list'] = EXPECTED .. '"item, 1i in $.list"',
        ['item, i, j in $.list'] = EXPECTED .. '"item, i, j in $.list"',
        ['item.x in $.list'] = EXPECTED .. '"item.x in $.list"',
        ['\239\189\137 in $.list'] = EXPECTED .. '"\239\189\137 in $.list"',
        ['item in    '] = 'x-each: collection expression is required',
        ['item  in \194\160'] = 'x-each: collection expression is required',
        ['item, item in $.list'] = 'x-each: item name and index name must differ',
        ['x, x in nope()'] = 'x-each: item name and index name must differ',
        ['item in #'] = 'x-each: expression parse error at position 0: ' ..
            'unexpected character "#"',
        ['item in   #'] = 'x-each: expression parse error at position 0: ' ..
            'unexpected character "#"',
        ['item in $.list $.x'] = 'x-each: expression parse error at ' ..
            'position 7: unexpected token',
        ['item in $.list in $.y'] = 'x-each: expression parse error at ' ..
            'position 7: unexpected token',
        ['item, i in ('] = 'x-each: expression parse error at position 1: ' ..
            'unexpected end of expression',
        ['item in nope($.x)'] = 'x-each: unknown helper "nope" \226\128\148 ' ..
            'register it via reflow.new({ helpers = { nope = fn } })',
        ['item in ok(nope())'] = 'x-each: unknown helper "nope" ' ..
            '\226\128\148 register it via reflow.new({ helpers = ' ..
            '{ nope = fn } })',
    }

    for value, message in pairs(cases) do
        local err = compile_error('<p x-each="' .. value .. '"></p>')
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-each')
    end
end

function testcase.x_each_uses_a_fixed_head_except_for_helper_errors()
    local err = compile_error('<p r-each="v in #"></p>', 'r-')
    assert.equal(err.message.message, 'x-each: expression parse error at ' ..
                     'position 0: unexpected character "#"')
    assert.equal(err.meta.attribute, 'r-each')

    err = compile_error('<p r-each="v"></p>', 'r-')
    assert.equal(err.message.message, EXPECTED .. '"v"')

    err = compile_error('<p r-each="v in nope()"></p>', 'r-')
    assert.equal(err.message.message, 'r-each: unknown helper "nope" ' ..
                     '\226\128\148 register it via reflow.new({ helpers = ' ..
                     '{ nope = fn } })')
end

function testcase.x_each_errors_locate_the_position_in_the_collection()
    local err = compile_error('<p x-each="v, i in  $.a #"></p>')
    assert.equal(err.meta.expression, {
        source = '$.a #',
        byte = 5,
        char = 5,
    })

    err = compile_error('<p x-each="v in ok(nope())"></p>')
    assert.equal(err.meta.expression, {
        source = 'ok(nope())',
        byte = 4,
        char = 4,
    })
end

function testcase.x_each_rejects_iteration_and_structural_combinations()
    -- Expected messages were produced by the JavaScript version.
    local iteration = 'conflicting iteration directives on same element ' ..
                          '(x-for / x-each are mutually exclusive)'
    local structural = 'cannot combine structural directive with iteration ' ..
                           'directive on same element; use nesting'
    local cases = {
        ['<p x-each="v in $.a" x-for="i = 1, 2"></p>'] = iteration,
        ['<p x-for="i = 1, 2" x-each="v in $.a"></p>'] = iteration,
        ['<p x-each="v in $.a" x-if="$.b"></p>'] = structural,
        ['<div x-match="$.a"><p x-case="1" x-each="v in $.a"></p></div>'] = structural,
    }

    for html, message in pairs(cases) do
        local err = compile_error(html)
        assert.equal(err.message.message, message)
        assert.is_nil(err.meta.attribute)
    end
end

function testcase.x_each_shares_an_element_with_other_directives()
    local ir, err = compile('page', '<p x-each="v in $.a" x-text="$.v" ' ..
                                'x-with="w = $.w"></p>', 'x-', HELPERS)
    assert.not_nil(ir, err)
    local directives = ir:to_table().root.children[1].directives
    assert.equal(directives.each.item, 'v')
    assert.equal(directives.text.source, '$.v')
    assert.equal(directives.with[1].name, 'w')
end

function testcase.x_each_rejects_a_duplicate_on_the_same_element()
    -- The JavaScript version keeps the last duplicate; it is rejected here
    -- like every other duplicate directive.
    local err = compile_error('<p x-each="v in $.a" x-each="w in $.b"></p>')
    assert.equal(err.message.message, 'duplicate x-each on element')
    assert.equal(err.meta.attribute, 'x-each')
end

function testcase.x_each_parses_deep_collections_without_c_recursion()
    local depth = 2000
    local each = each_of('v in ' .. string.rep('ok(', depth) .. '1' ..
                             string.rep(')', depth))
    local node = each.collection.root
    for _ = 1, depth do
        assert.equal(node.type, 'call')
        node = node.arguments[1]
    end
    assert.equal(node.type, 'literal')
end
