require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

-- The helpers used to produce the expected results with the JavaScript
-- version, written in Lua.
local HELPERS = {
    boom = function()
        error('boom', 0)
    end,
    upper = function(s)
        return tostring(s):upper()
    end,
}

local function helper_names(helpers)
    local names = {}
    for name in pairs(helpers) do
        names[name] = true
    end
    return names
end

-- Compile every template into a registry and render the one named main.
local function render(sources, data, max_include_depth, helpers)
    helpers = helpers or HELPERS
    local templates = {}
    for name, html in pairs(sources) do
        templates[name] = assert(
                              compile(name, html, 'x-', helper_names(helpers)))
    end
    return interpret.render('main', templates.main, data, helpers, templates,
                            max_include_depth)
end

-- JavaScript null as rendering sees it.
local NULL
assert(render({
    main = '<p x-data="v: null" x-text="capture(.v)"></p>',
}, nil, nil, {
    capture = function(v)
        NULL = v
    end,
}))

local function describe(v)
    if v == nil then
        return ''
    elseif v == NULL then
        return 'null'
    end
    return tostring(v)
end

function testcase.render_matches_the_javascript_version_for_includes()
    -- Expected outputs and errors were produced by the JavaScript version
    -- with the same templates, data, helpers, and include depth limit.
    local cases = {
        {
            {
                ["main"] = "<div><header>H</header><section x-include=\"$.content\"></section><footer>F</footer></div>",
                ["page"] = "<article><h1 x-text=\"$.title\"></h1></article>",
            },
            {
                ["content"] = "page",
                ["title"] = "Hello",
            },
            nil,
            'ok',
            "<div><header>H</header><section><article><h1>Hello</h1></article></section><footer>F</footer></div>",
        },
        {
            {
                ["main"] = "<article x-include=\"'panel'\" x-with=\"title = $.t, body = $.b\"></article>",
                ["panel"] = "<section><h2 x-text=\"@title\"></h2><p x-text=\"@body\"></p></section>",
            },
            {
                ["t"] = "Hello",
                ["b"] = "World",
            },
            nil,
            'ok',
            "<article><section><h2>Hello</h2><p>World</p></section></article>",
        },
        {
            {
                ["main"] = "<section x-include=\"$.n\"></section>",
            },
            {
                ["n"] = 42,
            },
            nil,
            'err',
            "ReflowIncludeError|x-include: value must evaluate to a template name (string), got number|main|main|1|1|<section>|invalid|42",
        },
        {
            {
                ["main"] = "<div><section x-include=\"$.content\"></section></div>",
                ["page"] = "<article><p x-text=\".missing.field\"></p></article>",
            },
            {
                ["content"] = "page",
            },
            nil,
            'err',
            "ReflowRuntimeError|Cannot read properties of undefined (reading 'field')|page|main>page|1|10|<p>||",
        },
        {
            {
                ["main"] = "<section x-include=\"'b'\"></section>",
                ["b"] = "<section x-include=\"'main'\"></section>",
            },
            {},
            nil,
            'err',
            "ReflowIncludeError|include cycle detected|b|main>b|1|1|<section>|cycle|main",
        },
        {
            {
                ["main"] = "<section x-include=\"'main'\"></section>",
            },
            {},
            3,
            'err',
            "ReflowIncludeError|include cycle detected|main|main|1|1|<section>|cycle|main",
        },
        {
            {
                ["main"] = "<section x-include=\"$.name\"></section>",
            },
            {
                ["name"] = "not-there",
            },
            nil,
            'err',
            "ReflowIncludeError|template not found: \"not-there\"|main|main|1|1|<section>|not_found|not-there",
        },
        {
            {
                ["main"] = "<section x-include=\"$.n\"></section>",
            },
            {
                ["n"] = NULL,
            },
            nil,
            'err',
            "ReflowIncludeError|x-include: value must evaluate to a template name (string), got null|main|main|1|1|<section>|invalid|null",
        },
        {
            {
                ["main"] = "<section x-include=\"$.n\"></section>",
            },
            {},
            nil,
            'err',
            "ReflowIncludeError|x-include: value must evaluate to a template name (string), got undefined|main|main|1|1|<section>|invalid|",
        },
        {
            {
                ["main"] = "<section x-include=\"$.n\"></section>",
            },
            {
                ["n"] = true,
            },
            nil,
            'err',
            "ReflowIncludeError|x-include: value must evaluate to a template name (string), got boolean|main|main|1|1|<section>|invalid|true",
        },
        {
            {
                ["main"] = "<div x-data=\"a: 1\" x-include=\"'child'\"></div>",
                ["child"] = "<p x-text=\"@a ?? 'none'\"></p>",
            },
            {},
            nil,
            'ok',
            "<div><p>none</p></div>",
        },
        {
            {
                ["main"] = "<div x-with=\"a = 2, m = $.missing\" x-include=\"'child'\"></div>",
                ["child"] = "<p x-text=\"@a\"></p><p x-text=\".m ?? 'U'\"></p>",
            },
            {},
            nil,
            'ok',
            "<div><p>2</p><p>U</p></div>",
        },
        {
            {
                ["main"] = "<div x-data=\"m: 1\"><p x-with=\"m = $.missing\" x-include=\"'child'\"></p></div>",
                ["child"] = "<i x-text=\".m ?? 'U'\"></i>",
            },
            {},
            nil,
            'ok',
            "<div><p><i>U</i></p></div>",
        },
        {
            {
                ["main"] = "<div x-data=\"o: 1\"><p x-with=\"w = 1\" x-include=\"'child'\"></p></div>",
                ["child"] = "<i x-text=\".o ?? 'hidden'\"></i><i x-text=\".w\"></i>",
            },
            {},
            nil,
            'ok',
            "<div><p><i>hidden</i><i>1</i></p></div>",
        },
        {
            {
                ["main"] = "<ul><li x-for=\"i = 1, 2\" x-with=\"n = .i\" x-include=\"'child'\"></li></ul>",
                ["child"] = "<b x-text=\".n\"></b><i x-text=\".i ?? '-'\"></i>",
            },
            {},
            nil,
            'ok',
            "<ul><li><b>1</b><i>-</i></li><li><b>2</b><i>-</i></li></ul>",
        },
        {
            {
                ["main"] = "<ul><li x-each=\"v in $.a\" x-with=\"v = .v\" x-include=\"'child'\"></li></ul>",
                ["child"] = "<b x-text=\"upper(.v)\"></b>",
            },
            {
                ["a"] = {
                    "x",
                    "y",
                },
            },
            nil,
            'ok',
            "<ul><li><b>X</b></li><li><b>Y</b></li></ul>",
        },
        {
            {
                ["main"] = "<div x-include=\"$.name\">old <b>children</b></div>",
                ["child"] = "<i>C</i>",
            },
            {
                ["name"] = "child",
            },
            nil,
            'ok',
            "<div><i>C</i></div>",
        },
        {
            {
                ["main"] = "<div x-include=\"'nope'\"></div>",
            },
            {},
            nil,
            'err',
            "ReflowIncludeError|template not found: \"nope\"|main|main|1|1|<div>|not_found|nope",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<i x-include=\"'b'\"></i>",
                ["b"] = "<u x-include=\"'a'\"></u>",
            },
            {},
            nil,
            'err',
            "ReflowIncludeError|include cycle detected|b|main>a>b|1|1|<u>|cycle|a",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<i x-include=\"'b'\"></i>",
                ["b"] = "<s>B</s>",
            },
            {},
            2,
            'err',
            "ReflowIncludeError|include depth limit exceeded (2)|a|main>a|1|1|<i>|depth_exceeded|b",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<i x-include=\"'b'\"></i>",
                ["b"] = "<s>B</s>",
            },
            {},
            3,
            'ok',
            "<div><i><s>B</s></i></div>",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<s>A</s>",
            },
            {},
            1,
            'err',
            "ReflowIncludeError|include depth limit exceeded (1)|main|main|1|1|<div>|depth_exceeded|a",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<s>A</s>",
            },
            {},
            0,
            'err',
            "ReflowIncludeError|include depth limit exceeded (0)|main|main|1|1|<div>|depth_exceeded|a",
        },
        {
            {
                ["main"] = "<div>\n<p x-include=\"'child'\"></p></div>",
                ["child"] = "<i>\n<b x-text=\"boom()\"></b></i>",
            },
            {},
            nil,
            'err',
            "ReflowRuntimeError|boom|child|main>child|2|1|<b>||",
        },
        {
            {
                ["main"] = "<br x-include=\"'child'\"><i>after</i>",
                ["child"] = "<i>c</i>",
            },
            {},
            nil,
            'ok',
            "<br><i>c</i><i>after</i>",
        },
        {
            {
                ["main"] = "<p x-include=\"'a'\"></p><p x-include=\"'a'\"></p>",
                ["a"] = "<i>A</i>",
            },
            {},
            nil,
            'ok',
            "<p><i>A</i></p><p><i>A</i></p>",
        },
        {
            {
                ["main"] = "<p x-include=\"'a'\" x-bind:title=\"'t'\" class=\"c\"></p>",
                ["a"] = "<i>A</i>",
            },
            {},
            nil,
            'ok',
            "<p class=\"c\" title=\"t\"><i>A</i></p>",
        },
        {
            {
                ["main"] = "<ul><li x-for=\"i = 1, 3\"><p x-include=\"'a'\" x-break-if=\".i == 2\"></p></li></ul>",
                ["a"] = "<i>A</i>",
            },
            {},
            nil,
            'ok',
            "<ul><li><p><i>A</i></p></li><li><p><i>A</i></p></li></ul>",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<ul><li x-for=\"i = 1, 3\"><b x-text=\".i\"></b><s x-break-if=\".i == 2\"></s></li></ul>",
            },
            {},
            nil,
            'ok',
            "<div><ul><li><b>1</b></li><li><b>2</b></li></ul></div>",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div><p x-text=\"$.v\"></p>",
                ["a"] = "<p x-if=\"$.v\"><i x-include=\"'b'\"></i></p>",
                ["b"] = "<b x-text=\"$.v\"></b>",
            },
            {
                ["v"] = "yes",
            },
            nil,
            'ok',
            "<div><p><i><b>yes</b></i></p></div><p>yes</p>",
        },
        {
            {
                ["main"] = "<div x-include=\"'a'\"></div>",
                ["a"] = "<p x-include=\"$.missing.x\"></p>",
            },
            {},
            nil,
            'err',
            "ReflowRuntimeError|Cannot read properties of undefined (reading 'x')|a|main>a|1|1|<p>||",
        },
    }
    for _, case in ipairs(cases) do
        local sources, data, max, kind, expected = case[1], case[2], case[3],
                                                   case[4], case[5]
        local out, err = render(sources, data, max)
        if kind == 'ok' then
            assert.equal(out, expected, sources.main)
        else
            assert.is_nil(out, sources.main)
            local meta = err.meta
            assert.equal(table.concat({
                errorlib.typeof(err) == reflow_error.EINCLUDE and
                    'ReflowIncludeError' or 'ReflowRuntimeError',
                err.message.message,
                meta.template_name,
                table.concat(meta.include_stack, '>'),
                meta.line,
                meta.column,
                meta.element,
                meta.reason or '',
                describe(meta.requested),
            }, '|'), expected, sources.main)
        end
    end
end

function testcase.render_writes_the_top_level_text_of_an_included_template()
    -- The JavaScript version drops text and comments outside any element.
    assert.equal(render({
        main = '<div x-include="\'child\'"></div>',
        child = 'top <i>x</i><!--c-->',
    }), '<div>top <i>x</i><!--c--></div>')
end

function testcase.render_shares_lua_values_with_included_templates()
    local data = {
        item = {},
    }
    local seen = {}
    assert(render({
        main = '<div x-with="w = $.item" x-include="\'child\'"></div>',
        child = '<i x-text="keep($.item)"></i><i x-text="keep(@w)"></i>',
    }, data, nil, {
        keep = function(v)
            seen[#seen + 1] = v
        end,
    }))
    assert.rawequal(seen[1], data.item)
    assert.rawequal(seen[2], data.item)
end

function testcase.render_checks_the_include_arguments()
    local ir = assert(compile('main', '<p></p>', 'x-', {}))
    for _, case in ipairs({
        {
            1,
            nil,
            'templates must be a table or nil',
        },
        {
            nil,
            -1,
            'max_include_depth must be a non-negative integer or nil',
        },
        {
            nil,
            1.5,
            'max_include_depth must be a non-negative integer or nil',
        },
        {
            nil,
            '1',
            'max_include_depth must be a non-negative integer or nil',
        },
        {
            nil,
            0 / 0,
            'max_include_depth must be a non-negative integer or nil',
        },
    }) do
        local out, err =
            interpret.render('main', ir, nil, nil, case[1], case[2])
        assert.is_nil(out)
        assert.equal(err.message.message, case[3])
        assert.equal(err.meta.template_name, 'main')
    end

    -- Without a registry, every include is not found.
    ir = assert(compile('main', '<p x-include="\'a\'"></p>', 'x-', {}))
    local out, err = interpret.render('main', ir)
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'not_found')

    -- A registry entry that is not a compiled template is not found either.
    out, err = interpret.render('main', ir, nil, nil, {
        a = '<p></p>',
    })
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'not_found')
end

function testcase.render_nests_includes_without_c_recursion()
    local depth = 2000
    local sources = {
        main = '<b x-include="\'t1\'"></b>',
    }
    for i = 1, depth - 1 do
        sources['t' .. i] = '<i x-with="n = ' .. i .. '" x-include="\'t' ..
                                (i + 1) .. '\'"></i>'
    end
    sources['t' .. depth] = '<s x-text=".n ?? \'top\'"></s>'
    assert.equal(render(sources, nil, depth + 1),
                 '<b>' .. string.rep('<i>', depth - 1) .. '<s>' .. depth - 1 ..
                     '</s>' .. string.rep('</i>', depth - 1) .. '</b>')

    -- The limit counts the entry template.
    local out, err = render(sources, nil, depth)
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'depth_exceeded')
    assert.equal(#err.meta.include_stack, depth)
    assert.equal(err.meta.template_name, 't' .. depth - 1)
end

function testcase.render_reports_out_of_memory_in_includes()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local templates = {
            main = assert(compile('main',
                '<ul><li x-each="v in $.a" x-with="v = {x: .v}" ' ..
                'x-include="\'item\'"></li></ul>', 'x-', {})),
            item = assert(compile('item',
                '<b x-text="@v.x"></b><i x-include="\'leaf\'"></i>', 'x-', {})),
            leaf = assert(compile('leaf', '<s x-text="$.t"></s>', 'x-', {})),
        }
        local text = string.rep('abcdefgh', 16)
        local data = { a = { 'p', 'q' }, t = text }
        local item = function(v)
            return '<li><b>' .. v .. '</b><i><s>' .. text .. '</s></i></li>'
        end
        local expected = '<ul>' .. item('p') .. item('q') .. '</ul>'
        local kinds = {}

        -- When so little memory is left that even the error cannot be
        -- created, the memory error is raised instead.
        for extra = 1024, 1024 * 1024, 16 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, out, err = pcall(interpret.render, 'main',
                                           templates.main, data, nil,
                                           templates)
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
