-- Ports test/unit/expr-edge.test.js of the JavaScript version through the
-- public API: an expression is evaluated by rendering it in x-text through a
-- helper that captures the value, and a parse error is a compile error.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow = require('reflow')

local captured

-- An attribute value is read as written, with no character reference
-- decoded, so a double quote needs a single-quoted attribute, and both
-- quotes need an unquoted one.
local function attr(expr)
    if expr:find('"') and expr:find("'") then
        assert(not expr:find('[%s>]'), expr)
        return expr
    end
    local q = expr:find('"') and "'" or '"'
    return q .. expr .. q
end

local function eval1(expr, frame, helpers)
    helpers = helpers or {}
    helpers.cap = function(v)
        captured = v
        return ''
    end
    local html = '<p x-text=' .. attr('cap(' .. expr .. ')') .. '></p>'
    if frame then
        html = '<div x-data="' .. frame .. '">' .. html .. '</div>'
    end
    captured = 'not evaluated'
    local out, err = reflow.render(html, nil, {
        helpers = helpers,
    })
    assert.not_nil(out, err)
    return captured
end

local function assert_parse_error(expr, pattern)
    local out, err = reflow.render('<p x-text=' .. attr(expr) .. '></p>', nil, {
        helpers = {
            foo = function()
            end,
        },
    })
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    if pattern then
        assert.match(err.message.message, pattern, true)
    end
end

function testcase.parses_all_string_escape_sequences()
    assert.equal(eval1("'\\n'"), '\n')
    assert.equal(eval1("'\\t'"), '\t')
    assert.equal(eval1("'\\r'"), '\r')
    assert.equal(eval1("'\\\\'"), '\\')
    assert.equal(eval1("'\\''"), "'")
    assert.equal(eval1("'\\\"'"), '"')
    assert.equal(eval1("'\\`'"), '`')
    assert.equal(eval1("'\\0'"), '\0')
end

function testcase.rejects_invalid_escape_sequences()
    assert_parse_error("'\\z'", 'invalid escape')
end

function testcase.rejects_unterminated_strings()
    assert_parse_error("'unterminated", 'unterminated')
end

function testcase.parses_numbers_with_exponent()
    assert.equal(eval1('1e3'), 1000)
    assert.equal(eval1('1E3'), 1000)
    assert.equal(eval1('1e+3'), 1000)
    assert.equal(eval1('1e-3'), 0.001)
    assert.equal(eval1('1.5e2'), 150)
end

function testcase.rejects_unexpected_end_of_expression()
    -- a blank directive value is rejected as required before parsing, so
    -- the end is reached after an operator
    assert_parse_error('!', 'unexpected end')
    assert_parse_error('(', 'unexpected end')
end

function testcase.rejects_unexpected_characters()
    assert_parse_error('~', 'unexpected')
    assert_parse_error('#', 'unexpected')
end

function testcase.dollar_must_be_followed_by_dot()
    assert_parse_error('$', 'must be followed')
    assert_parse_error('$foo', 'must be followed')
end

function testcase.at_must_be_followed_by_identifier()
    assert_parse_error('@', 'must be followed')
    assert_parse_error('@.foo', 'must be followed')
end

function testcase.dot_must_be_followed_by_identifier()
    assert_parse_error('.', 'must be followed')
    assert_parse_error('.9', 'must be followed')
end

function testcase.optional_dot_must_be_followed_by_identifier()
    assert_parse_error('.a?.', 'expected identifier after')
end

function testcase.rejects_trailing_content_after_expression()
    assert_parse_error("'hi' xyz", 'unexpected')
end

function testcase.parses_parenthesized_expressions()
    assert.is_true(eval1('(.a == 1) && (.b == 2)', 'a: 1, b: 2'))
end

function testcase.helper_call_with_zero_args()
    assert.equal(eval1('now()', nil, {
        now = function()
            return 'now-value'
        end,
    }), 'now-value')
end

function testcase.helper_call_rejects_a_trailing_comma()
    assert_parse_error('foo(.a,)')
end

function testcase.nested_member_access()
    assert.equal(eval1('.a.b.c.d', "a: { b: { c: { d: 'deep' } } }"), 'deep')
end

function testcase.mixed_optional_and_non_optional_chain()
    assert.is_nil(eval1('.a.b?.c', 'a: { b: null }'))
end

function testcase.and_short_circuit_returns_the_left_value()
    assert.equal(eval1('.falsy && .truthy', 'falsy: 0, truthy: 5'), 0)
    assert.equal(eval1('.truthy && .falsy', 'falsy: 0, truthy: 5'), 0)
end

function testcase.or_returns_the_right_value_when_the_left_is_falsy()
    assert.equal(eval1('.a || .b', "a: '', b: 'x'"), 'x')
end
