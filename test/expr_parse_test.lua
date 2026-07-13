require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- helper: expression should parse successfully
local function ok(expr)
    local r, err = compiler.parse_expr(expr)
    assert.equal(r, 'ok', ('expected parse OK for %q but got error: %s')
        :format(expr, tostring(err)))
end

-- helper: expression should fail to parse
local function fail(expr)
    local r = compiler.parse_expr(expr)
    assert(r ~= 'ok', ('expected parse failure for %q'):format(expr))
end

-- helper: expression should fail with a specific message pattern
local function fail_match(expr, pattern)
    local r, err = compiler.parse_expr(expr)
    assert(r ~= 'ok', ('expected parse failure for %q'):format(expr))
    assert(err and err:find(pattern, 1, true),
        ('expected error %q to match %q for %q')
            :format(tostring(err), pattern, expr))
end

-- ===== literals =====

function testcase.literal_string()
    ok("'hi'")
    ok('"hi"')
    ok([["hello world"]])
    ok("'with \\'quote'")
end

function testcase.literal_number()
    ok("42")
    ok("-1")
    ok("3.14")
    ok("0")
    ok("1e3")
    ok("1E3")
    ok("1e+3")
    ok("1e-3")
    ok("1.5e2")
    ok("-0.5e10")
end

function testcase.literal_bool_null()
    ok("true")
    ok("false")
    ok("null")
end

function testcase.string_escapes()
    ok([['\n']])
    ok([['\t']])
    ok([['\r']])
    ok([['\\']])
    ok([['\'']])
    ok([['\"']])
    ok([['\`']])
    ok([['\0']])
end

-- ===== scope references =====

function testcase.scope_dollar()
    ok("$.user")
    ok("$.user.name")
    ok("$.a.b.c.d")
end

function testcase.scope_at()
    ok("@conf")
    ok("@conf.locale")
    ok("@item")
end

function testcase.scope_dot()
    ok(".conf")
    ok(".conf.locale")
    ok(".item")
    ok(".a.b.c.d")
end

-- ===== member access / optional chaining =====

function testcase.optional_chaining()
    ok(".a?.foo")
    ok(".b?.c")
    ok(".missing?.x")
    ok("$.user?.name")
    ok(".a.b?.c.d")
    ok(".a?.b?.c?.d")
end

function testcase.nested_member()
    ok(".a.b.c.d")
    ok("$.x.y.z")
    ok("@a.b.c")
end

-- ===== operators =====

function testcase.comparison()
    ok(".n == 5")
    ok(".n != 5")
    ok(".n < 10")
    ok(".n > 100")
    ok(".n <= 5")
    ok(".n >= 5")
end

function testcase.logical()
    ok(".n < 10 && .s == \"x\"")
    ok(".n > 100 || .s == \"y\"")
    ok("!(.n == 5)")
    ok("!.flag")
end

function testcase.ternary()
    ok('.active ? "on" : "off"')
    ok('.a ? .b ? "x" : "y" : "z"')
end

function testcase.coalesce()
    ok('.a ?? "default"')
    ok(".b ?? 99")
    ok(".c ?? \"x\"")
    ok(".a ?? .b ?? .c")
end

-- ===== helper calls =====

function testcase.helper_call()
    ok("upper(.name)")
    ok("concat(\"Hello, \", upper(.name), \"!\")")
    ok("now()")
    ok("foo(.a, .b)")
end

function testcase.helper_nested()
    ok("upper(lower(.x))")
    ok("a(b(c(d(.x))))")
end

-- ===== parenthesized =====

function testcase.parenthesized()
    ok("(.a == 1)")
    ok("(.a == 1) && (.b == 2)")
    ok("((.a))")
end

-- ===== object literals =====

function testcase.object_basic()
    ok("{ a: 1 }")
    ok([[{ a: 1, "b": 'x', 3: true }]])
    ok("{}")
    ok("{ }")
end

function testcase.object_trailing_comma()
    ok("{ a: 1, b: 2, }")
end

function testcase.object_computed_key()
    ok("{ [$.k]: 'val' }")
    ok("{ ['x']: 1 }")
    ok("{ [42]: 1 }")
    ok("{ [7]: 'x' }")
    ok("{ [-1]: 'x' }")
    ok("{ [$.meta.key]: 'v' }")
    ok("{ [$.missing?.key]: 'v' }")
    ok("{ [@k]: 1 }")
    ok("{ [.k]: 1 }")
end

function testcase.object_nested()
    ok("{ nums: [1, 2, [3, 4]] }")
    ok([[{ user: { name: upper($.name) }, tags: ['a', 'b'] }]])
end

function testcase.object_in_ternary()
    ok("$.mode == 'a' ? { kind: 'A' } : { kind: 'B' }")
end

-- ===== array literals =====

function testcase.array_basic()
    ok("[ 1, $.n, .extra, true, null ]")
    ok("[]")
    ok("[ ]")
end

function testcase.array_trailing_comma()
    ok("[ 1, 2, ]")
end

function testcase.array_nested()
    ok("[1, 2, [3, 4]]")
    ok("[[1], [2], [3]]")
end

-- ===== error cases =====

function testcase.reject_arithmetic()
    fail("1 + 2")
    fail("a - b")
end

function testcase.reject_method_call()
    fail(".a.b()")
end

function testcase.reject_bare_identifier()
    fail("foo")
    fail_match("foo", "bare identifier")
end

function testcase.reject_empty()
    fail_match("", "empty expression")
    fail_match("  ", "unexpected end")
end

function testcase.reject_unexpected_char()
    fail("~")
    fail("#")
    fail_match("~", "unexpected character")
end

function testcase.reject_dollar_no_dot()
    fail_match("$", "must be followed")
    fail_match("$foo", "must be followed")
end

function testcase.reject_at_no_identifier()
    fail_match("@", "must be followed")
    fail_match("@.foo", "must be followed")
end

function testcase.reject_dot_no_identifier()
    fail_match(".", "must be followed")
    fail_match(".9", "must be followed")
end

function testcase.reject_optional_no_identifier()
    fail_match(".a?.", "expected identifier after")
end

function testcase.reject_trailing_content()
    fail_match("'hi' xyz", "unexpected")
end

function testcase.reject_invalid_escape()
    fail_match([['\z']], "invalid escape")
end

function testcase.reject_unterminated_string()
    fail_match("'unterminated", "unterminated")
end

function testcase.reject_trailing_comma_in_args()
    fail("foo(a,)")
end

function testcase.reject_unterminated_object()
    fail("{ a: 1")
    fail("[ 1, 2")
end

function testcase.reject_missing_colon()
    fail("{ a 1 }")
end

function testcase.reject_missing_key()
    fail("{ : 1 }")
end

function testcase.reject_computed_key_helper()
    fail_match("{ [upper('x')]: 1 }",
        "computed object key must be a string, number, or scope reference")
end

function testcase.reject_computed_key_operator()
    fail_match("{ [true && 'x']: 1 }",
        "computed object key must be a string, number, or scope reference")
end

function testcase.reject_computed_key_nested_literal()
    fail("{ [{a:1}]: 1 }")
    fail("{ [[1,2]]: 1 }")
end

function testcase.reject_computed_key_bare_dollar()
    fail_match("{ [$]: 1 }", "must be followed")
end

function testcase.reject_computed_key_bare_at()
    fail_match("{ [@]: 1 }", "must be followed")
end

function testcase.reject_computed_key_bare_dot()
    fail_match("{ [.9]: 1 }", "must be followed")
end

function testcase.reject_computed_key_unbracketed()
    fail("{ .k: 1 }")
end
