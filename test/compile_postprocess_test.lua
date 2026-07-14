require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local compiler = require('reflow.compiler')

local function C(html)
    return compiler.compile_template(html)
end

-- ===== chain consolidation =====

function testcase.if_only_forms_chain()
    local ir = assert(C('<div x-if="$.a">x</div>'))
    assert.equal(#ir.children, 1)
    assert.equal(ir.children[1].type, 'chain')
    assert.equal(ir.children[1].n_branches, 1)
end

function testcase.if_elseif_else_forms_chain()
    local ir = assert(C(
        '<p x-if="$.a">1</p><p x-elseif="$.b">2</p><p x-else>3</p>'))
    assert.equal(#ir.children, 1)
    assert.equal(ir.children[1].type, 'chain')
    assert.equal(ir.children[1].n_branches, 3)
end

function testcase.whitespace_between_chain_members_stripped()
    local ir = assert(C(
        '<p x-if="$.a">1</p>  <p x-elseif="$.b">2</p>'))
    assert.equal(#ir.children, 1)
    assert.equal(ir.children[1].type, 'chain')
end

function testcase.non_chain_sibling_ends_chain()
    -- Once a non-ignorable sibling appears (here <span>), the chain ends;
    -- a subsequent x-else becomes an orphan and is rejected.
    local r, err = C(
        '<p x-if="$.a">1</p><span>x</span><p x-else>2</p>')
    assert.is_nil(r)
    assert.match(err, 'x-else has no preceding')
end

function testcase.reject_elseif_after_else()
    local r, err = C(
        '<p x-if="$.a">1</p><p x-else>2</p><p x-elseif="$.c">3</p>')
    assert.is_nil(r)
    assert.match(err, 'x-elseif after x-else')
end

function testcase.reject_multiple_else()
    local r, err = C(
        '<p x-if="$.a">1</p><p x-else>2</p><p x-else>3</p>')
    assert.is_nil(r)
    assert.match(err, 'multiple x-else')
end

-- ===== orphan detection =====

function testcase.orphan_elseif()
    local r, err = C('<div x-elseif="$.a">x</div>')
    assert.is_nil(r)
    assert.match(err, 'x-elseif has no preceding x-if')
end

function testcase.orphan_else()
    local r, err = C('<div x-else>x</div>')
    assert.is_nil(r)
    assert.match(err, 'x-else has no preceding')
end

function testcase.orphan_case_outside_match()
    local r, err = C('<div x-case="1">x</div>')
    assert.is_nil(r)
    assert.match(err, 'x-case must be a direct child of x-match')
end

function testcase.orphan_nocase_outside_match()
    local r, err = C('<div x-nocase>x</div>')
    assert.is_nil(r)
    assert.match(err, 'x-nocase must be a direct child of x-match')
end

-- ===== match branch collection =====

function testcase.match_children_moved_to_branches()
    local ir = assert(C(
        '<div x-match="$.n">' ..
        '<span x-case="1">one</span>' ..
        '<span x-case="2">two</span>' ..
        '</div>'))
    -- Match children are consumed into directives.match; the element's
    -- children list is cleared.
    local m = ir.children[1]
    assert.equal(m.type, 'element')
    assert.equal(#m.children, 0)
end

function testcase.match_nocase_last()
    local ir = assert(C(
        '<div x-match="$.n">' ..
        '<span x-case="1">a</span>' ..
        '<span x-nocase>fb</span>' ..
        '</div>'))
    assert.equal(ir.children[1].type, 'element')
    assert.equal(#ir.children[1].children, 0)
end

function testcase.reject_case_after_nocase()
    local r, err = C(
        '<div x-match="$.n">' ..
        '<span x-nocase>fb</span>' ..
        '<span x-case="1">a</span>' ..
        '</div>')
    assert.is_nil(r)
    assert.match(err, 'x-case must not appear after x-nocase')
end

function testcase.reject_multiple_nocase()
    local r, err = C(
        '<div x-match="$.n">' ..
        '<span x-case="1">a</span>' ..
        '<span x-nocase>1</span>' ..
        '<span x-nocase>2</span>' ..
        '</div>')
    assert.is_nil(r)
    assert.match(err, 'multiple x-nocase')
end

function testcase.reject_match_without_case()
    local r, err = C('<div x-match="$.n">   </div>')
    assert.is_nil(r)
    assert.match(err, 'x-match requires at least one x-case')
end

function testcase.reject_match_only_nocase()
    local r, err = C(
        '<div x-match="$.n"><span x-nocase>x</span></div>')
    assert.is_nil(r)
    assert.match(err, 'x-match requires at least one x-case')
end

function testcase.reject_match_with_non_case_child()
    local r, err = C(
        '<div x-match="$.n">' ..
        '<span x-case="1">a</span>' ..
        '<span>bad</span>' ..
        '</div>')
    assert.is_nil(r)
    assert.match(err, 'x-match: direct children must be x-case')
end

-- ===== whitespace / comments ignored around match children =====

function testcase.match_ignores_whitespace_and_comments()
    local ir = assert(C(
        '<div x-match="$.n">\n' ..
        '  <!-- header -->\n' ..
        '  <span x-case="1">a</span>\n' ..
        '</div>'))
    assert.equal(ir.children[1].type, 'element')
end

-- ===== break context =====

function testcase.break_outside_loop_rejected()
    local r, err = C('<div><span x-break></span></div>')
    assert.is_nil(r)
    assert.match(err, 'outside of x-for or x-each')
end

function testcase.break_if_outside_loop_rejected()
    local r, err = C('<div><span x-break-if="$.a"></span></div>')
    assert.is_nil(r)
    assert.match(err, 'outside of x-for or x-each')
end

function testcase.break_inside_for_ok()
    local ir = assert(C(
        '<ul x-for="i=1,5"><li x-break></li></ul>'))
    assert.equal(ir.children[1].tag, 'ul')
end

function testcase.break_inside_each_ok()
    local ir = assert(C(
        '<ul x-each="v in $.items"><li x-break></li></ul>'))
    assert.equal(ir.children[1].tag, 'ul')
end

function testcase.break_deeply_nested_in_loop_ok()
    local ir = assert(C(
        '<ul x-each="v in $.items">' ..
        '<li><span><em x-break></em></span></li>' ..
        '</ul>'))
    assert.equal(ir.children[1].tag, 'ul')
end

function testcase.break_in_match_branch_inside_loop_ok()
    local ir = assert(C(
        '<ul x-each="v in $.items">' ..
        '<li x-match="$.mode">' ..
        '<span x-case="1"><em x-break></em></span>' ..
        '</li>' ..
        '</ul>'))
    assert.equal(ir.children[1].tag, 'ul')
end
