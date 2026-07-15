require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local new_selector = require('reflow.selector')

-- ============================================================
-- factory + accessors
-- ============================================================

function testcase.parse_and_source()
    local s, err = new_selector('div.foo')
    assert(s, err and err.message)
    assert.equal(s:source(), 'div.foo')
end

function testcase.has_positional_false_for_static_selector()
    local s = new_selector('div .foo')
    assert.is_false(s:has_positional())
end

function testcase.has_positional_true_for_positional_pseudo()
    local s = new_selector('li:first-child')
    assert.is_true(s:has_positional())
end

-- ============================================================
-- dual return on parse errors
-- ============================================================

function testcase.syntax_error_returns_error_table()
    local s, err = new_selector('[a=]')
    assert.equal(s, nil)
    assert.equal(err.type, 'ReflowSelectorError')
    assert.equal(err.reason, 'syntax')
    assert.equal(err.source, '[a=]')
end

function testcase.unsupported_pseudo_returns_feature_id()
    local s, err = new_selector(':not(.x)')
    assert.equal(s, nil)
    assert.equal(err.reason, 'unsupported')
    assert.equal(err.feature, 'pseudo:not')
end

function testcase.unsupported_combinator_returns_feature_id()
    local s, err = new_selector('a + b')
    assert.equal(s, nil)
    assert.equal(err.feature, 'combinator:+')
end

-- ============================================================
-- selector userdata is independent (can be reused)
-- ============================================================

function testcase.selector_is_reusable_across_uses()
    local s = new_selector('#target')
    assert.equal(s:source(), '#target')
    assert.equal(s:source(), '#target')  -- second call still works
end

function testcase.two_selectors_are_independent()
    local a = new_selector('.a')
    local b = new_selector('.b')
    assert.equal(a:source(), '.a')
    assert.equal(b:source(), '.b')
end
