require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
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
local ERROR_TYPES = {
    ReflowCompileError = reflow.ReflowCompileError,
    ReflowRuntimeError = reflow.ReflowRuntimeError,
    ReflowIncludeError = reflow.ReflowIncludeError,
}

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

-- Load the data, the expected error, and the Lua fixture of a fixture.
local function load_fixture(dir)
    local fixture = {}
    local src = read(dir .. '/fixture.lua')
    if src then
        fixture = assert((loadstring or load)(src, '=' .. dir))()
    end
    local data = read(dir .. '/data.json')
    fixture.data = data and cjson.decode(data) or {}
    local expected_error = read(dir .. '/expected-error.json')
    fixture.expected_error = expected_error and cjson.decode(expected_error)
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

-- Translate a messagePattern, a JavaScript regular expression, into Lua
-- patterns. Only '.*' and top-level '|' are used by the fixtures; any other
-- metacharacter of JavaScript is rejected.
local function to_lua_patterns(source)
    local patterns = {}
    for alternative in (source .. '|'):gmatch('([^|]*)|') do
        local literal = alternative:gsub('%.%*', '')
        assert(not literal:find('[%^%$%(%)%[%]%+%?%.\\{}*]'),
               'unsupported messagePattern: ' .. source)
        local pattern = alternative:gsub('%.%*', '\0'):gsub(
                            '[%%%-%^%$%(%)%[%]%+%?%.%*]', '%%%0')
                            :gsub('%z', '.-')
        patterns[#patterns + 1] = pattern
    end
    return patterns
end

local function matches(message, source)
    for _, pattern in ipairs(to_lua_patterns(source)) do
        if message:find(pattern) then
            return true
        end
    end
    return false
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

function testcase.invalid_fixtures_fail_as_expected()
    local names = list('invalid')
    assert.equal(#names, 24)
    for _, name in ipairs(names) do
        local dir = root() .. 'invalid/' .. name
        local fixture = load_fixture(dir)
        local expected = assert(fixture.expected_error, dir)
        local engine = new_engine(fixture)
        local ok, err = engine:compile('main',
                                       assert(read(dir .. '/template.html')))
        if expected.phase == 'render' then
            assert.is_true(ok, dir .. ': ' .. tostring(err))
            ok, err = engine:render('main', fixture.data)
        end
        assert.is_nil(ok, dir)
        local errt = assert(ERROR_TYPES[expected.class], dir)
        assert.rawequal(errorlib.typeof(err), errt, dir .. ': ' .. tostring(err))
        if expected.reason then
            assert.equal(err.meta.reason, expected.reason, dir)
        end
        if expected.messagePattern then
            assert.is_true(
                matches(err.message.message, expected.messagePattern),
                dir .. ': ' .. err.message.message)
        end
    end
end

function testcase.message_patterns_are_translated()
    assert.is_true(matches('a structural and iteration',
                           'structural.*iteration|iteration.*structural'))
    assert.is_true(matches('iteration then structural',
                           'structural.*iteration|iteration.*structural'))
    assert.is_false(matches('structural only',
                            'structural.*iteration|iteration.*structural'))
    assert.is_true(matches('expected "=" here', 'expected "="'))
    assert.is_true(matches('outside of x-for', 'of x-for'))
    assert.is_false(matches('xxfor', 'x-for'))
    assert.throws(to_lua_patterns, 'a.c')
    assert.throws(to_lua_patterns, 'a+b')
end

function testcase.cjson_null_is_the_null_of_rendering()
    local engine = assert(reflow.new())
    assert(engine:compile('t', '<p x-text="$.v == null"></p>'))
    assert.equal(engine:render('t', {
        v = cjson.null,
    }), '<p>true</p>')
    assert.equal(engine:render('t', cjson.decode('{"v": null}')), '<p>true</p>')
end
