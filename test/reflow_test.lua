require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local reflow = require('reflow')

local function new_engine(config)
    local engine, err = reflow.new(config)
    assert.not_nil(engine, err)
    return engine
end

-- reflow.new

function testcase.new_rejects_invalid_settings()
    for _, case in ipairs({
        {
            'config',
            'config must be a table or nil',
        },
        {
            {
                prefix = 1,
            },
            'prefix must be a string',
        },
        {
            {
                helpers = 'f',
            },
            'helpers must be a table',
        },
        {
            {
                helpers = {
                    function()
                    end,
                },
            },
            'helpers keys must be strings',
        },
        {
            {
                helpers = {
                    f = 'not a function',
                },
            },
            'helper "f" must be a function',
        },
        {
            {
                loader = 'path',
            },
            'loader must be a function',
        },
        {
            {
                max_include_depth = -1,
            },
            'max_include_depth must be a non-negative integer',
        },
        {
            {
                max_include_depth = 1.5,
            },
            'max_include_depth must be a non-negative integer',
        },
    }) do
        local engine, err = reflow.new(case[1])
        assert.is_nil(engine)
        assert.rawequal(errorlib.typeof(err), reflow_error.EREFLOW)
        assert.equal(err.op, 'reflow.new')
        assert.equal(err.message.message, case[2])
    end

    -- the selector cache checks its own size
    local engine, err = reflow.new({
        selector_cache_size = -1,
    })
    assert.is_nil(engine)
    assert.rawequal(errorlib.typeof(err), reflow_error.EREFLOW)
    assert.equal(err.op, 'reflow.selector_cache.new')
end

function testcase.new_accepts_valid_settings()
    local loader = function()
    end
    local engine = new_engine({
        prefix = 'v-',
        helpers = {},
        loader = loader,
        max_include_depth = 0,
        selector_cache_size = 2,
        unknown = true,
    })
    assert.equal(engine._prefix, 'v-')
    assert.rawequal(engine._loader, loader)
    assert.equal(engine._max_include_depth, 0)
    assert.equal(engine._selector_cache:max_size(), 2)
end

function testcase.default_settings()
    local engine = new_engine()
    assert.equal(engine._selector_cache:max_size(), 128)
    assert.equal(engine._max_include_depth, 50)
    assert.equal(engine._prefix, 'x-')
end

function testcase.engine_metatable_is_protected()
    local engine = new_engine()
    assert.equal(getmetatable(engine), 'reflow.engine')
    assert.throws(setmetatable, engine, {})
end
