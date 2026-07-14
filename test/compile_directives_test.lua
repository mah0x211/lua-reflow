require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

local function C(html, opts)
    return compiler.compile_template(html, opts)
end

-- ===== x-data =====

function testcase.data_stored_raw_and_validated()
    local ir = assert(C('<div x-data="a: 1, b: {c: 2}">x</div>'))
    local div = ir.children[1]
    assert.equal(div.directives.data_raw, 'a: 1, b: {c: 2}')
end

function testcase.data_invalid_json5_rejected()
    local r, err = C('<div x-data="!!!bad!!!">x</div>')
    assert.is_nil(r)
    assert.match(err, 'x-data')
end

function testcase.data_duplicate_dropped_by_tokenizer()
    -- HTML5 tokenizer keeps only the FIRST attribute occurrence, so a
    -- literal duplicate x-data can't reach us. The stored raw slice is
    -- the value of the first occurrence.
    local ir = assert(C('<div x-data="a:1" x-data="b:2">x</div>'))
    assert.equal(ir.children[1].directives.data_raw, 'a:1')
end

-- ===== x-with =====

function testcase.with_bindings_recorded()
    local ir = assert(C('<div x-with="a = 1, b = 2">x</div>'))
    local d = ir.children[1].directives
    assert.equal(#d.with, 2)
    assert.equal(d.with[1], 'a')
    assert.equal(d.with[2], 'b')
end

function testcase.with_duplicate_rejected()
    local r, err = C('<div x-with="a=1, a=2">x</div>')
    assert.is_nil(r)
    assert.match(err, 'duplicate')
end

-- ===== x-if / elseif / else =====

function testcase.if_recorded()
    local ir = assert(C('<div x-if="$.ok">x</div>'))
    assert.is_true(ir.children[1].directives.if_expr)
end

function testcase.elseif_recorded()
    local ir = assert(C('<div x-elseif="$.ok">x</div>'))
    assert.is_true(ir.children[1].directives.elseif_expr)
end

function testcase.else_marker()
    local ir = assert(C('<div x-else>x</div>'))
    assert.is_true(ir.children[1].directives.else_mark)
end

function testcase.else_with_value_rejected()
    local r, err = C('<div x-else="x">y</div>')
    assert.is_nil(r)
    assert.match(err, 'must not have a value')
end

-- ===== x-match / case / nocase =====

function testcase.match_recorded()
    local ir = assert(C('<div x-match="$.x">c</div>'))
    assert.is_true(ir.children[1].directives.match_expr)
end

function testcase.case_recorded()
    local ir = assert(C('<div x-case="1">c</div>'))
    assert.is_true(ir.children[1].directives.case_expr)
end

function testcase.nocase_marker()
    local ir = assert(C('<div x-nocase>c</div>'))
    assert.is_true(ir.children[1].directives.nocase_mark)
end

-- ===== x-for =====

function testcase.for_recorded()
    local ir = assert(C('<div x-for="i = 1, 5">x</div>'))
    local s = ir.children[1].directives.for_spec
    assert.equal(s.var, 'i')
    assert.equal(s.start, 1)
    assert.equal(s.stop, 5)
    assert.equal(s.step, 1)
end

-- ===== x-each =====

function testcase.each_recorded()
    local ir = assert(C('<div x-each="item in $.items">x</div>'))
    local s = ir.children[1].directives.each_spec
    assert.equal(s.item, 'item')
    assert.is_nil(s.index)
end

function testcase.each_with_index()
    local ir = assert(C('<li x-each="v, i in $.items">x</li>'))
    local s = ir.children[1].directives.each_spec
    assert.equal(s.item, 'v')
    assert.equal(s.index, 'i')
end

-- ===== x-text / html / include =====

function testcase.text_recorded()
    local ir = assert(C('<span x-text="$.name"></span>'))
    assert.is_true(ir.children[1].directives.text)
end

function testcase.html_recorded()
    local ir = assert(C('<div x-html="$.raw"></div>'))
    assert.is_true(ir.children[1].directives.html)
end

function testcase.include_recorded()
    local ir = assert(C('<div x-include="\'partial\'"></div>'))
    assert.is_true(ir.children[1].directives.include)
end

-- ===== x-break / break-if =====

function testcase.break_marker()
    local ir = assert(C('<li x-break></li>'))
    assert.is_true(ir.children[1].directives.break_mark)
end

function testcase.break_if_recorded()
    local ir = assert(C('<li x-break-if="$.done"></li>'))
    assert.is_true(ir.children[1].directives.break_if_expr)
end

-- ===== x-bind:<attr> =====

function testcase.bind_recorded()
    local ir = assert(C('<a x-bind:href="$.url">l</a>'))
    local d = ir.children[1].directives
    assert.equal(#d.binds, 1)
    assert.equal(d.binds[1], 'href')
end

function testcase.bind_multiple_distinct()
    local ir = assert(C('<a x-bind:href="$.u" x-bind:title="$.t">l</a>'))
    local d = ir.children[1].directives
    assert.equal(#d.binds, 2)
end

function testcase.bind_duplicate_dropped_by_tokenizer()
    -- Same as x-data: HTML5 tokenizer keeps only the first occurrence.
    local ir = assert(C(
        '<a x-bind:href="$.a" x-bind:href="$.b">l</a>'))
    local d = ir.children[1].directives
    assert.equal(#d.binds, 1)
end

function testcase.bind_empty_target_rejected()
    local r, err = C('<a x-bind:="$.a">l</a>')
    assert.is_nil(r)
    assert.match(err, 'attribute name after "bind:"')
end

-- ===== unknown directive =====

function testcase.unknown_directive_rejected()
    local r, err = C('<div x-unknown="x">y</div>')
    assert.is_nil(r)
    assert.match(err, 'unknown directive')
end

-- ===== combination rules =====

function testcase.reject_if_and_match()
    local r, err = C('<div x-if="$.a" x-match="$.b">x</div>')
    assert.is_nil(r)
    assert.match(err, 'conflicting structural directives')
end

function testcase.reject_for_and_each()
    local r, err = C('<div x-for="i=1,5" x-each="v in $.a">x</div>')
    assert.is_nil(r)
    assert.match(err, 'conflicting iteration directives')
end

function testcase.reject_text_and_html()
    local r, err = C('<div x-text="$.a" x-html="$.b"></div>')
    assert.is_nil(r)
    assert.match(err, 'conflicting content directives')
end

function testcase.reject_break_and_break_if()
    local r, err = C('<li x-break x-break-if="$.x"></li>')
    assert.is_nil(r)
    assert.match(err, 'conflicting control directives')
end

function testcase.reject_structural_with_iteration()
    local r, err = C('<div x-if="$.a" x-each="v in $.b">x</div>')
    assert.is_nil(r)
    assert.match(err, 'structural directive with iteration')
end

function testcase.reject_structural_with_control()
    local r, err = C('<div x-if="$.a" x-break>x</div>')
    assert.is_nil(r)
    assert.match(err, 'structural directive with control')
end

function testcase.reject_iteration_with_control()
    local r, err = C('<div x-each="v in $.a" x-break>x</div>')
    assert.is_nil(r)
    assert.match(err, 'iteration directive with control')
end

-- ===== helper name validation =====

function testcase.helper_registered()
    local ir = assert(C('<span x-text="fmt($.n)">x</span>',
                        { helpers = { 'fmt' } }))
    assert.is_true(ir.children[1].directives.text)
end

function testcase.helper_set_form()
    local ir = assert(C('<span x-text="fmt($.n)">x</span>',
                        { helpers = { fmt = true } }))
    assert.is_true(ir.children[1].directives.text)
end

function testcase.helper_unknown_rejected()
    local r, err = C('<span x-text="unknownfn($.n)">x</span>')
    assert.is_nil(r)
    assert.match(err, 'unknown helper')
    assert.match(err, 'unknownfn')
end

-- ===== K-only invisible marker =====

function testcase.k_only_marks_invisible()
    local ir = assert(C('<li x-break></li>'))
    assert.is_true(ir.children[1].invisible_marker)
end

function testcase.k_with_regular_attr_not_invisible()
    local ir = assert(C('<li x-break class="a"></li>'))
    -- class="a" is a regular attr; invisibility requires zero regular attrs
    assert.is_nil(ir.children[1].invisible_marker or nil)
end

-- ===== custom prefix =====

function testcase.custom_prefix_data()
    local ir = assert(C('<div data-if="$.x">y</div>', { prefix = 'data-' }))
    assert.is_true(ir.children[1].directives.if_expr)
end

-- ===== integration with SAX-to-tree output =====

function testcase.regular_attrs_still_pass_through()
    local ir = assert(C('<a href="/x" class="c">l</a>'))
    local a = ir.children[1]
    assert.equal(#a.attrs, 2)
    assert.is_nil(next(a.directives))
end
