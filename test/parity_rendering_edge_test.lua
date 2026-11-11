-- Ports test/unit/rendering-edge.test.js of the JavaScript version. Its last
-- case asserts nothing and is listed in test/PARITY.md.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local cjson = require('cjson')
local reflow = require('reflow')

local function render(html, data, config)
    local out, err = reflow.render(html, data, config)
    assert.not_nil(out, err)
    return out
end

local function render_error(html, data, config)
    local out, err = reflow.render(html, data, config)
    assert.is_nil(out)
    return err
end

function testcase.compile_rejects_a_name_that_is_not_a_non_empty_string()
    local engine = assert(reflow.new())
    for _, name in ipairs({
        '',
        cjson.null,
    }) do
        local ok, err = engine:compile(name, '<p>x</p>')
        assert.is_nil(ok)
        assert.match(err.message.message, 'non-empty string', true)
    end
end

function testcase.compile_rejects_html_that_is_not_a_string()
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t', 123)
    assert.is_nil(ok)
    assert.match(err.message.message, 'html must be a string', true)
end

function testcase.x_bind_rejects_an_object_value()
    local err = render_error('<div x-bind:data-x="$.obj"></div>', {
        obj = {
            a = 1,
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'must be primitive', true)
end

function testcase.x_bind_allows_zero()
    assert.equal(render('<div x-bind:data-x="$.n"></div>', {
        n = 0,
    }), '<div data-x="0"></div>')
end

function testcase.x_bind_allows_an_empty_string()
    assert.equal(render('<div x-bind:title="$.s"></div>', {
        s = '',
    }), '<div title=""></div>')
end

function testcase.x_html_rejects_a_value_that_is_not_a_string()
    local err = render_error('<div x-html="$.obj"></div>', {
        obj = {
            a = 1,
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'must be string', true)
end

function testcase.x_html_with_null_or_undefined_writes_nothing()
    assert.equal(render('<div x-html="$.x"></div>', {
        x = cjson.null,
    }), '<div></div>')
    assert.equal(render('<div x-html="$.missing"></div>', {}), '<div></div>')
end

function testcase.x_text_with_undefined_writes_nothing()
    assert.equal(render('<p x-text="$.missing"></p>', {}), '<p></p>')
end

function testcase.x_text_writes_a_number_as_a_string()
    assert.equal(render('<p x-text="$.n"></p>', {
        n = 42,
    }), '<p>42</p>')
end

function testcase.x_bind_overrides_an_existing_attribute()
    assert.equal(render('<img src="fallback.jpg" x-bind:src="$.src">', {
        src = 'real.jpg',
    }), '<img src="real.jpg">')
end

function testcase.x_bind_omits_the_attribute_when_undefined()
    assert.equal(render('<img src="fallback.jpg" x-bind:src="$.missing">', {}),
                 '<img>')
end

function testcase.x_match_without_a_matching_case_writes_an_empty_body()
    assert.equal(render('<div x-match="$.v"><p x-case="\'a\'">A</p></div>', {
        v = 'z',
    }), '<div></div>')
end

function testcase.x_for_descending_inclusive()
    assert.equal(render('<ul><li x-for="i = 3, 1, -1" x-text=".i"></li></ul>'),
                 '<ul><li>3</li><li>2</li><li>1</li></ul>')
end

function testcase.x_for_start_equal_to_stop_iterates_once()
    assert.equal(render('<ul><li x-for="i = 5, 5" x-text=".i"></li></ul>'),
                 '<ul><li>5</li></ul>')
end

function testcase.unconditional_x_break_stops_immediately()
    assert.equal(render(
                     '<ul><li x-for="i = 1, 5"><span x-text=".i"></span><br x-break></li></ul>'),
                 '<ul><li><span>1</span></li></ul>')
end

function testcase.elseif_chain_selects_the_first_true()
    assert.equal(render('<div><template x-if="$.a">A</template>' ..
                            '<template x-elseif="$.b">B</template>' ..
                            '<template x-elseif="$.c">C</template></div>', {
        a = false,
        b = false,
        c = true,
    }), '<div><template>C</template></div>')
end

function testcase.x_include_with_a_value_that_is_not_a_string_fails()
    local err = render_error('<section x-include="$.n"></section>', {
        n = 42,
    })
    assert.match(err.message.message, 'must evaluate to a template name', true)
end

function testcase.render_keeps_comments()
    assert.match(render('<div><!-- keep --><p>x</p></div>'), '<!-- keep -->',
                 true)
end

function testcase.x_elseif_joins_the_chain_across_white_space()
    local out = render('<div>\n  <template x-if="$.a">A</template>\n' ..
                           '  <template x-elseif="$.b">B</template>\n</div>', {
        a = false,
        b = true,
    })
    assert.equal(out:gsub('%s+', ''), '<div><template>B</template></div>')
end

function testcase.x_data_under_x_if()
    local html = '<template x-if="$.show"><div x-data="c: {x: 10}">' ..
                     '<p x-text=".c.x"></p></div></template>'
    assert.equal(render(html, {
        show = true,
    }), '<template><div><p>10</p></div></template>')
    assert.equal(render(html, {
        show = false,
    }), '')
end

function testcase.nested_x_data_shadows_the_outer_one()
    assert.equal(render('<div x-data="a: {v: 1}"><div x-data="a: {v: 2}">' ..
                            '<p x-text=".a.v"></p></div></div>'),
                 '<div><div><p>2</p></div></div>')
end

function testcase.a_helper_error_is_a_runtime_error_with_its_cause()
    local cause = errorlib.new('boom-msg')
    local err = render_error('<p x-text="boom()"></p>', nil, {
        helpers = {
            boom = function()
                error(cause)
            end,
        },
    })
    assert.rawequal(errorlib.typeof(err), reflow.ReflowRuntimeError)
    assert.match(err.message.message, 'boom-msg', true)
    assert.rawequal(errorlib.unwrap(err), cause)
end

function testcase.x_bind_on_a_void_element()
    assert.equal(render('<img x-bind:src="$.s" x-bind:alt="$.a">', {
        s = 'x.png',
        a = 'x',
    }), '<img src="x.png" alt="x">')
end

function testcase.x_text_on_a_void_element()
    assert.match(render('<img x-text="$.a">', {
        a = 'x',
    }), '^<img', false)
end

function testcase.render_once_with_helpers()
    assert.equal(render('<p x-text="upper($.n)"></p>', {
        n = 'hi',
    }, {
        helpers = {
            upper = function(s)
                return tostring(s):upper()
            end,
        },
    }), '<p>HI</p>')
end

function testcase.render_file_once_uses_the_loader()
    local out, err = reflow.render_file('fake/path', {
        x = 'hello',
    }, {
        loader = function(path)
            return '<p>' .. path .. ': <span x-text="$.x"></span></p>'
        end,
    })
    assert.not_nil(out, err)
    assert.match(out, '<span>hello</span>', true)
end
