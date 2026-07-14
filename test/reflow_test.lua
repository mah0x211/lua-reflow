require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local Reflow = require('reflow')

-- ===== new / defaults =====

function testcase.new_defaults()
    local r = Reflow.new()
    assert.equal(#r:templates(), 0)
end

function testcase.new_with_helpers()
    local r = Reflow.new({
        helpers = { upper = string.upper },
    })
    r:compile('t', [[<span x-text="upper($.n)"></span>]])
    assert.equal(r:render('t', [[{n: "hi"}]]),
                 '<span>HI</span>')
end

-- ===== compile + render =====

function testcase.compile_and_render_basic()
    local r = Reflow.new()
    r:compile('t', '<div>hi</div>')
    assert.equal(r:render('t'), '<div>hi</div>')
end

function testcase.compile_returns_self()
    local r = Reflow.new()
    local self = r:compile('t', '<p>x</p>')
    assert.equal(self, r)
end

function testcase.render_with_json5_data()
    local r = Reflow.new()
    r:compile('t', [[<span x-text="$.n"></span>]])
    assert.equal(r:render('t', [[{n: "world"}]]),
                 '<span>world</span>')
end

function testcase.render_missing_template_errors()
    local r = Reflow.new()
    local ok, err = pcall(function() r:render('missing', nil) end)
    assert.is_false(ok)
    assert.match(err, 'not registered')
end

function testcase.compile_error_surfaces()
    local r = Reflow.new()
    local ok, err = pcall(function()
        r:compile('bad', '<div x-unknown="x">y</div>')
    end)
    assert.is_false(ok)
    assert.match(err, 'unknown directive')
end

function testcase.render_directive_error_surfaces()
    local r = Reflow.new()
    r:compile('t', [[<div x-html="$.n"></div>]])
    local ok, err = pcall(function()
        r:render('t', [[{n: 42}]])
    end)
    assert.is_false(ok)
    assert.match(err, 'value must be a string')
end

-- ===== templates =====

function testcase.templates_lists_registered()
    local r = Reflow.new()
    r:compile('a', '<a></a>')
    r:compile('b', '<b></b>')
    local names = r:templates()
    table.sort(names)
    assert.equal(names[1], 'a')
    assert.equal(names[2], 'b')
end

function testcase.templates_empty()
    local r = Reflow.new()
    assert.equal(#r:templates(), 0)
end

-- ===== clear =====

function testcase.clear_single()
    local r = Reflow.new()
    r:compile('a', '<a></a>')
    r:compile('b', '<b></b>')
    local removed = r:clear('a')
    assert.equal(#removed, 1)
    assert.equal(removed[1], 'a')
    assert.equal(#r:templates(), 1)
end

function testcase.clear_missing_returns_empty()
    local r = Reflow.new()
    r:compile('a', '<a></a>')
    local removed = r:clear('missing')
    assert.equal(#removed, 0)
    assert.equal(#r:templates(), 1)
end

function testcase.clear_all()
    local r = Reflow.new()
    r:compile('a', '<a></a>')
    r:compile('b', '<b></b>')
    local removed = r:clear()
    assert.equal(#removed, 2)
    assert.equal(#r:templates(), 0)
end

-- ===== helpers =====

function testcase.add_helper_after_new()
    local r = Reflow.new()
    r:add_helper('upper', string.upper)
    r:compile('t', [[<span x-text="upper($.n)"></span>]])
    assert.equal(r:render('t', [[{n: "hi"}]]),
                 '<span>HI</span>')
end

function testcase.compile_before_helper_registration_errors()
    -- Helpers must be registered before compile validates references.
    local r = Reflow.new()
    local ok, err = pcall(function()
        r:compile('t', [[<span x-text="unknownfn($.n)"></span>]])
    end)
    assert.is_false(ok)
    assert.match(err, 'unknown helper')
end

-- ===== custom prefix =====

function testcase.custom_prefix()
    local r = Reflow.new({ prefix = 'data-' })
    r:compile('t', [[<span data-text="$.n"></span>]])
    assert.equal(r:render('t', [[{n: "hi"}]]),
                 '<span>hi</span>')
end

-- ===== independence between instances =====

function testcase.instances_independent()
    local a = Reflow.new()
    local b = Reflow.new()
    a:compile('t', '<a></a>')
    assert.equal(#a:templates(), 1)
    assert.equal(#b:templates(), 0)
end

-- ===== chaining =====

function testcase.compile_chainable()
    local r = Reflow.new()
        :compile('a', '<a></a>')
        :compile('b', '<b></b>')
    assert.equal(#r:templates(), 2)
end
