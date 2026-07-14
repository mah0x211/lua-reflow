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
    -- x-if triggers chain wrapping around the element
    assert.equal(ir.children[1].type, 'chain')
end

function testcase.elseif_recorded_in_chain()
    local ir = assert(C(
        '<div x-if="$.a">a</div><div x-elseif="$.b">b</div>'))
    assert.equal(ir.children[1].type, 'chain')
    assert.equal(ir.children[1].n_branches, 2)
end

function testcase.else_marker_in_chain()
    local ir = assert(C(
        '<div x-if="$.a">a</div><div x-else>b</div>'))
    assert.equal(ir.children[1].type, 'chain')
    assert.equal(ir.children[1].n_branches, 2)
end

function testcase.else_with_value_rejected()
    local r, err = C('<div x-if="$.a">y</div><div x-else="x">z</div>')
    assert.is_nil(r)
    assert.match(err, 'must not have a value')
end

-- ===== x-match / case / nocase =====

function testcase.match_recorded()
    local ir = assert(C(
        '<div x-match="$.x"><span x-case="1">a</span></div>'))
    -- x-match consumes its case children into directives.match; the
    -- element itself remains at the top with cleared children.
    local m = ir.children[1]
    assert.equal(m.type, 'element')
    assert.is_true(m.directives.match_expr)
end

function testcase.case_recorded_in_match()
    local ir = assert(C(
        '<div x-match="$.x"><span x-case="1">a</span></div>'))
    -- inner case is not orphan (parent has x-match)
    assert.equal(ir.children[1].type, 'element')
end

function testcase.nocase_marker_in_match()
    local ir = assert(C(
        '<div x-match="$.x">' ..
        '<span x-case="1">a</span><span x-nocase>fb</span></div>'))
    assert.equal(ir.children[1].type, 'element')
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

function testcase.break_marker_inside_loop()
    local ir = assert(C(
        '<ul x-each="v in $.a"><li x-break></li></ul>'))
    local ul = ir.children[1]
    assert.is_true(ul.children[1].directives.break_mark)
end

function testcase.break_if_recorded_inside_loop()
    local ir = assert(C(
        '<ul x-each="v in $.a"><li x-break-if="$.done"></li></ul>'))
    local ul = ir.children[1]
    assert.is_true(ul.children[1].directives.break_if_expr)
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

function testcase.k_only_marks_invisible_inside_loop()
    local ir = assert(C(
        '<ul x-each="v in $.a"><li x-break></li></ul>'))
    local li = ir.children[1].children[1]
    assert.is_true(li.invisible_marker)
end

function testcase.k_with_regular_attr_not_invisible()
    local ir = assert(C(
        '<ul x-each="v in $.a"><li x-break class="a"></li></ul>'))
    -- class="a" is a regular attr; invisibility requires zero regular attrs
    local li = ir.children[1].children[1]
    assert.is_nil(li.invisible_marker or nil)
end

-- ===== custom prefix =====

function testcase.custom_prefix_data()
    local ir = assert(C('<div data-if="$.x">y</div>', { prefix = 'data-' }))
    -- data-if becomes a chain the same way x-if does
    assert.equal(ir.children[1].type, 'chain')
end

-- ===== integration with existing stage-1 behavior =====

function testcase.regular_attrs_still_pass_through()
    local ir = assert(C('<a href="/x" class="c">l</a>'))
    local a = ir.children[1]
    assert.equal(#a.attrs, 2)
    assert.is_nil(next(a.directives))
end
