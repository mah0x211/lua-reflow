require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

-- Exercised through the resolve hook because matchCompound isn't a
-- public C entry point.  Each test builds a one-element document and
-- checks whether the given compound selector picks it up.
local function matches(html, selector)
    local r, err = compiler.resolve_selector(html, selector)
    if not r then
        error(('resolve failed: %s'):format(
            err and (err.message or tostring(err)) or 'nil'))
    end
    return #r > 0
end

-- ============================================================
-- Attribute presence / equality
-- ============================================================

function testcase.presence_matches_any_value_including_empty()
    assert.is_true(matches('<a title="x"></a>', '[title]'))
    assert.is_true(matches('<a title=""></a>', '[title]'))
    -- Valueless attribute is still present.
    assert.is_true(matches('<a disabled></a>', '[disabled]'))
end

function testcase.presence_rejects_missing_attribute()
    assert.is_false(matches('<a></a>', '[title]'))
end

function testcase.equality_exact_match()
    assert.is_true(matches('<a title="hi"></a>', '[title=hi]'))
    assert.is_false(matches('<a title="his"></a>', '[title=hi]'))
    assert.is_false(matches('<a title=" hi"></a>', '[title=hi]'))
end

-- ============================================================
-- Whitespace-token matching (~=)
-- ============================================================

function testcase.tilde_equals_finds_token_in_list()
    assert.is_true(matches('<a class="foo bar baz"></a>', '[class~=bar]'))
    assert.is_true(matches('<a class="  foo  bar  "></a>', '[class~=foo]'))
    -- Partial substring must not match.
    assert.is_false(matches('<a class="foobar"></a>', '[class~=foo]'))
end

function testcase.tilde_equals_target_with_whitespace_is_rejected()
    -- The parser accepts a quoted value with whitespace, but the
    -- semantic rule for ~= says such a target can never match.
    assert.is_false(matches('<a class="a b"></a>', '[class~="a b"]'))
end

function testcase.tilde_equals_empty_target_never_matches()
    assert.is_false(matches('<a class=""></a>', '[class~=""]'))
end

-- ============================================================
-- Prefix / suffix / substring / hyphen semantics
-- ============================================================

function testcase.pipe_equals_matches_hyphen_prefix()
    assert.is_true(matches('<a lang="en"></a>', '[lang|=en]'))
    assert.is_true(matches('<a lang="en-US"></a>', '[lang|=en]'))
    assert.is_false(matches('<a lang="english"></a>', '[lang|=en]'))
end

function testcase.caret_equals_matches_prefix()
    assert.is_true(matches('<a title="prefix-mid"></a>', '[title^=prefix]'))
    assert.is_false(matches('<a title="mid-prefix"></a>', '[title^=prefix]'))
end

function testcase.dollar_equals_matches_suffix()
    assert.is_true(matches('<a title="mid-suffix"></a>', '[title$=suffix]'))
    assert.is_false(matches('<a title="suffix-mid"></a>', '[title$=suffix]'))
end

function testcase.star_equals_matches_substring()
    assert.is_true(matches('<a title="ab-mid-cd"></a>', '[title*=mid]'))
    assert.is_false(matches('<a title="abcd"></a>', '[title*=xyz]'))
end

function testcase.prefix_ops_reject_empty_target()
    -- Empty target on ^=, $=, *=, ~= never matches, matching CSS Level 3.
    assert.is_false(matches('<a title="x"></a>', '[title^=""]'))
    assert.is_false(matches('<a title="x"></a>', '[title$=""]'))
    assert.is_false(matches('<a title="x"></a>', '[title*=""]'))
end

-- ============================================================
-- Compound composition
-- ============================================================

function testcase.tag_plus_class_plus_attr()
    local html = '<a class="cls" title="t"></a>'
    assert.is_true(matches(html, 'a.cls[title=t]'))
end

function testcase.id_and_class_together()
    local html = '<div id="x" class="y"></div>'
    assert.is_true(matches(html, '#x.y'))
    assert.is_false(matches(html, '#x.z'))
end

function testcase.multi_class_requires_all()
    local html = '<div class="a b"></div>'
    assert.is_true(matches(html, '.a.b'))
    assert.is_false(matches(html, '.a.b.c'))
end

-- ============================================================
-- Case handling
-- ============================================================

function testcase.attr_name_lookup_case_insensitive()
    -- Compile lowercases attribute names, so mixed-case source still
    -- matches lowercase selectors.
    assert.is_true(matches('<a HREF="/"></a>', '[href]'))
end

function testcase.attr_value_lookup_case_sensitive()
    -- Values keep their original case; selector value must match exactly.
    assert.is_false(matches('<a title="Hi"></a>', '[title=hi]'))
    assert.is_true(matches('<a title="Hi"></a>', '[title=Hi]'))
end

function testcase.tag_lookup_case_insensitive()
    assert.is_true(matches('<DIV></DIV>', 'div'))
    assert.is_true(matches('<div></div>', 'DIV'))
end
