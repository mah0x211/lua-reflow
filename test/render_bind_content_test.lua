require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

-- The helpers used to produce the expected results with the JavaScript
-- version, written in Lua.
local HELPERS = {
    upper = function(s)
        return tostring(s):upper()
    end,
    first = function(a)
        return a[1]
    end,
    obj = function()
        return {
            a = 1,
        }
    end,
    fn = function()
        return function()
            return 1
        end
    end,
    u = function()
        return nil
    end,
    boom = function()
        error('boom', 0)
    end,
}

local function helper_names(helpers)
    local names = {}
    for name in pairs(helpers) do
        names[name] = true
    end
    return names
end

local function render(html, data, helpers)
    helpers = helpers or HELPERS
    local ir = assert(compile('page', html, 'x-', helper_names(helpers)))
    return interpret.render('page', ir, data, helpers)
end

-- JavaScript null as rendering sees it.
local NULL
assert(render('<p x-data="v: null" x-text="capture(.v)"></p>', nil, {
    capture = function(v)
        NULL = v
    end,
}))

function testcase.render_matches_the_javascript_version_for_with_bind_and_html()
    -- Expected outputs and error messages were produced by the JavaScript
    -- version with the same templates, data, and helpers.
    local cases = {
        {
            "<div>\n<img x-bind:src=\"$.src\" x-bind:alt=\"$.alt\">\n<input x-bind:disabled=\"$.dis\" x-bind:hidden=\"$.hidden\" x-bind:required=\"$.req\">\n<a x-bind:href=\"$.missing\">link</a>\n</div>",
            {
                ["src"] = "logo.png",
                ["alt"] = "Logo & tagline",
                ["dis"] = true,
                ["hidden"] = false,
                ["req"] = "",
            },
            'ok',
            "<div>\n<img src=\"logo.png\" alt=\"Logo &amp; tagline\">\n<input disabled required=\"\">\n<a>link</a>\n</div>",
        },
        {
            "<div x-html=\"$.markup\"></div>",
            {
                ["markup"] = "<strong>bold</strong>",
            },
            'ok',
            "<div><strong>bold</strong></div>",
        },
        {
            "<div x-with=\"user = { name: $.name, id: $.id }\">\n<p x-text=\"@user.name\"></p>\n<p x-text=\"@user.id\"></p>\n</div>",
            {
                ["name"] = "Alice",
                ["id"] = 42,
            },
            'ok',
            "<div>\n<p>Alice</p>\n<p>42</p>\n</div>",
        },
        {
            "<div x-with=\"a = 1, b = 'text', c = [10, 20, 30], d = { key: upper($.name) }\">\n<p x-text=\"@a\"></p>\n<p x-text=\"@b\"></p>\n<p x-text=\"first(@c)\"></p>\n<p x-text=\"@d.key\"></p>\n</div>",
            {
                ["name"] = "ada",
            },
            'ok',
            "<div>\n<p>1</p>\n<p>text</p>\n<p>10</p>\n<p>ADA</p>\n</div>",
        },
        {
            "<section x-data=\"user: { name: 'Outer' }\">\n<p x-text=\"@user.name\"></p>\n<div x-with=\"user = { name: 'Inner' }\">\n<p x-text=\"@user.name\"></p>\n</div>\n<p x-text=\"@user.name\"></p>\n</section>",
            {},
            'ok',
            "<section>\n<p>Outer</p>\n<div>\n<p>Inner</p>\n</div>\n<p>Outer</p>\n</section>",
        },
        {
            "<div x-with=\"obj = { [$.k]: 'v' }\">\n<p x-text=\"@obj.dyn\"></p>\n</div>",
            {
                ["k"] = "dyn",
            },
            'ok',
            "<div>\n<p>v</p>\n</div>",
        },
        {
            "<div><br><hr><img x-bind:src=\"$.img\"><input x-bind:type=\"'text'\"></div>",
            {
                ["img"] = "a.png",
            },
            'ok',
            "<div><br><hr><img src=\"a.png\"><input type=\"text\"></div>",
        },
        {
            "<div>\n<p x-text=\"$.payload\"></p>\n<a x-bind:href=\"$.url\">click</a>\n</div>",
            {
                ["payload"] = "<script>alert('xss')</script>",
                ["url"] = "javascript:alert(1)\"onclick=\"x",
            },
            'ok',
            "<div>\n<p>&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;</p>\n<a href=\"javascript:alert(1)&quot;onclick=&quot;x\">click</a>\n</div>",
        },
        {
            "<div x-with=\"obj = { [$.k]: 'v' }\" x-text=\"@obj.dyn\"></div>",
            {
                ["k"] = {
                    ["nested"] = true,
                },
            },
            'err',
            "object literal: computed key must evaluate to a string or number, got object",
        },
        {
            "<div x-bind:data-x=\"$.obj\"></div>",
            {
                ["obj"] = {
                    ["a"] = 1,
                },
            },
            'err',
            "x-bind:data-x: value must be primitive, got object",
        },
        {
            "<div x-bind:data-x=\"$.n\"></div>",
            {
                ["n"] = 0,
            },
            'ok',
            "<div data-x=\"0\"></div>",
        },
        {
            "<div x-bind:title=\"$.s\"></div>",
            {
                ["s"] = "",
            },
            'ok',
            "<div title=\"\"></div>",
        },
        {
            "<div x-html=\"$.obj\"></div>",
            {
                ["obj"] = {
                    ["a"] = 1,
                },
            },
            'err',
            "x-html: value must be string, null, or undefined, got object",
        },
        {
            "<div x-html=\"$.x\"></div>",
            {
                ["x"] = NULL,
            },
            'ok',
            "<div></div>",
        },
        {
            "<div x-html=\"$.missing\"></div>",
            {},
            'ok',
            "<div></div>",
        },
        {
            "<img src=\"fallback.jpg\" x-bind:src=\"$.src\">",
            {
                ["src"] = "real.jpg",
            },
            'ok',
            "<img src=\"real.jpg\">",
        },
        {
            "<img src=\"fallback.jpg\" x-bind:src=\"$.missing\">",
            {},
            'ok',
            "<img>",
        },
        {
            "<img x-bind:src=\"$.s\" x-bind:alt=\"$.a\">",
            {
                ["s"] = "x.png",
                ["a"] = "x",
            },
            'ok',
            "<img src=\"x.png\" alt=\"x\">",
        },
        {
            "<div x-data=\"a: 1\"><p x-with=\"a = .missing\"><span x-text=\".a ?? 'U'\"></span><span x-text=\"@a ?? 'U'\"></span></p></div>",
            {},
            'ok',
            "<div><p><span>U</span><span>U</span></p></div>",
        },
        {
            "<div x-data=\"a: 1\"><p x-with=\"a = null\"><span x-text=\".a ?? 'N'\"></span><span x-text=\"@a ?? 'N'\"></span></p></div>",
            {},
            'ok',
            "<div><p><span>N</span><span>N</span></p></div>",
        },
        {
            "<p x-with=\"a = 1\"><b x-with=\"a = u()\"><i x-text=\".a ?? 'U'\"></i></b><i x-text=\".a\"></i></p>",
            {},
            'ok',
            "<p><b><i>U</i></b><i>1</i></p>",
        },
        {
            "<div x-data=\"a: 1\" x-with=\"b = .a, c = @a\"><i x-text=\".b\"></i><i x-text=\"@c\"></i></div>",
            {},
            'ok',
            "<div><i>1</i><i>1</i></div>",
        },
        {
            "<p x-with=\"x = 1, y = .x\"><i x-text=\".y ?? 'Y'\"></i></p>",
            {},
            'ok',
            "<p><i>Y</i></p>",
        },
        {
            "<p x-with=\"v = $.v\" x-bind:title=\".v\" x-text=\".v\"></p>",
            {
                ["v"] = "w",
            },
            'ok',
            "<p title=\"w\">w</p>",
        },
        {
            "<p x-with=\"v = 1\" x-html=\"$.x\" x-bind:a=\".v\"></p>",
            {
                ["x"] = "<i>",
            },
            'ok',
            "<p a=\"1\"><i></p>",
        },
        {
            "<p x-with=\"v = boom()\"></p>",
            {},
            'err',
            "boom",
        },
        {
            "<p x-with=\"v = $.missing.x\"></p>",
            {},
            'err',
            "Cannot read properties of undefined (reading 'x')",
        },
        {
            "<a href=\"o\" title=\"t\" x-bind:href=\"$.u\" x-bind:data-x=\"$.n\"></a>",
            {
                ["u"] = "a\"b<&>'",
                ["n"] = 1.5,
            },
            'ok',
            "<a href=\"a&quot;b&lt;&amp;&gt;'\" title=\"t\" data-x=\"1.5\"></a>",
        },
        {
            "<a href=\"o\" title=\"t\" x-bind:href=\"$.f\"></a>",
            {
                ["f"] = false,
            },
            'ok',
            "<a title=\"t\"></a>",
        },
        {
            "<a href=\"o\" title=\"t\" x-bind:title=\"$.nul\"></a>",
            {
                ["nul"] = NULL,
            },
            'ok',
            "<a href=\"o\"></a>",
        },
        {
            "<a x-bind:hidden=\"true\" x-bind:x=\"null\" x-bind:y=\"0\" x-bind:z=\"''\"></a>",
            {},
            'ok',
            "<a hidden y=\"0\" z=\"\"></a>",
        },
        {
            "<a href=\"1\" href=\"2\" x-bind:href=\"3\"></a>",
            {},
            'ok',
            "<a href=\"3\" href=\"3\"></a>",
        },
        {
            "<a href=\"1\" x-bind:data-b=\"2\" title=\"t\" x-bind:data-a=\"3\" x-bind:title=\"4\"></a>",
            {},
            'ok',
            "<a href=\"1\" title=\"4\" data-b=\"2\" data-a=\"3\"></a>",
        },
        {
            "<a X-BIND:HREF=\"1\"></a>",
            {},
            'ok',
            "<a href=\"1\"></a>",
        },
        {
            "<a x-bind:href=\"obj()\"></a>",
            {},
            'err',
            "x-bind:href: value must be primitive, got object",
        },
        {
            "<a x-bind:href=\"$.arr\"></a>",
            {
                ["arr"] = {
                    1,
                },
            },
            'err',
            "x-bind:href: value must be primitive, got array",
        },
        {
            "<a x-bind:href=\"fn()\"></a>",
            {},
            'err',
            "x-bind:href: value must be primitive, got function",
        },
        {
            "<a x-bind:x=\"1e21\" x-bind:y=\"-0\" x-bind:z=\"0.5\" x-bind:w=\"-1.25e-7\"></a>",
            {},
            'ok',
            "<a x=\"1e+21\" y=\"0\" z=\"0.5\" w=\"-1.25e-7\"></a>",
        },
        {
            "<a x-bind:x=\"$.big\" x-bind:y=\"$.small\" x-bind:z=\"$.f\"></a>",
            {
                ["big"] = 1e+21,
                ["small"] = 1e-7,
                ["f"] = 0.1,
            },
            'ok',
            "<a x=\"1e+21\" y=\"1e-7\" z=\"0.1\"></a>",
        },
        {
            "<p x-with=\"v = 1\"><i x-bind:x=\"boom()\"></i></p>",
            {},
            'err',
            "boom",
        },
        {
            "<div x-html=\"1\"></div>",
            {},
            'err',
            "x-html: value must be string, null, or undefined, got number",
        },
        {
            "<div x-html=\"true\"></div>",
            {},
            'err',
            "x-html: value must be string, null, or undefined, got boolean",
        },
        {
            "<p x-html=\"obj()\"></p>",
            {},
            'err',
            "x-html: value must be string, null, or undefined, got object",
        },
        {
            "<p x-html=\"$.arr\"></p>",
            {
                ["arr"] = {
                    1,
                },
            },
            'err',
            "x-html: value must be string, null, or undefined, got array",
        },
        {
            "<div x-html=\"$.h\">child<b>x</b></div>",
            {
                ["h"] = "<i>raw</i>&amp;'",
            },
            'ok',
            "<div><i>raw</i>&amp;'</div>",
        },
        {
            "<br x-html=\"'<i>'\">",
            {},
            'ok',
            "<br><i>",
        },
        {
            "<input x-bind:value=\"$.v\" x-text=\"$.t\">",
            {
                ["v"] = "a'b",
                ["t"] = "c'd",
            },
            'ok',
            "<input value=\"a'b\">c&#39;d",
        },
    }
    for _, case in ipairs(cases) do
        local html, data, kind, expected = case[1], case[2], case[3], case[4]
        local out, err = render(html, data)
        if kind == 'ok' then
            assert.equal(out, expected, html)
        else
            assert.is_nil(out, html)
            assert.equal(err.message.message, expected, html)
        end
    end
end

function testcase.render_binds_x_with_values_unchanged()
    -- A table returned by a helper reaches another helper through x-with.
    local made = {}
    local seen
    assert.equal(render('<p x-with="v = make()" x-text="keep(@v)"></p>', nil, {
        make = function()
            return made
        end,
        keep = function(v)
            seen = v
            return 'ok'
        end,
    }), '<p>ok</p>')
    assert.rawequal(seen, made)

    -- A binding to nil hides the outer binding, and the lookup gives nil
    -- rather than any stored stand-in.
    local got = {}
    assert(render('<div x-data="a: 1"><p x-with="a = $.missing">' ..
                      '<i x-text="keep(@a)"></i><i x-text="keep(.a)"></i>' ..
                      '</p><b x-text="keep(@a)"></b></div>', nil, {
        keep = function(...)
            got[#got + 1] = {
                n = select('#', ...),
                v = (...),
            }
        end,
    }))
    assert.equal(#got, 3)
    assert.equal(got[1].n, 1)
    assert.is_nil(got[1].v)
    assert.is_nil(got[2].v)
    assert.equal(got[3].v, 1)

    -- A binding to null is the null sentinel.
    assert(render('<p x-with="v = null" x-text="keep(.v)"></p>', nil, {
        keep = function(v)
            seen = v
        end,
    }))
    assert.rawequal(seen, NULL)
end

function testcase.render_formats_bound_numbers_like_x_text()
    local data = {
        i = 12345678901,
        f = 0.1,
        n = -0.0,
    }
    if math.maxinteger then
        data.i = math.maxinteger
    end
    local out = assert(render(
                           '<a x-bind:i="$.i" x-bind:f="$.f" x-bind:n="$.n"></a>' ..
                               '<p x-text="$.i"></p>', data))
    local i = tostring(data.i):gsub('%.0$', '')
    assert.equal(out, '<a i="' .. i .. '" f="0.1" n="0"></a><p>' .. i .. '</p>')
end

function testcase.render_reports_the_directive_of_a_wrong_value()
    local out, err = render('<div>\n  <a x-bind:data-x="$.v"></a>\n</div>', {
        v = {},
    })
    assert.is_nil(out)
    assert.equal(err.message.message,
                 'x-bind:data-x: value must be primitive, got object')
    assert.equal(err.meta.element, '<a>')
    assert.equal(err.meta.line, 2)
    assert.equal(err.meta.column, 3)
    assert.equal(err.meta.directive, 'x-bind:data-x')
    assert.equal(err.meta.attribute, 'data-x')
    assert.equal(err.meta.expression, {
        source = '$.v',
    })

    out, err = render('<p x-html="$.v"></p>', {
        v = 1,
    })
    assert.is_nil(out)
    assert.equal(err.meta.directive, 'x-html')
    assert.is_nil(err.meta.attribute)
    assert.equal(err.meta.expression, {
        source = '$.v',
    })

    -- An error while evaluating x-with or x-bind names the expression only.
    for _, html in ipairs({
        '<p x-with="v = boom()"></p>',
        '<p x-bind:title="boom()"></p>',
    }) do
        out, err = render(html)
        assert.is_nil(out)
        assert.equal(err.message.message, 'boom')
        assert.is_nil(err.meta.directive)
        assert.is_nil(err.meta.attribute)
        assert.equal(err.meta.expression, {
            source = 'boom()',
        })
        assert.not_nil(errorlib.unwrap(err))
    end
end

function testcase.render_restores_the_scope_after_x_with()
    local depth = 3000
    local html = string.rep('<div x-with="d = 1">', depth) ..
                     '<i x-text=".d"></i>' .. string.rep('</div>', depth) ..
                     '<b x-text=".d"></b>'
    assert.equal(render(html),
                 string.rep('<div>', depth) .. '<i>1</i>' ..
                     string.rep('</div>', depth) .. '<b></b>')

    -- Void elements and elements with content close their frames too.
    assert.equal(render(
                     '<input x-with="v = 1" x-bind:value=".v"><p x-with="v = 2" x-text=".v"></p>' ..
                         '<p x-with="v = 3" x-html="\'h\'"></p><i x-text=".v"></i>'),
                 '<input value="1"><p>2</p><p>h</p><i></i>')
end

function testcase.render_binds_many_attributes_of_one_element()
    -- The values are kept on the Lua stack until the start tag is written.
    -- Lua 5.1 and LuaJIT limit the stack of a C function to about 8000
    -- slots, so the larger count is either rendered or reported.
    for _, n in ipairs({
        300,
        9000,
    }) do
        local parts = {}
        local expected = {}
        for i = 1, n do
            parts[i] = 'x-bind:a' .. i .. '="' .. i .. '"'
            expected[i] = 'a' .. i .. '="' .. i .. '"'
        end
        local out, err = render('<p ' .. table.concat(parts, ' ') .. '></p>')
        if out or n == 300 then
            assert.equal(out, '<p ' .. table.concat(expected, ' ') .. '></p>')
        else
            assert.equal(err.message.message,
                         'too many x-bind directives on one element')
            assert.equal(err.meta.element, '<p>')
        end
    end
end

function testcase.render_reports_out_of_memory_in_with_bind_and_html()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local ir = assert(compile('oom',
            '<div x-with="a = {b: text()}, c = [1, 2]" x-bind:title="@a.b">' ..
            '<p x-bind:data-n="first(@c)" x-html="@a.b"></p></div>',
            'x-', { text = true, first = true }))
        local text = string.rep('abcdefgh', 64)
        local helpers = {
            text = function() return text end,
            first = function(a) return a[1] end,
        }
        local expected = '<div title="' .. text .. '"><p data-n="1">' ..
                         text .. '</p></div>'
        local kinds = {}

        -- Small steps reach both the pool allocations and the Lua
        -- allocations made between them. When so little memory is left
        -- that even the error cannot be created, the memory error is
        -- raised instead.
        for extra = 1024, 1024 * 1024, 16 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, out, err = pcall(interpret.render, 'oom', ir, nil,
                                           helpers)
            memlimit.maxsize(0)
            if not called then
                assert(tostring(out):find('not enough memory', 1, true),
                       tostring(out))
            elseif out then
                kinds.success = out == expected
                break
            else
                assert(errorlib.typeof(err) == errors.ERUNTIME)
                assert(err.message.message == 'out of memory while rendering',
                       err.message.message)
                kinds.pool = kinds.pool or errorlib.unwrap(err) == nil
            end
        end
        return kinds
    ]])
    assert.is_true(ok, tostring(kinds))
    assert.equal(kinds, {
        pool = true,
        success = true,
    })
end
