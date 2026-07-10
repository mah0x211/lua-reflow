require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

function testcase.esct_basic()
    assert.equal(compiler.esct('hello'), 'hello')
    assert.equal(compiler.esct('a<b>c'), 'a&lt;b&gt;c')
    assert.equal(compiler.esct('a&b'), 'a&amp;b')
    assert.equal(compiler.esct('say "hi"'), 'say &quot;hi&quot;')
    assert.equal(compiler.esct("it's"), 'it&#39;s')
end

function testcase.esca_basic()
    assert.equal(compiler.esca('hello'), 'hello')
    assert.equal(compiler.esca('a<b'), 'a&lt;b')
    assert.equal(compiler.esca('say "hi"'), 'say &quot;hi&quot;')
    -- single quote passes through in attr (double-quoted context)
    assert.equal(compiler.esca("it's"), "it's")
end
