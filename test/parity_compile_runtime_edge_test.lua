-- Ports test/unit/compile-runtime-edge.test.js of the JavaScript version.
-- Each JavaScript regular expression on the message is written as a Lua
-- pattern.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow = require('reflow')

local function compile_error(html, pattern)
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t', html)
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow.ReflowCompileError)
    assert.match(err.message.message, pattern, false)
end

local function render_error(templates, name, data, config)
    local engine = assert(reflow.new(config))
    for _, t in ipairs(templates) do
        assert(engine:compile(t[1], t[2]))
    end
    local out, err = engine:render(name, data)
    assert.is_nil(out)
    return err
end

function testcase.x_for_missing_equals_fails()
    compile_error('<li x-for="i 1, 3" x-text=".i"></li>', 'x%-for.-=')
end

function testcase.x_for_invalid_variable_name_fails()
    compile_error('<li x-for="1x = 1, 3" x-text=".i"></li>',
                  'invalid variable name')
end

function testcase.x_for_wrong_argument_count_fails()
    compile_error('<li x-for="i = 1" x-text=".i"></li>',
                  'expected 2 or 3 arguments')
end

function testcase.x_each_missing_in_keyword_fails()
    compile_error('<li x-each="user $.users" x-text=".user"></li>',
                  'expected .-in')
end

function testcase.x_each_with_item_equal_to_index_fails()
    compile_error('<li x-each="x, x in $.users" x-text=".x"></li>',
                  'must differ')
end

function testcase.x_each_invalid_collection_expression_fails()
    compile_error('<li x-each="u in .a.b()"></li>', 'x%-each')
end

function testcase.x_elseif_with_empty_value_fails()
    compile_error(
        '<div><template x-if=".a">A</template><template x-elseif="">B</template></div>',
        'x%-elseif: value is required')
end

function testcase.elseif_after_else_fails()
    compile_error(
        '<div><template x-if=".a">A</template><template x-else>E</template>' ..
            '<template x-elseif=".b">B</template></div>',
        'x%-elseif after x%-else')
end

function testcase.multiple_x_else_in_chain_fails()
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t',
                                   '<div><template x-if=".a">A</template><template x-else>E1</template>' ..
                                       '<template x-else>E2</template></div>')
    assert.is_nil(ok)
    local message = err.message.message
    assert(message:find('multiple x-else', 1, true) or
               message:find('x-else has no preceding', 1, true) or
               message:find('x-elseif after x-else', 1, true), message)
end

function testcase.x_match_with_a_child_that_is_not_a_case_fails()
    compile_error(
        '<div x-match="$.v"><p>plain</p><p x-case="\'a\'">A</p></div>',
        'x%-match.-must be x%-case or x%-nocase')
end

function testcase.multiple_x_nocase_in_the_same_match_fails()
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t',
                                   '<div x-match="$.v"><p x-case="\'a\'">A</p><p x-nocase>D1</p></div>' ..
                                       '<div x-match="$.v"><p x-case="\'b\'">B</p><p x-nocase>D1</p>' ..
                                       '<p x-nocase>D2</p></div>')
    assert.is_nil(ok)
    local message = err.message.message
    assert(message:find('multiple x-nocase', 1, true) or
               message:find('x-case must not appear after x-nocase', 1, true),
           message)
end

function testcase.structural_and_control_on_the_same_element_fails()
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t',
                                   '<ul><li x-for="i = 1, 3"><span x-if=".a" x-break>x</span></li></ul>')
    assert.is_nil(ok)
    local message = err.message.message
    assert(message:find('structural.*control') or
               message:find('control.*structural'), message)
end

function testcase.duplicate_x_data_on_the_same_element_is_rejected()
    compile_error(
        '<div x-data="a: {x: 1}" x-data="b: {y: 2}"><p x-text=".a.x"></p></div>',
        'duplicate x%-data')
end

function testcase.include_not_found_has_the_requested_name()
    local err = render_error({
        {
            't',
            '<section x-include="\'no-such\'"></section>',
        },
    }, 't')
    assert.equal(err.meta.reason, 'not_found')
    assert.equal(err.meta.requested, 'no-such')
end

function testcase.include_depth_exceeded_without_a_cycle()
    local err = render_error({
        {
            'a',
            '<section x-include="\'b\'"></section>',
        },
        {
            'b',
            '<section x-include="\'c\'"></section>',
        },
        {
            'c',
            '<section x-include="\'d\'"></section>',
        },
        {
            'd',
            '<p>leaf</p>',
        },
    }, 'a', nil, {
        max_include_depth = 2,
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowIncludeError)
    assert.equal(err.meta.reason, 'depth_exceeded')
end

function testcase.runtime_error_inside_x_for_iteration()
    local err = render_error({
        {
            't',
            '<ul><li x-for="i = 1, 3"><span x-text="boom()"></span></li></ul>',
        },
    }, 't', nil, {
        helpers = {
            boom = function()
                error('boom', 0)
            end,
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'boom', true)
end

function testcase.runtime_error_inside_x_each_iteration()
    local err = render_error({
        {
            't',
            '<ul><li x-each="u in $.users"><span x-text="boom()"></span></li></ul>',
        },
    }, 't', {
        users = {
            1,
            2,
        },
    }, {
        helpers = {
            boom = function()
                error('boom-each', 0)
            end,
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'boom-each', true)
end

function testcase.x_bind_with_an_array_value_fails()
    local err = render_error({
        {
            't',
            '<div x-bind:class="$.arr"></div>',
        },
    }, 't', {
        arr = {
            'a',
            'b',
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'must be primitive', true)
end
