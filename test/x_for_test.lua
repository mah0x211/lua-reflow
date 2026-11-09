require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local COMBINE = 'cannot combine structural directive with iteration ' ..
                    'directive on same element; use nesting'

local function compile_for(value)
    return compile('page', '<p x-for="' .. value .. '"></p>', 'x-', {})
end

local function range_of(value)
    local ir, err = compile_for(value)
    assert.not_nil(ir, err)
    return ir:to_table().root.children[1].directives['for']
end

local function compile_error(html)
    local ir, err = compile('page', html, 'x-', {})
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

-- Lua 5.3 and later keep integer and float subtypes apart; the range holds
-- JavaScript numbers, so compare by value.
local function assert_range(range, name, start, stop, step)
    assert.equal(range.name, name)
    assert.is_true(range.start == start)
    assert.is_true(range.stop == stop)
    assert.is_true(range.step == step)
end

function testcase.x_for_parses_inclusive_integer_ranges()
    -- Expected ranges were produced by the JavaScript version.
    assert_range(range_of('i = 1, 3'), 'i', 1, 3, 1)
    assert_range(range_of(' i=1,3,1 '), 'i', 1, 3, 1)
    assert_range(range_of('i = 3, 1, -1'), 'i', 3, 1, -1)
    assert_range(range_of('i = 1, 1'), 'i', 1, 1, 1)
    assert_range(range_of('i = 1, 1, -1'), 'i', 1, 1, -1)
    assert_range(range_of('i = -5, 5, 2'), 'i', -5, 5, 2)
    assert_range(range_of('$_x9 = 0, 10'), '$_x9', 0, 10, 1)
    assert_range(range_of('i\194\160= 1,\194\1602'), 'i', 1, 2, 1)
end

function testcase.x_for_reads_digits_as_javascript_numbers()
    -- Digits beyond double precision round to the nearest double, as
    -- Number() does.
    assert_range(range_of('i = 1, 9007199254740993'), 'i', 1, 9007199254740992,
                 1)
    assert_range(range_of('i = 123456789012345678901, 1, -1'), 'i',
                 123456789012345678901, 1, -1)

    -- -0 is stored as +0.
    local range = range_of('i = 007, -0, -1')
    assert_range(range, 'i', 7, 0, -1)
    assert.equal(1 / range.stop, math.huge)
end

function testcase.x_for_reports_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local missing = 'x-for: missing "=", expected ' ..
                        '"<var> = <start>, <stop>[, <step>]"'
    local cases = {
        ['i = 1, 2, 3, 4'] = 'x-for: expected 2 or 3 arguments after "=", got 4',
        ['i = 1'] = 'x-for: expected 2 or 3 arguments after "=", got 1',
        ['i = '] = 'x-for: expected 2 or 3 arguments after "=", got 1',
        [''] = missing,
        ['i 1, 2'] = missing,
        ['1i = 1, 2'] = 'x-for: invalid variable name "1i"',
        [' = 1, 2'] = 'x-for: invalid variable name ""',
        ['i.j = 1, 2'] = 'x-for: invalid variable name "i.j"',
        ['\239\189\137 = 1, 2'] = 'x-for: invalid variable name ' ..
            '"\239\189\137"',
        ['i = 1.5, 2'] = 'x-for: "1.5" is not an integer',
        ['i = +1, 2'] = 'x-for: "+1" is not an integer',
        ['i = 1e3, 2'] = 'x-for: "1e3" is not an integer',
        ['i = 1, , 2'] = 'x-for: "" is not an integer',
        ['i = 1,2,'] = 'x-for: "" is not an integer',
        ['i = a, 2'] = 'x-for: "a" is not an integer',
        ['i = 1 2, 3'] = 'x-for: "1 2" is not an integer',
        ['i == 1, 2'] = 'x-for: "= 1" is not an integer',
        ['i = 1, 2 = 3'] = 'x-for: "2 = 3" is not an integer',
        ['i = \239\188\145, 2'] = 'x-for: "\239\188\145" is not an integer',
        ['i = 1, 2, 0'] = 'x-for: step must not be zero',
        ['i = 1, 2, -0'] = 'x-for: step must not be zero',
        ['i = 1, 2, -1'] = 'x-for: direction mismatch ' ..
            '(start=1 < stop=2 but step=-1 < 0)',
        ['i = 2, 1, 1'] = 'x-for: direction mismatch ' ..
            '(start=2 > stop=1 but step=1 > 0)',
    }

    for value, message in pairs(cases) do
        local ir, err = compile_for(value)
        assert.is_nil(ir)
        assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute, 'x-for')
    end
end

function testcase.x_for_writes_numbers_in_messages_as_javascript_does()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['i = 1, 3000000000000000000000, -1'] = '(start=1 < ' ..
            'stop=3e+21 but step=-1 < 0)',
        ['i = 99999999999999999999999, 1'] = '(start=1e+23 > stop=1 ' ..
            'but step=1 > 0)',
        ['i = 1, 0, 123456789012345678901'] = '(start=1 > stop=0 but ' ..
            'step=123456789012345680000 > 0)',
        ['i = 1, 0, 1234567890123456789012345'] = '(start=1 > stop=0 but ' ..
            'step=1.2345678901234568e+24 > 0)',
        ['i = 0, 1, -' .. string.rep('9', 400)] = '(start=0 < stop=1 but ' ..
            'step=-Infinity < 0)',
        ['i = -0, 1, -1'] = '(start=0 < stop=1 but step=-1 < 0)',
    }

    for value, detail in pairs(cases) do
        local _, err = compile_for(value)
        assert.equal(err.message.message, 'x-for: direction mismatch ' .. detail)
    end
end

function testcase.x_for_rejects_structural_directives_on_the_same_element()
    for _, html in ipairs({
        '<p x-for="i = 1, 2" x-if="$.a"></p>',
        '<p x-else x-for="i = 1, 2"></p>',
        '<p x-for="i = 1, 2" x-match="$.a"><b x-case="1"></b></p>',
        '<div x-match="$.a"><p x-case="1" x-for="i = 1, 2"></p></div>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message, COMBINE)
        assert.is_nil(err.meta.attribute)
    end
end

function testcase.x_for_shares_an_element_with_other_directives()
    local ir, err = compile('page', '<p x-for="i = 1, 2" x-text="$.v" ' ..
                                'x-bind:id="$.id" x-with="w = $.w"></p>', 'x-',
                            {})
    assert.not_nil(ir, err)
    local directives = ir:to_table().root.children[1].directives
    assert_range(directives['for'], 'i', 1, 2, 1)
    assert.equal(directives.text.source, '$.v')
    assert.equal(directives.bind[1].name, 'id')
    assert.equal(directives.with[1].name, 'w')
end

function testcase.x_for_rejects_a_duplicate_on_the_same_element()
    -- The JavaScript version keeps the last duplicate; it is rejected here
    -- like every other duplicate directive.
    local err = compile_error('<p x-for="i = 1, 2" x-for="j = 1, 2"></p>')
    assert.equal(err.message.message, 'duplicate x-for on element')
    assert.equal(err.meta.attribute, 'x-for')
end
