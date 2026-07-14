require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- helper: return the ordered list of `order` fields matching selector
-- against html.  A failing selector call raises the assertion so callers
-- do not have to manually unpack the two-value return.
local function orders(html, selector)
    local r, err = compiler.resolve_selector(html, selector)
    if not r then
        error(('resolve failed for %q: %s'):format(
            selector, err and (err.message or tostring(err)) or 'nil'))
    end
    local out = {}
    for i = 1, #r do
        out[i] = r[i].order
    end
    return out
end

-- helper: assert resolve fails with the given reason / feature.
local function fail(html, selector, reason, feature)
    local r, err = compiler.resolve_selector(html, selector)
    assert(not r, ('expected failure for %q'):format(selector))
    assert.equal(err.type, 'ReflowSelectorError')
    if reason then assert.equal(err.reason, reason) end
    if feature then assert.equal(err.feature, feature) end
end

local function seq(...)
    return {...}
end

-- Standard fixture for the majority of resolve cases: a single container
-- with a small set of tagged children plus one nested element.
local HTML = [[<div id="root" class="c1 c2">
<p class="a">1</p>
<p class="a b">2</p>
<span class="b">3</span>
<em>4</em>
<p><a href="/x">5</a></p>
</div>]]

-- ============================================================
-- basic anchors
-- ============================================================

function testcase.by_id_anchor()
    assert.equal(orders(HTML, '#root'), seq(1))
end

function testcase.by_class_anchor_single()
    assert.equal(orders(HTML, '.b'), seq(3, 4))
end

function testcase.by_class_anchor_intersection()
    -- .a.b — both classes required on the same element
    assert.equal(orders(HTML, '.a.b'), seq(3))
end

function testcase.by_tag_anchor()
    assert.equal(orders(HTML, 'p'), seq(2, 3, 6))
end

function testcase.by_attr_anchor()
    assert.equal(orders(HTML, '[href]'), seq(7))
end

function testcase.universal_anchor_falls_back_to_all()
    -- Bare `*` returns every element in document order.
    local o = orders(HTML, '*')
    assert.equal(#o, 7)
    for i = 1, 7 do assert.equal(o[i], i) end
end

-- ============================================================
-- combinators
-- ============================================================

function testcase.descendant_combinator()
    assert.equal(orders(HTML, 'div a'), seq(7))
end

function testcase.child_combinator()
    assert.equal(orders(HTML, 'div > a'), seq()) -- a is nested inside a p
    assert.equal(orders(HTML, 'p > a'), seq(7))
end

function testcase.chained_child_combinators()
    assert.equal(orders(HTML, 'div > p > a'), seq(7))
end

function testcase.selector_list_dedupes()
    -- Both selectors would match #root; a single result must appear.
    local o = orders(HTML, '#root, div')
    assert.equal(#o, 1)
    assert.equal(o[1], 1)
end

function testcase.selector_list_document_order()
    local o = orders(HTML, 'em, p.a')
    -- em is order=5, p.a occurrences are 2 and 3; document order returns
    -- 2, 3, 5.
    assert.equal(o, seq(2, 3, 5))
end

-- ============================================================
-- attribute operator semantics
-- ============================================================

local ATTR_HTML =
    '<a title="hello world"></a>' ..     -- 1
    '<a title="hello"></a>' ..           -- 2
    '<a title="hi-world"></a>' ..        -- 3
    '<a title=""></a>' ..                -- 4
    '<a title="foobar"></a>' ..          -- 5
    '<a data-empty></a>'                 -- 6 (attribute with no value)

function testcase.attr_presence()
    -- Every <a> above declares `title` except the last.
    assert.equal(orders(ATTR_HTML, '[title]'), seq(1, 2, 3, 4, 5))
end

function testcase.attr_equals()
    assert.equal(orders(ATTR_HTML, '[title=hello]'), seq(2))
end

function testcase.attr_tilde_equals()
    assert.equal(orders(ATTR_HTML, '[title~=hello]'), seq(1, 2))
end

function testcase.attr_pipe_equals()
    assert.equal(orders(ATTR_HTML, '[title|=hi]'), seq(3))
end

function testcase.attr_caret_equals()
    assert.equal(orders(ATTR_HTML, '[title^=hello]'), seq(1, 2))
end

function testcase.attr_dollar_equals()
    assert.equal(orders(ATTR_HTML, '[title$=world]'), seq(1, 3))
end

function testcase.attr_star_equals()
    assert.equal(orders(ATTR_HTML, '[title*=oo]'), seq(5))
end

function testcase.attr_valueless_attribute_matches_presence_only()
    -- data-empty has no value; presence check must still find it.
    assert.equal(orders(ATTR_HTML, '[data-empty]'), seq(6))
end

-- ============================================================
-- positional pseudo attachment (runtime evaluation is out of scope)
-- ============================================================

function testcase.positional_attached_to_candidate()
    local r = compiler.resolve_selector('<a></a><a></a><a></a>',
                                        ':first-child')
    assert(r)
    assert.equal(#r, 3)
    for i = 1, 3 do
        assert.equal(r[i].positional[1].name, 'first-child')
    end
end

function testcase.positional_nth_carries_argument()
    local r = compiler.resolve_selector('<a></a><a></a><a></a>',
                                        ':nth-child(2)')
    assert.equal(r[1].positional[1].name, 'nth-child')
    assert.equal(r[1].positional[1].n, 2)
end

function testcase.err_ancestor_positional_pseudo()
    fail('<div><p></p></div>', ':first-child p',
         'unsupported', 'pseudo-ancestor:first-child')
end

-- ============================================================
-- non-matching selectors return empty result
-- ============================================================

function testcase.non_matching_returns_empty_list()
    assert.equal(orders(HTML, '#missing'), seq())
    assert.equal(orders(HTML, '.no-such-class'), seq())
    assert.equal(orders(HTML, 'aside'), seq())
end

function testcase.empty_document_returns_empty()
    assert.equal(orders('', '*'), seq())
end

-- ============================================================
-- element attribute case handling
-- ============================================================

function testcase.tag_names_lowercased_on_both_sides()
    -- Uppercase tag in source is preserved as-is by lexbor's tokenizer
    -- (which lowercases); the selector parser also lowercases the tag,
    -- so DIV matches div.
    assert.equal(orders('<DIV></DIV>', 'div'), seq(1))
    assert.equal(orders('<div></div>', 'DIV'), seq(1))
end

function testcase.attr_names_lowercased()
    -- HTML attribute names are case-insensitive; the parser lowercases
    -- them and matching should honour that.
    assert.equal(orders('<a HREF="/x"></a>', '[href]'), seq(1))
end

function testcase.attr_values_case_sensitive()
    -- Attribute values must match exactly (case-sensitive), matching CSS.
    assert.equal(orders('<a title="Hi"></a>', '[title=hi]'), seq())
    assert.equal(orders('<a title="Hi"></a>', '[title=Hi]'), seq(1))
end
