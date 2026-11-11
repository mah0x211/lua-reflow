require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local selector = require('reflow.selector')
local reflow = require('reflow')

local function new_engine(config)
    local engine, err = reflow.new(config)
    assert.not_nil(engine, err)
    return engine
end

local function engine_with(templates, config)
    local engine = new_engine(config)
    for _, t in ipairs(templates) do
        assert(engine:compile(t[1], t[2]))
    end
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

-- engine:compile

function testcase.duplicate_compile_is_a_compile_error()
    -- ports test/unit/reflow-api.test.js of the JavaScript version
    local engine = engine_with({
        {
            't',
            '<p>hi</p>',
        },
    })
    local ir = engine._templates.t
    local ok, err = engine:compile('t', '<p>bye</p>')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.op, 'reflow.engine.compile')
    assert.equal(err.message.message, 'template "t" already exists; call ' ..
                     'clear("t") before recompiling')
    assert.equal(err.meta, {
        template_name = 't',
    })
    -- the registered template is kept
    assert.rawequal(engine._templates.t, ir)
end

function testcase.compile_errors_are_returned_as_is()
    local engine = new_engine()
    local ok, err = engine:compile('', '<p></p>')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.message.message, 'name must be a non-empty string')
    ok, err = engine:compile('t', 42)
    assert.is_nil(ok)
    assert.equal(err.message.message, 'html must be a string')
    assert.is_nil(next(engine._templates))
end

-- engine:render, porting test/unit/reflow-api.test.js and
-- test/reflow.selector.test.js of the JavaScript version except the file
-- loading and one-shot rendering

function testcase.render_of_an_unregistered_template_is_a_runtime_error()
    local engine = new_engine()
    for _, name in ipairs({
        'missing',
        42,
    }) do
        local out, err = engine:render(name)
        assert.is_nil(out)
        assert.rawequal(errorlib.typeof(err), reflow_error.ERUNTIME)
        assert.equal(err.op, 'reflow.engine.render')
        assert.equal(err.message.message,
                     'template not found: "' .. tostring(name) .. '"')
        assert.equal(err.meta, {
            template_name = name,
            reason = 'not_found',
            requested = name,
        })
    end
end

function testcase.helpers_are_available()
    local engine = engine_with({
        {
            't',
            '<p x-text="upper($.n)"></p>',
        },
    }, {
        helpers = {
            upper = function(s)
                return tostring(s):upper()
            end,
        },
    })
    assert.equal(engine:render('t', {
        n = 'alice',
    }), '<p>ALICE</p>')
end

function testcase.render_with_a_selector_returns_the_fragment()
    local engine = engine_with({
        {
            't',
            '<div><header id="h"><h1>Hi</h1></header><main>x</main></div>',
        },
    })
    assert.equal(engine:render('t', {}, '#h'),
                 '<header id="h"><h1>Hi</h1></header>')
    -- without a selector the whole template is rendered
    assert.equal(engine:render('t', {}),
                 '<div><header id="h"><h1>Hi</h1></header><main>x</main></div>')
end

function testcase.compiled_selector_bypasses_the_cache()
    local engine = engine_with({
        {
            't',
            '<div><p id="a">1</p></div>',
        },
    })
    local sel = assert(selector.compile('#a'))
    assert.equal(engine:render('t', {}, sel), '<p id="a">1</p>')
    assert.equal(engine:render('t', {}, sel), '<p id="a">1</p>')
    assert.equal(engine._selector_cache:size(), 0)
end

function testcase.string_selector_populates_the_cache()
    local engine = engine_with({
        {
            't',
            '<div><p id="a">1</p></div>',
        },
    }, {
        selector_cache_size = 4,
    })
    assert.equal(engine:render('t', {}, '#a'), '<p id="a">1</p>')
    assert.equal(engine._selector_cache:size(), 1)
    local cached = engine._selector_cache:peek('#a')
    assert.equal(engine:render('t', {}, '#a'), '<p id="a">1</p>')
    assert.equal(engine._selector_cache:size(), 1)
    assert.rawequal(engine._selector_cache:peek('#a'), cached)
end

function testcase.selector_cache_size_zero_disables_caching()
    local engine = engine_with({
        {
            't',
            '<div><p id="a">1</p></div>',
        },
    }, {
        selector_cache_size = 0,
    })
    assert.equal(engine:render('t', {}, '#a'), '<p id="a">1</p>')
    assert.equal(engine._selector_cache:size(), 0)
end

function testcase.selector_errors_surface_from_render()
    local engine = engine_with({
        {
            't',
            '<div><p class="a">1</p><p class="a">2</p></div>',
        },
    })
    local out, err = engine:render('t', {}, '#missing')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
    assert.equal(err.meta.reason, 'no_match')
    out, err = engine:render('t', {}, '.a')
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'multiple_matches')
    out, err = engine:render('t', {}, '??')
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'syntax')
    -- a selector that is neither a string nor a compiled one
    out, err = engine:render('t', {}, 42)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
end

function testcase.template_not_found_comes_first_with_a_selector()
    local engine = new_engine()
    local out, err = engine:render('missing', {}, '#x')
    assert.is_nil(out)
    assert.equal(err.meta.reason, 'not_found')
end

function testcase.include_searches_the_registered_templates()
    local engine = engine_with({
        {
            'layout',
            '<div><main x-include="$.body"></main></div>',
        },
        {
            'page',
            '<section><p id="t">hello</p></section>',
        },
    })
    assert.equal(engine:render('layout', {
        body = 'page',
    }), '<div><main><section><p id="t">hello</p></section></main></div>')
    assert.equal(engine:render('layout', {
        body = 'page',
    }, '#t'), '<p id="t">hello</p>')
end

function testcase.prefix_retargets_directives()
    local engine = engine_with({
        {
            't',
            '<p data-text="$.v" x-text="$.w"></p>',
        },
    }, {
        prefix = 'data-',
    })
    assert.equal(engine:render('t', {
        v = 'V',
    }), '<p x-text="$.w">V</p>')
end

function testcase.max_include_depth_is_applied()
    local engine = engine_with({
        {
            'a',
            '<div x-include="\'b\'"></div>',
        },
        {
            'b',
            '<p>b</p>',
        },
    }, {
        max_include_depth = 1,
    })
    local out, err = engine:render('a')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.EINCLUDE)
    assert.equal(err.meta.reason, 'depth_exceeded')
end

function testcase.helpers_are_copied_when_created()
    local helpers = {
        f = function()
            return 'one'
        end,
    }
    local engine = engine_with({
        {
            't',
            '<p x-text="f()"></p>',
        },
    }, {
        helpers = helpers,
    })
    helpers.f = function()
        return 'two'
    end
    helpers.g = function()
        return 'three'
    end
    assert.equal(engine:render('t'), '<p>one</p>')
    -- a helper added later is not registered
    local ok, err = engine:compile('u', '<p x-text="g()"></p>')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
end

function testcase.render_errors_are_returned_as_is()
    local engine = engine_with({
        {
            't',
            '<p x-text=".a.b"></p>',
        },
    })
    local out, err = engine:render('t', {})
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ERUNTIME)
    out, err = engine:render('t', 'data')
    assert.is_nil(out)
    assert.equal(err.message.message, 'data must be a table or nil')

    -- an error value raised by a helper is kept as the cause
    local raised = errorlib.new('boom')
    engine = engine_with({
        {
            't',
            '<p x-text="boom()"></p>',
        },
    }, {
        helpers = {
            boom = function()
                error(raised)
            end,
        },
    })
    out, err = engine:render('t')
    assert.is_nil(out)
    assert.rawequal(errorlib.unwrap(err), raised)
end

-- engine:templates

function testcase.templates_lists_names_in_registration_order()
    local engine = engine_with({
        {
            'b',
            '<p></p>',
        },
        {
            'a',
            '<p></p>',
        },
    })
    assert.equal(engine:templates(), {
        'b',
        'a',
    })
    -- a new array is returned each time
    local names = engine:templates()
    names[1] = 'changed'
    assert.equal(engine:templates(), {
        'b',
        'a',
    })
    assert.equal(new_engine():templates(), {})
end

-- engine:clear, porting test/unit/reflow-api.test.js of the JavaScript
-- version

function testcase.clear_removes_templates()
    local engine = engine_with({
        {
            'a',
            '<p>A</p>',
        },
        {
            'b',
            '<p>B</p>',
        },
    })
    assert.equal(engine:clear('a'), {
        'a',
    })
    assert.equal(engine:templates(), {
        'b',
    })
    assert.equal(engine:clear('nope'), {})
    assert.equal(engine:clear(42), {})
    assert.equal(engine:templates(), {
        'b',
    })
    assert(engine:compile('c', '<p>C</p>'))
    assert.equal(engine:clear(), {
        'b',
        'c',
    })
    assert.equal(engine:templates(), {})
    assert.equal(engine:clear(), {})
end

function testcase.recompile_after_clear_is_allowed()
    local engine = engine_with({
        {
            't',
            '<p>v1</p>',
        },
    })
    engine:clear('t')
    assert(engine:compile('t', '<p>v2</p>'))
    assert.equal(engine:render('t'), '<p>v2</p>')
end

function testcase.cleared_templates_are_collectable()
    local refs = setmetatable({}, {
        __mode = 'v',
    })
    local engine = engine_with({
        {
            'a',
            '<p></p>',
        },
        {
            'b',
            '<p></p>',
        },
    })
    refs[1] = engine._templates.a
    refs[2] = engine._templates.b
    engine:clear('a')
    collectgarbage('collect')
    collectgarbage('collect')
    assert.is_nil(refs[1])
    assert.not_nil(refs[2])
    engine:clear()
    collectgarbage('collect')
    collectgarbage('collect')
    assert.is_nil(refs[2])
end

-- reflow.compile_selector

function testcase.compile_selector_returns_a_compiled_selector()
    local engine = engine_with({
        {
            't',
            '<div><p id="a">1</p></div>',
        },
    })
    local sel = assert(reflow.compile_selector('#a'))
    assert.is_true(selector.is_compiled(sel))
    assert.equal(engine:render('t', {}, sel), '<p id="a">1</p>')
    local _, err = reflow.compile_selector('??')
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
    assert.equal(err.op, 'reflow.selector.compile')
end

-- error types

function testcase.error_types_are_exported()
    assert.rawequal(reflow.ReflowError, reflow_error.EREFLOW)
    assert.rawequal(reflow.ReflowCompileError, reflow_error.ECOMPILE)
    assert.rawequal(reflow.ReflowRuntimeError, reflow_error.ERUNTIME)
    assert.rawequal(reflow.ReflowIncludeError, reflow_error.EINCLUDE)
    assert.rawequal(reflow.ReflowSelectorError, reflow_error.ESELECTOR)
    local _, err = new_engine():render('missing')
    assert.rawequal(errorlib.is(err, reflow.ReflowRuntimeError), err)
end

-- engine:compile_file, porting test/unit/reflow-api.test.js of the
-- JavaScript version

function testcase.compile_file_requires_a_loader()
    local ok, err = new_engine():compile_file('t', '/some/path')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow_error.EREFLOW)
    assert.equal(err.op, 'reflow.engine.compile_file')
    assert.equal(err.message.message, 'compile_file: loader is required; ' ..
                     'provide config.loader in reflow.new')
end

function testcase.compile_file_uses_the_loader()
    local engine = new_engine({
        loader = function(path)
            return '<p>path=' .. path .. '</p>'
        end,
    })
    assert.is_true(engine:compile_file('t', 'x/y'))
    assert.equal(engine:render('t'), '<p>path=x/y</p>')
    -- the name is registered once, as with compile
    local ok, err = engine:compile_file('t', 'x/y')
    assert.is_nil(ok)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
end

function testcase.compile_file_keeps_loader_errors()
    local raised = errorlib.new('cannot read')
    local returned = {}
    for _, case in ipairs({
        {
            function()
                error(raised)
            end,
            raised,
        },
        {
            function()
                error('plain', 0)
            end,
            'plain',
        },
        {
            function()
                return nil, returned
            end,
            returned,
        },
    }) do
        local ok, err = new_engine({
            loader = case[1],
        }):compile_file('t', 'p')
        assert.is_nil(ok)
        assert.rawequal(err, case[2])
    end
end

function testcase.compile_file_rejects_a_result_that_is_not_html()
    for _, loader in ipairs({
        function()
        end,
        function()
            return nil, nil
        end,
        function()
            return 42
        end,
        function()
            return false, 'err'
        end,
    }) do
        local ok, err = new_engine({
            loader = loader,
        }):compile_file('t', 'p')
        assert.is_nil(ok)
        assert.rawequal(errorlib.typeof(err), reflow_error.EREFLOW)
        assert.equal(err.message.message, 'loader must return an HTML string')
    end
end
