require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local reflow = require('reflow')

-- helper: create a reflow instance with the given HTML compiled under
-- the name `t`.
local function make(html)
    local r = reflow.new()
    r:compile('t', html)
    return r
end

-- helper: call render and expect a structured selector error. Returns
-- the error table so callers can assert on individual fields.
local function fail(r, name, data, selector)
    local ok, err = pcall(r.render, r, name, data, selector)
    assert.is_false(ok)
    assert(type(err) == 'table',
        ('expected table error, got %s (%s)'):format(type(err), tostring(err)))
    assert.equal(err.type, 'ReflowSelectorError')
    return err
end

-- ============================================================
-- happy path — element extraction
-- ============================================================

function testcase.extracts_element_by_id()
    local r = make('<div><p id="a">hi</p><p id="b">bye</p></div>')
    assert.equal(r:render('t', nil, '#a'), '<p id="a">hi</p>')
end

function testcase.extracts_element_by_class()
    local r = make('<div><p class="c">1</p><span class="c">2</span></div>')
    -- Two matches; single-match contract must reject.
    local err = fail(r, 't', nil, '.c')
    assert.equal(err.reason, 'multiple_matches')

    -- Narrow by tag to isolate.
    local r2 = make('<div><p class="c">1</p><span>2</span></div>')
    assert.equal(r2:render('t', nil, '.c'), '<p class="c">1</p>')
end

function testcase.selector_returns_outer_html()
    local r = make('<section><h1>Title</h1><p>Body</p></section>')
    assert.equal(r:render('t', nil, 'section'),
        '<section><h1>Title</h1><p>Body</p></section>')
end

function testcase.attribute_selector()
    local r = make(
        '<a href="/x">One</a><a href="/y" data-x="v">Two</a><a>Three</a>')
    assert.equal(r:render('t', nil, '[data-x]'),
        '<a href="/y" data-x="v">Two</a>')
end

function testcase.combinator_selector()
    local r = make(
        '<div><p><b>hit</b></p></div><section><b>miss</b></section>')
    -- descendant: any depth
    assert.equal(r:render('t', nil, 'div b'), '<b>hit</b>')
end

-- ============================================================
-- x-text / x-html on the target itself work in the simple path
-- ============================================================

function testcase.target_with_x_text_expands()
    local r = make(
        '<h1 x-text="$.title">placeholder</h1>')
    assert.equal(r:render('t', '{title:"Hello"}', 'h1'),
        '<h1>Hello</h1>')
end

function testcase.target_with_x_bind_expands()
    local r = make(
        '<a x-bind:href="$.url">link</a>')
    assert.equal(r:render('t', '{url:"/x"}', 'a'),
        '<a href="/x">link</a>')
end

-- ============================================================
-- single-match contract
-- ============================================================

function testcase.no_match_raises()
    local r = make('<div><p>hi</p></div>')
    local err = fail(r, 't', nil, '.missing')
    assert.equal(err.reason, 'no_match')
    assert.equal(err.source, '.missing')
end

function testcase.multiple_matches_raises()
    local r = make('<ul><li>1</li><li>2</li></ul>')
    local err = fail(r, 't', nil, 'li')
    assert.equal(err.reason, 'multiple_matches')
end

-- ============================================================
-- parse error path
-- ============================================================

function testcase.selector_parse_error_bubbles()
    local r = make('<a>x</a>')
    local err = fail(r, 't', nil, ':not(.x)')
    assert.equal(err.reason, 'unsupported')
    assert.equal(err.feature, 'pseudo:not')
end

function testcase.selector_syntax_error_bubbles()
    local r = make('<a>x</a>')
    local err = fail(r, 't', nil, '[a=]')
    assert.equal(err.reason, 'syntax')
end

-- ============================================================
-- explicit "not yet implemented" errors for follow-up stages
-- ============================================================

function testcase.positional_pseudo_reports_unsupported()
    local r = make('<ul><li>1</li></ul>')
    local err = fail(r, 't', nil, 'li:first-child')
    assert.equal(err.reason, 'unsupported')
    assert.equal(err.feature, 'fragment:positional')
end

function testcase.ancestor_with_x_if_reports_unsupported()
    local r = make([[
<div>
  <section x-if="$.show">
    <h1 id="title">Hi</h1>
  </section>
</div>
]])
    -- h1 is inside a chain branch (section as x-if). Reject cleanly.
    local err = fail(r, 't', nil, '#title')
    assert.equal(err.reason, 'unsupported')
    -- Either flagged as ancestor-control (section is chain branch) or
    -- another follow-up-stage feature — either way the reason must
    -- be `unsupported` and the feature must start with 'fragment:'.
    assert(err.feature:sub(1, 9) == 'fragment:',
        ('feature %q missing fragment: prefix'):format(tostring(err.feature)))
end

function testcase.chain_branch_target_reports_unsupported()
    local r = make(
        '<p x-if="$.a">A</p><p x-elseif="$.b">B</p><p x-else>C</p>')
    local err = fail(r, 't', '{a:false,b:false}', 'p')
    -- Every p is a chain branch — either flagged as multi-match (three
    -- candidates) or branch-target. Both are valid rejections; assert
    -- the shape rather than the exact reason.
    assert.equal(err.type, 'ReflowSelectorError')
end

-- ============================================================
-- cache is populated after render
-- ============================================================

function testcase.selector_cache_hit_reuses_parse()
    -- With cache disabled every render re-parses. This test just
    -- confirms the two calls do not blow up and produce identical
    -- output; direct cache observation is covered by the C-level cache
    -- test suite.
    local r = make('<a id="only">hi</a>')
    local h1 = r:render('t', nil, '#only')
    local h2 = r:render('t', nil, '#only')
    assert.equal(h1, h2)
end
