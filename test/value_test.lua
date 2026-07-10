require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')
local ntos = compiler.ntos

function testcase.ntos_integer()
    assert.equal(ntos(0), '0')
    assert.equal(ntos(1), '1')
    assert.equal(ntos(-1), '-1')
    assert.equal(ntos(100), '100')
end

function testcase.ntos_decimal()
    assert.equal(ntos(0.5), '0.5')
    assert.equal(ntos(0.1 + 0.2), '0.30000000000000004')
    assert.equal(ntos(1.5), '1.5')
    assert.equal(ntos(3.14), '3.14')
end

function testcase.ntos_exponential()
    assert.equal(ntos(1e21), '1e+21')
    assert.equal(ntos(1e-7), '1e-7')
    assert.equal(ntos(1e20), '100000000000000000000')
end

function testcase.ntos_special()
    assert.equal(ntos(0 / 0), 'NaN')
    assert.equal(ntos(1 / 0), 'Infinity')
    assert.equal(ntos(-1 / 0), '-Infinity')
    assert.equal(ntos(-0.0), '0')
end

function testcase.ntos_precision_boundary()
    assert.equal(ntos(9007199254740991), '9007199254740991')
    assert.equal(ntos(1.7976931348623157e+308), '1.7976931348623157e+308')
    assert.equal(ntos(5e-324), '5e-324')
end
