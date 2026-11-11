require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local cjson = require('cjson')
local reflow = require('reflow')

-- The fixtures of test/fixtures were copied from the JavaScript version
-- (b1f46947ac8516cf4f15070c6e01a5a3e1bb453c), as its fixture-runner.js
-- reads them. helpers.js and setup.js are written as fixture.lua.
-- testcase loads this file from the repository root and runs each test in
-- the directory of the file, so the fixtures are looked up when a test runs.
local function root()
    for _, dir in ipairs({
        'fixtures/',
        'test/fixtures/',
    }) do
        local f = io.open(dir .. 'valid/x-text-basic/template.html')
        if f then
            f:close()
            return dir
        end
    end
    error('test/fixtures not found')
end
local function read(path)
    local f = io.open(path, 'rb')
    if not f then
        return nil
    end
    local s = f:read('*a')
    f:close()
    return s
end

local function list(dir)
    local names = {}
    local p = assert(io.popen('ls -1 ' .. root() .. dir))
    for name in p:lines() do
        names[#names + 1] = name
    end
    p:close()
    return names
end

local function trim(s)
    return (s:gsub('\r\n', '\n'):gsub('^%s+', ''):gsub('%s+$', ''))
end

-- Load the data and the Lua fixture of a fixture.
local function load_fixture(dir)
    local fixture = {}
    local src = read(dir .. '/fixture.lua')
    if src then
        fixture = assert((loadstring or load)(src, '=' .. dir))()
    end
    local data = read(dir .. '/data.json')
    fixture.data = data and cjson.decode(data) or {}
    return fixture
end

local function new_engine(fixture)
    local engine = assert(reflow.new({
        helpers = fixture.helpers,
    }))
    if fixture.setup then
        fixture.setup(engine)
    end
    return engine
end

function testcase.valid_fixtures_render_the_expected_output()
    local names = list('valid')
    assert.equal(#names, 23)
    for _, name in ipairs(names) do
        local dir = root() .. 'valid/' .. name
        local fixture = load_fixture(dir)
        local engine = new_engine(fixture)
        assert(engine:compile('main', assert(read(dir .. '/template.html'))))
        local out, err = engine:render('main', fixture.data)
        assert.not_nil(out, dir .. ': ' .. tostring(err))
        assert.equal(trim(out), trim(assert(read(dir .. '/expected.html'))), dir)
    end
end

function testcase.cjson_null_is_the_null_of_rendering()
    local engine = assert(reflow.new())
    assert(engine:compile('t', '<p x-text="$.v == null"></p>'))
    assert.equal(engine:render('t', {
        v = cjson.null,
    }), '<p>true</p>')
    assert.equal(engine:render('t', cjson.decode('{"v": null}')), '<p>true</p>')
end
