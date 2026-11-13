require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local newstate = require('newstate')

local function render(html, data)
    local ir, err = compile('page', html, 'x-', {})
    assert.not_nil(ir, err)
    local out, rerr = interpret.render('page', ir, data)
    assert.not_nil(out, rerr)
    return out
end

local function assert_runtime_error(err, message, template_name)
    assert.rawequal(errorlib.typeof(err), reflow_error.ERUNTIME)
    assert.equal(err.op, 'reflow.interpret.render')
    assert.equal(err.message.message, message)
    if template_name == nil then
        assert.is_nil(err.meta)
    else
        assert.equal(err.meta, {
            template_name = template_name,
            include_stack = {
                template_name,
            },
        })
    end
end

function testcase.module_exposes_only_render()
    local names = {}
    for name, value in pairs(interpret) do
        names[#names + 1] = name
        assert.equal(type(value), 'function')
    end
    assert.equal(names, {
        'render',
    })
end

function testcase.render_matches_the_javascript_version_without_directives()
    -- Expected outputs were produced by the JavaScript version.
    local cases = {
        ['<p>hello</p>'] = '<p>hello</p>',
        ['<!DOCTYPE html><html><head><title>t</title></head>' ..
            '<body>x</body></html>'] = '<html><head><title>t</title></head>' ..
            '<body>x</body></html>',
        ['<p>x &amp; y &lt; z > w " \'</p>'] = '<p>x &amp; y &lt; z > w " \'</p>',
        ['<p title=\'q"q\' data-a=b data-c data-d="">t</p>'] = '<p title="q&quot;q" data-a="b" data-c="" data-d="">t</p>',
        ['<input disabled><br/><img src="a"/><hr>'] = '<input disabled=""><br><img src="a"><hr>',
        ['<P CLASS="A" Data-X="1">x</P>'] = '<p class="A" data-x="1">x</p>',
        ['<script>if (a < b && c) { x = "</p>"; }</script>'] = '<script>if (a < b && c) { x = "</p>"; }</script>',
        ['<style>a > b { c: "&amp;" }</style>'] = '<style>a > b { c: "&amp;" }</style>',
        ['<textarea>a &amp; <b></textarea>'] = '<textarea>a &amp; <b></textarea>',
        ['<ul><li>a<li>b</ul>'] = '<ul><li>a<li>b</li></li></ul>',
        ['<p>a</div>b</p>'] = '<p>ab</p>',
        ['<div><span>unclosed</div>'] = '<div><span>unclosed</span></div>',
        ['<p>&nbsp;&copy;&#x41;&#65;&amp</p>'] = '<p>&nbsp;&copy;&#x41;&#65;&amp</p>',
        ['<p\ttitle = "x"   id=y>t</p>'] = '<p title="x" id="y">t</p>',
        ['<template><p>t</p></template>'] = '<template><p>t</p></template>',
        [''] = '',
        ['<p>multi\nline\r\nCRLF\rCR</p>'] = '<p>multi\nline\r\nCRLF\rCR</p>',
        ['<p a="1\r\n2">t</p>'] = '<p a="1\r\n2">t</p>',
        ['<p>\195\169\227\129\130\240\159\152\128</p>'] = '<p>\195\169\227\129\130\240\159\152\128</p>',
        ['<p>\0nul</p>'] = '<p>\0nul</p>',
        ['<p a=1 a=2>t</p>'] = '<p a="1" a="2">t</p>',
        ['<br></br>'] = '<br>',
        ['<svg viewBox="0 0 1 1"><foreignObject></foreignObject></svg>'] = '<svg viewbox="0 0 1 1"><foreignobject></foreignobject></svg>',
        ['<my-el Foo="1"></my-el>'] = '<my-el foo="1"></my-el>',
        ['<p a=\'x"y\' b="x\'y" c=x"y>t</p>'] = '<p a="x&quot;y" b="x\'y" c="x&quot;y">t</p>',
        ['<p><![CDATA[x]]></p>'] = '<p><!--[CDATA[x]]--></p>',
        ['<p><?php echo 1 ?></p>'] = '<p><!--?php echo 1 ?--></p>',
        ['<p><!--a--!>b</p>'] = '<p><!--a-->b</p>',
        ['<p><!doctype html>x</p>'] = '<p>x</p>',
        ['<p a="1"b="2">t</p>'] = '<p a="1" b="2">t</p>',
        ['<p/a>t</p>'] = '<p a="">t</p>',
        ['<noscript><p>t</p></noscript>'] = '<noscript><p>t</p></noscript>',
        ['<xmp><b>t</b></xmp>'] = '<xmp><b>t</b></xmp>',
        ['<select><option>a<option>b</select>'] = '<select><option>a<option>b</option></option></select>',
    }
    for html, expected in pairs(cases) do
        assert.equal(render(html), expected)
    end
end

function testcase.render_keeps_the_approved_differences_from_the_javascript_version()
    -- Top-level text and comments are rendered.
    assert.equal(render('top <b>bold</b> tail<!--c-->'),
                 'top <b>bold</b> tail<!--c-->')
    assert.equal(render('   \n  '), '   \n  ')

    -- Elements left open at the end of the document are closed there.
    assert.equal(render('<p>unclosed at eof'), '<p>unclosed at eof</p>')
    assert.equal(render('<div/>t'), '<div>t</div>')
    assert.equal(render('<svg><path d="M0"/></svg>'),
                 '<svg><path d="M0"></path></svg>')

    -- Source attribute values keep their character references; only '"' is
    -- encoded so that the value survives double quotes.
    assert.equal(render('<p title="a&amp;b &lt; &quot; \' &#39; &gt;">t</p>'),
                 '<p title="a&amp;b &lt; &quot; \' &#39; &gt;">t</p>')
    assert.equal(render('<a href="?a=1&b=2" c=">">l</a>'),
                 '<a href="?a=1&b=2" c=">">l</a>')
end

function testcase.render_walks_deep_trees_without_c_recursion()
    local depth = 5000
    local html = string.rep('<div><span>', depth) .. 'x' ..
                     string.rep('</span></div>', depth)
    assert.equal(render(html), html)
end

function testcase.render_accepts_a_table_or_nil_as_data_and_repeats()
    local ir = assert(compile('page', '<p a="1">t</p>', 'x-', {}))
    local values = {
        interpret.render('page', ir),
    }
    assert.equal(values, {
        '<p a="1">t</p>',
    })
    assert.equal(interpret.render('page', ir, nil), '<p a="1">t</p>')
    assert.equal(interpret.render('page', ir, {
        a = 1,
    }), '<p a="1">t</p>')
end

function testcase.render_returns_runtime_errors_for_invalid_arguments()
    local ir = assert(compile('page', '<p>t</p>', 'x-', {}))
    local cases = {
        {
            args = {
                nil,
                ir,
            },
            message = 'name must be a non-empty string',
        },
        {
            args = {
                '',
                ir,
            },
            message = 'name must be a non-empty string',
        },
        {
            args = {
                'page',
                '<p>t</p>',
            },
            message = 'ir must be a compiled template',
            template_name = 'page',
        },
        {
            args = {
                'page',
                io.stdout,
            },
            message = 'ir must be a compiled template',
            template_name = 'page',
        },
        {
            args = {
                'page',
                newproxy and newproxy() or require('reflow.pool').new(0),
            },
            message = 'ir must be a compiled template',
            template_name = 'page',
        },
        {
            args = {
                'page',
                ir,
                1,
            },
            message = 'data must be a table or nil',
            template_name = 'page',
        },
    }
    for _, case in ipairs(cases) do
        local values = {
            interpret.render(case.args[1], case.args[2], case.args[3]),
        }
        assert.equal(#values, 2)
        assert.is_nil(values[1])
        assert_runtime_error(values[2], case.message, case.template_name)
    end
end

function testcase.render_reports_out_of_memory_and_recovers()
    local ok, kinds, recovered = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local ir = assert(compile('oom', string.rep('<p>abcdefgh</p>', 16384),
                                  'x-', {}))
        local kinds = {}

        -- Raise the limit step by step until the render succeeds.
        for extra = 16 * 1024, 2 * 1024 * 1024, 8 * 1024 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, out, err = pcall(interpret.render, 'oom', ir)
            memlimit.maxsize(0)
            if not called then
                assert(tostring(out):find('not enough memory', 1, true),
                       tostring(out))
            elseif out then
                kinds.success = #out == 16384 * 15
                break
            else
                assert(errorlib.typeof(err) == errors.ERUNTIME)
                assert(err.message.message == 'out of memory while rendering')
                assert(err.meta.template_name == 'oom')
                kinds[errorlib.unwrap(err) == nil and 'pool' or 'lua'] = true
            end
        end
        collectgarbage('collect')
        return kinds, interpret.render('oom', ir) ==
                   string.rep('<p>abcdefgh</p>', 16384)
    ]])
    assert.is_true(ok, kinds)
    -- Whether a failure is returned as an error or raised as a memory error
    -- depends on the Lua version: the library does not collect garbage, and
    -- Lua 5.1 and LuaJIT have no emergency collection.
    assert.is_true(kinds.success)
    assert.is_true(recovered)
end
