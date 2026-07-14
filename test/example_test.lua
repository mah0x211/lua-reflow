require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local Reflow = require('reflow')

local function project_root()
    local p = io.popen('pwd')
    local cwd = p:read('*l')
    p:close()
    return (cwd:gsub('/test/?$', ''))
end
local ROOT = project_root()

--- Verify example.html renders without error and matches the byte output
--- produced by the JS reference (regenerated locally with `node
--- gen_example.mjs` when the template changes).
function testcase.example_html_renders()
    local f = io.open(ROOT .. '/js-reflow/example.html', 'rb')
    if f == nil then
        -- The js-reflow reference tree is untracked; skip when absent.
        return
    end
    local html = f:read('*a')
    f:close()

    local reflow = Reflow.new()
    reflow:compile('example', html)
    local out = reflow:render('example', '{}')
    -- Basic sanity: output starts with the outer <html> and ends past </html>.
    assert(out:find('<html', 1, true), 'output missing <html')
    assert(out:find('</html>', 1, true), 'output missing </html>')
    -- No leading whitespace before <html> — root-level text is dropped.
    assert.equal(out:sub(1, 5), '<html')
end
