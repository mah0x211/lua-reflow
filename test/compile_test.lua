require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local newstate = require('newstate')

local function assert_compile_error(err, template_name)
    assert.equal(type(err), 'table')
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    assert.equal(err.op, 'reflow.compile.compile')
    if template_name ~= nil then
        assert.equal(type(err.meta), 'table')
        assert.equal(err.meta.template_name, template_name)
    end
end

local function compile_ok(name, html, prefix, helper_names)
    local result, err = compile.compile(name, html, prefix or 'x-',
                                        helper_names or {})
    assert.not_nil(result, err)
    assert.equal(type(result), 'userdata')
    assert.equal(getmetatable(result), 'reflow.ir')
    return result
end

local function run(source)
    local state = assert(newstate.new())
    return state:dostring(source)
end

function testcase.module_exposes_only_the_named_compile_operation()
    assert.equal(type(compile), 'table')
    assert.equal(type(compile.compile), 'function')
    local count = 0
    for _ in pairs(compile) do
        count = count + 1
    end
    assert.equal(count, 1)
end

function testcase.success_returns_one_new_ir_userdata_each_time()
    local values = {
        compile.compile('page', '<main>hello</main>', 'x-', {}),
    }
    assert.equal(#values, 1)
    assert.equal(type(values[1]), 'userdata')
    assert.equal(getmetatable(values[1]), 'reflow.ir')

    local second = compile_ok('page', '<main>hello</main>')
    assert.not_equal(tostring(values[1]), tostring(second))
end

function testcase.accepts_basic_html_notification_cases()
    local cases = {
        '',
        'plain text',
        '<!DOCTYPE html><main>body</main>',
        '<DIV ID="first" id=second disabled>body</DIV>',
        '<p>a &amp; b</p>',
        '<main><!-- note --><p>日本語🙂</p></main>',
        '<custom-element data-value="ok">custom</custom-element>',
        '<svg viewBox="0 0 1 1"><path d="M0 0"/></svg>',
        '<main><p>implicitly closed</main></orphan>',
        '</orphan><section>kept',
        '<section/>self-closing mark is not an HTML close',
        '<div data-value=">">quoted &gt; value</div>',
        '<p>a\0b</p>',
    }

    for i, html in ipairs(cases) do
        compile_ok('case-' .. i, html)
    end
end

function testcase.accepts_markup_like_text_in_raw_text_and_rcdata_elements()
    local cases = {
        '<script>const html = "<div x-unknown>";</script>',
        '<style>.item::before { content: "<div x-unknown>"; }</style>',
        '<textarea><div x-unknown></textarea>',
        '<title><div x-unknown></title>',
    }

    for i, html in ipairs(cases) do
        compile_ok('raw-text-' .. i, html)
    end
end

function testcase.accepts_many_attributes_without_a_fixed_limit()
    local attrs = {}
    for i = 1, 160 do
        attrs[i] = ' DATA-VALUE="' .. i .. '"'
    end
    compile_ok('attributes', '<div' .. table.concat(attrs) .. '>ok</div>',
               'x-')
end

function testcase.accepts_deep_html_without_a_fixed_limit()
    local depth = 2000
    local html = string.rep('<i>', depth) .. 'leaf' ..
                     string.rep('</i>', depth)
    compile_ok('deep', html)
end

function testcase.result_remains_owned_after_input_values_are_collected()
    local result
    do
        local name = table.concat({
            'owned',
            '-name',
        })
        local html = table.concat({
            '<article>',
            'owned html',
            '</article>',
        })
        result = compile_ok(name, html)
    end
    collectgarbage('collect')
    collectgarbage('collect')
    assert.equal(type(result), 'userdata')
    assert.equal(getmetatable(result), 'reflow.ir')
end

function testcase.rejects_invalid_input_contract_with_compile_errors()
    local invalid = {
        function()
            return compile.compile(nil, '', 'x-', {})
        end,
        function()
            return compile.compile('', '', 'x-', {})
        end,
        function()
            return compile.compile('page', 1, 'x-', {})
        end,
        function()
            return compile.compile('page', '', false, {})
        end,
        function()
            return compile.compile('page', '', 'x-', nil)
        end,
        function()
            return compile.compile('page', '', 'x-', {
                [1] = true,
            })
        end,
    }

    for _, invoke in ipairs(invalid) do
        local result, err = invoke()
        assert.is_nil(result)
        assert_compile_error(err)
    end
end

function testcase.rejects_configured_prefix_attributes_without_downgrading()
    local result, err = compile.compile('page',
                                        '<div X-UNKNOWN="value"></div>',
                                        'x-', {})
    assert.is_nil(result)
    assert_compile_error(err, 'page')
    assert.equal(err.meta.attribute, 'x-unknown')

    result, err = compile.compile('empty-prefix', '<div id="value"></div>',
                                  '', {})
    assert.is_nil(result)
    assert_compile_error(err, 'empty-prefix')
    assert.equal(err.meta.attribute, 'id')

    compile_ok('different-prefix', '<div x-unknown="value"></div>', 'r-')
end

function testcase.callback_failure_cleans_the_lexbor_session_for_the_next_call()
    local result, err = compile.compile('rejected', '<div x-unknown></div>',
                                        'x-', {})
    assert.is_nil(result)
    assert_compile_error(err, 'rejected')

    compile_ok('recovered', '<div>ok</div>')
end

function testcase.result_pool_oom_returns_no_ir_and_allows_a_later_compile()
    local ok, failed, typed, recovered = run([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local html = string.rep('x', 1024 * 1024)

        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 65536)
        local result, err = compile.compile('oom', html, 'x-', {})
        memlimit.maxsize(0)
        collectgarbage('collect')
        collectgarbage('collect')

        local retry = compile.compile('retry', '<p>ok</p>', 'x-', {})
        return result == nil, errorlib.typeof(err) == errors.ECOMPILE,
               type(retry) == 'userdata' and
                   getmetatable(retry) == 'reflow.ir'
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.is_true(typed)
    assert.is_true(recovered)
end
