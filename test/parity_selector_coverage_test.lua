-- Ports the cases of test/selector-coverage.test.js of the JavaScript
-- version that are not partial renders: the parser through
-- reflow.selector.compile, matchCompound and resolveSelector through the
-- reference interface reflow.selector.resolve, and the DOM index through
-- ir:to_table(). Its partial rendering cases are ported in
-- test/render_fragment_test.lua.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local compile = require('reflow.compile')
local selector = require('reflow.selector')

local function assert_syntax_error(source, pattern)
    local sel, err = selector.compile(source)
    assert.is_nil(sel)
    assert.equal(err.meta.reason, 'syntax')
    if pattern then
        assert.match(err.message.message, pattern, true)
    end
end

-- Return the number of elements of html that the static conditions of
-- source match.
local function count(html, source)
    local ir = assert(compile('t', html, 'x-', {}))
    return #assert(selector.resolve(ir, assert(selector.compile(source))))
end

function testcase.dot_followed_by_a_non_identifier_is_rejected()
    assert_syntax_error('div.')
    assert_syntax_error('div. ')
end

function testcase.attribute_missing_an_operator_or_bracket_is_rejected()
    assert_syntax_error('[a b]')
    assert_syntax_error('[a  ')
end

function testcase.colon_followed_by_a_non_identifier_is_rejected()
    assert_syntax_error('div:')
    assert_syntax_error('div:(')
end

function testcase.string_escape_at_the_end_is_rejected()
    assert_syntax_error('[a="foo\\')
end

function testcase.trailing_content_after_a_selector_is_rejected()
    assert_syntax_error('a)', 'unexpected')
end

function testcase.hash_without_an_identifier_is_rejected()
    assert_syntax_error('#', 'identifier after "#"')
end

function testcase.id_condition_rejects_an_element_without_the_id()
    assert.equal(count('<div class="x"></div>', '.x#missing'), 0)
end

function testcase.classes_reject_an_element_missing_one()
    assert.equal(count('<div class="a"></div>', '.a.b'), 0)
end

function testcase.attribute_operator_rejects_an_element_missing_the_attribute()
    assert.equal(count('<div></div><p data-x="v"></p>', 'div[data-x="v"]'), 0)
end

function testcase.attribute_equal_is_an_exact_match()
    assert.equal(count('<div data-x="v"></div>', '[data-x=v]'), 1)
end

function testcase.includes_rejects_a_target_with_white_space()
    assert.equal(count('<div data-x="a b c"></div>', '[data-x~="a b"]'), 0)
end

function testcase.descendant_combinator_without_a_matching_ancestor()
    assert.equal(count('<div><section><p>x</p></section></div>', '.missing p'),
                 0)
end

function testcase.index_drops_the_empty_class_names_of_repeated_spaces()
    local ir = assert(compile('t', '<div class="  a   b  "></div>', 'x-', {}))
    local names = {}
    for name in pairs(ir:to_table().index.by_class) do
        names[#names + 1] = name
    end
    table.sort(names)
    assert.equal(names, {
        'a',
        'b',
    })
end
