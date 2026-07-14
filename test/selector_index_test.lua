require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

local function build(html)
    local r, err = compiler.build_selector_index(html)
    assert(r, ('index build failed: %s'):format(tostring(err)))
    return r
end

-- helper: return the annotation for the element whose tag matches `tag`
-- and whose 1-based occurrence in document order is `nth`. Fails the
-- test if no such element exists.
local function pick(idx, tag, nth)
    nth = nth or 1
    local seen = 0
    for i = 1, #idx.all do
        local ann = idx.annotations[idx.all[i]]
        if ann.tag == tag then
            seen = seen + 1
            if seen == nth then return ann end
        end
    end
    error(('no #%d <%s> in index'):format(nth, tag))
end

-- ============================================================
-- annotations
-- ============================================================

function testcase.order_is_document_order()
    local i = build('<a></a><b></b><c></c>')
    assert.equal(pick(i, 'a').order, 1)
    assert.equal(pick(i, 'b').order, 2)
    assert.equal(pick(i, 'c').order, 3)
end

function testcase.depth_starts_at_zero()
    local i = build('<a><b><c></c></b></a>')
    assert.equal(pick(i, 'a').depth, 0)
    assert.equal(pick(i, 'b').depth, 1)
    assert.equal(pick(i, 'c').depth, 2)
end

function testcase.parent_points_at_enclosing_element()
    local i = build('<a><b><c></c></b></a>')
    local a = pick(i, 'a')
    local b = pick(i, 'b')
    local c = pick(i, 'c')
    assert.equal(a.parent, nil)  -- root child has no parent element
    assert.equal(b.parent, a.order)
    assert.equal(c.parent, b.order)
end

function testcase.chain_branches_share_parent_and_depth()
    local i = build(
        '<div><p x-if="$.a">A</p><p x-elseif="$.b">B</p><p x-else>C</p></div>')
    local div = pick(i, 'div')
    -- Every branch element points to the enclosing <div> as parent and
    -- sits at the same depth as an ordinary child of that div.
    for k = 1, 3 do
        local p = pick(i, 'p', k)
        assert.equal(p.parent, div.order)
        assert.equal(p.depth, div.depth + 1)
        assert.equal(p.chainBranch, k - 1)
    end
end

function testcase.match_cases_transparent()
    local i = build(
        '<ul x-match="$.k"><li x-case="1">A</li><li x-nocase>B</li></ul>')
    local ul = pick(i, 'ul')
    for k = 1, 2 do
        local li = pick(i, 'li', k)
        assert.equal(li.parent, ul.order)
        assert.equal(li.depth, ul.depth + 1)
        assert.equal(li.matchBranch, k - 1)
    end
end

-- ============================================================
-- buckets
-- ============================================================

function testcase.by_tag_covers_every_element()
    local i = build('<div><p></p><span></span><p></p></div>')
    assert.equal(#i.byTag.div, 1)
    assert.equal(#i.byTag.p, 2)
    assert.equal(#i.byTag.span, 1)
end

function testcase.by_id_indexed_by_static_id()
    local i = build('<div id="root"><p id="p1"></p></div>')
    assert.equal(#i.byId.root, 1)
    assert.equal(#i.byId.p1, 1)
end

function testcase.by_id_ignores_empty_value()
    local i = build('<div id=""></div>')
    assert.equal(i.byId.root, nil)
    -- The element is still counted in the byAttrName bucket for `id`.
    assert.equal(#i.byAttrName.id, 1)
end

function testcase.by_class_splits_on_whitespace()
    local i = build('<div class="  a  b\tc  "></div>')
    assert.equal(#i.byClass.a, 1)
    assert.equal(#i.byClass.b, 1)
    assert.equal(#i.byClass.c, 1)
end

function testcase.by_class_multiple_elements_per_key()
    local i = build('<a class="x"></a><b class="x y"></b><c class="y"></c>')
    assert.equal(#i.byClass.x, 2)
    assert.equal(#i.byClass.y, 2)
end

function testcase.by_attr_name_lists_every_static_attr()
    local i = build('<a href="/x" title="t"></a><b href="/y"></b>')
    assert.equal(#i.byAttrName.href, 2)
    assert.equal(#i.byAttrName.title, 1)
end

function testcase.buckets_preserve_document_order()
    local i = build('<p class="x"></p><p class="x"></p><p class="x"></p>')
    -- The three <p> elements were emitted in order 1, 2, 3 — the bucket
    -- must reflect that so downstream code can sort candidates.
    assert.equal(i.byClass.x[1], 1)
    assert.equal(i.byClass.x[2], 2)
    assert.equal(i.byClass.x[3], 3)
end

-- ============================================================
-- includes and `all`
-- ============================================================

function testcase.all_lists_every_element_in_document_order()
    local i = build('<a></a><b><c></c></b>')
    assert.equal(#i.all, 3)
    assert.equal(i.all[1], 1)  -- <a>
    assert.equal(i.all[2], 2)  -- <b>
    assert.equal(i.all[3], 3)  -- <c>
end

function testcase.includes_lists_only_x_include_elements()
    -- A missing template still lets the index build succeed — resolution
    -- happens at render time.
    local i = build('<a></a><div x-include="\'other\'"></div>')
    assert.equal(#i.includes, 1)
    -- The include element is the <div>, i.e. document order #2.
    assert.equal(i.includes[1], 2)
end

-- ============================================================
-- edge cases
-- ============================================================

function testcase.empty_document()
    local i = build('')
    assert.equal(#i.all, 0)
    assert.equal(#i.includes, 0)
    -- Buckets are present but empty.
    assert.equal(next(i.byTag), nil)
end

function testcase.text_and_comment_ignored()
    local i = build('hello<!--c--><a></a>tail')
    assert.equal(#i.all, 1)
    assert.equal(pick(i, 'a').order, 1)
end
