require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- ===== element / endtag pairing =====

function testcase.element_open_close()
    local events = assert(compiler.parse_html('<div>hi</div>'))
    assert.equal(#events, 3)
    -- element
    assert.equal(events[1][1], 'element')
    assert.equal(events[1][2], 'div')
    assert.equal(#events[1][3], 0)          -- no attrs
    assert.is_false(events[1][4])            -- not self-closing
    -- text
    assert.equal(events[2][1], 'text')
    assert.equal(events[2][2], 'hi')
    -- endtag
    assert.equal(events[3][1], 'endtag')
    assert.equal(events[3][2], 'div')
    -- token matches
    assert.equal(events[3][3], events[1][5])
end

function testcase.tag_lowercased()
    local events = assert(compiler.parse_html('<DIV></DIV>'))
    assert.equal(events[1][2], 'div')
    assert.equal(events[2][2], 'div')
end

function testcase.nested_elements()
    local events = assert(compiler.parse_html(
        '<div><p>x</p></div>'))
    assert.equal(#events, 5)
    assert.equal(events[1][2], 'div')
    assert.equal(events[2][2], 'p')
    assert.equal(events[3][2], 'x')          -- text
    assert.equal(events[4][2], 'p')          -- endtag
    assert.equal(events[5][2], 'div')        -- endtag
    -- outer close pairs with outer open, inner with inner
    assert.equal(events[4][3], events[2][5]) -- p pair
    assert.equal(events[5][3], events[1][5]) -- div pair
end

-- ===== void elements =====

function testcase.void_element_no_endtag()
    local events = assert(compiler.parse_html('<div><br><img></div>'))
    -- div, br(self-closing), img(self-closing), div-end
    assert.equal(#events, 4)
    assert.equal(events[1][2], 'div')
    assert.is_false(events[1][4])
    assert.equal(events[2][2], 'br')
    assert.is_true(events[2][4])
    assert.equal(events[3][2], 'img')
    assert.is_true(events[3][4])
    assert.equal(events[4][1], 'endtag')
    assert.equal(events[4][2], 'div')
end

-- ===== attributes =====

function testcase.attr_quoted()
    local events = assert(compiler.parse_html('<div id="a" class="b">x</div>'))
    local attrs = events[1][3]
    assert.equal(#attrs, 2)
    assert.equal(attrs[1][1], 'id')
    assert.equal(attrs[1][2], 'a')
    assert.equal(attrs[2][1], 'class')
    assert.equal(attrs[2][2], 'b')
end

function testcase.attr_unquoted()
    local events = assert(compiler.parse_html('<input type=text>'))
    local attrs = events[1][3]
    assert.equal(attrs[1][1], 'type')
    assert.equal(attrs[1][2], 'text')
end

function testcase.attr_single_quotes()
    local events = assert(compiler.parse_html("<div id='a b'>x</div>"))
    local attrs = events[1][3]
    assert.equal(attrs[1][2], 'a b')  -- quotes stripped, spaces kept
end

function testcase.attr_no_value()
    local events = assert(compiler.parse_html('<input disabled>'))
    local attrs = events[1][3]
    assert.equal(attrs[1][1], 'disabled')
    assert.is_nil(attrs[1][2])
end

function testcase.attr_x_directive_raw()
    -- attribute values must reach the caller verbatim
    local events = assert(compiler.parse_html('<div x-text="$.name">x</div>'))
    local attrs = events[1][3]
    assert.equal(attrs[1][1], 'x-text')
    assert.equal(attrs[1][2], '$.name')
end

-- ===== entities NOT decoded =====

function testcase.text_entities_not_decoded()
    local events = assert(compiler.parse_html('<p>a &amp; b &lt; c</p>'))
    assert.equal(events[2][1], 'text')
    assert.equal(events[2][2], 'a &amp; b &lt; c')
end

function testcase.attr_entities_not_decoded()
    local events = assert(compiler.parse_html([[<a href="?x=1&amp;y=2">l</a>]]))
    local attrs = events[1][3]
    assert.equal(attrs[1][2], '?x=1&amp;y=2')
end

-- ===== comments =====

function testcase.comment()
    local events = assert(compiler.parse_html(
        '<div><!-- hi --></div>'))
    assert.equal(events[1][1], 'element')
    assert.equal(events[2][1], 'comment')
    assert.equal(events[2][2], ' hi ')
    assert.equal(events[3][1], 'endtag')
end

-- ===== doctype dropped =====

function testcase.doctype_dropped()
    local events = assert(compiler.parse_html(
        '<!DOCTYPE html><div>x</div>'))
    -- doctype must NOT produce an event
    assert.equal(events[1][1], 'element')
    assert.equal(events[1][2], 'div')
end

-- ===== source offsets =====

function testcase.source_offsets()
    local html = '<div>x</div>'
    --           0    5
    local events = assert(compiler.parse_html(html))
    local el = events[1]
    -- element source_start = offset of '<' (index 6),
    -- source_end = one past '>' (index 7)
    assert.equal(el[6], 0)      -- '<'
    assert.equal(el[7], 5)      -- one past '>' (exclusive)
end

function testcase.source_offsets_close()
    local html = '<div>x</div>'
    --           012345678901
    local events = assert(compiler.parse_html(html))
    -- endtag has no source offsets in the event schema, but element[7]
    -- for the open tag ends before the text starts.
    assert.equal(events[1][6], 0)
    assert.equal(events[1][7], 5)
end

-- ===== error cases =====

function testcase.reject_unclosed()
    local r, err = compiler.parse_html('<div>x')
    assert.is_nil(r)
    assert.match(err, 'unclosed element')
end

function testcase.reject_mismatched_close()
    local r, err = compiler.parse_html('<div>x</span>')
    assert.is_nil(r)
    assert.match(err, 'mismatched close tag')
end

function testcase.reject_stray_close()
    local r, err = compiler.parse_html('</div>')
    assert.is_nil(r)
    assert.match(err, 'unexpected close tag')
end

-- ===== empty input =====

function testcase.empty()
    local events = assert(compiler.parse_html(''))
    assert.equal(#events, 0)
end

-- ===== last_in_text_node marker =====

function testcase.text_last_in_node()
    local events = assert(compiler.parse_html('<p>hi</p>'))
    assert.equal(events[2][1], 'text')
    -- All text tokens set last_in_text_node true (no chunk coalescing yet)
    assert.is_true(events[2][3])
end
