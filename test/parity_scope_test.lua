-- Ports test/unit/scope.test.js of the JavaScript version through the
-- public API: a data frame is written as x-data, a loop frame as x-each,
-- and names are resolved through a helper that captures their values.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local reflow = require('reflow')

local captured

local function render(html, globals)
    local out, err = reflow.render(html, globals, {
        helpers = {
            cap = function(...)
                captured = {
                    n = select('#', ...),
                    ...,
                }
                return ''
            end,
        },
    })
    assert.not_nil(out, err)
    return out
end

function testcase.dollar_resolves_the_globals()
    -- resolveDollar: "$" alone is not an expression, so the globals are
    -- read through "$.<name>"
    render('<p x-text="cap($.a, $.b)"></p>', {
        a = 1,
    })
    assert.equal(captured[1], 1)
    assert.is_nil(captured[2])
end

function testcase.dot_returns_undefined_when_not_found()
    render('<p x-text="cap(.missing)"></p>')
    assert.equal(captured.n, 1)
    assert.is_nil(captured[1])
end

function testcase.dot_searches_the_innermost_frame_first()
    render([[<div x-data="x: 'outer'"><div x-data="x: 'inner'">]] ..
               [[<p x-text="cap(.x)"></p></div></div>]])
    assert.equal(captured[1], 'inner')
end

function testcase.at_skips_loop_frames()
    render([[<div x-data="name: 'from-data'">]] ..
               [[<p x-each="name in $.loop" x-text="cap(.name, @name)"></p></div>]],
           {
        loop = {
            'from-loop',
        },
    })
    assert.equal(captured[1], 'from-loop')
    assert.equal(captured[2], 'from-data')
end

function testcase.at_returns_undefined_when_only_loop_frames_have_it()
    render(
        '<p x-each="onlyLoop in $.loop" x-text="cap(@onlyLoop, .onlyLoop)"></p>',
        {
            loop = {
                1,
            },
        })
    assert.is_nil(captured[1])
    assert.equal(captured[2], 1)
end

function testcase.closing_a_frame_restores_the_previous_one()
    render([[<div x-data="x: 1"><div x-data="x: 2"></div>]] ..
               [[<p x-text="cap(.x)"></p></div>]])
    assert.equal(captured[1], 1)
end
