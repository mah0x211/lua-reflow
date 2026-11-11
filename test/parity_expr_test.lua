-- Ports test/unit/expr.test.js of the JavaScript version through the public
-- API: an expression is evaluated by rendering it in x-text through a
-- helper that captures the value. A data frame of the JavaScript tests is
-- written as x-data.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local cjson = require('cjson')
local reflow = require('reflow')

local captured

-- Evaluate expr with globals and the x-data declarations frame, and return
-- the value.
local function eval1(expr, globals, frame, helpers)
    helpers = helpers or {}
    helpers.cap = function(v)
        captured = v
        return ''
    end
    -- an attribute value is read as written, so a double quote needs a
    -- single-quoted attribute
    local q = expr:find('"') and "'" or '"'
    local html = '<p x-text=' .. q .. 'cap(' .. expr .. ')' .. q .. '></p>'
    if frame then
        html = '<div x-data="' .. frame .. '">' .. html .. '</div>'
    end
    captured = 'not evaluated'
    local out, err = reflow.render(html, globals, {
        helpers = helpers,
    })
    assert.not_nil(out, err)
    return captured
end

local function assert_compile_error(expr)
    local out, err = reflow.render('<p x-text="' .. expr .. '"></p>')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
end

function testcase.parses_literals()
    assert.equal(eval1("'hi'"), 'hi')
    assert.equal(eval1('"hi"'), 'hi')
    assert.equal(eval1('42'), 42)
    assert.equal(eval1('-1'), -1)
    assert.equal(eval1('3.14'), 3.14)
    assert.is_true(eval1('true'))
    assert.is_false(eval1('false'))
    assert.rawequal(eval1('null'), cjson.null)
end

function testcase.parses_scope_references()
    local globals = {
        user = {
            name = 'Alice',
        },
    }
    local frame = "conf: { locale: 'ja' }, item: 5"
    assert.equal(eval1('$.user.name', globals, frame), 'Alice')
    assert.equal(eval1('@conf.locale', globals, frame), 'ja')
    assert.equal(eval1('.conf.locale', globals, frame), 'ja')
    assert.equal(eval1('.item', globals, frame), 5)
end

function testcase.optional_chaining_short_circuits_on_null_and_undefined()
    local frame = 'a: null, b: { c: 3 }'
    assert.is_nil(eval1('.a?.foo', nil, frame))
    assert.equal(eval1('.b?.c', nil, frame), 3)
    assert.is_nil(eval1('.missing?.x', nil, frame))
end

function testcase.comparison_and_logical_operators()
    local frame = "n: 5, s: 'x'"
    assert.is_true(eval1('.n == 5', nil, frame))
    assert.is_false(eval1('.n != 5', nil, frame))
    assert.is_true(eval1('.n < 10 && .s == "x"', nil, frame))
    assert.is_false(eval1('.n > 100 || .s == "y"', nil, frame))
    assert.is_false(eval1('!(.n == 5)', nil, frame))
end

function testcase.ternary()
    assert.equal(eval1(".active ? 'on' : 'off'", nil, 'active: true'), 'on')
end

function testcase.null_coalescing()
    local frame = "a: null, b: 0, c: ''"
    assert.equal(eval1(".a ?? 'default'", nil, frame), 'default')
    assert.equal(eval1('.b ?? 99', nil, frame), 0)
    assert.equal(eval1(".c ?? 'x'", nil, frame), '')
end

function testcase.helper_calls()
    local helpers = {
        upper = function(s)
            return tostring(s):upper()
        end,
        concat = function(...)
            return table.concat({
                ...,
            })
        end,
    }
    assert.equal(eval1('upper(.name)', nil, "name: 'alice'", helpers), 'ALICE')
    assert.equal(eval1("concat('Hello, ', upper(.name), '!')", nil,
                       "name: 'alice'", helpers), 'Hello, ALICE!')
end

function testcase.helper_names_are_collected_from_nested_calls()
    -- collectHelperNames: every nested helper must be registered when the
    -- template is compiled.
    local expr = 'upper(lower(.x)) ? foo(.a, .b) : bar()'
    local fn = function()
    end
    local names = {
        'upper',
        'lower',
        'foo',
        'bar',
    }
    for i = 1, #names do
        local helpers = {}
        for j, name in ipairs(names) do
            if j ~= i then
                helpers[name] = fn
            end
        end
        local engine = assert(reflow.new({
            helpers = helpers,
        }))
        local ok, err = engine:compile('t', '<p x-text="' .. expr .. '"></p>')
        assert.is_nil(ok)
        assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
        assert.match(err.message.message, names[i], false)
    end
end

function testcase.rejects_arithmetic_operators()
    assert_compile_error('1 + 2')
    assert_compile_error('.a - .b')
end

function testcase.rejects_method_call_syntax()
    assert_compile_error('.a.b()')
end

function testcase.rejects_bare_identifier_without_call()
    assert_compile_error('foo')
end

function testcase.rejects_reserved_keyword_literals_only()
    assert.is_true(eval1('true'))
    assert.rawequal(eval1('null'), cjson.null)
end
