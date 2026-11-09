require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

-- Values passed to t() in call order, to show which expressions ran.
local calls = {}

local describe

function describe(v)
    if v == nil then
        return 'undefined'
    elseif type(v) == 'userdata' then
        return 'null'
    elseif type(v) == 'number' then
        -- Lua 5.3 and later write an integral float as "1.0".
        return string.format('%.14g', v)
    elseif type(v) == 'table' then
        -- As String(array) does in JavaScript.
        local items = {}
        for i, item in ipairs(v) do
            items[i] = describe(item)
        end
        return table.concat(items, ',')
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
    ty = function(v)
        return type(v) == 'userdata' and 'null' or type(v)
    end,
    upper = function(s)
        return tostring(s):upper()
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

function testcase.render_matches_the_javascript_version_for_loops()
    -- Expected outputs, error messages with their element and directive,
    -- and the values passed to t() were produced by the JavaScript version
    -- with the same templates, data, and helpers.
    local cases = {
        {
            "<ul><li x-for=\"i = 1, 2\"><ol><li x-for=\"j = 1, 2\"><b x-text=\".i\"></b>-<b x-text=\".j\"></b></li></ol></li></ul>",
            {},
            'ok',
            "<ul><li><ol><li><b>1</b>-<b>1</b></li><li><b>1</b>-<b>2</b></li></ol></li><li><ol><li><b>2</b>-<b>1</b></li><li><b>2</b>-<b>2</b></li></ol></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 10\"><span x-text=\".i\"></span><br x-break-if=\".i >= 3\"></li></ul>",
            {},
            'ok',
            "<ul><li><span>1</span></li><li><span>2</span></li><li><span>3</span></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 10\"><span x-text=\".i\"></span><br x-break></li></ul>",
            {},
            'ok',
            "<ul><li><span>1</span></li></ul>",
            "",
        },
        {
            "<ul><li x-each=\"u in $.users\" x-text=\".u.name\"></li></ul>",
            {
                ["users"] = {
                    {
                        ["name"] = "Alice",
                    },
                    {
                        ["name"] = "Bob",
                    },
                    {
                        ["name"] = "Chloe",
                    },
                },
            },
            'ok',
            "<ul><li>Alice</li><li>Bob</li><li>Chloe</li></ul>",
            "",
        },
        {
            "<ul><li x-each=\"u, i in $.users\"><b x-text=\".i\"></b>:<b x-text=\".u\"></b></li></ul>",
            {
                ["users"] = {
                    "a",
                    "b",
                    "c",
                },
            },
            'ok',
            "<ul><li><b>0</b>:<b>a</b></li><li><b>1</b>:<b>b</b></li><li><b>2</b>:<b>c</b></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 5\" x-text=\".i\"></li></ul>",
            {},
            'ok',
            "<ul><li>1</li><li>2</li><li>3</li><li>4</li><li>5</li></ul>",
            "",
        },
        {
            "<ol><li x-for=\"k = 5, 1, -2\" x-text=\".k\"></li></ol>",
            {},
            'ok',
            "<ol><li>5</li><li>3</li><li>1</li></ol>",
            "",
        },
        {
            "<ul>\n<li x-each=\"item in $.items\" x-with=\"up = upper(.item.name)\" x-text=\"@up\"></li>\n</ul>",
            {
                ["items"] = {
                    {
                        ["name"] = "alice",
                    },
                    {
                        ["name"] = "bob",
                    },
                },
            },
            'ok',
            "<ul>\n<li>ALICE</li><li>BOB</li>\n</ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 3, 1, -1\" x-text=\".i\"></li></ul>",
            {},
            'ok',
            "<ul><li>3</li><li>2</li><li>1</li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 5, 5\" x-text=\".i\"></li></ul>",
            {},
            'ok',
            "<ul><li>5</li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 5\"><span x-text=\".i\"></span><br x-break></li></ul>",
            {},
            'ok',
            "<ul><li><span>1</span></li></ul>",
            "",
        },
        {
            "<ul><li x-each=\"u in $.items\"><span x-text=\".u\"></span><br x-break-if=\".u == 2\"></li></ul>",
            {
                ["items"] = {
                    1,
                    2,
                    3,
                    4,
                },
            },
            'ok',
            "<ul><li><span>1</span></li><li><span>2</span></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><span x-text=\"boom()\"></span></li></ul>",
            {},
            'err',
            "boom|<span>|",
            "",
        },
        {
            "<i x-for=\"i = 1, 3, 2\" x-text=\".i\"></i>",
            {},
            'ok',
            "<i>1</i><i>3</i>",
            "",
        },
        {
            "<i x-for=\"i = -2, 2, 2\" x-text=\".i\"></i>",
            {},
            'ok',
            "<i>-2</i><i>0</i><i>2</i>",
            "",
        },
        {
            "<i x-for=\"i = 1, 2\" x-text=\"@i ?? 'none'\"></i>",
            {},
            'ok',
            "<i>none</i><i>none</i>",
            "",
        },
        {
            "<i x-for=\"i = 1, 2\" x-text=\"ty(.i)\"></i>",
            {},
            'ok',
            "<i>number</i><i>number</i>",
            "",
        },
        {
            "<div x-data=\"i: 'd'\"><i x-for=\"i = 1, 2\"><b x-text=\".i\"></b><b x-text=\"@i\"></b></i><b x-text=\".i\"></b></div>",
            {},
            'ok',
            "<div><i><b>1</b><b>d</b></i><i><b>2</b><b>d</b></i><b>d</b></div>",
            "",
        },
        {
            "<i x-each=\"v, k in $.a\" x-text=\".k\"></i>",
            {
                ["a"] = {
                    "x",
                    "y",
                },
            },
            'ok',
            "<i>0</i><i>1</i>",
            "",
        },
        {
            "<i x-each=\"v, k in $.a\" x-text=\"ty(.k)\"></i>",
            {
                ["a"] = {
                    "x",
                },
            },
            'ok',
            "<i>number</i>",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v\"></i>",
            {
                ["a"] = "str",
            },
            'err',
            "x-each: expected array, got string|<i>|x-each",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v\"></i>",
            {},
            'err',
            "x-each: expected array, got undefined|<i>|x-each",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v\"></i>",
            {
                ["a"] = NULL,
            },
            'err',
            "x-each: expected array, got null|<i>|x-each",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v\"></i>",
            {
                ["a"] = 3,
            },
            'err',
            "x-each: expected array, got number|<i>|x-each",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v\"></i>",
            {
                ["a"] = true,
            },
            'err',
            "x-each: expected array, got boolean|<i>|x-each",
            "",
        },
        {
            "<i x-each=\"v in t($.a)\" x-text=\".v\"></i>",
            {
                ["a"] = {
                    1,
                    2,
                },
            },
            'ok',
            "<i>1</i><i>2</i>",
            "1,2",
        },
        {
            "<i x-each=\"v in $.a\" x-data=\"d: 1\"><b x-text=\".v\"></b><b x-text=\".d\"></b></i>",
            {
                ["a"] = {
                    1,
                },
            },
            'ok',
            "<i><b>1</b><b>1</b></i>",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-with=\"w = .v\"><b x-text=\".w\"></b></i><b x-text=\".w ?? 'gone'\"></b>",
            {
                ["a"] = {
                    1,
                    2,
                },
            },
            'ok',
            "<i><b>1</b></i><i><b>2</b></i><b>gone</b>",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-bind:title=\".v\"></i>",
            {
                ["a"] = {
                    "p",
                    "q",
                },
            },
            'ok',
            "<i title=\"p\"></i><i title=\"q\"></i>",
            "",
        },
        {
            "<i x-each=\"v in $.a\" x-text=\".v ?? 'N'\"></i>",
            {
                ["a"] = {
                    NULL,
                    1,
                },
            },
            'ok',
            "<i>N</i><i>1</i>",
            "",
        },
        {
            "<i x-each=\"v in $.a\"><b x-each=\"w in .v\" x-text=\".w\"></b></i>",
            {
                ["a"] = {
                    {
                        1,
                        2,
                    },
                    {
                        3,
                    },
                },
            },
            'ok',
            "<i><b>1</b><b>2</b></i><i><b>3</b></i>",
            "",
        },
        {
            "<i x-for=\"v = 1, 2\" x-text=\".v\"></i><b x-text=\".v ?? 'gone'\"></b>",
            {},
            'ok',
            "<i>1</i><i>2</i><b>gone</b>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 5\"><b x-text=\".i\"></b><span x-break-if=\".i == 2\">B</span><u>after</u></li></ul>",
            {},
            'ok',
            "<ul><li><b>1</b><u>after</u></li><li><b>2</b></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><b x-text=\".i\"></b><span x-break></span></li></ul>",
            {},
            'ok',
            "<ul><li><b>1</b></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><b x-text=\".i\"></b><span x-break class=\"c\">x</span></li></ul>",
            {},
            'ok',
            "<ul><li><b>1</b><span class=\"c\">x</span></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p><em><s x-break-if=\".i == 2\"></s></em><u>u</u></p><b>after</b></li><z>end</z></ul>",
            {},
            'ok',
            "<ul><li><p><em></em><u>u</u></p><b>after</b></li><li><p><em></em></p></li><z>end</z></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 2\"><p x-for=\"j = 1, 3\"><s x-text=\".j\"></s><s x-break-if=\".j == 2\"></s></p></li></ul>",
            {},
            'ok',
            "<ul><li><p><s>1</s></p><p><s>2</s></p></li><li><p><s>1</s></p><p><s>2</s></p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-if=\".i == 2\"><s x-break></s></p><b x-text=\".i\"></b></li></ul>",
            {},
            'ok',
            "<ul><li><b>1</b></li><li><p></p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-match=\".i\"><s x-case=\"2\"><b x-break></b></s><s x-nocase>n</s></p></li></ul>",
            {},
            'ok',
            "<ul><li><p><s>n</s></p></li><li><p><s></s></p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-break-if=\"@d\" x-data=\"d: true\">x</p></li></ul>",
            {},
            'ok',
            "<ul><li><p>x</p></li><li><p>x</p></li><li><p>x</p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-break-if=\".w\" x-with=\"w = true\">x</p></li></ul>",
            {},
            'ok',
            "<ul><li><p>x</p></li><li><p>x</p></li><li><p>x</p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><br x-break-if=\".i == 1\"></li></ul>",
            {},
            'ok',
            "<ul><li></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-text=\".i\" x-break-if=\".i == 2\"></p></li></ul>",
            {},
            'ok',
            "<ul><li><p>1</p></li><li><p>2</p></li></ul>",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-break-if=\"t(.i) == 2\"></p></li></ul>",
            {},
            'ok',
            "<ul><li></li><li></li></ul>",
            "1,2",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-break-if=\"$.missing.x\"></p></li></ul>",
            {},
            'err',
            "Cannot read properties of undefined (reading 'x')|<p>|",
            "",
        },
        {
            "<ul><li x-for=\"i = 1, 3\"><p x-break-if=\"boom()\" x-text=\".i\"></p></li></ul>",
            {},
            'err',
            "boom|<p>|",
            "",
        },
        {
            "<ul><li x-each=\"v in $.a\" x-text=\".v\"></li><li x-for=\"i = 1, 2\" x-text=\".i\"></li></ul>",
            {
                ["a"] = {},
            },
            'ok',
            "<ul><li>1</li><li>2</li></ul>",
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
            -- x-each also accepts objects (hash tables), so its message
            -- names both.
            expected = expected:gsub('^x%-each: expected array,',
                                     'x-each: expected array or object,')
            assert.is_nil(out, html)
            assert.equal(
                err.message.message .. '|' .. err.meta.element .. '|' ..
                    (err.meta.directive or ''), expected, html)
        end
        assert.equal(table.concat(calls, ','), called, html)
    end
end

function testcase.render_iterates_hash_tables_with_their_keys()
    -- A table of length 0 is a hash table: the index variable is the key.
    -- The order is not specified, so the pairs are compared as a set.
    local seen = {}
    local out = assert(render(
                           '<i x-each="v, k in $.m" x-text="keep(.k, .v)"></i>',
                           {
            m = {
                a = 1,
                b = 'x',
                [10] = true,
                [2.5] = NULL,
            },
        }, {
            keep = function(k, v)
                seen[k] = v
                return 'i'
            end,
        }))
    assert.equal(out, string.rep('<i>i</i>', 4))
    assert.equal(seen, {
        a = 1,
        b = 'x',
        [10] = true,
        [2.5] = NULL,
    })

    -- An empty table renders nothing.
    assert.equal(render('<i x-each="v in $.m" x-text=".v"></i>', {
        m = {},
    }), '')
end

function testcase.render_iterates_arrays_up_to_the_first_nil()
    local keys = {}
    local keep = {
        keep = function(i, v)
            keys[#keys + 1] = i .. '=' .. v
        end,
    }
    -- The hash part of a table whose length is positive is not iterated.
    assert(render('<i x-each="v, i in $.a" x-text="keep(.i, .v)"></i>', {
        a = {
            'x',
            'y',
            k = 'hash',
        },
    }, keep))
    assert.equal(keys, {
        '0=x',
        '1=y',
    })

    -- Keys are read without metamethods, from 1 until the first nil.
    keys = {}
    assert(render('<i x-each="v, i in $.a" x-text="keep(.i, .v)"></i>', {
        a = setmetatable({
            'x',
        }, {
            __index = function(_, k)
                return 'meta' .. k
            end,
        }),
    }, keep))
    assert.equal(keys, {
        '0=x',
    })
end

function testcase.render_passes_loop_values_as_lua_values()
    local items = {
        {},
        {},
    }
    local seen = {}
    assert(render('<i x-each="v, i in $.a" x-text="keep(.v, .i)"></i>' ..
                      '<b x-for="n = 1, 2" x-text="keep(.n)"></b>', {
        a = items,
    }, {
        keep = function(v, i)
            seen[#seen + 1] = {
                v,
                i,
            }
        end,
    }))
    assert.rawequal(seen[1][1], items[1])
    assert.rawequal(seen[2][1], items[2])
    assert.equal(seen[1][2], 0)
    assert.equal(seen[2][2], 1)
    assert.equal(seen[3][1], 1)
    if math.type then
        assert.equal(math.type(seen[2][2]), 'integer')
        assert.equal(math.type(seen[3][1]), 'integer')
    end
end

function testcase.render_reports_the_value_of_x_each_that_is_not_a_table()
    for _, v in ipairs({
        {
            'abc',
            'string',
        },
        {
            print,
            'function',
        },
        {
            io.tmpfile(),
            'userdata',
        },
        {
            coroutine.create(function()
            end),
            'thread',
        },
    }) do
        local out, err = render('<i x-each="v in $.a"></i>', {
            a = v[1],
        })
        assert.is_nil(out)
        assert.equal(err.message.message,
                     'x-each: expected array or object, got ' .. v[2])
        assert.equal(err.meta.directive, 'x-each')
        assert.equal(err.meta.expression, {
            source = '$.a',
        })
    end
end

function testcase.render_nests_loops_and_breaks_without_c_recursion()
    local depth = 2000
    -- Each level repeats once and breaks after its content.
    local html = string.rep('<i x-for="n = 1, 2"><b>', depth) .. 'x' ..
                     string.rep('</b><s x-break></s></i>', depth)
    local out, err = render(html)
    if out then
        assert.equal(out, string.rep('<i><b>', depth) .. 'x' ..
                         string.rep('</b></i>', depth))
    else
        -- Lua 5.1 and LuaJIT limit the stack of a C function to about
        -- 8000 slots, and each loop keeps its values there.
        assert.equal(err.message.message, 'loops are too deeply nested')
    end

    -- A break deep inside closes every open element up to its loop.
    html = '<ul><li x-for="n = 1, 3">' .. string.rep('<p>', depth) ..
               '<s x-break-if=".n == 2"></s>' .. string.rep('</p>', depth) ..
               '<b x-text=".n"></b></li></ul>'
    local level = string.rep('<p>', depth) .. string.rep('</p>', depth)
    assert.equal(render(html), '<ul><li>' .. level .. '<b>1</b></li><li>' ..
                     level .. '</li></ul>')
end

function testcase.render_reports_out_of_memory_in_loops()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local ir = assert(compile('oom',
            '<ul><li x-each="v, k in $.m" x-with="w = {x: .v}">' ..
            '<b x-for="n = 1, 3" x-text="@w.x"></b>' ..
            '<s x-break-if=".k == \'never\'"></s></li></ul>',
            'x-', {}))
        local data = { m = { a = string.rep('abcdefgh', 16) } }
        local item = '<b>' .. data.m.a .. '</b>'
        local expected = '<ul><li>' .. string.rep(item, 3) .. '</li></ul>'
        local kinds = {}

        -- When so little memory is left that even the error cannot be
        -- created, the memory error is raised instead.
        for extra = 1024, 1024 * 1024, 16 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, out, err = pcall(interpret.render, 'oom', ir, data)
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
