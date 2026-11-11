-- Ports test/unit/x-with-parser.test.js of the JavaScript version through
-- the public API: the bindings of x-with are observed by capturing their
-- values in x-text, and a parse error is a compile error. The node types
-- of the JavaScript AST are checked through the values they evaluate to.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow = require('reflow')

local captured

local function attr(value)
    if value:find('"') and value:find("'") then
        assert(not value:find('[%s>]'), value)
        return value
    end
    local q = value:find('"') and "'" or '"'
    return q .. value .. q
end

local HELPERS = {
    concat = function(...)
        return table.concat({
            ...,
        })
    end,
}

-- Render x-with="bindings" and return the values of the named bindings.
local function bind(bindings, names, globals, frame)
    local helpers = {
        concat = HELPERS.concat,
        cap = function(...)
            captured = {
                ...,
            }
            return ''
        end,
    }
    local args = {}
    for i, name in ipairs(names) do
        args[i] = '@' .. name
    end
    local html = '<p x-with=' .. attr(bindings) .. ' x-text="cap(' ..
                     table.concat(args, ', ') .. ')"></p>'
    if frame then
        html = '<div x-data="' .. frame .. '">' .. html .. '</div>'
    end
    captured = nil
    local out, err = reflow.render(html, globals, {
        helpers = helpers,
    })
    assert.not_nil(out, err)
    return captured
end

local function assert_parse_error(bindings, pattern)
    local out, err = reflow.render('<p x-with=' .. attr(bindings) .. '></p>',
                                   nil, {
        helpers = HELPERS,
    })
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    assert.match(err.message.message, pattern, false)
    return err
end

function testcase.single_binding_with_primitive()
    assert.equal(bind('a = 1', {
        'a',
    }), {
        1,
    })
end

function testcase.multiple_bindings_separated_by_commas()
    assert.equal(bind("a = 1, b = 'x', c = true", {
        'a',
        'b',
        'c',
    }), {
        1,
        'x',
        true,
    })
end

function testcase.commas_inside_object_and_array_literals_are_not_separators()
    assert.equal(bind('a = { x: 1, y: 2 }, b = [1, 2, 3]', {
        'a',
        'b',
    }), {
        {
            x = 1,
            y = 2,
        },
        {
            1,
            2,
            3,
        },
    })
end

function testcase.commas_inside_helper_calls_are_not_separators()
    assert.equal(bind('a = concat(1, 2, 3), b = 4', {
        'a',
        'b',
    }), {
        '123',
        4,
    })
end

function testcase.commas_inside_string_literals_are_not_separators()
    assert.equal(bind("a = 'one, two, three', b = 'x'", {
        'a',
        'b',
    }), {
        'one, two, three',
        'x',
    })
end

function testcase.escaped_quotes_inside_string_literals_do_not_end_the_string()
    assert.equal(bind("a = 'it\\'s a test', b = 2", {
        'a',
        'b',
    }), {
        "it's a test",
        2,
    })
end

function testcase.scope_reference_values()
    assert.equal(bind('a = $.name, b = @outer.value, c = .item.field', {
        'a',
        'b',
        'c',
    }, {
        name = 'N',
    }, "outer: { value: 'O' }, item: { field: 'F' }"), {
        'N',
        'O',
        'F',
    })
end

function testcase.rejects_duplicate_binding_names()
    assert_parse_error('a = 1, b = 2, a = 3', 'duplicate binding name "a"')
end

function testcase.rejects_missing_equals()
    assert_parse_error('a', 'expected "="')
    assert_parse_error('a b', 'expected "="')
end

function testcase.rejects_missing_binding_name()
    assert_parse_error('= 1', 'expected binding name')
    assert_parse_error('123 = 1', 'expected binding name')
end

function testcase.rejects_empty_value()
    assert_parse_error('', 'at least one binding')
    assert_parse_error('   ', 'at least one binding')
end

function testcase.rejects_binding_with_empty_expression()
    assert_parse_error('a = ', 'value expression is required for binding "a"')
    assert_parse_error('a = , b = 1',
                       'value expression is required for binding "a"')
end

function testcase.rejects_unbalanced_brackets()
    assert_parse_error('a = { x: 1', 'unbalanced brackets')
    assert_parse_error('a = [1, 2', 'unbalanced brackets')
    assert_parse_error('a = 1 }', 'unbalanced "}"')
end

function testcase.rejects_unterminated_string()
    assert_parse_error("a = 'unterminated", 'unterminated string literal')
end

function testcase.rejects_invalid_expression()
    local err = assert_parse_error('a = 1 + 2', 'x-with: ')
    assert.match(err.message.message, '(in binding "a")', true)
end

function testcase.trailing_comma_at_top_level_is_tolerated()
    assert.equal(bind('a = 1,', {
        'a',
    }), {
        1,
    })
end

function testcase.tolerates_whitespace_around_tokens()
    assert.equal(bind("   a   =   1  ,   b  =  'x'   ", {
        'a',
        'b',
    }), {
        1,
        'x',
    })
end

function testcase.newlines_between_bindings_are_allowed()
    assert.equal(bind([[
    a = 1,
    b = {
      x: 1,
      y: 2,
    },
    c = 'ok'
  ]], {
        'a',
        'b',
        'c',
    }), {
        1,
        {
            x = 1,
            y = 2,
        },
        'ok',
    })
end
