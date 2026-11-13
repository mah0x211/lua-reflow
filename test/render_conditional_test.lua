require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

-- Values passed to t() in call order, to show which expressions ran.
local calls = {}

local function describe(v)
    if v == nil then
        return 'undefined'
    elseif type(v) == 'userdata' then
        return 'null'
    elseif type(v) == 'number' then
        -- Lua 5.3 and later write an integral float as "1.0".
        return string.format('%.14g', v)
    end
    return tostring(v)
end

-- The helpers used to produce the expected results with the JavaScript
-- version, written in Lua.
local HELPERS = {
    t = function(v)
        calls[#calls + 1] = describe(v)
        return v
    end,
    boom = function()
        error('boom', 0)
    end,
    obj = function()
        return {}
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

function testcase.render_matches_the_javascript_version_for_branches()
    -- Expected outputs, error messages with their element, and the values
    -- passed to t() were produced by the JavaScript version with the same
    -- templates, data, and helpers.
    local cases = {
        {
            "<div>\n<template x-if=\"$.n > 0\">positive</template>\n<template x-elseif=\"$.n == 0\">zero</template>\n<template x-else>negative</template>\n</div>",
            {
                ["n"] = -1,
            },
            'ok',
            "<div>\n<template>negative</template>\n</div>",
            "",
        },
        {
            "<ul x-match=\"$.role\">\n<li x-case=\"'admin'\">admin</li>\n<li x-case=\"'user'\">user</li>\n<li x-nocase>guest</li>\n</ul>",
            {
                ["role"] = "user",
            },
            'ok',
            "<ul><li>user</li></ul>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"'a'\">A</p></div>",
            {
                ["v"] = "z",
            },
            'ok',
            "<div></div>",
            "",
        },
        {
            "<div><template x-if=\"$.a\">A</template><template x-elseif=\"$.b\">B</template><template x-elseif=\"$.c\">C</template></div>",
            {
                ["a"] = false,
                ["b"] = false,
                ["c"] = true,
            },
            'ok',
            "<div><template>C</template></div>",
            "",
        },
        {
            "<div>\n  <template x-if=\"$.a\">A</template>\n  <template x-elseif=\"$.b\">B</template>\n</div>",
            {
                ["a"] = false,
                ["b"] = true,
            },
            'ok',
            "<div>\n  <template>B</template>\n</div>",
            "",
        },
        {
            "<template x-if=\"$.show\"><div x-data=\"c: {x: 10}\"><p x-text=\".c.x\"></p></div></template>",
            {
                ["show"] = true,
            },
            'ok',
            "<template><div><p>10</p></div></template>",
            "",
        },
        {
            "<template x-if=\"$.show\"><div x-data=\"c: {x: 10}\"><p x-text=\".c.x\"></p></div></template>",
            {
                ["show"] = false,
            },
            'ok',
            "",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"'a'\">A</p><p x-nocase>D</p></div>",
            {
                ["v"] = "zzz",
            },
            'ok',
            "<div><p>D</p></div>",
            "",
        },
        {
            "<div x-match=\"$.v\"><!-- comment --><p x-case=\"'a'\">A</p><!-- --><p x-nocase>D</p></div>",
            {
                ["v"] = "a",
            },
            'ok',
            "<div><p>A</p></div>",
            "",
        },
        {
            "<div><p x-if=\"$.a\">A</p><!-- ok --><p x-elseif=\"$.b\">B</p><!-- --><p x-else>D</p></div>",
            {
                ["a"] = false,
                ["b"] = true,
            },
            'ok',
            "<div><p>B</p></div>",
            "",
        },
        {
            "<div><template x-if=\"$.a\">A</template><p x-text=\"$.msg\">middle</p></div>",
            {
                ["a"] = true,
                ["msg"] = "hi",
            },
            'ok',
            "<div><template>A</template><p>hi</p></div>",
            "",
        },
        {
            "<div><template x-if=\"$.a\">A</template><p x-text=\"$.msg\">middle</p></div>",
            {
                ["a"] = false,
                ["msg"] = "hi",
            },
            'ok',
            "<div><p>hi</p></div>",
            "",
        },
        {
            "<p x-if=\"$.a\">A</p><p x-elseif=\"$.b\">B</p><p x-else>C</p>",
            {
                ["a"] = 0,
                ["b"] = "",
            },
            'ok',
            "<p>C</p>",
            "",
        },
        {
            "<p x-if=\"$.a\">A</p><p x-elseif=\"$.b\">B</p><p x-else>C</p>",
            {
                ["a"] = NULL,
                ["b"] = "x",
            },
            'ok',
            "<p>B</p>",
            "",
        },
        {
            "<p x-if=\"$.a\">A</p><p x-else>C</p>",
            {
                ["a"] = {},
            },
            'ok',
            "<p>A</p>",
            "",
        },
        {
            "<p x-if=\"$.a\">A</p><p x-else>C</p>",
            {
                ["a"] = "0",
            },
            'ok',
            "<p>A</p>",
            "",
        },
        {
            "<p x-if=\"t(1)\">A</p><p x-elseif=\"t(2)\">B</p><p x-else>C</p>",
            {},
            'ok',
            "<p>A</p>",
            "1",
        },
        {
            "<p x-if=\"t(0)\">A</p><p x-elseif=\"t(2)\">B</p><p x-elseif=\"t(3)\">C</p>",
            {},
            'ok',
            "<p>B</p>",
            "0,2",
        },
        {
            "<p x-if=\"t(0)\">A</p><p x-elseif=\"t(null)\">B</p>",
            {},
            'ok',
            "",
            "0,null",
        },
        {
            "<p x-if=\"@a\" x-data=\"a: 1\">A</p><p x-else>E</p>",
            {},
            'ok',
            "<p>E</p>",
            "",
        },
        {
            "<p x-if=\".w\" x-with=\"w = 1\">A</p><p x-else>E</p>",
            {},
            'ok',
            "<p>E</p>",
            "",
        },
        {
            "<div x-data=\"a: 1\"><p x-if=\"@a\" x-data=\"a: 0\" x-text=\"@a\"></p></div>",
            {},
            'ok',
            "<div><p>0</p></div>",
            "",
        },
        {
            "<p x-if=\"$.v\" x-bind:title=\"$.v\" x-with=\"w = 2\" x-text=\".w\"></p>",
            {
                ["v"] = "x",
            },
            'ok',
            "<p title=\"x\">2</p>",
            "",
        },
        {
            "<p x-if=\"boom()\">A</p>",
            {},
            'err',
            "boom|<p>",
            "",
        },
        {
            "<p x-if=\"0\">A</p><p x-elseif=\"boom()\">B</p>",
            {},
            'err',
            "boom|<p>",
            "",
        },
        {
            "<p x-if=\"1\">A</p><p x-elseif=\"boom()\">B</p>",
            {},
            'ok',
            "<p>A</p>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"1\">one</p><p x-case=\"'1'\">s</p><p x-nocase>no</p></div>",
            {
                ["v"] = "1",
            },
            'ok',
            "<div><p>s</p></div>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"1\">one</p><p x-nocase>no</p></div>",
            {
                ["v"] = 1,
            },
            'ok',
            "<div><p>one</p></div>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"1\">one</p></div>",
            {
                ["v"] = 2,
            },
            'ok',
            "<div></div>",
            "",
        },
        {
            "<div x-match=\"t(1)\"><p x-case=\"t(2)\">a</p><p x-case=\"t(1)\">b</p><p x-case=\"t(3)\">c</p></div>",
            {},
            'ok',
            "<div><p>b</p></div>",
            "1,2,1",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"null\">N</p><p x-case=\"$.missing\">U</p></div>",
            {},
            'ok',
            "<div><p>U</p></div>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"null\">N</p><p x-nocase>X</p></div>",
            {
                ["v"] = NULL,
            },
            'ok',
            "<div><p>N</p></div>",
            "",
        },
        {
            "<div x-match=\"$.v\"><p x-case=\"true\">T</p><p x-case=\"1\">1</p></div>",
            {
                ["v"] = true,
            },
            'ok',
            "<div><p>T</p></div>",
            "",
        },
        {
            "<div x-match=\"$.o\"><p x-case=\"$.o\">same</p><p x-nocase>X</p></div>",
            {
                ["o"] = {},
            },
            'ok',
            "<div><p>same</p></div>",
            "",
        },
        {
            "<div x-match=\"obj()\"><p x-case=\"obj()\">same</p><p x-nocase>X</p></div>",
            {},
            'ok',
            "<div><p>X</p></div>",
            "",
        },
        {
            "<div x-match=\"1\"><p x-case=\"1\" x-data=\"c: 9\" x-text=\"@c\"></p></div>",
            {},
            'ok',
            "<div><p>9</p></div>",
            "",
        },
        {
            "<div x-match=\"1\"><p x-case=\"@c\" x-data=\"c: 1\">a</p><p x-nocase>X</p></div>",
            {},
            'ok',
            "<div><p>X</p></div>",
            "",
        },
        {
            "<div x-match=\"boom()\"><p x-case=\"1\">a</p></div>",
            {},
            'err',
            "boom|<div>",
            "",
        },
        {
            "<div x-match=\"1\"><p x-case=\"boom()\">a</p></div>",
            {},
            'err',
            "boom|<p>",
            "",
        },
        {
            "<div x-match=\"1\"><p x-case=\"1\">a</p><p x-case=\"boom()\">b</p></div>",
            {},
            'ok',
            "<div><p>a</p></div>",
            "",
        },
        {
            "<div x-match=\"1\">\n  <!-- c -->\n  <p x-case=\"1\">a</p>\n  <p x-nocase>b</p>\n</div>",
            {},
            'ok',
            "<div><p>a</p></div>",
            "",
        },
        {
            "<div x-match=\"0\"><p x-case=\"-0\">z</p><p x-nocase>X</p></div>",
            {},
            'ok',
            "<div><p>z</p></div>",
            "",
        },
        {
            "<div x-match=\"$.n\"><p x-case=\"$.m\">same</p><p x-nocase>X</p></div>",
            {
                ["n"] = 1.5,
                ["m"] = 1.5,
            },
            'ok',
            "<div><p>same</p></div>",
            "",
        },
        {
            "<ul><li x-if=\"$.a\">a</li>\n<!-- x -->\n<li x-else>b</li></ul>",
            {},
            'ok',
            "<ul><li>b</li></ul>",
            "",
        },
        {
            "<div x-match=\"1\" class=\"m\" x-bind:title=\"'t'\"><p x-case=\"1\" class=\"c\">a</p></div>",
            {},
            'ok',
            "<div class=\"m\" title=\"t\"><p class=\"c\">a</p></div>",
            "",
        },
        {
            "<div x-match=\"1\" x-text=\"'ignored'\"><p x-case=\"1\">a</p></div>",
            {},
            'ok',
            "<div><p>a</p></div>",
            "",
        },
        {
            "<div x-match=\"1\"><p x-case=\"1\"><i x-if=\"0\">a</i><i x-else>b</i></p></div>",
            {},
            'ok',
            "<div><p><i>b</i></p></div>",
            "",
        },
        {
            "<i x-if=\"1\"><b x-match=\"2\"><u x-case=\"2\"><s x-if=\"0\">n</s><s x-elseif=\"1\">y</s></u></b></i>",
            {},
            'ok',
            "<i><b><u><s>y</s></u></b></i>",
            "",
        },
        {
            "<p x-if=\"1\">A</p><p x-if=\"1\">B</p><p x-if=\"0\">C</p><p x-else>D</p>",
            {},
            'ok',
            "<p>A</p><p>B</p><p>D</p>",
            "",
        },
    }
    for _, case in ipairs(cases) do
        local html, data, kind, expected, called = case[1], case[2], case[3],
                                                   case[4], case[5]
        calls = {}
        local out, err = render(html, data)
        if kind == 'ok' then
            assert.equal(out, expected, html)
        else
            assert.is_nil(out, html)
            assert.equal(err.message.message .. '|' .. err.meta.element,
                         expected, html)
        end
        assert.equal(table.concat(calls, ','), called, html)
    end
end

function testcase.render_pushes_the_frames_of_an_x_match_element()
    -- The JavaScript version does not push the x-data and x-with frames of
    -- an x-match element, against its documentation; they are visible to
    -- the match value, the x-case values, x-bind, and the branches here.
    assert.equal(render('<div x-match="@a" x-data="a: 1">' ..
                            '<p x-case="1">one</p><p x-nocase>X</p></div>'),
                 '<div><p>one</p></div>')
    assert.equal(
        render('<div x-data="a: 5"><div x-match="@a" x-data="a: 1" ' ..
                   'x-bind:title="@a"><p x-case="@a" x-text="@a"></p>' ..
                   '<p x-nocase>X</p></div><i x-text="@a"></i></div>'),
        '<div><div title="1"><p>1</p></div><i>5</i></div>')
    assert.equal(render('<div x-match=".w" x-with="w = 1">' ..
                            '<p x-case="1" x-text=".w ?? \'none\'"></p></div>' ..
                            '<i x-text=".w ?? \'gone\'"></i>'),
                 '<div><p>1</p></div><i>gone</i>')
end

function testcase.render_compares_x_case_values_as_lua_values()
    local t = {}
    local out = assert(render('<div x-match="$.v"><p x-case="$.w">same</p>' ..
                                  '<p x-nocase>X</p></div>', {
        v = t,
        w = t,
    }))
    assert.equal(out, '<div><p>same</p></div>')
    -- A Lua integer and an equal float are the same JavaScript number.
    assert.equal(render('<div x-match="$.v"><p x-case="1">one</p></div>', {
        v = 1.0,
    }), '<div><p>one</p></div>')
    -- NaN never matches, and null is not undefined.
    assert.equal(render('<div x-match="$.v"><p x-case="$.v">same</p>' ..
                            '<p x-nocase>X</p></div>', {
        v = 0 / 0,
    }), '<div><p>X</p></div>')
    assert.equal(render('<div x-match="$.v"><p x-case="$.missing">U</p>' ..
                            '<p x-nocase>X</p></div>', {
        v = NULL,
    }), '<div><p>X</p></div>')
end

function testcase.render_nests_branches_without_c_recursion()
    local depth = 3000
    local html = string.rep('<i x-if="1">', depth) .. 'x' ..
                     string.rep('</i>', depth)
    assert.equal(render(html),
                 string.rep('<i>', depth) .. 'x' .. string.rep('</i>', depth))

    html = string.rep('<b x-match="1"><u x-case="1" x-data="d: 1">', depth) ..
               '<s x-text=".d"></s>' .. string.rep('</u></b>', depth) ..
               '<s x-text=".d"></s>'
    assert.equal(render(html),
                 string.rep('<b><u>', depth) .. '<s>1</s>' ..
                     string.rep('</u></b>', depth) .. '<s></s>')
end

function testcase.render_reports_out_of_memory_in_branches()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local ir = assert(compile('oom',
            '<p x-if="0">A</p><div x-elseif="[1]" x-data="a: {b: [1]}">' ..
            '<ul x-match="text()" x-with="w = {x: text()}">' ..
            '<li x-case="1">one</li><li x-nocase x-text="@w.x"></li></ul></div>',
            'x-', { text = true }))
        local text = string.rep('abcdefgh', 64)
        local helpers = { text = function() return text end }
        local expected = '<div><ul><li>' .. text .. '</li></ul></div>'
        local kinds = {}

        -- When so little memory is left that even the error cannot be
        -- created, the memory error is raised instead.
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
    -- Whether a failure is returned as an error or raised as a memory error
    -- depends on the Lua version: the library does not collect garbage, and
    -- Lua 5.1 and LuaJIT have no emergency collection.
    assert.is_true(kinds.success)
end
