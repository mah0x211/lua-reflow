-- Ports test/unit/snippet.test.js of the JavaScript version through the
-- public API: offsetToLineCol and makeSnippet are observed as the line,
-- the column, and the snippet of a compile error at the element.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local reflow = require('reflow')

-- Return the meta of the compile error of html, whose offending element is
-- the one with x-else.
local function error_meta(html)
    local engine = assert(reflow.new())
    local ok, err = engine:compile('t', html)
    assert.is_nil(ok)
    return err.meta
end

function testcase.line_and_column_at_start()
    local meta = error_meta('<b x-else>x</b>')
    assert.equal(meta.line, 1)
    assert.equal(meta.column, 1)
end

function testcase.line_and_column_within_first_line()
    local meta = error_meta('abc<b x-else>x</b>')
    assert.equal(meta.line, 1)
    assert.equal(meta.column, 4)
end

function testcase.line_and_column_across_lines()
    local meta = error_meta('hello\n<b x-else>x</b>\n')
    assert.equal(meta.line, 2)
    assert.equal(meta.column, 1)
    meta = error_meta('hello\nwo<b x-else>x</b>\n')
    assert.equal(meta.line, 2)
    assert.equal(meta.column, 3)
end

function testcase.snippet_includes_context_lines_and_caret()
    local meta = error_meta('aaa\nbbb\n<b x-else>ccc</b>\nddd\neee\n')
    assert.match(meta.snippet, '2 | bbb', true)
    assert.match(meta.snippet, '3 | <b x-else>ccc</b>', true)
    assert.match(meta.snippet, '4 | ddd', true)
    assert.match(meta.snippet, '^^^', true)
end
