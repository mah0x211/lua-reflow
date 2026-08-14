require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local reflow = require('reflow')

-- ============================================================
-- default_loader
-- ============================================================

function testcase.default_loader_reads_existing_file()
    local tmp = os.tmpname()
    local f = io.open(tmp, 'w'); f:write('hello world'); f:close()
    local content, err = reflow.default_loader(tmp)
    os.remove(tmp)
    assert.equal(err, nil)
    assert.equal(content, 'hello world')
end

function testcase.default_loader_missing_file_returns_error()
    local content, err = reflow.default_loader('/no/such/path/for-sure')
    assert.equal(content, nil)
    assert(type(err) == 'string')
end

-- ============================================================
-- compile_file
-- ============================================================

function testcase.compile_file_delegates_to_loader()
    local tmp = os.tmpname()
    local f = io.open(tmp, 'w'); f:write('<p>from file</p>'); f:close()
    local r = reflow.new()
    local self, err = r:compile_file('t', tmp)
    os.remove(tmp)
    assert.equal(err, nil)
    assert.equal(self, r)
    assert.equal(r:render('t'), '<p>from file</p>')
end

function testcase.compile_file_propagates_loader_errors()
    local r = reflow.new()
    local self, err = r:compile_file('t', '/no/such/path/for-sure')
    assert.equal(self, nil)
    assert(err ~= nil)
end

function testcase.compile_file_uses_configured_loader()
    -- Custom loader returns fixed content regardless of path.
    local r = reflow.new({
        loader = function(_pathname) return '<a>custom loader</a>' end,
    })
    local self, err = r:compile_file('t', 'irrelevant')
    assert.equal(err, nil)
    assert.equal(self, r)
    assert.equal(r:render('t'), '<a>custom loader</a>')
end

-- ============================================================
-- static reflow.render / reflow.render_file
-- ============================================================

function testcase.static_render_one_shot()
    local html, err = reflow.render('<p x-text="$.n">?</p>', '{n:"hi"}')
    assert.equal(err, nil)
    assert.equal(html, '<p>hi</p>')
end

function testcase.static_render_with_helpers()
    local html = reflow.render(
        '<p x-text="upper($.n)">?</p>',
        '{n:"hi"}',
        { helpers = { upper = string.upper } })
    assert.equal(html, '<p>HI</p>')
end

function testcase.static_render_selector_extracts_fragment()
    local html = reflow.render(
        '<div><p id="hit">Yes</p><p>No</p></div>',
        nil,
        { selector = '#hit' })
    assert.equal(html, '<p id="hit">Yes</p>')
end

function testcase.static_render_returns_error_on_compile_failure()
    local html, err = reflow.render('<p x-unknown="x">?</p>')
    assert.equal(html, nil)
    assert.equal(err.type, 'ReflowCompileError')
end

function testcase.static_render_file_reads_and_renders()
    local tmp = os.tmpname()
    local f = io.open(tmp, 'w'); f:write('<p x-text="$.n">?</p>'); f:close()
    local html, err = reflow.render_file(tmp, '{n:"hi"}')
    os.remove(tmp)
    assert.equal(err, nil)
    assert.equal(html, '<p>hi</p>')
end

function testcase.static_render_file_uses_config_loader()
    local html, err = reflow.render_file(
        'irrelevant',
        '{n:"loader"}',
        { loader = function(_) return '<p x-text="$.n">?</p>' end })
    assert.equal(err, nil)
    assert.equal(html, '<p>loader</p>')
end

-- ============================================================
-- Coordinator's cached selector reuse
-- ============================================================

function testcase.selector_cached_across_renders()
    local r = reflow.new()
    r:compile('t', '<div><p id="one">A</p><p id="two">B</p></div>')
    -- Two renders with the same selector source; second should hit
    -- the coordinator's cache (no observable difference beyond
    -- performance, but the tests should pass identically).
    assert.equal(r:render('t', nil, '#one'), '<p id="one">A</p>')
    assert.equal(r:render('t', nil, '#one'), '<p id="one">A</p>')
    -- The internal cache table should now contain one entry.
    assert.equal(next(r._selectors) ~= nil, true)
end

function testcase.selector_cache_size_zero_disables_retention()
    local r = reflow.new({ selector_cache_size = 0 })
    r:compile('t', '<div><p id="a">A</p></div>')
    assert.equal(r:render('t', nil, '#a'), '<p id="a">A</p>')
    assert.equal(next(r._selectors), nil)
    assert.equal(#r._selector_order, 0)
end

function testcase.selector_cache_size_one_evicts_oldest()
    local r = reflow.new({ selector_cache_size = 1 })
    r:compile('t', '<div><p id="a">A</p><p id="b">B</p></div>')
    r:render('t', nil, '#a')
    r:render('t', nil, '#b')
    assert.equal(r._selectors['#a'], nil)
    assert(r._selectors['#b'] ~= nil)
    assert.equal(r._selector_order, {'#b'})
end

function testcase.selector_cache_hit_promotes_before_eviction()
    local r = reflow.new({ selector_cache_size = 2 })
    r:compile('t', '<div><p id="a">A</p><p id="b">B</p><p id="c">C</p></div>')
    r:render('t', nil, '#a')
    r:render('t', nil, '#b')
    r:render('t', nil, '#a')
    assert.equal(r._selector_order, {'#b', '#a'})

    r:render('t', nil, '#c')
    assert.equal(r._selectors['#b'], nil)
    assert(r._selectors['#a'] ~= nil)
    assert(r._selectors['#c'] ~= nil)
    assert.equal(r._selector_order, {'#a', '#c'})
end

function testcase.selector_cache_size_rejects_invalid_values()
    local ok_negative, err_negative = pcall(reflow.new, {
        selector_cache_size = -1,
    })
    assert.equal(ok_negative, false)
    assert.match(tostring(err_negative), 'non-negative integer')

    local ok_fraction, err_fraction = pcall(reflow.new, {
        selector_cache_size = 1.5,
    })
    assert.equal(ok_fraction, false)
    assert.match(tostring(err_fraction), 'non-negative integer')
end

function testcase.userdata_selector_passes_through()
    local r = reflow.new()
    r:compile('t', '<div><p id="a">A</p></div>')
    local sel = reflow.new_selector('#a')
    assert.equal(r:render('t', nil, sel), '<p id="a">A</p>')
end

-- ============================================================
-- module surface
-- ============================================================

function testcase.module_exposes_expected_functions()
    assert.is_function(reflow.new)
    assert.is_function(reflow.new_selector)
    assert.is_function(reflow.new_template)
    assert.is_function(reflow.render)
    assert.is_function(reflow.render_file)
    assert.is_function(reflow.default_loader)
    assert.is_function(reflow.version)
end
