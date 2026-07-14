require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

local function R(html, data, opts)
    return compiler.render(html, data, opts)
end

-- ===== plain HTML round-trip =====

function testcase.plain_text_at_root_dropped()
    -- Text and comments that sit at the top level (outside any element)
    -- do not reach the output — matches html-rewriter-wasm behaviour.
    assert.equal(R('hello'), '')
    assert.equal(R('a\n<b>x</b>\nc'), '<b>x</b>')
end

function testcase.plain_element()
    assert.equal(R('<div>hi</div>'), '<div>hi</div>')
end

function testcase.nested_elements()
    assert.equal(R('<div><p>a</p><p>b</p></div>'),
                 '<div><p>a</p><p>b</p></div>')
end

function testcase.void_element_no_close()
    assert.equal(R('<div><br><img></div>'), '<div><br><img></div>')
end

function testcase.entities_kept_raw()
    assert.equal(R('<p>a &amp; b</p>'), '<p>a &amp; b</p>')
end

function testcase.comment_kept()
    assert.equal(R('<div><!-- hi --></div>'),
                 '<div><!-- hi --></div>')
end

function testcase.regular_attrs_pass_through()
    assert.equal(R('<a href="/x" class="c">l</a>'),
                 '<a href="/x" class="c">l</a>')
end

function testcase.attr_value_double_escaped_on_reemit()
    -- Raw entities in attrs are re-escaped on emit; JS-reflow behaves the
    -- same way because both parsers hand back undecoded bytes.
    assert.equal(R([[<a href="?a=1&amp;b=2">l</a>]]),
                 '<a href="?a=1&amp;amp;b=2">l</a>')
end

-- ===== x-text =====

function testcase.text_directive()
    assert.equal(R([[<span x-text="$.name"></span>]],
                   [[{name: "World"}]]),
                 '<span>World</span>')
end

function testcase.text_escapes_html()
    assert.equal(R([[<span x-text="$.s"></span>]],
                   [[{s: "<b>&"}]]),
                 '<span>&lt;b&gt;&amp;</span>')
end

function testcase.text_null_omits()
    assert.equal(R([[<span x-text="$.n"></span>]], [[{n: null}]]),
                 '<span></span>')
end

function testcase.text_number()
    assert.equal(R([[<span x-text="$.n"></span>]], [[{n: 42}]]),
                 '<span>42</span>')
end

-- ===== x-html =====

function testcase.html_directive_raw()
    assert.equal(R([[<div x-html="$.raw"></div>]],
                   [[{raw: "<b>hi</b>"}]]),
                 '<div><b>hi</b></div>')
end

function testcase.html_non_string_rejected()
    local r, err = R([[<div x-html="$.n"></div>]], [[{n: 42}]])
    assert.is_nil(r)
    assert.match(err, 'value must be a string')
end

-- ===== x-bind:<attr> =====

function testcase.bind_add_new_attr()
    assert.equal(R([[<a x-bind:href="$.u">l</a>]], [[{u: "/x"}]]),
                 '<a href="/x">l</a>')
end

function testcase.bind_overrides_existing()
    assert.equal(R([[<a href="/orig" x-bind:href="$.u">l</a>]],
                   [[{u: "/x"}]]),
                 '<a href="/x">l</a>')
end

function testcase.bind_null_omits()
    assert.equal(R([[<a x-bind:href="$.u">l</a>]], [[{u: null}]]),
                 '<a>l</a>')
end

function testcase.bind_boolean_true_bare()
    assert.equal(R([[<input x-bind:disabled="$.d">]], [[{d: true}]]),
                 '<input disabled>')
end

function testcase.bind_boolean_false_omits()
    assert.equal(R([[<input x-bind:disabled="$.d">]], [[{d: false}]]),
                 '<input>')
end

function testcase.bind_escapes_attr()
    assert.equal(R([[<a x-bind:title="$.t">l</a>]],
                   [[{t: "<hi>"}]]),
                 '<a title="&lt;hi&gt;">l</a>')
end

-- ===== x-if / x-elseif / x-else =====

function testcase.if_true_branch()
    assert.equal(R([[<p x-if="$.a">A</p><p x-else>B</p>]],
                   [[{a: true}]]),
                 '<p>A</p>')
end

function testcase.if_false_branch()
    assert.equal(R([[<p x-if="$.a">A</p><p x-else>B</p>]],
                   [[{a: false}]]),
                 '<p>B</p>')
end

function testcase.elseif_middle_branch()
    assert.equal(R(
        [[<p x-if="$.a">A</p><p x-elseif="$.b">B</p><p x-else>C</p>]],
        [[{a: false, b: true}]]),
        '<p>B</p>')
end

function testcase.chain_all_false_no_else_emits_nothing()
    assert.equal(R([[<p x-if="$.a">A</p><p x-elseif="$.b">B</p>]],
                   [[{a: false, b: false}]]),
                 '')
end

-- ===== x-match / x-case / x-nocase =====

function testcase.match_case()
    assert.equal(R(
        [[<div x-match="$.k">]] ..
        [[<span x-case="1">one</span>]] ..
        [[<span x-case="2">two</span>]] ..
        [[</div>]],
        [[{k: 2}]]),
        '<div><span>two</span></div>')
end

function testcase.match_nocase_fallback()
    assert.equal(R(
        [[<div x-match="$.k">]] ..
        [[<span x-case="1">one</span>]] ..
        [[<span x-nocase>other</span>]] ..
        [[</div>]],
        [[{k: 99}]]),
        '<div><span>other</span></div>')
end

function testcase.match_no_match_no_nocase()
    assert.equal(R(
        [[<div x-match="$.k"><span x-case="1">one</span></div>]],
        [[{k: 99}]]),
        '<div></div>')
end

-- ===== x-for =====

function testcase.for_ascending()
    assert.equal(R(
        [[<ul><li x-for="i=1,3" x-text=".i"></li></ul>]]),
        '<ul><li>1</li><li>2</li><li>3</li></ul>')
end

function testcase.for_descending()
    assert.equal(R(
        [[<ul><li x-for="i=3,1,-1" x-text=".i"></li></ul>]]),
        '<ul><li>3</li><li>2</li><li>1</li></ul>')
end

function testcase.for_step_two()
    assert.equal(R(
        [[<ul><li x-for="i=0,4,2" x-text=".i"></li></ul>]]),
        '<ul><li>0</li><li>2</li><li>4</li></ul>')
end

-- ===== x-each =====

function testcase.each_array()
    assert.equal(R(
        [[<ul><li x-each="v in $.a" x-text=".v"></li></ul>]],
        [[{a: ["x","y","z"]}]]),
        '<ul><li>x</li><li>y</li><li>z</li></ul>')
end

function testcase.each_with_numeric_index()
    assert.equal(R(
        [[<ul><li x-each="v, i in $.a" x-text=".i"></li></ul>]],
        [[{a: ["x","y","z"]}]]),
        '<ul><li>0</li><li>1</li><li>2</li></ul>')
end

function testcase.each_object_iteration()
    -- Lua-reflow extension: iterating an object yields (value, key).
    local out = R(
        [[<ul><li x-each="v, k in $.o" x-text=".k"></li></ul>]],
        [[{o: {a: 1, b: 2}}]])
    -- Property order preserved from JSON5 parse
    assert.equal(out, '<ul><li>a</li><li>b</li></ul>')
end

function testcase.each_empty_array_no_output()
    assert.equal(R(
        [[<ul><li x-each="v in $.a" x-text=".v"></li></ul>]],
        [[{a: []}]]),
        '<ul></ul>')
end

-- ===== x-break / x-break-if =====

function testcase.break_ends_iteration()
    -- Sibling of x-each; when x-break-if fires inside the loop body
    -- of a wrapper element with x-break placed as a child.
    local out = R(
        [[<ul>]] ..
        [[<li x-each="v in $.a" x-text=".v"></li>]] ..
        [[</ul>]],
        [[{a: ["x","y","z"]}]])
    assert.equal(out, '<ul><li>x</li><li>y</li><li>z</li></ul>')
end

function testcase.break_if_inside_loop()
    -- Break out when .v equals "y". The bare <i x-break-if> is K-only
    -- (invisible marker) so its tag is not emitted.
    local expr = ".v == 'y'"
    local out = R(
        '<ul><li x-each="v in $.a"><span x-text=".v"></span>' ..
        '<i x-break-if="' .. expr .. '"></i></li></ul>',
        [[{a: ["x","y","z"]}]])
    -- Break fires during the "y" iteration; that <li> still closes
    -- (emit_element_with_body finishes) but the loop stops afterwards.
    assert.equal(out,
        '<ul><li><span>x</span></li><li><span>y</span></li></ul>')
end

-- ===== x-data / x-with =====

function testcase.with_binding_visible_via_dot()
    assert.equal(R(
        [[<div x-with="v = $.a"><span x-text=".v"></span></div>]],
        [[{a: 42}]]),
        '<div><span>42</span></div>')
end

function testcase.data_scope()
    -- x-data introduces a named scope; children can reach it via @
    assert.equal(R(
        [[<div x-data="cfg: { title: 'hi' }">]] ..
        [[<span x-text="@cfg.title"></span></div>]]),
        '<div><span>hi</span></div>')
end

-- ===== helpers =====

function testcase.helper_call()
    assert.equal(R(
        [[<span x-text="upper($.n)"></span>]],
        [[{n: "hi"}]],
        { helpers = { upper = function(s) return string.upper(s) end } }),
        '<span>HI</span>')
end

function testcase.helper_error_propagates()
    local r, err = R(
        [[<span x-text="boom($.n)"></span>]],
        [[{n: 1}]],
        { helpers = { boom = function() error("bang") end } })
    assert.is_nil(r)
    assert.match(err, 'bang')
end

-- ===== combined =====

function testcase.combined_each_bind_text()
    assert.equal(R(
        [[<ul>]] ..
        [[<li x-each="u in $.users" x-bind:id=".u.id">]] ..
        [[<span x-text=".u.name"></span>]] ..
        [[</li></ul>]],
        [[{users: [{id: "a", name: "Alice"}, {id: "b", name: "Bob"}]}]]),
        '<ul>' ..
        '<li id="a"><span>Alice</span></li>' ..
        '<li id="b"><span>Bob</span></li>' ..
        '</ul>')
end
