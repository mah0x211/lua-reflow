local testcase = require('testcase')

function testcase.reflow_module_loads()
    local reflow = require('reflow')
    assert.equal(type(reflow), 'table')
end
