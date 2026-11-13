require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

local function count_keys(t)
    local n = 0
    for _ in pairs(t) do
        n = n + 1
    end
    return n
end

local function type_name(v)
    if v == nil then
        return 'undefined'
    elseif type(v) == 'userdata' then
        return 'null'
    elseif type(v) == 'table' then
        return #v > 0 and 'array' or 'object'
    end
    return type(v)
end

-- The helpers of the JavaScript fixtures, written in Lua.
local HELPERS = {
    upper = function(s)
        return tostring(s):upper()
    end,
    concat = function(...)
        local parts = {}
        for i = 1, select('#', ...) do
            parts[i] = tostring((select(i, ...)))
        end
        return table.concat(parts)
    end,
    now = function()
        return 'now-value'
    end,
    boom = function()
        error('boom', 0)
    end,
    ty = type_name,
    count = count_keys,
    get = function(o, k)
        return o[k]
    end,
    first = function(a)
        return a[1]
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

-- Return JavaScript null as rendering sees it.
local function null_value()
    local value
    assert(render('<p x-data="v: null" x-text="capture(.v)"></p>', nil, {
        capture = function(v)
            value = v
        end,
    }))
    return value
end

local function fixture_data()
    return {
        n = 0,
        s = '',
        z = '0',
        ten = '10',
        five = 5,
        items = {
            1,
            2,
        },
        str = 'abc',
        f = 1.5,
        big = 1e21,
        small = 1e-7,
        t = true,
        nul = null_value(),
        user = {
            name = 'Alice',
            tags = {
                'a',
                'b',
            },
        },
        title = 'hello <world>',
        payload = "<script>alert('xss')</script>",
        k = 'key',
        meta = {
            key = 'mk',
        },
        deep = {
            a = {
                b = {
                    c = {
                        d = 'deep',
                    },
                },
            },
        },
        emoji = '\240\159\152\128',
        pua = '\238\128\128',
    }
end

function testcase.render_matches_the_javascript_version_for_expressions()
    -- Expected outputs and error messages were produced by the JavaScript
    -- version with the same data and helpers.
    local cases = {
        {
            '<p x-text="\'hi\'"></p>',
            'ok',
            '<p>hi</p>',
        },
        {
            '<p x-text=\'"hi"\'></p>',
            'ok',
            '<p>hi</p>',
        },
        {
            '<p x-text="\'a\\nb\\tc\\\\d\\\'e\'"></p>',
            'ok',
            '<p>a\nb\tc\\d&#39;e</p>',
        },
        {
            '<p x-text="42"></p>',
            'ok',
            '<p>42</p>',
        },
        {
            '<p x-text="-1"></p>',
            'ok',
            '<p>-1</p>',
        },
        {
            '<p x-text="3.14"></p>',
            'ok',
            '<p>3.14</p>',
        },
        {
            '<p x-text="1e3"></p>',
            'ok',
            '<p>1000</p>',
        },
        {
            '<p x-text="1e-3"></p>',
            'ok',
            '<p>0.001</p>',
        },
        {
            '<p x-text="1.5e2"></p>',
            'ok',
            '<p>150</p>',
        },
        {
            '<p x-text="true"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="false"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="null">c</p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.user.name"></p>',
            'ok',
            '<p>Alice</p>',
        },
        {
            '<div x-data="conf: {locale: \'ja\'}, item: 5"><i x-text="@conf.locale"></i><i x-text=".conf.locale"></i><i x-text=".item"></i></div>',
            'ok',
            '<div><i>ja</i><i>ja</i><i>5</i></div>',
        },
        {
            '<p x-text=".missing"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="@missing"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.missing"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<div x-data="user: {name: \'outer\'}"><div x-data="user: {name: \'inner\'}"><i x-text=".user.name"></i><i x-text="@user.name"></i></div><b x-text=".user.name"></b></div>',
            'ok',
            '<div><div><i>inner</i><i>inner</i></div><b>outer</b></div>',
        },
        {
            '<div x-data="a: 1"><p x-data="a: null" x-text=".a ?? \'hidden\'"></p></div>',
            'ok',
            '<div><p>hidden</p></div>',
        },
        {
            '<div x-data="a: {x: 1, y: 2}, b: {name: \'hello\'}"><p x-text=".a.x"></p><p x-text="@a.y"></p><p x-text=".b.name"></p></div>',
            'ok',
            '<div><p>1</p><p>2</p><p>hello</p></div>',
        },
        {
            '<div x-data="a: 1"><p x-text=".a"></p></div><p x-text=".a"></p>',
            'ok',
            '<div><p>1</p></div><p></p>',
        },
        {
            '<p x-data="x: \'self\'" x-text=".x"></p>',
            'ok',
            '<p>self</p>',
        },
        {
            '<p x-text=".deep" x-data="deep: {a: {b: {c: {d: \'deep\'}}}}"></p>',
            'err',
            'x-text: value must be primitive, got object',
        },
        {
            '<p x-text=".deep.a.b.c.d" x-data="deep: {a: {b: {c: {d: \'deep\'}}}}"></p>',
            'ok',
            '<p>deep</p>',
        },
        {
            '<p x-text="$.nul?.foo"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.user?.name"></p>',
            'ok',
            '<p>Alice</p>',
        },
        {
            '<p x-text="$.missing?.x"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.user.missing?.c"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.missing?.x.y"></p>',
            'err',
            'Cannot read properties of undefined (reading \'y\')',
        },
        {
            '<p x-text="$.five.x"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.t.x"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.five == 5"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five != 5"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="1 == \'1\'"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="$.nul == null"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.missing == null"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="$.missing != null"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="\'a\' == \'a\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="true == true"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.user == $.user"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="1 == 1.0"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five < 10"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five > 10"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="$.five <= 5"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five >= 6"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="\'a\' < \'b\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="\'b\' <= \'a\'"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="\'abc\' < \'abd\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="\'ab\' < \'abc\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="\'10\' > \'9\'"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="$.emoji > $.pua"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="\'Z\' < \'a\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="-1 < 0"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five < 10 && $.str == \'abc\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.five > 100 || $.str == \'y\'"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="!($.five == 5)"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="!\'\'"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="!\'0\'"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="!$.items"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="!$.missing"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="!$.nul"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="!0"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="!1"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="0 && 1"></p>',
            'ok',
            '<p>0</p>',
        },
        {
            '<p x-text="1 && 0"></p>',
            'ok',
            '<p>0</p>',
        },
        {
            '<p x-text="\'\' || \'d\'"></p>',
            'ok',
            '<p>d</p>',
        },
        {
            '<p x-text="\'x\' || \'d\'"></p>',
            'ok',
            '<p>x</p>',
        },
        {
            '<p x-text="$.n && boom()"></p>',
            'ok',
            '<p>0</p>',
        },
        {
            '<p x-text="$.t || boom()"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="$.missing && 1"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.nul || \'fallback\'"></p>',
            'ok',
            '<p>fallback</p>',
        },
        {
            '<p x-text="$.nul ?? \'default\'"></p>',
            'ok',
            '<p>default</p>',
        },
        {
            '<p x-text="$.n ?? 99"></p>',
            'ok',
            '<p>0</p>',
        },
        {
            '<p x-text="$.s ?? \'x\'"></p>',
            'ok',
            '<p></p>',
        },
        {
            '<p x-text="$.missing ?? \'m\'"></p>',
            'ok',
            '<p>m</p>',
        },
        {
            '<p x-text="$.five ?? boom()"></p>',
            'ok',
            '<p>5</p>',
        },
        {
            '<p x-text="false ?? 1"></p>',
            'ok',
            '<p>false</p>',
        },
        {
            '<p x-text="$.t ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>on</p>',
        },
        {
            '<p x-text="$.n ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>off</p>',
        },
        {
            '<p x-text="$.z ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>on</p>',
        },
        {
            '<p x-text="$.s ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>off</p>',
        },
        {
            '<p x-text="$.items ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>on</p>',
        },
        {
            '<p x-text="$.missing ? \'on\' : \'off\'"></p>',
            'ok',
            '<p>off</p>',
        },
        {
            '<p x-text="$.t ? \'a\' : boom()"></p>',
            'ok',
            '<p>a</p>',
        },
        {
            '<p x-text="$.n ? boom() : \'b\'"></p>',
            'ok',
            '<p>b</p>',
        },
        {
            '<p x-text="$.t ? $.n ? \'x\' : \'y\' : \'z\'"></p>',
            'ok',
            '<p>y</p>',
        },
        {
            '<p x-text="(.a == 1) && (.b == 2)" x-data="a: 1, b: 2"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="upper($.user.name)"></p>',
            'ok',
            '<p>ALICE</p>',
        },
        {
            '<p x-text="concat(\'Hello, \', upper($.user.name), \'!\')"></p>',
            'ok',
            '<p>Hello, ALICE!</p>',
        },
        {
            '<p x-text="now()"></p>',
            'ok',
            '<p>now-value</p>',
        },
        {
            '<p x-text="ty($.nul)"></p>',
            'ok',
            '<p>null</p>',
        },
        {
            '<p x-text="ty($.missing)"></p>',
            'ok',
            '<p>undefined</p>',
        },
        {
            '<p x-text="ty($.items)"></p>',
            'ok',
            '<p>array</p>',
        },
        {
            '<p x-text="ty($.user)"></p>',
            'ok',
            '<p>object</p>',
        },
        {
            '<p x-text="count($.items)"></p>',
            'ok',
            '<p>2</p>',
        },
        {
            '<p x-text="get($.user, \'name\')"></p>',
            'ok',
            '<p>Alice</p>',
        },
        {
            '<p x-text="ty({})"></p>',
            'ok',
            '<p>object</p>',
        },
        {
            '<p x-text="count({a: 1, \'b\': 2, 3: 4})"></p>',
            'ok',
            '<p>3</p>',
        },
        {
            '<p x-text="get({a: 1, \'b\': 2, 3: 4}, \'3\')"></p>',
            'ok',
            '<p>4</p>',
        },
        {
            '<p x-text="get({a: 1, a: 2}, \'a\')"></p>',
            'ok',
            '<p>2</p>',
        },
        {
            '<p x-text="get({[$.k]: \'val\'}, \'key\')"></p>',
            'ok',
            '<p>val</p>',
        },
        {
            '<p x-text="get({[$.meta.key]: \'v\'}, \'mk\')"></p>',
            'ok',
            '<p>v</p>',
        },
        {
            '<p x-text="get({[\'s\']: 1, [2]: \'two\'}, \'2\')"></p>',
            'ok',
            '<p>two</p>',
        },
        {
            '<p x-text="get({[-1]: \'neg\'}, \'-1\')"></p>',
            'ok',
            '<p>neg</p>',
        },
        {
            '<p x-text="get({[1.5]: \'f\'}, \'1.5\')"></p>',
            'ok',
            '<p>f</p>',
        },
        {
            '<p x-text="get({a: 1, [$.k]: 2, key: 3}, \'key\')"></p>',
            'ok',
            '<p>3</p>',
        },
        {
            '<p x-text="get({user: {name: upper($.user.name)}}, \'user\').name"></p>',
            'ok',
            '<p>ALICE</p>',
        },
        {
            '<p x-text="count([1, $.five, true, null])"></p>',
            'ok',
            '<p>4</p>',
        },
        {
            '<p x-text="first([upper(\'x\')])"></p>',
            'ok',
            '<p>X</p>',
        },
        {
            '<p x-text="count([1, 2,])"></p>',
            'ok',
            '<p>2</p>',
        },
        {
            '<div x-data="k: \'dk\'"><p x-text="get({[.k]: 1, [@k]: 2}, \'dk\')"></p></div>',
            'ok',
            '<div><p>2</p></div>',
        },
        {
            '<p x-text="$.title"></p>',
            'ok',
            '<p>hello &lt;world&gt;</p>',
        },
        {
            '<p x-text="$.payload"></p>',
            'ok',
            '<p>&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;</p>',
        },
        {
            '<p x-text="$.f"></p>',
            'ok',
            '<p>1.5</p>',
        },
        {
            '<p x-text="$.big"></p>',
            'ok',
            '<p>1e+21</p>',
        },
        {
            '<p x-text="$.small"></p>',
            'ok',
            '<p>1e-7</p>',
        },
        {
            '<p x-text="$.n"></p>',
            'ok',
            '<p>0</p>',
        },
        {
            '<p x-text="$.t"></p>',
            'ok',
            '<p>true</p>',
        },
        {
            '<p x-text="\'<a href=&quot;x&quot;>&amp;\\\'</a>\'"></p>',
            'ok',
            '<p>&lt;a href=&amp;quot;x&amp;quot;&gt;&amp;amp;&#39;&lt;/a&gt;</p>',
        },
        {
            '<p x-text="\'&quot;\'"></p>',
            'ok',
            '<p>&amp;quot;</p>',
        },
        {
            '<p x-text="\'x\'"><b>child</b></p>',
            'ok',
            '<p>x</p>',
        },
        {
            '<br x-text="\'x\'">',
            'ok',
            '<br>x',
        },
        {
            '<img src="a" x-text="\'t\'">',
            'ok',
            '<img src="a">t',
        },
        {
            '<p x-text="$.emoji"></p>',
            'ok',
            '<p>😀</p>',
        },
        {
            '<p x-text="$.missing.x"></p>',
            'err',
            'Cannot read properties of undefined (reading \'x\')',
        },
        {
            '<p x-text="$.nul.x"></p>',
            'err',
            'Cannot read properties of null (reading \'x\')',
        },
        {
            '<p x-text="$.missing?.x.y.z"></p>',
            'err',
            'Cannot read properties of undefined (reading \'y\')',
        },
        {
            '<p x-text="$.user"></p>',
            'err',
            'x-text: value must be primitive, got object',
        },
        {
            '<p x-text="$.items"></p>',
            'err',
            'x-text: value must be primitive, got array',
        },
        {
            '<p x-text="{}"></p>',
            'err',
            'x-text: value must be primitive, got object',
        },
        {
            '<p x-text="boom()"></p>',
            'err',
            'boom',
        },
        {
            '<p x-text="get({[$.nul]: 1}, \'a\')"></p>',
            'err',
            'object literal: computed key must evaluate to a string or number, got null',
        },
        {
            '<p x-text="get({[$.missing]: 1}, \'a\')"></p>',
            'err',
            'object literal: computed key must evaluate to a string or number, got undefined',
        },
        {
            '<p x-text="get({[$.t]: 1}, \'a\')"></p>',
            'err',
            'object literal: computed key must evaluate to a string or number, got boolean',
        },
        {
            '<p x-text="get({[$.items]: 1}, \'a\')"></p>',
            'err',
            'object literal: computed key must evaluate to a string or number, got array',
        },
        {
            '<p x-text="get({[$.user]: 1}, \'a\')"></p>',
            'err',
            'object literal: computed key must evaluate to a string or number, got object',
        },
        {
            '<div x-data="a: 1"><span x-text="$.missing.x"></span></div>',
            'err',
            'Cannot read properties of undefined (reading \'x\')',
        },
    }
    local data = fixture_data()
    for _, case in ipairs(cases) do
        local html, kind, expected = case[1], case[2], case[3]
        local out, err = render(html, data)
        if kind == 'ok' then
            assert.equal(out, expected, html)
        else
            assert.is_nil(out, html)
            assert.equal(err.message.message, expected, html)
        end
    end
end

function testcase.render_keeps_the_approved_differences_from_the_javascript_version()
    local data = fixture_data()

    -- An empty array and an empty object are both an empty table.
    assert.equal(render('<p x-text="ty([])"></p>', data), '<p>object</p>')
    local out, err = render('<p x-text="[]"></p>', data)
    assert.is_nil(out)
    assert.equal(err.message.message,
                 'x-text: value must be primitive, got object')

    -- Only two numbers or two strings can be ordered; JavaScript converts
    -- the operands instead.
    for html, message in pairs({
        ['<p x-text="$.ten > 9"></p>'] = 'cannot compare string with number using ">"',
        ['<p x-text="null < 1"></p>'] = 'cannot compare null with number using "<"',
        ['<p x-text="$.missing < 1"></p>'] = 'cannot compare undefined with number using "<"',
        ['<p x-text="true >= 0"></p>'] = 'cannot compare boolean with number using ">="',
        ['<p x-text="$.items <= 4"></p>'] = 'cannot compare array with number using "<="',
        ['<p x-text="$.user < $.user"></p>'] = 'cannot compare object with object using "<"',
    }) do
        out, err = render(html, data)
        assert.is_nil(out, html)
        assert.equal(err.message.message, message, html)
    end
    -- A missing value is undefined, which is not equal to null, so the
    -- guard uses ?? to cover both.
    assert.equal(render('<p x-text="$.nul != null && $.nul < 1"></p>', data),
                 '<p>false</p>')
    assert.equal(render(
                     '<p x-text="($.missing ?? null) != null && $.missing < 1"></p>',
                     data), '<p>false</p>')

    -- A function is not a primitive value; JavaScript prints its source.
    out, err = render('<p x-text="$.fn"></p>', {
        fn = print,
    })
    assert.is_nil(out)
    assert.equal(err.message.message,
                 'x-text: value must be primitive, got function')

    -- nil cannot be stored in a table: an undefined item leaves a hole and
    -- an undefined value leaves no key.
    local captured
    assert(render('<p x-text="keep([$.missing, 1], {k: $.missing})"></p>', {}, {
        keep = function(a, o)
            captured = {
                a,
                o,
            }
        end,
    }))
    assert.is_nil(captured[1][1])
    assert.equal(captured[1][2], 1)
    assert.equal(captured[2], {})
end

function testcase.render_represents_javascript_null_as_the_null_light_userdata()
    local null = null_value()
    assert.equal(type(null), 'userdata')

    -- Static null values and the null literal are the same value, and a
    -- null binding hides an outer binding of the same name.
    local seen = {}
    assert.equal(render(
                     '<div x-data="a: 1"><p x-data="a: null, b: [1, null]" x-text="see(.a, .b, null)"></p></div>',
                     nil, {
            see = function(a, b, c)
                seen = {
                    a,
                    b,
                    c,
                }
                return 'x'
            end,
        }), '<div><p>x</p></div>')
    assert.rawequal(seen[1], null)
    assert.equal(#seen[2], 2)
    assert.rawequal(seen[2][2], null)
    assert.rawequal(seen[3], null)

    -- The same value given as data is null, and nil is undefined.
    local data = {
        v = null,
    }
    assert.equal(render('<p x-text="$.v == null"></p>', data), '<p>true</p>')
    assert.equal(render('<p x-text="$.w == null"></p>', data), '<p>false</p>')
    assert.equal(render('<p x-text="$.v"></p>', data), '<p></p>')
    assert.equal(render('<p x-text="$.v ?? \'d\'"></p>', data), '<p>d</p>')
    assert.equal(render('<p x-text="$.v?.x"></p>', data), '<p></p>')
end

function testcase.render_passes_lua_values_to_helpers_unchanged()
    local data = {
        list = {
            1,
            2,
        },
        object = setmetatable({}, {
            __index = function(_, k)
                return 'meta:' .. k
            end,
        }),
    }
    local returned = {}
    local got = {}
    local helpers = {
        give = function()
            return returned
        end,
        take = function(...)
            got = {
                n = select('#', ...),
                ...,
            }
            return 'ok', 'ignored'
        end,
    }

    assert.equal(render('<p x-text="take($.list, give(), $.missing, 1)"></p>',
                        data, helpers), '<p>ok</p>')
    assert.equal(got.n, 4)
    assert.rawequal(got[1], data.list)
    assert.rawequal(got[2], returned)
    assert.is_nil(got[3])
    assert.equal(got[4], 1)

    -- Member access honors __index; it reads nothing from other values.
    assert.equal(render('<p x-text="$.object.name"></p>', data, helpers),
                 '<p>meta:name</p>')
    assert.equal(render('<p x-text="ty($.list.length)"></p>', data),
                 '<p>undefined</p>')

    -- A helper that returns nothing gives undefined.
    assert.equal(render('<p x-text="none()"></p>', nil, {
        none = function()
        end,
    }), '<p></p>')
end

function testcase.render_short_circuits_without_calling_helpers()
    local calls = 0
    local helpers = {
        hit = function()
            calls = calls + 1
            return true
        end,
    }
    for _, html in ipairs({
        '<p x-text="false && hit()"></p>',
        '<p x-text="1 || hit()"></p>',
        '<p x-text="0 ?? hit()"></p>',
        '<p x-text="true ? 1 : hit()"></p>',
        '<p x-text="false ? hit() : 1"></p>',
    }) do
        assert(render(html, nil, helpers))
    end
    assert.equal(calls, 0)
end

function testcase.render_formats_numbers_like_javascript()
    -- Each string is what JavaScript's String(n) gives for the number it
    -- denotes.
    local cases = {
        '0.30000000000000004',
        '0.1',
        '0.3333333333333333',
        '0.6666666666666666',
        '100',
        '123456789',
        '100000000000000000000',
        '1e+21',
        '1.5e+21',
        '123456789012345680000',
        '0.000001',
        '1e-7',
        '1.5e-7',
        '0.000001234',
        '5e-324',
        '2.2250738585072014e-308',
        '1.7976931348623157e+308',
        '9007199254740992',
        '9007199254740994',
        '0.5',
        '1.25',
        '-1.5',
        '-0.001',
        '-1e-7',
        '-1e+21',
        '4.35',
        '0.3',
        '1.1',
        '2.675',
        '1e+100',
        '1.2345e-100',
        '123.456',
        '999999999999999900000',
        '1e+22',
        '0.00001',
        '12345678.9',
        '3.141592653589793',
        '2.718281828459045',
        '1.0000000000000002',
        '0.9999999999999999',
        '4294967296',
        '4294967295.5',
        '-123456789.98765433',
        '-5.875760529772087e+95',
        '995.881',
        '5.584338502395872e+110',
        '883.172',
        '-7.752867821013646e+127',
        '337.742',
        '-1.8345381394653948e+181',
        '233.762',
        '-6.622005677285249e-180',
        '843.987',
        '-4.583422003613619e-240',
        '366.013',
        '-1.5154931239593865e-136',
        '870.765',
        '-2.6590216616526243e-50',
        '172.727',
        '4.04242423086861e-211',
        '504.497',
        '-4.187252767707036e+291',
        '225.301',
        '-5.6126588314859435e-40',
        '307.869',
        '-1.6787400416245804e+22',
        '111.067',
        '-1.457574397843945e+55',
        '399.591',
        '1.5929862354642346e+123',
        '400.386',
        '5.544015642604037e-51',
        '22.794',
        '-1.8697065702102448e-153',
        '5.493',
        '2.783180171803567e-132',
        '218.007',
        '-1.1893046736747896e+187',
        '848.052',
        '1.0027189396265551e+70',
        '554.075',
        '-3.5781354752811743e-32',
        '291.844',
        '6.065220953945735e-8',
        '519.183',
        '3.200864358889895e+250',
        '639.819',
        '-8.7720010866446e+91',
        '685.57',
        '-1.8068806773846568e-134',
        '492.832',
        '-2.1584326423352645e-191',
        '862.381',
        '4.896675826756272e+44',
        '934.199',
        '8.72828210726693e-255',
        '804.35',
        '8.603370323764422e-118',
        '804.653',
        '1.1868169429646182e-56',
        '322.501',
        '-3.666767810367904e+128',
        '997.305',
        '3.7072190686580824e+56',
        '5.694',
        '-5.044699046638794e-152',
        '685.653',
        '-1.4467626683596034e+106',
        '918.258',
        '-2.7739794760762803e+112',
        '212.985',
        '7.092498628809654e+108',
        '685.069',
        '1.4147556850319196e-89',
        '331.245',
        '-1.6691558699638697e-235',
        '700.595',
        '-219404674807.5156',
        '319.821',
        '2.5536681041717507e-14',
        '887.199',
        '-3.5165382391481735e-96',
        '390.084',
        '5.389730467523424e-249',
        '123.512',
        '-3.254513424640953e+166',
        '354.455',
        '-6.423460062698679e-174',
        '313.521',
        '6.251840190599632e-250',
        '983.401',
        '-5.6977277370961124e-170',
        '974.028',
        '1.9031603657909465e-152',
        '818.964',
        '-1.4690517376456186e-131',
        '525.614',
        '2.2459545968461254e+120',
        '136.317',
        '1.487408427138666e+166',
        '493.729',
        '-2.427065458029988e-23',
        '627.077',
        '4.057506805168395e+47',
        '433.573',
        '-8.521323004975211e+55',
        '931.108',
        '-8.855749581355469e+70',
        '270.075',
        '2.266819768836392e-282',
        '937.38',
        '2.0918987005028228e+293',
        '378.121',
        '5.731647614243849e-203',
        '954.245',
        '-1.873217362487588e+134',
        '467.043',
        '3.0716133217891984e+104',
        '783.247',
        '-6.488614477251602e+60',
        '85.474',
        '1.0556203112623261e+253',
        '656.212',
        '-4.031071387664907e-131',
        '951.221',
        '8338966283553279',
        '123.312',
        '-2.4700036547934554e-170',
        '269.345',
        '-1.6695595143585676e-109',
        '697.217',
        '-3.121382011982576e+125',
        '823.196',
        '3.2428816072679807e+163',
        '85.122',
        '7.281988083072967e-158',
        '494.613',
        '-4.290049220659356e-270',
        '905.589',
        '1.307871914526542e-193',
        '781.109',
        '-0.20659478148508012',
        '115.194',
        '-5.897733963277468e+190',
        '623.543',
        '-6.212243632006617e-112',
        '552.339',
        '1.0203060957817384e+170',
        '991.33',
        '4.928000044239842e+182',
        '196.78',
        '8.04258067524846e+104',
        '472.713',
        '3.765375662499586e+290',
        '685.97',
        '-807008276638859100',
        '717.586',
        '1.5680621997634319e-131',
        '161.312',
        '7.94619061195432e+217',
        '4.334',
        '-9.957564926178975e-246',
        '586.441',
        '-9.204508729711625e+293',
        '833.092',
        '-4.6127902351102195e+297',
        '133.575',
        '-2.5397949043424196e+181',
        '495.966',
        '3.673987516989181e+250',
        '671.395',
        '-4.798037465144108e-235',
        '403.519',
        '-8.900150117420438e-35',
        '438.854',
        '-1.3974318326741895e+80',
        '776.847',
        '-1.517332832244776e-74',
        '57.162',
        '9.665296581978243e-293',
        '861.423',
        '4.0556396876333605e+93',
        '85.027',
        '-9.043671900949341e+55',
        '79.223',
        '8.463663153114606e-138',
        '181.532',
        '-1.3558432676876713e-243',
        '104.786',
        '1.9843670219819417e-10',
        '704.673',
        '3.540635538007029e+110',
        '135.803',
        '-6.275548890561209e-186',
        '653.882',
        '3.747043373371952e-175',
        '773.955',
        '2.3194121271923046e-298',
        '382.479',
        '4.87685184631203e-48',
        '44.846',
    }
    local ir = assert(compile('page', '<p x-text="$.v"></p>', 'x-', {}))
    for _, s in ipairs(cases) do
        local v = tonumber(s) * 1.0
        assert.equal(interpret.render('page', ir, {
            v = v,
        }), '<p>' .. s .. '</p>', s)
        assert.equal(interpret.render('page', ir, {
            v = -v,
        }),
                     '<p>' ..
                         (v == 0 and '0' or s:sub(1, 1) == '-' and s:sub(2) or
                             '-' .. s) .. '</p>', s)
    end
    for v, s in pairs({
        [1 / 0] = 'Infinity',
        [-1 / 0] = '-Infinity',
    }) do
        assert.equal(interpret.render('page', ir, {
            v = v,
        }), '<p>' .. s .. '</p>')
    end
    assert.equal(interpret.render('page', ir, {
        v = 0 / 0,
    }), '<p>NaN</p>')
    assert.equal(interpret.render('page', ir, {
        v = -0.0,
    }), '<p>0</p>')

    -- A Lua integer is written exactly.
    if math.type then
        assert.equal(interpret.render('page', ir, {
            v = math.maxinteger,
        }), '<p>' .. tostring(math.maxinteger) .. '</p>')
        assert.equal(interpret.render('page', ir, {
            v = -7,
        }), '<p>-7</p>')
    end
end

function testcase.render_compares_strings_in_utf16_order()
    -- A supplementary character sorts before U+E000 in UTF-16, after it in
    -- code point order.
    local data = {
        emoji = '\240\159\152\128',
        pua = '\238\128\128',
        cjk = '\228\184\128',
        bad = '\255',
    }
    for html, expected in pairs({
        ['<p x-text="$.emoji < $.pua"></p>'] = 'true',
        ['<p x-text="$.cjk < $.emoji"></p>'] = 'true',
        ['<p x-text="$.emoji < $.emoji"></p>'] = 'false',
        ['<p x-text="$.emoji <= $.emoji"></p>'] = 'true',
        ['<p x-text="\'\' < $.emoji"></p>'] = 'true',
        ['<p x-text="$.bad > \'a\'"></p>'] = 'true',
    }) do
        assert.equal(render(html, data), '<p>' .. expected .. '</p>', html)
    end
end

function testcase.render_reports_runtime_errors_with_their_site()
    local html = '<div>\n  <p class="x" x-text="$.user.name">t</p>\n</div>'
    local out, err = render(html, {})
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ERUNTIME)
    assert.equal(err.op, 'reflow.interpret.render')
    assert.equal(err.message.message,
                 "Cannot read properties of undefined (reading 'name')")
    assert.is_nil(errorlib.unwrap(err))
    assert.equal(err.meta.template_name, 'page')
    assert.equal(err.meta.include_stack, {
        'page',
    })
    assert.equal(err.meta.line, 2)
    assert.equal(err.meta.column, 3)
    assert.equal(err.meta.element, '<p class="x">')
    assert.equal(err.meta.expression, {
        source = '$.user.name',
    })
    assert.is_nil(err.meta.directive)
    assert.equal(type(err.meta.snippet), 'string')

    -- A value of the wrong type names the directive.
    out, err = render('<p x-text="$.user"></p>', {
        user = {},
    })
    assert.is_nil(out)
    assert.equal(err.meta.directive, 'x-text')
end

function testcase.render_keeps_the_value_raised_by_a_helper_as_the_cause()
    local raised = errorlib.new('helper failed')
    local out, err = render('<p x-text="fail()"></p>', nil, {
        fail = function()
            error(raised)
        end,
    })
    assert.is_nil(out)
    assert.equal(err.message.message, 'helper failed')
    assert.rawequal(errorlib.unwrap(err), raised)
    assert.equal(err.meta.expression, {
        source = 'fail()',
    })

    -- A raised table that is not an error has no message of its own.
    out, err = render('<p x-text="fail()"></p>', nil, {
        fail = function()
            error({})
        end,
    })
    assert.is_nil(out)
    assert.equal(err.message.message, 'failed to render')
    assert.not_nil(errorlib.unwrap(err))

    -- An __index metamethod that raises is treated the same way.
    out, err = render('<p x-text="$.o.x"></p>', {
        o = setmetatable({}, {
            __index = function()
                error('index failed', 0)
            end,
        }),
    })
    assert.is_nil(out)
    assert.equal(err.message.message, 'index failed')
end

function testcase.render_checks_the_helpers_argument()
    local ir = assert(compile('page', '<p x-text="f()"></p>', 'x-', {
        f = true,
    }))
    local out, err = interpret.render('page', ir, nil, 1)
    assert.is_nil(out)
    assert.equal(err.message.message, 'helpers must be a table or nil')

    -- A helper known at compile time must also be given at render time.
    for _, helpers in ipairs({
        false,
        {},
        {
            f = 'not a function',
        },
    }) do
        out, err = interpret.render('page', ir, nil, helpers or nil)
        assert.is_nil(out)
        assert.equal(err.message.message, 'helper "f" is not registered')
    end
end

function testcase.render_evaluates_deep_expressions_and_values()
    local depth = 3000
    local data = {
        t = true,
    }
    assert.equal(render('<p x-text="' .. string.rep('!', depth) .. '$.t"></p>',
                        data), '<p>true</p>')
    assert.equal(render('<p x-text="' .. string.rep('(', depth) .. '$.t' ..
                            string.rep(')', depth) .. '"></p>', data),
                 '<p>true</p>')

    -- Nested array literals and a nested static object.
    local nested
    assert(render('<p x-text="keep(' .. string.rep('[', depth) ..
                      string.rep(']', depth) .. ')"></p>', nil, {
        keep = function(v)
            nested = v
        end,
    }))
    local levels = 0
    while type(nested) == 'table' do
        levels = levels + 1
        nested = nested[1]
    end
    assert.equal(levels, depth)

    local value = string.rep('{a: ', depth) .. '1' .. string.rep('}', depth)
    assert.equal(render(
                     '<p x-data="v: ' .. value .. '" x-text="depth(.v)"></p>',
                     nil, {
            depth = function(v)
                local n = 0
                while type(v) == 'table' do
                    n = n + 1
                    v = v.a
                end
                return n
            end,
        }), '<p>' .. depth .. '</p>')

    -- Nested x-data elements each push and pop a frame.
    local html = string.rep('<div x-data="d: 1">', depth) ..
                     '<i x-text=".d"></i>' .. string.rep('</div>', depth) ..
                     '<b x-text=".d"></b>'
    assert.equal(render(html),
                 string.rep('<div>', depth) .. '<i>1</i>' ..
                     string.rep('</div>', depth) .. '<b></b>')
end

function testcase.render_reports_out_of_memory_while_evaluating()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local ir = assert(compile('oom',
            '<div x-data="a: {b: [1, {c: null}], d: \'v\'}">' ..
            '<p x-text="pick({k: [.a.b, 3], [.a.d]: .a.b}).x ?? .a.d"></p></div>',
            'x-', { pick = true }))
        -- The text is large so that both the output buffer and the final
        -- Lua string can run out of memory.
        local big = string.rep('abcdefgh', 32768)
        local helpers = { pick = function(o) return { x = o.v and big } end }
        local kinds = {}

        for extra = 16 * 1024, 2 * 1024 * 1024, 8 * 1024 do
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
                kinds.success = out == '<div><p>' .. big .. '</p></div>'
                break
            else
                assert(errorlib.typeof(err) == errors.ERUNTIME)
                assert(err.message.message == 'out of memory while rendering',
                       err.message.message)
                kinds[errorlib.unwrap(err) == nil and 'pool' or 'lua'] = true
            end
        end
        return kinds
    ]])
    assert.is_true(ok, kinds)
    -- Whether a failure is returned as an error or raised as a memory error
    -- depends on the Lua version: the library does not collect garbage, and
    -- Lua 5.1 and LuaJIT have no emergency collection.
    assert.is_true(kinds.success)
end
