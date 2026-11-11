-- Ports test/unit/scanner.test.js and test/unit/scanner-snippet-edge.test.js
-- of the JavaScript version. scanElementRanges finds the source range of
-- each element for HTMLRewriter, which reports none; the Lua version takes
-- the ranges from Lexbor tokens, so they are compared through the
-- reference interface ir:to_table(). The snippet cases that pass offsets
-- outside the source cannot happen through the public API and are listed
-- as not applicable in test/PARITY.md.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local compile = require('reflow.compile')
local reflow = require('reflow')

-- Return "tag=<start tag source>" of every element in document order.
local function ranges(html)
    local ir = assert(compile('t', html, 'x-', {}))
    local out = {}
    local stack = {
        ir:to_table().root,
    }
    while #stack > 0 do
        local node = table.remove(stack)
        if node.type == 'element' then
            out[#out + 1] = node.tag_name .. '=' ..
                                html:sub(node.source_start + 1, node.source_end)
        end
        local children = node.children or {}
        for i = #children, 1, -1 do
            stack[#stack + 1] = children[i]
        end
    end
    return out
end

function testcase.scans_plain_elements()
    assert.equal(ranges('<div><p>hi</p><span/></div>'), {
        'div=<div>',
        'p=<p>',
        'span=<span/>',
    })
end

function testcase.skips_comments()
    assert.equal(ranges('<!-- <p>ignored</p> --><div><p>hi</p></div>'), {
        'div=<div>',
        'p=<p>',
    })
end

function testcase.skips_doctype()
    assert.equal(ranges('<!DOCTYPE html><div></div>'), {
        'div=<div>',
    })
end

function testcase.respects_quoted_attribute_values_with_gt_inside()
    assert.equal(ranges('<a href="x?y=1>2">link</a>'), {
        'a=<a href="x?y=1>2">',
    })
end

function testcase.skips_script_content()
    assert.equal(ranges('<script>if (a<b) {}</script><p>hi</p>'), {
        'script=<script>',
        'p=<p>',
    })
end

function testcase.skips_style_content()
    assert.equal(ranges('<style>a>b{color:red}</style><p>hi</p>'), {
        'style=<style>',
        'p=<p>',
    })
end

function testcase.handles_nested_elements()
    assert.equal(ranges('<div><section><p>hi</p></section></div>'), {
        'div=<div>',
        'section=<section>',
        'p=<p>',
    })
end

function testcase.handles_processing_instructions()
    assert.equal(ranges('<?xml version="1.0"?><div></div>'), {
        'div=<div>',
    })
end

function testcase.handles_stray_lt_character()
    assert.equal(ranges('<div>a < b</div>'), {
        'div=<div>',
    })
end

function testcase.handles_unterminated_comment_doctype_and_processing_instruction()
    assert.equal(ranges('<!-- never closed <div></div>'), {})
    assert.equal(ranges('<!DOCTYPE never closed'), {})
    assert.equal(ranges('<?xml never closed'), {})
end

function testcase.handles_unterminated_close_tag()
    assert.equal(ranges('<div></div  never closed'), {
        'div=<div>',
    })
end

function testcase.handles_unterminated_open_tag()
    assert.equal(ranges('<div attr="never closed'), {})
end

function testcase.handles_unterminated_script_content()
    assert.equal(ranges('<script>never closed'), {
        'script=<script>',
    })
end

function testcase.normalizes_uppercase_tag_names()
    assert.equal(ranges('<DIV></DIV>'), {
        'div=<DIV>',
    })
end

function testcase.snippet_highlight_of_a_range_spanning_lines()
    -- makeSnippet: a start tag spanning lines is highlighted to the end of
    -- its first line; the expected snippet is the one of the JavaScript
    -- version for the same template
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t', 'aaa<b\nx-else\n>ccc</b>')
    assert.is_nil(ok)
    assert.equal(err.meta.snippet, table.concat({
        '1 | aaa<b',
        '  |    ^^',
        '2 | x-else',
        '3 | >ccc</b>',
    }, '\n'))
end
