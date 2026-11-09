require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local CONFLICT = 'conflicting structural directives on same element ' ..
                     '(x-if / x-elseif / x-else / x-match / x-case / ' ..
                     'x-nocase are mutually exclusive)'
local NOT_A_BRANCH = 'x-match: direct children must be x-case or x-nocase ' ..
                         'elements'

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
        local s = node.tag_name
        if node.directives and node.directives.match then
            local out = {}
            for i, branch in ipairs(node.directives.match.branches) do
                out[i] = (branch.condition and 'case' or 'nocase') .. ':' ..
                             shape(branch.node)
            end
            s = s .. '{match:' .. table.concat(out, ' | ') .. '}'
        end
        return s .. '(' .. shape_list(node.children) .. ')'
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

function testcase.x_match_collects_case_and_nocase_children_as_branches()
    -- Expected shapes were produced by the JavaScript version.
    local cases = {
        ['<div x-match="$.v"><p x-case="1">A</p><p x-case="2">B</p>' ..
            '<p x-nocase>C</p></div>'] = 'div{match:case:p("A") | ' ..
            'case:p("B") | nocase:p("C")}()',
        ['<div x-match="$.v">\n  <!-- c -->\n  <p x-case="1">A</p>\n' ..
            '  <p x-nocase>C</p>\n</div>'] = 'div{match:case:p("A") | ' ..
            'nocase:p("C")}()',
        ['<div x-match="$.v" x-text="1"><p x-case="1">A</p></div>'] = 'div{' ..
            'match:case:p("A")}()',
        ['<section><div x-match="$.v"><p x-case="1">A</p></section>'] = 'section(div{match:case:p("A")}())',
        ['<div x-match="$.v"><p x-case="1"><b x-match="$.w">' ..
            '<i x-case="2">B</i></b></p></div>'] = 'div{match:case:p(b{' ..
            'match:case:i("B")}())}()',
    }

    for html, expected in pairs(cases) do
        assert.equal(shape(tree(html)), expected)
    end
end

function testcase.x_match_keeps_conditions_and_branch_markers()
    local html = '<div x-match="ok($.v)"><p x-case="1">A</p>' ..
                     '<p x-case="$.b">B</p><p x-nocase>C</p></div>'
    local ir, err = compile('page', html, 'x-', {
        ok = true,
    })
    assert.not_nil(ir, err)
    local div = ir:to_table().root.children[1]
    local match = div.directives.match
    assert.equal(match.condition.root.callee, 'ok')
    assert.equal(#div.children, 0)
    assert.equal(#match.branches, 3)
    assert.equal(match.branches[1].condition.source, '1')
    assert.equal(match.branches[2].condition.source, '$.b')
    assert.is_nil(match.branches[3].condition)
    assert.equal(match.branches[1].node.directives.case.source, '1')
    assert.is_true(match.branches[3].node.directives.nocase)
    local start = assert(html:find('<p x-case="$.b">', 1, true))
    assert.equal(match.branches[2].node.source_start, start - 1)
end

function testcase.x_match_reports_structure_errors_like_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<div x-match="$.v"> hi <p x-case="1">A</p></div>'] = NOT_A_BRANCH ..
            ' (found text " hi ")',
        ['<div x-match="$.v">a very long text node that exceeds twenty' ..
            '<p x-case="1">A</p></div>'] = NOT_A_BRANCH ..
            ' (found text "a very long text nod")',
        ['<div x-match="$.v">日本語のテキストが二十文字を超える場合の切り詰めの' ..
            '確認です<p x-case="1">A</p></div>'] = NOT_A_BRANCH ..
            ' (found text "日本語のテキストが二十文字を超える場合の")',
        ['<div x-match="$.v"><span>s</span></div>'] = NOT_A_BRANCH,
        ['<div x-match="$.v"><p x-case="1">A</p><span x-text="1">s</span>' ..
            '</div>'] = NOT_A_BRANCH,
        ['<div x-match="$.v"><p x-if="$.a">A</p></div>'] = NOT_A_BRANCH ..
            ' (found chain)',
        ['<div x-match="$.v"><p x-nocase>C</p><p x-case="1">A</p></div>'] = 'x-case must not appear after x-nocase in the same x-match',
        ['<div x-match="$.v"><p x-case="1">A</p><p x-nocase>C</p>' ..
            '<p x-nocase>D</p></div>'] = 'multiple x-nocase in the same x-match',
        ['<div x-match="$.v"><p x-nocase>C</p></div>'] = 'x-match requires at least one x-case',
        ['<div x-match="$.v"></div>'] = 'x-match requires at least one x-case',
        ['<div x-match="$.v">  </div>'] = 'x-match requires at least one x-case',
        ['<p x-case="1">A</p>'] = 'x-case must be a direct child of x-match',
        ['<p x-nocase>A</p>'] = 'x-nocase must be a direct child of x-match',
        ['<div><p x-case="1">A</p></div>'] = 'x-case must be a direct child ' ..
            'of x-match',
        ['<div x-match="$.v"><p x-case="1"><span x-case="2">X</span></p>' ..
            '</div>'] = 'x-case must be a direct child of x-match',
        ['<div x-match="$.v" x-if="$.a"><p x-case="1">A</p></div>'] = CONFLICT,
        ['<p x-case="1" x-nocase>A</p>'] = CONFLICT,
        ['<div x-match="$.v"><div x-case="1" x-match="$.w">' ..
            '<p x-case="2">B</p></div></div>'] = CONFLICT,
        ['<div x-match=""><p x-case="1">A</p></div>'] = 'x-match: value is required',
        ['<div x-match="$.v"><p x-case="">A</p></div>'] = 'x-case: value is required',
        ['<div x-match="$.v"><p x-nocase="x">A</p></div>'] = 'x-nocase: ' ..
            'must not have a value; got "x-nocase="x""',
        ['<div x-match="$.v"><p x-case="#">A</p></div>'] = 'x-case: ' ..
            'expression parse error at position 0: unexpected character "#"',
    }

    for html, message in pairs(cases) do
        local err = compile_error('<section>' .. html .. '</section>')
        assert.equal(err.message.message, message)
    end
end

function testcase.x_match_quotes_text_without_splitting_astral_characters()
    -- JavaScript slices 20 UTF-16 code units and may cut a surrogate pair in
    -- half; the excerpt keeps such a character whole instead.
    local emoji = '\240\159\152\128'
    local err = compile_error('<div x-match="$.v">' .. string.rep('a', 19) ..
                                  emoji .. 'tail<p x-case="1">A</p></div>')
    assert.equal(err.message.message, NOT_A_BRANCH .. ' (found text "' ..
                     string.rep('a', 19) .. emoji .. '")')

    err = compile_error('<div x-match="$.v">' .. string.rep('a', 20) .. emoji ..
                            '<p x-case="1">A</p></div>')
    assert.equal(err.message.message, NOT_A_BRANCH .. ' (found text "' ..
                     string.rep('a', 20) .. '")')
end

function testcase.x_match_errors_locate_the_match_or_the_offending_child()
    local html = table.concat({
        '<main>',
        '  <div x-match="$.v">',
        '    <p x-case="1">A</p>',
        '    <span>s</span>',
        '  </div>',
        '</main>',
    }, '\n')
    local err = compile_error(html)
    assert.equal(err.message.message, NOT_A_BRANCH)
    assert.equal(err.meta.line, 4)
    assert.equal(err.meta.column, 5)
    assert.equal(err.meta.element, '<span>')

    err = compile_error(html:gsub('<span>s</span>', 'text'))
    assert.equal(err.message.message,
                 NOT_A_BRANCH .. ' (found text "\n' .. '    text\n  ")')
    assert.equal(err.meta.line, 2)
    assert.equal(err.meta.column, 3)
    assert.equal(err.meta.element, '<div>')
    assert.is_nil(err.meta.attribute)
end

function testcase.x_match_rejects_duplicate_markers_on_the_same_element()
    -- The JavaScript version keeps the last duplicate; it is rejected here
    -- like every other duplicate directive.
    for directive, html in pairs({
        ['x-match'] = '<div x-match="$.v" x-match="$.w"><p x-case="1"></p></div>',
        ['x-case'] = '<div x-match="$.v"><p x-case="1" x-case="2"></p></div>',
        ['x-nocase'] = '<div x-match="$.v"><p x-case="1"></p>' ..
            '<p x-nocase x-nocase></p></div>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message,
                     'duplicate ' .. directive .. ' on element')
        assert.equal(err.meta.attribute, directive)
    end
end

function testcase.x_match_handles_many_and_deep_branches_without_c_recursion()
    local branches = {}
    for i = 1, 1000 do
        branches[#branches + 1] = '<p x-case="' .. i .. '">' .. i .. '</p>'
    end
    branches[#branches + 1] = '<p x-nocase>none</p>'
    local root = tree('<div x-match="$.v">' .. table.concat(branches, '\n') ..
                          '</div>')
    local match = root.children[1].directives.match
    assert.equal(#match.branches, 1001)
    assert.is_nil(match.branches[1001].condition)

    local depth = 1000
    root = tree(
               string.rep('<div x-match="$.v"><p x-case="1">', depth) .. 'leaf' ..
                   string.rep('</p></div>', depth))
    local node = root.children[1]
    for _ = 1, depth do
        match = node.directives.match
        assert.equal(#match.branches, 1)
        local p = match.branches[1].node
        assert.equal(p.tag_name, 'p')
        node = p.children[1]
    end
    assert.equal(node.text, 'leaf')
end

function testcase.x_match_and_x_case_require_registered_helpers()
    for attribute, html in pairs({
        ['x-match'] = '<div x-match="nope($.v)"><p x-case="1"></p></div>',
        ['x-case'] = '<div x-match="$.v"><p x-case="nope(1)"></p></div>',
    }) do
        local err = compile_error(html)
        assert.equal(err.message.message,
                     attribute .. ': unknown helper "nope" \226\128\148 ' ..
                         'register it via reflow.new({ helpers = { nope = fn } })')
        assert.equal(err.meta.attribute, attribute)
    end
end
