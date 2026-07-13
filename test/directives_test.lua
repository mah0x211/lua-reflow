require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- ===== parse_data =====

function testcase.parse_data_basic()
    local t, err = compiler.dir_parse_data('user: { name: "alice", age: 30 }')
    assert.is_nil(err)
    assert.is_table(t)
    assert.is_table(t.user)
    assert.equal(t.user.name, 'alice')
    assert.equal(t.user.age, 30)
end

function testcase.parse_data_multiple_scopes()
    local t = assert(compiler.dir_parse_data('a: {x:1}, b: {y:2}'))
    assert.equal(t.a.x, 1)
    assert.equal(t.b.y, 2)
end

function testcase.parse_data_json5_sugar()
    local t = assert(compiler.dir_parse_data(
        "scope: { unq: 'single', trailing: 1, /* c */ }"))
    assert.equal(t.scope.unq, 'single')
    assert.equal(t.scope.trailing, 1)
end

function testcase.parse_data_reject_invalid_json5()
    local r, err = compiler.dir_parse_data('this is not json5 at all !!!')
    assert.is_nil(r)
    assert.match(err, 'x-data')
end

-- ===== parse_with =====

function testcase.parse_with_single()
    local names = assert(compiler.dir_parse_with('user = $.user'))
    assert.equal(#names, 1)
    assert.equal(names[1], 'user')
end

function testcase.parse_with_multiple()
    local names = assert(compiler.dir_parse_with(
        'a = 1, b = "s", c = { k: 1 }'))
    assert.equal(#names, 3)
    assert.equal(names[1], 'a')
    assert.equal(names[2], 'b')
    assert.equal(names[3], 'c')
end

function testcase.parse_with_bracket_nesting()
    -- comma inside array / object / call / string must not split bindings
    local names = assert(compiler.dir_parse_with(
        'a = [1, 2, 3], b = fn($.x, $.y), c = "hi, there"'))
    assert.equal(#names, 3)
end

function testcase.parse_with_reject_duplicate()
    local r, err = compiler.dir_parse_with('a = 1, a = 2')
    assert.is_nil(r)
    assert.match(err, 'duplicate')
end

function testcase.parse_with_reject_missing_eq()
    local r, err = compiler.dir_parse_with('a 1')
    assert.is_nil(r)
    assert.match(err, '"="')
end

function testcase.parse_with_reject_unbalanced()
    local r, err = compiler.dir_parse_with('a = [1, 2')
    assert.is_nil(r)
    assert.match(err, 'unbalanced')
end

function testcase.parse_with_reject_empty_value()
    local r, err = compiler.dir_parse_with('a = ')
    assert.is_nil(r)
    assert.match(err, 'value expression is required')
end

function testcase.parse_with_reject_no_bindings()
    local r, err = compiler.dir_parse_with('')
    assert.is_nil(r)
    assert.match(err, 'at least one binding')
end

function testcase.parse_with_reject_unterminated_string()
    local r, err = compiler.dir_parse_with([[a = "hi]])
    assert.is_nil(r)
    assert.match(err, 'unterminated')
end

-- ===== parse_for =====

function testcase.parse_for_ascending()
    local s = assert(compiler.dir_parse_for('i = 1, 5'))
    assert.equal(s.var, 'i')
    assert.equal(s.start, 1)
    assert.equal(s.stop, 5)
    assert.equal(s.step, 1)
end

function testcase.parse_for_descending()
    local s = assert(compiler.dir_parse_for('i = 10, 1, -1'))
    assert.equal(s.step, -1)
end

function testcase.parse_for_step_explicit()
    local s = assert(compiler.dir_parse_for('n = 0, 10, 2'))
    assert.equal(s.step, 2)
end

function testcase.parse_for_reject_missing_eq()
    local r, err = compiler.dir_parse_for('i 1, 5')
    assert.is_nil(r)
    assert.match(err, 'missing "="')
end

function testcase.parse_for_reject_non_integer()
    local r, err = compiler.dir_parse_for('i = 1.5, 5')
    assert.is_nil(r)
    assert.match(err, 'not an integer')
end

function testcase.parse_for_reject_step_zero()
    local r, err = compiler.dir_parse_for('i = 1, 5, 0')
    assert.is_nil(r)
    assert.match(err, 'step must not be zero')
end

function testcase.parse_for_reject_direction_asc()
    local r, err = compiler.dir_parse_for('i = 1, 5, -1')
    assert.is_nil(r)
    assert.match(err, 'direction mismatch')
end

function testcase.parse_for_reject_direction_desc()
    local r, err = compiler.dir_parse_for('i = 5, 1, 1')
    assert.is_nil(r)
    assert.match(err, 'direction mismatch')
end

function testcase.parse_for_reject_bad_varname()
    local r, err = compiler.dir_parse_for('1x = 1, 5')
    assert.is_nil(r)
    assert.match(err, 'invalid variable name')
end

-- ===== parse_each =====

function testcase.parse_each_item_only()
    local s = assert(compiler.dir_parse_each('item in $.items'))
    assert.equal(s.item, 'item')
    assert.is_nil(s.index)
    assert.is_true(s.has_collection)
end

function testcase.parse_each_item_index()
    local s = assert(compiler.dir_parse_each('user, i in $.users'))
    assert.equal(s.item, 'user')
    assert.equal(s.index, 'i')
end

function testcase.parse_each_reject_missing_in()
    local r, err = compiler.dir_parse_each('item $.items')
    assert.is_nil(r)
    assert.match(err, '"in" keyword')
end

function testcase.parse_each_reject_same_name()
    local r, err = compiler.dir_parse_each('x, x in $.items')
    assert.is_nil(r)
    assert.match(err, 'must differ')
end

function testcase.parse_each_reject_bad_item()
    local r, err = compiler.dir_parse_each('1x in $.items')
    assert.is_nil(r)
    assert.match(err, 'expected')
end

-- ===== parse_expr =====

function testcase.parse_expr_ok()
    assert.equal(compiler.dir_parse_expr('$.user.name', 'x-text'), 'ok')
end

function testcase.parse_expr_reject_empty()
    local r, err = compiler.dir_parse_expr('   ', 'x-text')
    assert.is_nil(r)
    assert.match(err, 'value is required')
end

function testcase.parse_expr_wraps_parser_error()
    local r, err = compiler.dir_parse_expr('$.', 'x-html')
    assert.is_nil(r)
    assert.match(err, 'x-html:')
end

-- ===== assert_empty =====

function testcase.assert_empty_nil()
    assert.equal(compiler.dir_assert_empty(nil, 'x-else'), 'ok')
end

function testcase.assert_empty_string()
    assert.equal(compiler.dir_assert_empty('', 'x-else'), 'ok')
end

function testcase.assert_empty_whitespace()
    assert.equal(compiler.dir_assert_empty('  \t\n', 'x-else'), 'ok')
end

function testcase.assert_empty_reject_non_empty()
    local r, err = compiler.dir_assert_empty('x', 'x-else')
    assert.is_nil(r)
    assert.match(err, 'must not have a value')
end

-- ===== is_known =====

function testcase.is_known_true()
    for _, name in ipairs({
        'data', 'with', 'if', 'elseif', 'else', 'match', 'case', 'nocase',
        'for', 'each', 'text', 'html', 'include', 'bind', 'break', 'break-if',
    }) do
        assert.is_true(compiler.dir_is_known(name),
            ('expected %q to be known'):format(name))
    end
end

function testcase.is_known_false()
    for _, name in ipairs({'unknown', 'foo', '', 'DATA', 'ifElse'}) do
        assert.is_false(compiler.dir_is_known(name),
            ('expected %q to be unknown'):format(name))
    end
end

-- ===== group =====

function testcase.group_data()
    assert.equal(compiler.dir_group('data'), 'D')
end

function testcase.group_with()
    assert.equal(compiler.dir_group('with'), 'W')
end

function testcase.group_structural()
    for _, name in ipairs({'if', 'elseif', 'else',
                           'match', 'case', 'nocase'}) do
        assert.equal(compiler.dir_group(name), 'S',
            ('expected %q to be group S'):format(name))
    end
end

function testcase.group_iteration()
    assert.equal(compiler.dir_group('for'), 'I')
    assert.equal(compiler.dir_group('each'), 'I')
end

function testcase.group_content()
    for _, name in ipairs({'text', 'html', 'include'}) do
        assert.equal(compiler.dir_group(name), 'C',
            ('expected %q to be group C'):format(name))
    end
end

function testcase.group_attribute()
    assert.equal(compiler.dir_group('bind'), 'A')
end

function testcase.group_control()
    assert.equal(compiler.dir_group('break'), 'K')
    assert.equal(compiler.dir_group('break-if'), 'K')
end

function testcase.group_unknown()
    assert.is_nil(compiler.dir_group('unknown'))
    assert.is_nil(compiler.dir_group(''))
end
