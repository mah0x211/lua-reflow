require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local Reflow = require('reflow')

-- ===== basic include =====

function testcase.include_basic()
    local r = Reflow.new()
    r:compile('inner', '<b>hi</b>')
    r:compile('outer', '<div x-include="\'inner\'"></div>')
    assert.equal(r:render('outer'), '<div><b>hi</b></div>')
end

function testcase.include_multiple()
    local r = Reflow.new()
    r:compile('h', '<header>H</header>')
    r:compile('f', '<footer>F</footer>')
    r:compile('page',
        '<div><a x-include="\'h\'"></a>' ..
        '<b x-include="\'f\'"></b></div>')
    assert.equal(r:render('page'),
        '<div><a><header>H</header></a><b><footer>F</footer></b></div>')
end

function testcase.include_reads_globals()
    -- included template sees $ (globals) from the caller's render data
    local r = Reflow.new()
    r:compile('greet', '<span x-text="$.who"></span>')
    r:compile('outer',
        '<div x-include="\'greet\'"></div>')
    assert.equal(r:render('outer', [[{who: "world"}]]),
                 '<div><span>world</span></div>')
end

function testcase.include_ignores_outer_scope()
    -- data / loop frames from the caller are NOT visible in the included
    -- template; only $ carries over.
    local r = Reflow.new()
    r:compile('inner', [[<span x-text=".x"></span>]])
    r:compile('outer',
        [[<div x-with="x = 42"><a x-include="'inner'"></a></div>]])
    -- .x is undefined inside inner (outer's x-with frame is hidden), so
    -- x-text on undefined emits empty content
    assert.equal(r:render('outer'), '<div><a><span></span></a></div>')
end

-- ===== error cases =====

function testcase.include_not_found()
    local r = Reflow.new()
    r:compile('page', [[<div x-include="'missing'"></div>]])
    local ok, err = pcall(function() r:render('page') end)
    assert.is_false(ok)
    assert.match(err, 'template "missing" not registered')
end

function testcase.include_cycle_direct()
    local r = Reflow.new()
    r:compile('a', [[<div x-include="'a'"></div>]])
    local ok, err = pcall(function() r:render('a') end)
    assert.is_false(ok)
    assert.match(err, 'cyclic include of "a"')
end

function testcase.include_cycle_indirect()
    local r = Reflow.new()
    r:compile('a', [[<div x-include="'b'"></div>]])
    r:compile('b', [[<div x-include="'a'"></div>]])
    local ok, err = pcall(function() r:render('a') end)
    assert.is_false(ok)
    assert.match(err, 'cyclic include')
end

function testcase.include_depth_exceeded()
    local r = Reflow.new({ max_include_depth = 3 })
    for i = 1, 4 do
        r:compile('t' .. i, '<div x-include="\'t' ..
                            (i + 1) .. '\'"></div>')
    end
    r:compile('t5', '<span>end</span>')
    local ok, err = pcall(function() r:render('t1') end)
    assert.is_false(ok)
    assert.match(err, 'max include depth')
end

function testcase.include_within_limit_ok()
    local r = Reflow.new({ max_include_depth = 5 })
    for i = 1, 3 do
        r:compile('t' .. i, '<div x-include="\'t' ..
                            (i + 1) .. '\'"></div>')
    end
    r:compile('t4', '<span>end</span>')
    assert.equal(r:render('t1'),
                 '<div><div><div><span>end</span></div></div></div>')
end

function testcase.include_non_string_value()
    local r = Reflow.new()
    r:compile('page', [[<div x-include="$.n"></div>]])
    local ok, err = pcall(function() r:render('page', [[{n: 42}]]) end)
    assert.is_false(ok)
    assert.match(err, 'value must be a string')
end

-- ===== helper visibility through include =====

function testcase.helpers_visible_in_included_template()
    local r = Reflow.new({ helpers = { upper = string.upper } })
    r:compile('inner', [[<span x-text="upper($.n)"></span>]])
    r:compile('outer', [[<div x-include="'inner'"></div>]])
    assert.equal(r:render('outer', [[{n: "hi"}]]),
                 '<div><span>HI</span></div>')
end
