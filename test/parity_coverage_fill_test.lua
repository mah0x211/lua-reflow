-- Ports test/unit/coverage-fill.test.js of the JavaScript version through
-- the public API.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow = require('reflow')

local captured

local function eval_in(expr, frame)
    local html = '<p x-text="cap(' .. expr .. ')"></p>'
    if frame then
        html = '<div x-data="' .. frame .. '">' .. html .. '</div>'
    end
    captured = 'not evaluated'
    local out, err = reflow.render(html, nil, {
        helpers = {
            cap = function(v)
                captured = v
                return ''
            end,
        },
    })
    assert.not_nil(out, err)
    return captured
end

local function assert_parse_error(expr, pattern)
    local out, err = reflow.render('<p x-text="' .. expr .. '"></p>')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    assert.match(err.message.message, pattern, true)
end

function testcase.le_evaluates_true_and_false()
    assert.is_true(eval_in('.a <= .b', 'a: 5, b: 5, c: 6'))
    assert.is_false(eval_in('.c <= .a', 'a: 5, b: 5, c: 6'))
end

function testcase.ge_evaluates_true_and_false()
    assert.is_true(eval_in('.a >= .b', 'a: 5, b: 5, c: 4'))
    assert.is_false(eval_in('.c >= .a', 'a: 5, b: 5, c: 4'))
end

function testcase.or_returns_left_when_truthy()
    assert.equal(eval_in('.a || .b', "a: 'x', b: 'y'"), 'x')
end

function testcase.ternary_false_branch()
    assert.equal(eval_in(".active ? 'on' : 'off'", 'active: false'), 'off')
end

function testcase.missing_closing_paren_is_rejected()
    assert_parse_error('(.a', 'expected')
end

function testcase.ne_without_left_operand_is_rejected()
    assert_parse_error('!=5', 'unexpected')
end

function testcase.ternary_missing_colon_is_rejected()
    assert_parse_error('.a ? .b .c', 'expected')
end

function testcase.number_literal_negative_exponent()
    assert.equal(eval_in('1.5e-1'), 0.15)
end

function testcase.x_match_nocase_branch_when_no_case_matches()
    assert.equal(reflow.render(
                     '<div x-match="$.v"><p x-case="\'a\'">A</p><p x-nocase>D</p></div>',
                     {
            v = 'zzz',
        }), '<div><p>D</p></div>')
end

function testcase.include_not_found_has_requested_and_reason()
    local out, err = reflow.render('<section x-include="$.name"></section>', {
        name = 'not-there',
    })
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'not_found')
    assert.equal(err.meta.requested, 'not-there')
end

function testcase.render_file_without_loader_fails()
    local out, err = reflow.render_file('some/path', {})
    assert.is_nil(out)
    assert.match(err.message.message, 'loader is required', true)
end

function testcase.member_access_with_a_trailing_dot_is_rejected()
    assert_parse_error('.a.', 'unexpected')
end
