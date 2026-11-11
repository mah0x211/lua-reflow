-- Ports test/unit/expr-composite-literals.test.js of the JavaScript version
-- through the public API: an expression is evaluated by rendering it in
-- x-text through a helper that captures the value, a parse error is a
-- compile error, and an evaluation error is a runtime error.
--
-- Lua values follow DESIGN-09: an array literal is a sequence table, a
-- number key of an object literal is a string key, and empty array and
-- object literals are both empty tables.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local cjson = require('cjson')
local reflow = require('reflow')

local captured

local function attr(expr)
    if expr:find('"') and expr:find("'") then
        assert(not expr:find('[%s>]'), expr)
        return expr
    end
    local q = expr:find('"') and "'" or '"'
    return q .. expr .. q
end

local function render(expr, globals, frame, helpers)
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
    return reflow.render(html, globals, {
        helpers = helpers,
    })
end

local function eval_in(expr, globals, frame, helpers)
    local out, err = render(expr, globals, frame, helpers)
    assert.not_nil(out, err)
    return captured
end

local function assert_parse_error(expr, pattern)
    local out, err = render(expr)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    if pattern then
        assert.match(err.message.message, pattern, true)
    end
end

local function assert_runtime_error(expr, globals, pattern)
    local out, err = render(expr, globals)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, pattern, true)
end

function testcase.object_literal_with_identifier_string_and_number_keys()
    -- a single-quoted string key stands for the double-quoted one, since an
    -- attribute value cannot hold both quotes and spaces
    assert.equal(eval_in("{ a: 1, 'b': 'x', 3: true }"), {
        a = 1,
        b = 'x',
        ['3'] = true,
    })
end

function testcase.object_literal_is_empty_when_body_is_empty()
    assert.equal(eval_in('{}'), {})
    assert.equal(eval_in('{ }'), {})
end

function testcase.object_literal_accepts_trailing_comma()
    assert.equal(eval_in('{ a: 1, b: 2, }'), {
        a = 1,
        b = 2,
    })
end

function testcase.computed_key_from_scope_reference()
    assert.equal(eval_in("{ [$.k]: 'val' }", {
        k = 'dyn',
    }), {
        dyn = 'val',
    })
end

function testcase.computed_key_from_string_and_number_literal()
    assert.equal(eval_in("{ ['x']: 1 }"), {
        x = 1,
    })
    assert.equal(eval_in('{ [42]: 1 }'), {
        ['42'] = 1,
    })
end

function testcase.computed_key_accepts_member_chain_and_optional_chaining()
    assert.equal(eval_in("{ [$.meta.key]: 'v' }", {
        meta = {
            key = 'k',
        },
    }), {
        k = 'v',
    })
    assert_runtime_error("{ [$.missing?.key]: 'v' }", {
        missing = cjson.null,
    }, 'computed key must evaluate to a string or number, got undefined')
end

function testcase.computed_key_rejects_helper_calls()
    assert_parse_error("{ [upper('x')]: 1 }",
                       'computed object key must be a string, number, or scope reference')
end

function testcase.computed_key_rejects_operators()
    assert_parse_error("{ [true && 'x']: 1 }",
                       'computed object key must be a string, number, or scope reference')
end

function testcase.computed_key_rejects_nested_object_and_array()
    assert_parse_error('{ [{a:1}]: 1 }')
    assert_parse_error('{ [[1,2]]: 1 }')
end

function testcase.computed_key_rejects_bare_dollar()
    assert_parse_error('{ [$]: 1 }', '"$" must be followed by ".<identifier>"')
end

function testcase.computed_key_rejects_bare_at()
    assert_parse_error('{ [@]: 1 }', '"@" must be followed by an identifier')
end

function testcase.computed_key_at_reference_resolves()
    assert.equal(eval_in('{ [@k]: 1 }', nil, "k: 'from-at'"), {
        ['from-at'] = 1,
    })
end

function testcase.computed_key_dot_reference_resolves()
    assert.equal(eval_in('{ [.k]: 1 }', nil, "k: 'from-dot'"), {
        ['from-dot'] = 1,
    })
end

function testcase.computed_key_rejects_bare_dot()
    assert_parse_error('{ [.9]: 1 }', '"." must be followed by an identifier')
end

function testcase.computed_key_numeric_literal()
    assert.equal(eval_in("{ [7]: 'x' }"), {
        ['7'] = 'x',
    })
end

function testcase.computed_key_negative_numeric_literal()
    assert.equal(eval_in("{ [-1]: 'x' }"), {
        ['-1'] = 'x',
    })
end

function testcase.computed_key_rejects_unbracketed_expression()
    assert_parse_error('{ .k: 1 }')
end

function testcase.computed_key_fails_when_the_result_is_not_a_string_or_number()
    assert_runtime_error("{ [$.obj]: 'v' }", {
        obj = {
            nested = true,
        },
    }, 'computed key must evaluate to a string or number, got object')
end

function testcase.computed_key_fails_for_null_undefined_boolean_and_array()
    local globals = {
        a = cjson.null,
        c = true,
        d = {
            1,
            2,
        },
    }
    assert_runtime_error('{ [$.a]: 1 }', globals, 'null')
    assert_runtime_error('{ [$.b]: 1 }', globals, 'undefined')
    assert_runtime_error('{ [$.c]: 1 }', globals, 'boolean')
    assert_runtime_error('{ [$.d]: 1 }', globals, 'array')
end

function testcase.object_literal_duplicate_keys_last_write_wins()
    assert.equal(eval_in('{ a: 1, a: 2 }'), {
        a = 2,
    })
    assert.equal(eval_in('{ a: 1, [$.k]: 2 }', {
        k = 'a',
    }), {
        a = 2,
    })
end

function testcase.object_literal_supports_nested_literals_and_helper_values()
    assert.equal(eval_in("{ user: { name: upper($.name) }, tags: ['a', 'b'] }",
                         {
        name = 'alice',
    }, nil, {
        upper = function(s)
            return tostring(s):upper()
        end,
    }), {
        user = {
            name = 'ALICE',
        },
        tags = {
            'a',
            'b',
        },
    })
end

function testcase.array_literal_with_mixed_expression_items()
    assert.equal(eval_in('[ 1, $.n, .extra, true, null ]', {
        n = 5,
    }, "extra: 'x'"), {
        1,
        5,
        'x',
        true,
        cjson.null,
    })
end

function testcase.array_literal_is_empty_when_body_is_empty_and_accepts_trailing_comma()
    assert.equal(eval_in('[]'), {})
    assert.equal(eval_in('[ ]'), {})
    assert.equal(eval_in('[ 1, 2, ]'), {
        1,
        2,
    })
end

function testcase.array_literal_nested_inside_object_literal()
    assert.equal(eval_in('{ nums: [1, 2, [3, 4]] }'), {
        nums = {
            1,
            2,
            {
                3,
                4,
            },
        },
    })
end

function testcase.object_literal_usable_in_ternary()
    assert.equal(eval_in("$.mode == 'a' ? { kind: 'A' } : { kind: 'B' }", {
        mode = 'a',
    }), {
        kind = 'A',
    })
end

function testcase.helper_names_are_collected_from_object_and_array_literals()
    local expr = '{ x: upper($.a), y: [lower($.b), foo()] }'
    local names = {
        'upper',
        'lower',
        'foo',
    }
    for i = 1, #names do
        local helpers = {}
        for j, name in ipairs(names) do
            if j ~= i then
                helpers[name] = function()
                end
            end
        end
        local engine = assert(reflow.new({
            helpers = helpers,
        }))
        local ok, err = engine:compile('t', '<p x-text="' .. expr .. '"></p>')
        assert.is_nil(ok)
        assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
        assert.match(err.message.message, names[i], true)
    end
end

function testcase.unterminated_object_and_array_literals_are_rejected()
    assert_parse_error('{ a: 1')
    assert_parse_error('[ 1, 2')
end

function testcase.missing_colon_after_key_is_rejected()
    assert_parse_error('{ a 1 }')
end

function testcase.missing_key_before_colon_is_rejected()
    assert_parse_error('{ : 1 }')
end
