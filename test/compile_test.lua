require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- ===== root shape =====

function testcase.empty_html()
    local ir = assert(compiler.compile_template(''))
    assert.equal(ir.type, 'root')
    assert.equal(#ir.children, 0)
end

function testcase.text_only()
    local ir = assert(compiler.compile_template('hello world'))
    assert.equal(#ir.children, 1)
    assert.equal(ir.children[1].type, 'text')
    assert.equal(ir.children[1].text, 'hello world')
end

-- ===== element / children =====

function testcase.single_element()
    local ir = assert(compiler.compile_template('<div>x</div>'))
    assert.equal(#ir.children, 1)
    local div = ir.children[1]
    assert.equal(div.type, 'element')
    assert.equal(div.tag, 'div')
    assert.equal(#div.attrs, 0)
    assert.equal(#div.children, 1)
    assert.equal(div.children[1].type, 'text')
    assert.equal(div.children[1].text, 'x')
end

function testcase.nested_elements()
    local ir = assert(compiler.compile_template(
        '<section><p>a</p><p>b</p></section>'))
    local sec = ir.children[1]
    assert.equal(sec.tag, 'section')
    assert.equal(#sec.children, 2)
    assert.equal(sec.children[1].tag, 'p')
    assert.equal(sec.children[1].children[1].text, 'a')
    assert.equal(sec.children[2].tag, 'p')
    assert.equal(sec.children[2].children[1].text, 'b')
end

function testcase.void_element_as_leaf()
    local ir = assert(compiler.compile_template('<div><br><img></div>'))
    local div = ir.children[1]
    assert.equal(#div.children, 2)
    assert.equal(div.children[1].tag, 'br')
    assert.equal(#div.children[1].children, 0)
    assert.equal(div.children[2].tag, 'img')
    assert.equal(#div.children[2].children, 0)
end

-- ===== attributes stored raw (directives NOT yet parsed) =====

function testcase.regular_attrs()
    local ir = assert(compiler.compile_template(
        '<div id="a" class="b">x</div>'))
    local div = ir.children[1]
    assert.equal(#div.attrs, 2)
    assert.equal(div.attrs[1][1], 'id')
    assert.equal(div.attrs[1][2], 'a')
    assert.equal(div.attrs[2][1], 'class')
    assert.equal(div.attrs[2][2], 'b')
end

function testcase.x_attrs_stored_raw()
    -- Stage 1 does NOT split directives yet; every attr is on element.attrs.
    local ir = assert(compiler.compile_template(
        '<div x-if="$.ok" class="a">x</div>'))
    local div = ir.children[1]
    assert.equal(#div.attrs, 2)
    assert.equal(div.attrs[1][1], 'x-if')
    assert.equal(div.attrs[1][2], '$.ok')
    assert.equal(div.attrs[2][1], 'class')
end

function testcase.attr_without_value()
    local ir = assert(compiler.compile_template('<input disabled>'))
    local input = ir.children[1]
    assert.equal(input.attrs[1][1], 'disabled')
    assert.is_nil(input.attrs[1][2])
end

-- ===== comments =====

function testcase.comment_kept()
    local ir = assert(compiler.compile_template(
        '<div><!-- hi -->x</div>'))
    local div = ir.children[1]
    assert.equal(#div.children, 2)
    assert.equal(div.children[1].type, 'comment')
    assert.equal(div.children[1].text, ' hi ')
    assert.equal(div.children[2].type, 'text')
    assert.equal(div.children[2].text, 'x')
end

-- ===== doctype dropped =====

function testcase.doctype_dropped()
    local ir = assert(compiler.compile_template(
        '<!DOCTYPE html><div>x</div>'))
    -- doctype must not produce an IR node
    assert.equal(#ir.children, 1)
    assert.equal(ir.children[1].tag, 'div')
end

-- ===== text coalescing =====

function testcase.text_across_entities_is_one_node()
    -- html-rewriter reports "a ", "&amp;", " b" as separate chunks but they
    -- belong to a single text node. Coalescing produces one IR_TEXT.
    local ir = assert(compiler.compile_template('<p>a &amp; b</p>'))
    local p = ir.children[1]
    assert.equal(#p.children, 1)
    assert.equal(p.children[1].type, 'text')
    assert.equal(p.children[1].text, 'a &amp; b')
end

-- ===== source offsets =====

function testcase.source_offsets_preserved()
    local html = '<div>x</div>'
    --           0    5
    local ir = assert(compiler.compile_template(html))
    local div = ir.children[1]
    assert.equal(div.source_start, 0)
    assert.equal(div.source_end, 5)
end

-- ===== error propagation =====

function testcase.reject_unclosed()
    local r, err = compiler.compile_template('<div>x')
    assert.is_nil(r)
    assert.match(err, 'unclosed element')
end

function testcase.reject_mismatched_close()
    local r, err = compiler.compile_template('<div>x</span>')
    assert.is_nil(r)
    assert.match(err, 'mismatched close tag')
end

function testcase.reject_stray_close()
    local r, err = compiler.compile_template('</div>')
    assert.is_nil(r)
    assert.match(err, 'unexpected close tag')
end

-- ===== text between siblings kept =====

function testcase.text_between_siblings()
    local ir = assert(compiler.compile_template(
        '<ul><li>a</li> <li>b</li></ul>'))
    local ul = ir.children[1]
    assert.equal(#ul.children, 3)
    assert.equal(ul.children[1].tag, 'li')
    assert.equal(ul.children[2].type, 'text')
    assert.equal(ul.children[2].text, ' ')
    assert.equal(ul.children[3].tag, 'li')
end
