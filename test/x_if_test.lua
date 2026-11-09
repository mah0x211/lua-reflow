require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local CONFLICT = 'conflicting structural directives on same element ' ..
                     '(x-if / x-elseif / x-else / x-match / x-case / ' ..
                     'x-nocase are mutually exclusive)'

-- Render the node tree in the notation used to compare with the
-- JavaScript version.
local shape
local function shape_list(list)
    local out = {}
    for i, node in ipairs(list) do
        out[i] = shape(node)
    end
    return table.concat(out, ',')
end

function shape(node)
    if node.type == 'text' then
        return string.format('%q', node.text)
    elseif node.type == 'comment' then
        return '<!--' .. node.text .. '-->'
    elseif node.type == 'chain' then
        local out = {}
        for i, branch in ipairs(node.branches) do
            out[i] = (branch.condition and 'if' or 'else') .. ':' ..
                         shape(branch.node)
        end
        return 'chain[' .. table.concat(out, ' | ') .. ']'
    elseif node.type == 'element' then
        return node.tag_name .. '(' .. shape_list(node.children) .. ')'
    end
    return shape_list(node.children)
end

local function tree(html)
    local ir, err = compile('page', html, 'x-', {})
    assert.not_nil(ir, err)
    return ir:to_table().root
end

local function compile_error(html)
    local ir, err = compile('page', html, 'x-', {})
    assert.is_nil(ir)
    assert.rawequal(errorlib.typeof(err), reflow_error.ECOMPILE)
    return err
end

function testcase.x_if_merges_adjacent_branches_into_a_chain()
    -- Expected shapes were produced by the JavaScript version.
    local cases = {
        ['<p x-if="$.a">A</p><p x-else>B</p>'] = 'chain[if:p("A") | else:p("B")]',
        ['<section><p x-if="$.a">A</p> <!-- c --> <p x-elseif="$.b">B</p>' ..
            '\n<p x-else>C</p> tail</section>'] = 'section(chain[if:p("A") | ' ..
            'if:p("B") | else:p("C")]," tail")',
        ['<section><p x-if="$.a">A</p> <!-- trailing --> <span>s</span>' ..
            '</section>'] = 'section(chain[if:p("A")]," ",' ..
            '<!-- trailing -->," ",span("s"))',
        ['<p x-if="$.a">A</p><p x-if="$.b">B</p><p x-else>C</p>'] = 'chain[' ..
            'if:p("A")],chain[if:p("B") | else:p("C")]',
        ['<div><p x-if="$.a">A</p><p x-else>B</p></div>'] = 'div(chain[' ..
            'if:p("A") | else:p("B")])',
        ['<br x-if="$.a"><br x-else>'] = 'chain[if:br() | else:br()]',
        ['<p x-if="$.a">A</p>\194\160<p x-else>B</p>'] = 'chain[if:p("A") | ' ..
            'else:p("B")]',
        ['<p x-if="$.a">A</p><p x-text="1" x-else></p>'] = 'chain[' ..
            'if:p("A") | else:p()]',
    }

    for html, expected in pairs(cases) do
        assert.equal(shape(tree(html)), expected)
    end
end

function testcase.x_if_chains_keep_conditions_and_source_ranges()
    local html =
        '<p x-if="$.a">A</p>\n<p x-elseif="ok($.b)">B</p><p x-else>C</p>'
    local ir, err = compile('page', html, 'x-', {
        ok = true,
    })
    assert.not_nil(ir, err)
    local chain = ir:to_table().root.children[1]
    assert.equal(chain.type, 'chain')
    assert.equal(chain.source_start, 0)
    local last_start = assert(html:find('<p x-else>', 1, true))
    assert.equal(chain.source_end, last_start - 1 + #'<p x-else>')
    assert.equal(#chain.branches, 3)
    assert.equal(chain.branches[1].condition.source, '$.a')
    assert.equal(chain.branches[2].condition.root.callee, 'ok')
    assert.is_nil(chain.branches[3].condition)
    assert.equal(chain.branches[1].node.directives['if'].source, '$.a')
    assert.equal(chain.branches[2].node.directives['elseif'].source, 'ok($.b)')
    assert.is_true(chain.branches[3].node.directives['else'])
end

function testcase.x_if_reports_branch_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<p x-else>B</p>'] = 'x-else has no preceding x-if / x-elseif',
        ['<p x-elseif="$.a">B</p>'] = 'x-elseif has no preceding x-if',
        ['<p x-if="$.a">A</p> <span>s</span><p x-else>B</p>'] = 'x-else ' ..
            'has no preceding x-if / x-elseif',
        ['<p x-if="$.a">A</p><p x-else>B</p><p x-elseif="$.c">C</p>'] = 'x-elseif after x-else is not allowed',
        ['<p x-if="$.a">A</p><p x-else>B</p><p x-else>C</p>'] = 'multiple ' ..
            'x-else in the same chain',
        ['<p x-if="$.a" x-else>A</p>'] = CONFLICT,
        ['<p x-if="$.a" x-elseif="$.b">A</p>'] = CONFLICT,
        ['<p x-else="x">B</p>'] = 'x-else: must not have a value; got ' ..
            '"x-else="x""',
        ['<p x-else="  ">B</p>'] = 'x-else has no preceding x-if / x-elseif',
        ['<p x-if="">A</p>'] = 'x-if: value is required',
        ['<p x-if="#">A</p>'] = 'x-if: expression parse error at position 0: ' ..
            'unexpected character "#"',
        ['<div>\n  <p x-else>B</p>\n</div>'] = 'x-else has no preceding ' ..
            'x-if / x-elseif',
        ['<section><p x-if="$.a">A</p> text <p x-else>B</p></section>'] = 'x-else has no preceding x-if / x-elseif',
    }

    for html, message in pairs(cases) do
        local err = compile_error(html)
        assert.equal(err.message.message, message)
    end
end

function testcase.x_if_branch_errors_locate_the_offending_element()
    local html = table.concat({
        '<main>',
        '  <p x-if="$.a">A</p>',
        '  <p x-else>B</p>',
        '  <p x-else>C</p>',
        '</main>',
    }, '\n')
    local err = compile_error(html)
    assert.equal(err.message.message, 'multiple x-else in the same chain')
    assert.equal(err.meta.line, 4)
    assert.equal(err.meta.column, 3)
    assert.equal(err.meta.element, '<p>')
    assert.match(err.meta.snippet, '<p x-else>')
    assert.is_nil(err.meta.attribute)
end

function testcase.x_if_rejects_duplicate_markers_on_the_same_element()
    for directive, html in pairs({
        ['x-if'] = '<p x-if="$.a" x-if="$.b">A</p>',
        ['x-elseif'] = '<p x-if="1"></p><p x-elseif="$.a" x-elseif="$.b"></p>',
        ['x-else'] = '<p x-if="1"></p><p x-else x-else></p>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message,
                     'duplicate ' .. directive .. ' on element')
        assert.equal(err.meta.attribute, directive)
    end
end

function testcase.x_if_text_between_branches_breaks_the_chain_at_any_level()
    -- Unlike the JavaScript version, top-level text is kept, so it separates
    -- branches exactly as it does inside an element.
    local err = compile_error('<p x-if="$.a">A</p> or <p x-else>B</p>')
    assert.equal(err.message.message, 'x-else has no preceding x-if / x-elseif')

    assert.equal(shape(tree('lead <p x-if="$.a">A</p><p x-else>B</p> end')),
                 '"lead ",chain[if:p("A") | else:p("B")]," end"')
end

function testcase.x_if_finalizes_elements_left_open_at_the_end()
    assert.equal(shape(tree('<div><p x-if="$.a">A</p> <p x-else>B</p>')),
                 'div(chain[if:p("A") | else:p("B")])')

    local err = compile_error('<div><p x-else>B</p>')
    assert.equal(err.message.message, 'x-else has no preceding x-if / x-elseif')
end

function testcase.x_if_handles_long_and_deep_chains_without_c_recursion()
    local branches = {
        '<p x-if="$.a">0</p>',
    }
    for i = 1, 1000 do
        branches[#branches + 1] = '<p x-elseif="$.a">' .. i .. '</p>'
    end
    branches[#branches + 1] = '<p x-else>last</p>'
    local root = tree(table.concat(branches, '\n'))
    assert.equal(#root.children, 1)
    assert.equal(#root.children[1].branches, 1002)
    assert.is_nil(root.children[1].branches[1002].condition)

    local depth = 2000
    root = tree(string.rep('<div x-if="$.a">', depth) .. 'leaf' ..
                    string.rep('</div>', depth))
    local node = root
    for _ = 1, depth do
        assert.equal(#node.children, 1)
        local chain = node.children[1]
        assert.equal(chain.type, 'chain')
        node = chain.branches[1].node
    end
    assert.equal(node.children[1].text, 'leaf')
end
