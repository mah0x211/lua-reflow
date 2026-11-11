-- Ports test/unit/final-coverage.test.js of the JavaScript version. Each
-- JavaScript regular expression on the message is written as a Lua pattern.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local cjson = require('cjson')
local reflow = require('reflow')

local function compile_error(html, ...)
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t', html)
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    local message = err.message.message
    local found = false
    for _, pattern in ipairs({
        ...,
    }) do
        found = found or message:find(pattern) ~= nil
    end
    assert.is_true(found, message)
    return err
end

local function render(html, data)
    local out, err = reflow.render(html, data)
    assert.not_nil(out, err)
    return out
end

function testcase.x_match_with_a_text_child_fails()
    compile_error('<div x-match="$.v">plain-text<p x-case="\'a\'">A</p></div>',
                  'x%-match: direct children must be x%-case or x%-nocase')
end

function testcase.x_match_with_comments_between_cases()
    assert.equal(render(
                     '<div x-match="$.v"><!-- comment --><p x-case="\'a\'">A</p>' ..
                         '<!-- --><p x-nocase>D</p></div>', {
            v = 'a',
        }), '<div><p>A</p></div>')
end

function testcase.x_if_chain_with_comments_between_members()
    assert.equal(render(
                     '<div><p x-if="$.a">A</p><!-- ok --><p x-elseif="$.b">B</p>' ..
                         '<!-- --><p x-else>D</p></div>', {
            a = false,
            b = true,
        }), '<div><p>B</p></div>')
end

function testcase.compile_error_rebuilds_the_open_tag_with_regular_attributes()
    local err = compile_error('<div class="foo" id="bar" x-unknown="x">',
                              'unknown directive')
    assert.equal(err.meta.element, '<div class="foo" id="bar">')
end

function testcase.compile_error_rebuilds_an_attribute_with_special_characters()
    local err = compile_error([[<div data-x='a"b<c&d' x-unknown="x">]],
                              'unknown directive')
    assert.match(err.meta.element, '&quot;', true)
    assert.match(err.meta.element, '&lt;', true)
    assert.match(err.meta.element, '&amp;', true)
end

function testcase.x_for_start_greater_than_stop_with_positive_step_fails()
    compile_error('<li x-for="i = 5, 1, 1" x-text=".i"></li>',
                  'direction mismatch')
end

function testcase.x_each_with_only_whitespace_collection_fails()
    compile_error('<li x-each="u in    " x-text=".u"></li>', 'x%-each')
end

function testcase.x_each_with_break_if()
    assert.equal(render(
                     '<ul><li x-each="u in $.items"><span x-text=".u"></span>' ..
                         '<br x-break-if=".u == 2"></li></ul>', {
            items = {
                1,
                2,
                3,
                4,
            },
        }), '<ul><li><span>1</span></li><li><span>2</span></li></ul>')
end

function testcase.x_include_with_a_null_value_fails()
    local out, err = reflow.render('<section x-include="$.n"></section>', {
        n = cjson.null,
    })
    assert.is_nil(out)
    assert.match(err.message.message, 'got null', true)
end

function testcase.conflicting_iteration_directives_fail()
    compile_error('<li x-for="i = 1, 3" x-each="u in $.u"></li>',
                  'conflicting iteration')
end

function testcase.conflicting_control_directives_fail()
    compile_error(
        '<ul><li x-for="i = 1, 3"><br x-break x-break-if=".i > 1"></li></ul>',
        'conflicting control')
end

function testcase.iteration_and_control_on_the_same_element_fail()
    compile_error('<li x-for="i = 1, 3" x-break></li>', 'iteration.*control',
                  'control.*iteration')
end

function testcase.bad_expression_in_x_if_fails()
    compile_error('<template x-if=":::">bad</template>', 'x%-if.*unexpected',
                  'x%-if.*parse')
end

function testcase.x_bind_with_an_empty_attribute_name_fails()
    compile_error('<div x-bind:="$.x"></div>',
                  'attribute name after "bind:" is required')
end

function testcase.multiple_structural_directives_on_the_same_element_fail()
    compile_error('<template x-if=".a" x-elseif=".b">x</template>',
                  'conflicting structural')
end

function testcase.chain_ends_at_an_element_with_an_unrelated_directive()
    local html =
        '<div><template x-if="$.a">A</template><p x-text="$.msg">middle</p></div>'
    assert.equal(render(html, {
        a = true,
        msg = 'hi',
    }), '<div><template>A</template><p>hi</p></div>')
    assert.equal(render(html, {
        a = false,
        msg = 'hi',
    }), '<div><p>hi</p></div>')
end

function testcase.orphan_x_nocase_outside_x_match_fails()
    compile_error('<div><p x-nocase>D</p></div>',
                  'x%-nocase must be a direct child of x%-match')
end
