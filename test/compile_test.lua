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
    assert.equal(err.op, 'reflow.compile')
    if template_name ~= nil then
        assert.equal(type(err.meta), 'table')
        assert.equal(err.meta.template_name, template_name)
    end
end

local function compile_ok(name, html, prefix, helper_names)
    local result, err = compile(name, html, prefix or 'x-', helper_names or {})
    assert.not_nil(result, err)
    assert.equal(type(result), 'userdata')
    assert.equal(getmetatable(result), 'reflow.ir')
    return result
end

local function run(source)
    local state = assert(newstate.new())
    return state:dostring(source)
end

function testcase.module_returns_the_compile_function()
    assert.equal(type(compile), 'function')
end

function testcase.success_returns_one_new_ir_userdata_each_time()
    local values = {
        compile('page', '<main>hello</main>', 'x-', {}),
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
    compile_ok('attributes', '<div' .. table.concat(attrs) .. '>ok</div>', 'x-')
end

function testcase.accepts_deep_html_without_a_fixed_limit()
    local depth = 2000
    local html = string.rep('<i>', depth) .. 'leaf' .. string.rep('</i>', depth)
    compile_ok('deep', html)
end

function testcase.compile_accepts_x_data_json5_values()
    local cases = {
        '<div x-data></div>',
        [[<div x-data="scope: 'value'"></div>]],
        [[<div x-data="scope: { key: 'value' }"></div>]],
        [[<div x-data="scope: { key: 'value', }"></div>]],
        '<div x-data="scope: 0x2a"></div>',
        [[<div x-data="scope: /* comment */ 'value'"></div>]],
        '<div x-data="scope: [1, 2,]"></div>',
        '<div x-data="scope: null"></div>',
        '<div x-data="scope: +.5"></div>',
        '<div x-data="scope: -0"></div>',
        '<div x-data="scope: NaN"></div>',
        '<div x-data="scope: Infinity"></div>',
    }

    for i, html in ipairs(cases) do
        compile_ok('x-data-json5-' .. i, html)
    end
end

function testcase.compile_accepts_deep_x_data_without_c_recursion()
    local depth = 2000
    local html = '<div x-data="scope: ' .. string.rep('[', depth) .. '0' ..
                     string.rep(']', depth) .. '"></div>'
    compile_ok('deep-x-data', html)
end

function testcase.compile_rejects_duplicate_x_data()
    local result, err = compile('duplicate-x-data',
                                '<div x-data="scope: 1" x-data="scope: 2">' ..
                                    '</div>', 'x-', {})
    assert.is_nil(result)
    assert_compile_error(err, 'duplicate-x-data')
    assert.equal(err.message.message, 'duplicate x-data on element')
    assert.equal(err.meta.attribute, 'x-data')
    assert.equal(err.meta.line, 1)
    assert.equal(err.meta.column, 1)
    assert.match(err.meta.snippet, '<div x-data="scope: 1" x-data="scope: 2">')
    assert.equal(err.meta.element, '<div>')
end

function testcase.compile_rejects_invalid_x_data_with_source_context()
    local html = table.concat({
        '<main>',
        '  <div x-data="scope: {"></div>',
        '</main>',
    }, '\n')
    local result, err = compile('invalid-x-data', html, 'x-', {})
    assert.is_nil(result)
    assert_compile_error(err, 'invalid-x-data')
    assert.equal(err.message.message, 'x-data: invalid JSON5')
    assert.equal(err.meta.attribute, 'x-data')
    assert.equal(err.meta.line, 2)
    assert.equal(err.meta.column, 3)
    assert.match(err.meta.snippet, '<div x-data="scope: {">')
    assert.equal(err.meta.element, '<div>')
    assert.rawequal(errorlib.typeof(errorlib.unwrap(err)), reflow_error.EREFLOW)
end

function testcase.compile_keeps_unimplemented_directives_rejected()
    local result, err = compile('unimplemented', '<div x-with="scope"></div>',
                                'x-', {})
    assert.is_nil(result)
    assert_compile_error(err, 'unimplemented')
    assert.equal(err.meta.attribute, 'x-with')
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
            return compile(nil, '', 'x-', {})
        end,
        function()
            return compile('', '', 'x-', {})
        end,
        function()
            return compile('page', 1, 'x-', {})
        end,
        function()
            return compile('page', '', false, {})
        end,
        function()
            return compile('page', '', 'x-', nil)
        end,
        function()
            return compile('page', '', 'x-', {
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
    local result, err = compile('page', '<div X-UNKNOWN="value"></div>', 'x-',
                                {})
    assert.is_nil(result)
    assert_compile_error(err, 'page')
    assert.equal(err.meta.attribute, 'x-unknown')

    result, err = compile('empty-prefix', '<div id="value"></div>', '', {})
    assert.is_nil(result)
    assert_compile_error(err, 'empty-prefix')
    assert.equal(err.meta.attribute, 'id')

    compile_ok('different-prefix', '<div x-unknown="value"></div>', 'r-')
end

function testcase.callback_failure_cleans_the_lexbor_session_for_the_next_call()
    local result, err = compile('rejected', '<div x-unknown></div>', 'x-', {})
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
        local result, err = compile('oom', html, 'x-', {})
        memlimit.maxsize(0)
        collectgarbage('collect')
        collectgarbage('collect')

        local retry = compile('retry', '<p>ok</p>', 'x-', {})
        return result == nil, errorlib.typeof(err) == errors.ECOMPILE,
               type(retry) == 'userdata' and
                   getmetatable(retry) == 'reflow.ir'
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.is_true(typed)
    assert.is_true(recovered)
end

function testcase.x_data_pool_oom_returns_no_ir_and_allows_a_later_compile()
    local ok, failed, typed, attributed, recovered = run([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local html = '<div x-data="scope: [' .. string.rep('null,', 8192) ..
                         'null]"></div>'

        collectgarbage('collect')
        local used = memlimit.used()
        memlimit.maxsize(math.max(used, memlimit.minsize()) + 1024 * 1024)
        local result, err = compile('x-data-oom', html, 'x-', {})
        memlimit.maxsize(0)
        collectgarbage('collect')
        collectgarbage('collect')

        local retry = compile('x-data-retry', '<p>ok</p>', 'x-', {})
        return result == nil, errorlib.typeof(err) == errors.ECOMPILE,
               err.meta.attribute == 'x-data',
               type(retry) == 'userdata' and
                   getmetatable(retry) == 'reflow.ir'
    ]])
    assert.is_true(ok)
    assert.is_true(failed)
    assert.is_true(typed)
    assert.is_true(attributed)
    assert.is_true(recovered)
end
local function element(node, tag_name)
    assert.equal(node.type, 'element')
    assert.equal(node.tag_name, tag_name)
    return node
end

-- Lua 5.3 and later keep the IR double as a float subtype, so numbers are
-- compared by value instead of through a table comparison.
local function assert_number_value(node, expected)
    assert.equal(node.type, 'number')
    assert.is_true(node.value == expected)
end

local function start_tag_range(html, start_tag, init)
    local first, last = string.find(html, start_tag, init or 1, true)
    assert.not_nil(first, start_tag)
    return first - 1, last
end

function testcase.ir_to_table_maps_name_source_and_root()
    local html = '<main>hello</main>'
    local ir = compile_ok('page', html)
    local tree = ir:to_table()

    assert.equal(tree.name, 'page')
    assert.equal(tree.html, html)
    assert.equal(tree.root.type, 'root')
    assert.equal(#tree.root.children, 1)
    local main = element(tree.root.children[1], 'main')
    assert.equal(main.source_start, 0)
    assert.equal(main.source_end, 6)
    assert.equal(#main.attrs, 0)
    assert.is_nil(main.directives)
    assert.equal(#main.children, 1)
    assert.equal(main.children[1].type, 'text')
    assert.equal(main.children[1].text, 'hello')
end

function testcase.ir_to_table_returns_a_new_table_without_changing_the_ir()
    local ir = compile_ok('page', '<main id="a">hello</main>')
    local first = ir:to_table()
    local second = ir:to_table()

    assert.not_rawequal(first, second)
    assert.not_rawequal(first.root, second.root)
    assert.equal(first, second)

    first.root.children[1].tag_name = 'changed'
    assert.equal(ir:to_table(), second)
end

function testcase.ir_to_table_rejects_values_other_than_reflow_ir()
    local ir = compile_ok('page', '<p>ok</p>')
    local to_table = ir.to_table

    assert.equal(type(to_table), 'function')
    for _, value in ipairs({
        false,
        1,
        'reflow.ir',
        {},
        newproxy and newproxy() or io.stdout,
    }) do
        local ok = pcall(to_table, value)
        assert.is_false(ok)
    end
    assert.equal(getmetatable(ir), 'reflow.ir')
end

function testcase.ir_keeps_attribute_order_duplicates_and_empty_values()
    local html = '<DIV ID="first" id=second disabled data-x="">body</DIV>'
    local tree = compile_ok('attrs', html):to_table()
    local div = element(tree.root.children[1], 'div')
    local start, stop = start_tag_range(html,
                                        '<DIV ID="first" id=second disabled data-x="">')

    assert.equal(div.source_start, start)
    assert.equal(div.source_end, stop)
    assert.equal(div.attrs, {
        {
            name = 'id',
            value = 'first',
        },
        {
            name = 'id',
            value = 'second',
        },
        {
            name = 'disabled',
            value = '',
        },
        {
            name = 'data-x',
            value = '',
        },
    })
end

function testcase.ir_keeps_node_order_text_comments_and_byte_ranges()
    local html =
        '<main><!-- note --><p>日本語🙂</p>a &amp; b<br>tail</main>'
    local tree = compile_ok('nodes', html):to_table()
    local main = element(tree.root.children[1], 'main')
    local p_start, p_stop = start_tag_range(html, '<p>')
    local br_start, br_stop = start_tag_range(html, '<br>')

    assert.equal(main.source_start, 0)
    assert.equal(main.source_end, 6)
    assert.equal(#main.children, 5)
    assert.equal(main.children[1], {
        type = 'comment',
        text = ' note ',
    })
    local p = element(main.children[2], 'p')
    assert.equal(p.source_start, p_start)
    assert.equal(p.source_end, p_stop)
    assert.equal(p.children, {
        {
            type = 'text',
            text = '日本語🙂',
        },
    })
    assert.equal(main.children[3], {
        type = 'text',
        text = 'a &amp; b',
    })
    local br = element(main.children[4], 'br')
    assert.equal(br.source_start, br_start)
    assert.equal(br.source_end, br_stop)
    assert.equal(#br.children, 0)
    assert.equal(main.children[5], {
        type = 'text',
        text = 'tail',
    })
end

function testcase.ir_keeps_lenient_closing_and_raw_text_structure()
    local tree = compile_ok('lenient',
                            '<main><p>implicitly closed</main></orphan>' ..
                                '<section>kept'):to_table()
    assert.equal(#tree.root.children, 2)
    local main = element(tree.root.children[1], 'main')
    local p = element(main.children[1], 'p')
    assert.equal(p.children[1].text, 'implicitly closed')
    local section = element(tree.root.children[2], 'section')
    assert.equal(section.children[1].text, 'kept')

    local script = '<script>const html = "<div x-unknown>";</script>'
    tree = compile_ok('raw-text', script):to_table()
    local node = element(tree.root.children[1], 'script')
    assert.equal(node.children, {
        {
            type = 'text',
            text = 'const html = "<div x-unknown>";',
        },
    })

    tree = compile_ok('nul', '<p>a\0b</p>'):to_table()
    assert.equal(tree.root.children[1].children, {
        {
            type = 'text',
            text = 'a\0b',
        },
    })

    tree = compile_ok('empty', ''):to_table()
    assert.equal(tree.root.children, {})
end

function testcase.ir_maps_x_data_values_with_types_order_and_duplicates()
    local html =
        '<div x-data="a: \'v\', b: { k: [1, true, null] }, a: -1.5, ' ..
            'n: NaN, e: {}, l: []" id="d"></div>'
    local tree = compile_ok('x-data', html):to_table()
    local div = element(tree.root.children[1], 'div')
    local data = div.directives.data

    assert.equal(div.attrs, {
        {
            name = 'id',
            value = 'd',
        },
    })
    assert.equal(data.type, 'object')
    assert.equal(#data.members, 6)
    assert.equal(data.members[1], {
        key = 'a',
        value = {
            type = 'string',
            value = 'v',
        },
    })
    assert.equal(data.members[2].key, 'b')
    local object = data.members[2].value
    assert.equal(object.type, 'object')
    assert.equal(#object.members, 1)
    assert.equal(object.members[1].key, 'k')
    local array = object.members[1].value
    assert.equal(array.type, 'array')
    assert.equal(#array.items, 3)
    assert_number_value(array.items[1], 1)
    assert.equal(array.items[2], {
        type = 'boolean',
        value = true,
    })
    assert.equal(array.items[3], {
        type = 'null',
    })
    assert.equal(data.members[3], {
        key = 'a',
        value = {
            type = 'number',
            value = -1.5,
        },
    })
    local nan = data.members[4].value.value
    assert.equal(data.members[4].key, 'n')
    assert.is_true(nan ~= nan)
    assert.equal(data.members[5], {
        key = 'e',
        value = {
            type = 'object',
            members = {},
        },
    })
    assert.equal(data.members[6], {
        key = 'l',
        value = {
            type = 'array',
            items = {},
        },
    })

    tree = compile_ok('empty-x-data', '<div x-data></div>'):to_table()
    assert.equal(tree.root.children[1].directives.data, {
        type = 'object',
        members = {},
    })
end

function testcase.ir_to_table_converts_deep_and_wide_trees_without_c_recursion()
    local depth = 2000
    local tree = compile_ok('deep', string.rep('<i>', depth) .. 'leaf' ..
                                string.rep('</i>', depth)):to_table()
    local node = tree.root
    for _ = 1, depth do
        assert.equal(#node.children, 1)
        node = element(node.children[1], 'i')
    end
    assert.equal(node.children[1].text, 'leaf')

    tree = compile_ok('deep-x-data',
                      '<div x-data="v: ' .. string.rep('[', depth) .. '0' ..
                          string.rep(']', depth) .. '"></div>'):to_table()
    local value = tree.root.children[1].directives.data.members[1].value
    for _ = 1, depth do
        assert.equal(value.type, 'array')
        assert.equal(#value.items, 1)
        value = value.items[1]
    end
    assert_number_value(value, 0)

    local count = 5000
    tree = compile_ok('wide', string.rep('<b></b>', count)):to_table()
    assert.equal(#tree.root.children, count)
    assert.equal(tree.root.children[count].source_start, (count - 1) * 7)
end

function testcase.ir_to_table_reads_owned_source_after_inputs_are_collected()
    local ir
    do
        ir = compile_ok(table.concat({
            'owned',
            '-name',
        }), table.concat({
            '<article>',
            'owned html',
            '</article>',
        }))
    end
    collectgarbage('collect')
    collectgarbage('collect')
    local tree = ir:to_table()
    assert.equal(tree.name, 'owned-name')
    assert.equal(tree.html, '<article>owned html</article>')
    assert.equal(element(tree.root.children[1], 'article').children[1].text,
                 'owned html')
end
