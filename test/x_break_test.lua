require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')

local OUTSIDE = 'x-break / x-break-if outside of x-for or x-each'

-- List the break markers in document order, without recursion. A trailing
-- "*" marks an invisible marker.
local function marks(root)
    local out = {}
    local stack = {
        root,
    }
    while #stack > 0 do
        local node = table.remove(stack)
        local kids = {}
        if node.type == 'element' then
            local d = node.directives or {}
            if d['break'] or d.break_if then
                out[#out + 1] = node.tag_name ..
                                    (d['break'] and '[break]' or '[break-if]') ..
                                    (node.invisible_marker and '*' or '')
            end
            if d.match then
                for _, branch in ipairs(d.match.branches) do
                    kids[#kids + 1] = branch.node
                end
            end
            for _, child in ipairs(node.children) do
                kids[#kids + 1] = child
            end
        elseif node.type == 'chain' then
            for _, branch in ipairs(node.branches) do
                kids[#kids + 1] = branch.node
            end
        else
            kids = node.children
        end
        for i = #kids, 1, -1 do
            stack[#stack + 1] = kids[i]
        end
    end
    return table.concat(out, ' ')
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

function testcase.x_break_is_accepted_anywhere_inside_a_loop()
    -- Expected markers were produced by the JavaScript version.
    local cases = {
        ['<ul x-for="i = 1, 3"><li><span x-break></span></li></ul>'] = 'span[break]*',
        ['<ul x-each="v in $.a"><li x-break-if="$.v"></li></ul>'] = 'li[break-if]*',
        ['<ul x-for="i = 1, 3"><br x-break></ul>'] = 'br[break]*',
        ['<ul x-for="i = 1, 3"><li x-break="  "></li></ul>'] = 'li[break]*',
        ['<ul x-for="i = 1, 3"><p x-if="$.a"><b x-break></b></p>' ..
            '<p x-else><b x-break-if="$.b"></b></p></ul>'] = 'b[break]* b[break-if]*',
        ['<ul x-for="i = 1, 3"><div x-match="$.a"><p x-case="1">' ..
            '<b x-break></b></p></div></ul>'] = 'b[break]*',
        ['<div x-for="i = 1, 2"><div x-each="v in $.a"><b x-break></b>' ..
            '</div><b x-break></b></div>'] = 'b[break]* b[break]*',
    }

    for html, expected in pairs(cases) do
        assert.equal(marks(tree(html)), expected)
    end
end

function testcase.x_break_is_invisible_only_without_other_attributes()
    -- Expected markers were produced by the JavaScript version.
    for _, extra in ipairs({
        'class="a"',
        'x-bind:id="$.a"',
        'x-text="$.a"',
        'x-data="a: 1"',
        'x-with="a = 1"',
    }) do
        local root = tree('<ul x-for="i = 1, 3"><li x-break ' .. extra ..
                              '></li></ul>')
        assert.equal(marks(root), 'li[break]')
    end

    assert.equal(marks(tree('<ul x-for="i = 1, 3"><li x-break></li></ul>')),
                 'li[break]*')
end

function testcase.x_break_if_keeps_its_condition()
    local ir, err = compile('page', '<ul x-for="i = 1, 3"><li x-break-if=' ..
                                '"ok($.a)"></li></ul>', 'x-', {
        ok = true,
    })
    assert.not_nil(ir, err)
    local li = ir:to_table().root.children[1].children[1]
    assert.equal(li.directives.break_if.root.callee, 'ok')
    assert.is_true(li.invisible_marker)
    assert.is_nil(li.directives['break'])
end

function testcase.x_break_outside_a_loop_is_rejected_at_the_marker()
    -- Expected messages were produced by the JavaScript version.
    for _, html in ipairs({
        '<p x-break></p>',
        '<p x-break-if="$.a"></p>',
        '<div><p><b x-break></b></p></div>',
        '<p x-if="$.a"><b x-break></b></p>',
        '<div x-match="$.a"><p x-case="1"><b x-break></b></p></div>',
        '<b x-break></b><div x-for="i = 1, 2"></div>',
    }) do
        assert.equal(compile_error(html).message.message, OUTSIDE)
    end

    local err = compile_error(table.concat({
        '<main>',
        '  <ul x-for="i = 1, 2"><li x-break></li></ul>',
        '  <p class="a" x-break-if="$.a"></p>',
        '  <p x-break></p>',
        '</main>',
    }, '\n'))
    assert.equal(err.message.message, OUTSIDE)
    assert.equal(err.meta.line, 3)
    assert.equal(err.meta.column, 3)
    assert.equal(err.meta.element, '<p class="a">')
    assert.is_nil(err.meta.attribute)
end

function testcase.x_break_placement_is_checked_after_every_other_error()
    -- Expected messages were produced by the JavaScript version, which checks
    -- placement once the whole template has been compiled.
    local cases = {
        ['<b x-break></b><p x-else></p>'] = 'x-else has no preceding x-if / x-elseif',
        ['<b x-break></b><div x-match="$.a"></div>'] = 'x-match requires at least one x-case',
        ['<b x-break></b><p x-bogus></p>'] = 'unknown directive "x-bogus"',
        ['<b x-break></b><p x-if="#"></p>'] = 'x-if: expression parse error ' ..
            'at position 0: unexpected character "#"',
    }

    for html, message in pairs(cases) do
        assert.equal(compile_error(html).message.message, message)
    end
end

function testcase.x_break_rejects_same_element_combinations()
    -- Expected messages were produced by the JavaScript version.
    local control = 'conflicting control directives on same element ' ..
                        '(x-break / x-break-if are mutually exclusive)'
    local structural = 'cannot combine structural directive with control ' ..
                           'directive on same element; use nesting'
    local iteration = 'cannot combine iteration directive with control ' ..
                          'directive on same element; place x-break / ' ..
                          'x-break-if on a child'
    local cases = {
        ['<li x-break x-break-if="$.a"></li>'] = control,
        ['<li x-break-if="$.a" x-break></li>'] = control,
        ['<li x-break x-if="$.a"></li>'] = structural,
        ['<li x-break-if="$.a" x-match="$.b"><b x-case="1"></b></li>'] = structural,
        ['<li x-break x-for="j = 1, 2"></li>'] = iteration,
        ['<li x-break x-each="v in $.a"></li>'] = iteration,
        ['<b x-break x-if="$.a" x-for="i = 1, 2"></b>'] = 'cannot combine ' ..
            'structural directive with iteration directive on same ' ..
            'element; use nesting',
        ['<b x-break x-text="1" x-html="2"></b>'] = 'conflicting content ' ..
            'directives on same element (x-text / x-html / x-include are ' ..
            'mutually exclusive)',
        ['<b x-break-if="$.a" x-if="$.b" x-else></b>'] = 'conflicting ' ..
            'structural directives on same element (x-if / x-elseif / ' ..
            'x-else / x-match / x-case / x-nocase are mutually exclusive)',
    }

    for html, message in pairs(cases) do
        local err = compile_error('<ul x-for="i = 1, 3">' .. html .. '</ul>')
        assert.equal(err.message.message, message)
    end
end

function testcase.x_break_values_follow_the_javascript_version()
    -- Expected messages were produced by the JavaScript version.
    local cases = {
        ['<li x-break="x"></li>'] = 'x-break: must not have a value; got ' ..
            '"x-break="x""',
        ['<li x-break-if=""></li>'] = 'x-break-if: value is required',
        ['<li x-break-if="#"></li>'] = 'x-break-if: expression parse error ' ..
            'at position 0: unexpected character "#"',
        ['<li x-break-if="nope()"></li>'] = 'x-break-if: unknown helper ' ..
            '"nope" \226\128\148 register it via reflow.new({ helpers = ' ..
            '{ nope = fn } })',
    }

    for html, message in pairs(cases) do
        local err = compile_error('<ul x-for="i = 1, 3">' .. html .. '</ul>')
        assert.equal(err.message.message, message)
        assert.equal(err.meta.attribute:sub(1, 7), 'x-break')
    end
end

function testcase.x_break_rejects_duplicates_on_the_same_element()
    -- The JavaScript version keeps the last duplicate; it is rejected here
    -- like every other duplicate directive.
    for directive, html in pairs({
        ['x-break'] = '<li x-break x-break></li>',
        ['x-break-if'] = '<li x-break-if="$.a" x-break-if="$.b"></li>',
    }) do
        local err = compile_error('<ul x-for="i = 1, 3">' .. html .. '</ul>')
        assert.equal(err.message.message,
                     'duplicate ' .. directive .. ' on element')
        assert.equal(err.meta.attribute, directive)
    end
end

function testcase.x_break_in_elements_left_open_is_checked_too()
    -- Unlike the JavaScript version, which drops elements left open at the
    -- end, they are kept as closed, so their markers are checked as well.
    assert.equal(marks(tree('<div x-for="i = 1, 2"><b x-break></b>')),
                 'b[break]*')
    assert.equal(compile_error('<b x-break>').message.message, OUTSIDE)
end

function testcase.x_break_handles_deep_nesting_without_c_recursion()
    local depth = 2000
    local root = tree('<div x-each="v in $.a">' .. string.rep('<div>', depth) ..
                          '<b x-break></b>' .. string.rep('</div>', depth) ..
                          '</div>')
    assert.equal(marks(root), 'b[break]*')

    local err = compile_error(string.rep('<div>', depth) .. '<b x-break></b>' ..
                                  string.rep('</div>', depth))
    assert.equal(err.message.message, OUTSIDE)
end
