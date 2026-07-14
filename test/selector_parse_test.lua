require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- helper: selector should parse successfully; returns the AST table
local function ok(src)
    local r, err = compiler.parse_selector(src)
    assert(r, ('expected parse OK for %q but got error: %s')
        :format(src, err and err.message or tostring(err)))
    return r
end

-- helper: selector must fail with the expected reason ('syntax' or
-- 'unsupported') and its message must contain `pattern` as a plain substring.
-- Returns the error table so callers can inspect additional fields (feature,
-- position, ...) without duplicating the reason/message assertions.
local function fail(src, reason, pattern)
    local r, err = compiler.parse_selector(src)
    assert(not r, ('expected parse failure for %q'):format(src))
    assert.equal(err.type, 'ReflowSelectorError')
    assert.equal(err.reason, reason,
        ('reason mismatch for %q: want %q, got %q'):format(
            src, reason, tostring(err.reason)))
    if pattern then
        assert(err.message:find(pattern, 1, true),
            ('expected error %q to contain %q for %q')
                :format(err.message, pattern, src))
    end
    return err
end

-- ============================================================
-- basic shapes
-- ============================================================

function testcase.list_type_and_source()
    local r = ok('div')
    assert.equal(r.type, 'list')
    assert.equal(r.source, 'div')
    assert.equal(#r.selectors, 1)
    assert.is_false(r.hasPositional)
end

function testcase.universal_tag()
    local r = ok('*')
    assert.equal(r.selectors[1].parts[1].compound.tag, nil)
end

function testcase.tag_lowercased()
    local r = ok('DIV')
    assert.equal(r.selectors[1].parts[1].compound.tag, 'div')
end

function testcase.id_class_attr_combined()
    local r = ok('a#foo.bar.baz[href]')
    local c = r.selectors[1].parts[1].compound
    assert.equal(c.tag, 'a')
    assert.equal(c.id, 'foo')
    assert.equal(#c.classes, 2)
    assert.equal(c.classes[1], 'bar')
    assert.equal(c.classes[2], 'baz')
    assert.equal(#c.attrs, 1)
    assert.equal(c.attrs[1].name, 'href')
    assert.equal(c.attrs[1].op, nil)
    assert.equal(c.attrs[1].value, nil)
end

-- ============================================================
-- attribute operators
-- ============================================================

function testcase.attr_operators()
    local operators = {'=', '~=', '|=', '^=', '$=', '*='}
    for _, op in ipairs(operators) do
        local r = ok(('[data-x%sval]'):format(op))
        assert.equal(r.selectors[1].parts[1].compound.attrs[1].op, op)
        assert.equal(r.selectors[1].parts[1].compound.attrs[1].value, 'val')
    end
end

function testcase.attr_quoted_value_preserves_case()
    local r = ok('[Data-X="Hello World"]')
    local a = r.selectors[1].parts[1].compound.attrs[1]
    -- attribute names are case-insensitive; values are not
    assert.equal(a.name, 'data-x')
    assert.equal(a.value, 'Hello World')
end

function testcase.attr_single_quoted()
    local r = ok("[a='v v']")
    assert.equal(r.selectors[1].parts[1].compound.attrs[1].value, 'v v')
end

function testcase.attr_string_escape()
    local r = ok('[a="a\\"b"]')
    assert.equal(r.selectors[1].parts[1].compound.attrs[1].value, 'a"b')
end

-- ============================================================
-- pseudo-classes
-- ============================================================

function testcase.boolean_pseudos()
    local names = {
        'first-child', 'last-child', 'only-child',
        'first-of-type', 'last-of-type', 'only-of-type',
    }
    for _, n in ipairs(names) do
        local r = ok(':' .. n)
        local p = r.selectors[1].parts[1].compound.pseudos[1]
        assert.equal(p.name, n)
        assert.equal(p.n, nil)
        assert.is_true(r.hasPositional)
    end
end

function testcase.nth_pseudos()
    local names = {
        'nth-child', 'nth-last-child',
        'nth-of-type', 'nth-last-of-type',
    }
    for _, n in ipairs(names) do
        local r = ok((':%s(3)'):format(n))
        local p = r.selectors[1].parts[1].compound.pseudos[1]
        assert.equal(p.name, n)
        assert.equal(p.n, 3)
    end
end

function testcase.nth_arg_allows_inner_ws()
    ok(':nth-child( 5 )')
end

-- ============================================================
-- combinators
-- ============================================================

function testcase.descendant_combinator()
    local r = ok('div p')
    local parts = r.selectors[1].parts
    assert.equal(#parts, 2)
    assert.equal(parts[1].combinator, nil)
    assert.equal(parts[2].combinator, ' ')
end

function testcase.child_combinator()
    local r = ok('div > p')
    assert.equal(r.selectors[1].parts[2].combinator, '>')
end

function testcase.child_combinator_no_space()
    local r = ok('div>p')
    assert.equal(#r.selectors[1].parts, 2)
    assert.equal(r.selectors[1].parts[2].combinator, '>')
end

function testcase.chained_combinators()
    local r = ok('a > b c > d')
    assert.equal(#r.selectors[1].parts, 4)
    assert.equal(r.selectors[1].parts[2].combinator, '>')
    assert.equal(r.selectors[1].parts[3].combinator, ' ')
    assert.equal(r.selectors[1].parts[4].combinator, '>')
end

function testcase.selector_list()
    local r = ok('div, p, span')
    assert.equal(#r.selectors, 3)
    assert.equal(r.selectors[1].parts[1].compound.tag, 'div')
    assert.equal(r.selectors[2].parts[1].compound.tag, 'p')
    assert.equal(r.selectors[3].parts[1].compound.tag, 'span')
end

function testcase.leading_and_trailing_whitespace()
    ok('   div   ')
end

-- ============================================================
-- error cases: reason == 'syntax'
-- ============================================================

function testcase.err_empty()
    fail('', 'syntax', 'empty selector')
    fail('   ', 'syntax', 'empty selector')
end

function testcase.err_trailing_comma()
    fail('div,', 'syntax', 'expected selector')
end

function testcase.err_leading_comma()
    fail(', div', 'syntax', 'expected selector')
end

function testcase.err_missing_ident_after_hash()
    fail('#', 'syntax', 'expected identifier after "#"')
end

function testcase.err_missing_ident_after_dot()
    fail('.', 'syntax', 'expected identifier after "."')
end

function testcase.err_missing_attribute_name()
    fail('[', 'syntax', 'expected attribute name')
end

function testcase.err_unclosed_attribute()
    fail('[a', 'syntax', 'expected attribute operator')
    fail('[a=b', 'syntax', 'expected "]" to close attribute selector')
end

function testcase.err_bad_attr_operator()
    -- lone ~ / ^ / $ / * / | without = should complain about the =
    fail('[a~b]', 'syntax', 'expected "=" after "~"')
    fail('[a^b]', 'syntax', 'expected "=" after "^"')
end

function testcase.err_missing_attribute_value()
    fail('[a=]', 'syntax', 'expected attribute value after "="')
    fail('[a=1]', 'syntax', 'expected attribute value after "="')
end

function testcase.err_multiple_ids()
    fail('div#a#b', 'syntax', 'multiple "#id"')
end

function testcase.err_boolean_pseudo_with_arg()
    fail(':first-child(1)', 'syntax', 'does not take an argument')
end

function testcase.err_nth_zero()
    fail(':nth-child(0)', 'syntax', 'must be a positive integer')
end

function testcase.err_string_literal_at_top()
    fail('"foo"', 'syntax', 'expected selector')
end

function testcase.err_unterminated_string()
    fail('[a="foo]', 'syntax', 'unterminated string literal')
end

-- ============================================================
-- error cases: reason == 'unsupported'
-- ============================================================

function testcase.err_pseudo_element()
    local e = fail('::before', 'unsupported', 'pseudo-elements')
    assert.equal(e.feature, 'pseudo-element')
end

function testcase.err_unknown_pseudo_class()
    local e = fail(':not(.x)', 'unsupported', ':not')
    assert.equal(e.feature, 'pseudo:not')
    fail(':is(.x)', 'unsupported', ':is')
    fail(':where(.x)', 'unsupported', ':where')
    fail(':has(.x)', 'unsupported', ':has')
    fail(':unknown', 'unsupported', ':unknown')
end

function testcase.err_nth_formula()
    local e = fail(':nth-child(2n+1)', 'unsupported', 'formulas')
    assert.equal(e.feature, 'pseudo-arg:nth-child')
    fail(':nth-child(odd)', 'unsupported', 'formulas')
    fail(':nth-child(even)', 'unsupported', 'formulas')
    fail(':nth-child(-1)', 'unsupported', 'formulas')
end

function testcase.err_sibling_combinators()
    local e = fail('a + b', 'unsupported', '"+" is not supported')
    assert.equal(e.feature, 'combinator:+')
    fail('a ~ b', 'unsupported', '"~" is not supported')
end

function testcase.err_column_combinator()
    local e = fail('a || b', 'unsupported', 'column combinator')
    assert.equal(e.feature, 'combinator:||')
end

function testcase.err_attr_namespace()
    local e = fail('[ns|name]', 'unsupported', 'attribute namespaces')
    assert.equal(e.feature, 'attr-namespace')
    -- [ns|=v] is the standard |= operator and must still parse
    ok('[a|=b]')
end

function testcase.err_attr_case_flag()
    local e = fail('[a=x i]', 'unsupported', 'case-sensitivity flag')
    assert.equal(e.feature, 'attr-case-flag')
    fail('[a=x s]', 'unsupported', 'case-sensitivity flag')
end

-- ============================================================
-- structured error fields
-- ============================================================

function testcase.error_has_position_and_source()
    local e = fail('div ++ span', 'unsupported', '"+" is not supported')
    assert.equal(e.source, 'div ++ span')
    assert.equal(e.position, 4)  -- 0-based offset of first '+'
    assert.equal(e.line, 1)
    assert.equal(e.column, 5)    -- 1-based column
    assert.equal(e.feature, 'combinator:+')
end

function testcase.error_position_after_newline()
    local e = fail('div,\n   ??', 'syntax', 'expected selector')
    assert.equal(e.line, 2)
end

-- ============================================================
-- hasPositional propagation
-- ============================================================

function testcase.has_positional_when_any_compound_has_pseudo()
    local r = ok('div, span:first-child')
    assert.is_true(r.hasPositional)
end

function testcase.has_positional_false_when_no_pseudos()
    local r = ok('div > span, .foo [bar]')
    assert.is_false(r.hasPositional)
end
