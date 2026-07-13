require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

local function ev(expr, data, helpers)
    return compiler.eval_expr(expr, data, helpers)
end

-- ===== literals =====

function testcase.eval_string()
    assert.equal(ev("'hi'", '{}'), 'hi')
    assert.equal(ev('"hi"', '{}'), 'hi')
end

function testcase.eval_number()
    assert.equal(ev("42", '{}'), 42)
    assert.equal(ev("-1", '{}'), -1)
    assert.equal(ev("3.14", '{}'), 3.14)
end

function testcase.eval_bool_null()
    assert.equal(ev("true", '{}'), true)
    assert.equal(ev("false", '{}'), false)
    assert.equal(ev("null", '{}'), nil)
end

-- ===== scope references =====

function testcase.eval_dollar()
    assert.equal(ev("$.name", '{"name":"Alice"}'), 'Alice')
    assert.equal(ev("$.user.name", '{"user":{"name":"Bob"}}'), 'Bob')
end

function testcase.eval_dot()
    assert.equal(ev(".item", '{"item":5}'), 5)
    assert.equal(ev(".conf.locale", '{"conf":{"locale":"ja"}}'), 'ja')
end

-- ===== optional chaining =====

function testcase.optional_chaining()
    assert.equal(ev(".a?.foo", '{"a":null}'), nil)
    assert.equal(ev(".b?.c", '{"b":{"c":3}}'), 3)
    assert.equal(ev(".missing?.x", '{}'), nil)
end

function testcase.member_on_undefined_error()
    local r, err = ev(".a.foo", '{"a":null}')
    assert.is_nil(r)
    assert.match(err, 'cannot read property', false)
end

-- ===== operators =====

function testcase.comparison()
    assert.equal(ev(".n == 5", '{"n":5}'), true)
    assert.equal(ev(".n != 5", '{"n":5}'), false)
    assert.equal(ev(".n < 10", '{"n":5}'), true)
    assert.equal(ev(".n > 10", '{"n":5}'), false)
    assert.equal(ev(".n <= 5", '{"n":5}'), true)
    assert.equal(ev(".n >= 6", '{"n":5}'), false)
end

function testcase.logical_and()
    assert.equal(ev(".n < 10 && .s == \"x\"", '{"n":5,"s":"x"}'), true)
    assert.equal(ev(".f && .t", '{"f":0,"t":1}'), 0)
    assert.equal(ev(".t && .f", '{"t":1,"f":0}'), 0)
end

function testcase.logical_or()
    assert.equal(ev(".n > 100 || .s == \"y\"", '{"n":5,"s":"y"}'), true)
    assert.equal(ev(".a || .b", '{"a":"","b":"x"}'), 'x')
    assert.equal(ev(".a || .b", '{"a":"val","b":"x"}'), 'val')
end

function testcase.unary_not()
    assert.equal(ev("!(.n == 5)", '{"n":5}'), false)
    assert.equal(ev("!.flag", '{"flag":false}'), true)
end

-- ===== ternary =====

function testcase.ternary()
    assert.equal(ev('.active ? "on" : "off"', '{"active":true}'), 'on')
    assert.equal(ev('.active ? "on" : "off"', '{"active":false}'), 'off')
end

-- ===== null coalescing =====

function testcase.coalesce()
    assert.equal(ev('.a ?? "default"', '{"a":null}'), 'default')
    assert.equal(ev('.b ?? 99', '{"b":0}'), 0)
    assert.equal(ev('.c ?? "x"', '{"c":""}'), '')
    assert.equal(ev('.a ?? .b ?? "c"', '{"a":null,"b":null}'), 'c')
end

-- ===== helper calls =====

function testcase.helper_call()
    local h = { upper = function(s) return s:upper() end }
    assert.equal(ev('upper(.name)', '{"name":"alice"}', h), 'ALICE')
end

function testcase.helper_nested()
    local h = {
        upper = function(s) return s:upper() end,
        lower = function(s) return s:lower() end,
    }
    assert.equal(ev('upper(lower(.name))', '{"name":"ALICE"}', h), 'ALICE')
end

function testcase.helper_multi_args()
    local h = { concat = function(...) return table.concat({...}) end }
    assert.equal(ev('concat("Hello, ", .name, "!")',
        '{"name":"World"}', h), 'Hello, World!')
end

function testcase.helper_zero_args()
    local h = { now = function() return 'now-value' end }
    assert.equal(ev('now()', '{}', h), 'now-value')
end

-- ===== object/array literals =====

function testcase.object_literal()
    local r = ev('{ a: 1, b: "x" }', '{}')
    assert.equal(r.a, 1)
    assert.equal(r.b, 'x')
end

function testcase.array_literal()
    local r = ev('[1, 2, 3]', '{}')
    assert.equal(r[1], 1)
    assert.equal(r[2], 2)
    assert.equal(r[3], 3)
end

function testcase.nested_literals()
    local r = ev('{ nums: [1, 2, [3, 4]] }', '{}')
    assert.equal(r.nums[1], 1)
    assert.equal(r.nums[3][1], 3)
end

function testcase.object_in_ternary()
    local r = ev("$.mode == 'a' ? { kind: 'A' } : { kind: 'B' }",
        '{"mode":"a"}')
    assert.equal(r.kind, 'A')
end

-- ===== numbers with exponent =====

function testcase.exponent()
    assert.equal(ev("1e3", '{}'), 1000)
    assert.equal(ev("1.5e2", '{}'), 150)
    assert.equal(ev("1e-3", '{}'), 0.001)
end
