require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local selector = require('reflow.selector')
local newstate = require('newstate')

-- Map the document order of each element to its table, descending into chain
-- and x-match branches without recursion.
local function elements_by_order(root)
    local out = {}
    local stack = {
        root,
    }
    while #stack > 0 do
        local node = table.remove(stack)
        local kids = {}
        if node.type == 'element' then
            out[node.order] = node
            if node.directives and node.directives.match then
                for _, branch in ipairs(node.directives.match.branches) do
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
        elseif node.type == 'root' then
            kids = node.children
        end
        for i = #kids, 1, -1 do
            stack[#stack + 1] = kids[i]
        end
    end
    return out
end

local function compile_template(html)
    local ir, err = compile('page', html, 'x-', {})
    assert.not_nil(ir, err)
    return {
        ir = ir,
        elements = elements_by_order(ir:to_table().root),
    }
end

local function resolve(template, source)
    local sel = assert(selector.compile(source))
    local candidates, err = selector.resolve(template.ir, sel)
    assert.not_nil(candidates, err)
    return candidates
end

local function static_id(element)
    for _, attr in ipairs(element.attrs) do
        if attr[1] == 'id' then
            return attr[2]
        end
    end
end

-- Name each candidate as tag or tag#id, as the JavaScript tests do.
local function names(template, candidates)
    local out = {}
    for i, c in ipairs(candidates) do
        local element = template.elements[c.order]
        local id = static_id(element)
        out[i] = element.tag_name .. (id and ('#' .. id) or '')
    end
    return out
end

local function sorted(list)
    table.sort(list)
    return list
end

-- The following cases port test/selector/resolve.test.js of the JavaScript
-- version. evalPositional is covered with partial rendering.

function testcase.id_seeds_directly()
    local t = compile_template(
                  '<div><section id="hero"></section><span id="hero-dupe"></span></div>')
    local cand = resolve(t, '#hero')
    assert.equal(#cand, 1)
    assert.equal(t.elements[cand[1].order].tag_name, 'section')
    assert.equal(cand[1].positional, {})
end

function testcase.class_seeds_by_class()
    local t = compile_template(
                  '<div class="a b"></div><span class="a"></span><em class="c"></em>')
    assert.equal(sorted(names(t, resolve(t, '.a'))), {
        'div',
        'span',
    })
end

function testcase.multiple_classes_require_all_to_match()
    local t = compile_template('<div class="a b"></div><span class="a"></span>')
    assert.equal(names(t, resolve(t, '.a.b')), {
        'div',
    })
end

function testcase.tag_seed_for_bare_type_selector()
    local t = compile_template(
                  '<section><article></article></section><div></div>')
    assert.equal(names(t, resolve(t, 'article')), {
        'article',
    })
end

function testcase.attribute_name_seed_for_bare_attribute()
    local t = compile_template(
                  '<div data-role="a"></div><span data-role="b"></span><em></em>')
    assert.equal(sorted(names(t, resolve(t, '[data-role]'))), {
        'div',
        'span',
    })
end

function testcase.attribute_value_operators()
    local t = compile_template([[
        <a href="https://example.com/foo"></a>
        <a href="/bar"></a>
        <a href="mailto:test"></a>
        <a title="one two three"></a>
    ]])
    for source, expected in pairs({
        ['[href^="https"]'] = {
            0,
        },
        ['[href$="/bar"]'] = {
            1,
        },
        ['[href*="example"]'] = {
            0,
        },
        ['[title~="two"]'] = {
            3,
        },
        ['[title~="four"]'] = {},
    }) do
        local orders = {}
        for i, c in ipairs(resolve(t, source)) do
            orders[i] = c.order
        end
        assert.equal(orders, expected)
    end
end

function testcase.dash_matches_exact_or_hyphen_prefix()
    local t = compile_template([[
        <p lang="en"></p>
        <p lang="en-US"></p>
        <p lang="fr"></p>
    ]])
    assert.equal(#resolve(t, '[lang|="en"]'), 2)
end

function testcase.descendant_combinator_matches_any_depth()
    local t = compile_template([[
        <section>
            <ul>
                <li>
                    <p><span>deep</span></p>
                </li>
            </ul>
        </section>
    ]])
    assert.equal(names(t, resolve(t, 'section span')), {
        'span',
    })
end

function testcase.child_combinator_requires_direct_parent()
    local t = compile_template([[
        <ul>
            <li>direct</li>
            <div><li>nested</li></div>
        </ul>
    ]])
    local cand = resolve(t, 'ul > li')
    assert.equal(#cand, 1)
    assert.equal(cand[1].order, 1)
end

function testcase.multi_part_complex_selector()
    local t = compile_template([[
        <section id="posts">
            <article>
                <div class="body">
                    <p>x</p>
                </div>
            </article>
        </section>
    ]])
    local cand = resolve(t, '#posts article > .body p')
    assert.equal(#cand, 1)
    assert.equal(t.elements[cand[1].order].tag_name, 'p')
end

function testcase.selector_list_unions_and_deduplicates()
    local t = compile_template([[
        <div id="a"></div>
        <div class="b"></div>
        <div id="a" class="b"></div>
    ]])
    assert.equal(#resolve(t, '#a, .b'), 3)
end

function testcase.candidates_are_sorted_in_document_order()
    local t = compile_template([[
        <section>
            <p class="target">first</p>
            <div><p class="target">second</p></div>
            <p class="target">third</p>
        </section>
    ]])
    local cand = resolve(t, '.target')
    assert.equal(#cand, 3)
    assert.is_true(cand[1].order < cand[2].order)
    assert.is_true(cand[2].order < cand[3].order)
end

function testcase.chain_branches_share_the_enclosing_parent()
    local t = compile_template([[
        <ul id="list">
            <li>a</li>
            <li x-if="$.cond">b</li>
            <li x-else>c</li>
        </ul>
    ]])
    assert.equal(#resolve(t, '#list > li'), 3)
end

function testcase.match_cases_are_children_of_the_match_element()
    local t = compile_template([[
        <div id="wrap" x-match="$.k">
            <section x-case="'a'">A</section>
            <section x-case="'b'">B</section>
        </div>
    ]])
    assert.equal(#resolve(t, '#wrap > section'), 2)
end

function testcase.positional_conditions_flow_to_candidates()
    local t = compile_template('<ul><li>1</li><li>2</li><li>3</li></ul>')
    local cand = resolve(t, 'li:nth-child(2)')
    assert.equal(#cand, 3)
    for _, c in ipairs(cand) do
        assert.equal(c.positional, {
            {
                name = 'nth-child',
                n = 2,
            },
        })
    end
end

function testcase.rejects_positional_conditions_on_non_rightmost_compound()
    local t = compile_template('<ul><li><p>x</p></li></ul>')
    local sel = assert(selector.compile('li:first-child p'))
    local cand, err = selector.resolve(t.ir, sel)
    assert.is_nil(cand)
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
    assert.equal(err.op, 'reflow.selector.resolve')
    assert.equal(err.message.message,
                 'positional pseudo-class ":first-child" is only supported ' ..
                     'on the rightmost compound selector')
    assert.equal(err.meta, {
        source = 'li:first-child p',
        reason = 'unsupported',
        feature = 'pseudo-ancestor:first-child',
    })
end

function testcase.no_seed_list_returns_empty()
    local t = compile_template('<div></div>')
    for _, source in ipairs({
        '#nothing',
        '.nope',
        'nosuchtag',
        '[data-none]',
    }) do
        assert.equal(resolve(t, source), {})
    end
end

function testcase.universal_falls_back_to_all_elements()
    local t = compile_template('<div><p></p></div>')
    assert.equal(#resolve(t, '*'), 2)
end

function testcase.attribute_intersection_uses_smallest_list()
    local t = compile_template([[
        <div data-role="header"></div>
        <div data-role="footer"></div>
        <div data-tag="x"></div>
    ]])
    assert.equal(#resolve(t, '[data-role][data-tag]'), 0)
end

-- Lua specific cases.

function testcase.attribute_operators_follow_javascript_edge_cases()
    local t = compile_template('<p a="x y" b="" c="-z" d="en" e="x' ..
                                   '\194\160' .. 'y"></p>')
    for source, n in pairs({
        -- an empty value never matches ^=, $=, *= and ~=
        ['[b^=""]'] = 0,
        ['[b$=""]'] = 0,
        ['[b*=""]'] = 0,
        ['[a~=""]'] = 0,
        -- a value with white space never matches ~=
        ['[a~="x y"]'] = 0,
        ['[a~="y"]'] = 1,
        -- |= with an empty value matches a leading hyphen
        ['[c|=""]'] = 1,
        ['[d|=""]'] = 0,
        ['[b=""]'] = 1,
        ['[b]'] = 1,
        -- U+00A0 is white space in JavaScript
        ['[e~="y"]'] = 1,
        -- names keep their case, as tags and attributes are lower-cased
        ['[A]'] = 0,
    }) do
        assert.equal(#resolve(t, source), n, source)
    end
end

function testcase.static_conditions_read_the_first_attribute_of_a_name()
    -- The index lists the element under both ids and both classes, but the
    -- conditions read the first attribute of each name.
    local t = compile_template(
                  '<p id="a" id="b" class="x" class="y" data-v="1" data-v="2"></p>')
    assert.equal(#resolve(t, '#a'), 1)
    assert.equal(#resolve(t, '#b'), 0)
    assert.equal(#resolve(t, '.x'), 1)
    assert.equal(#resolve(t, '.y'), 0)
    assert.equal(#resolve(t, '[data-v="1"]'), 1)
    assert.equal(#resolve(t, '[data-v="2"]'), 0)
end

function testcase.class_names_split_on_javascript_white_space()
    local t = compile_template('<p class="a' .. '\227\128\128' .. 'b\tc"></p>')
    assert.equal(#resolve(t, '.a.b.c'), 1)
end

function testcase.dynamic_attributes_are_not_matched()
    local t = compile_template('<p x-bind:id="$.id" x-bind:class="$.c"></p>')
    assert.equal(resolve(t, '#x'), {})
    assert.equal(resolve(t, '[id]'), {})
    assert.equal(#resolve(t, 'p'), 1)
end

function testcase.first_matching_selector_keeps_its_positional_conditions()
    local t = compile_template('<ul><li class="a">1</li><li>2</li></ul>')
    local cand = resolve(t, 'li:last-child, .a:first-child, ul')
    assert.equal(#cand, 3)
    assert.equal(cand[1].order, 0)
    assert.equal(cand[1].positional, {})
    assert.equal(cand[2].positional, {
        {
            name = 'last-child',
        },
    })
    assert.equal(cand[3].positional, {
        {
            name = 'last-child',
        },
    })
end

function testcase.ancestor_positional_condition_is_rejected_only_on_a_match()
    local t = compile_template('<ul><li><p>x</p></li></ul>')
    -- no element matches, so the selector is not rejected
    assert.equal(resolve(t, 'ol:first-child p'), {})
    -- a later complex selector is rejected once it matches
    local sel = assert(selector.compile('p, li:nth-child(1) > p'))
    local cand, err = selector.resolve(t.ir, sel)
    assert.is_nil(cand)
    assert.equal(err.meta.feature, 'pseudo-ancestor:nth-child')
end

function testcase.descendant_combinator_walks_deep_ancestors()
    local depth = 3000
    local t = compile_template(string.rep('<div>', depth) .. '<p></p>' ..
                                   string.rep('</div>', depth))
    local cand = resolve(t, 'div div > div p')
    assert.equal(#cand, 1)
    assert.equal(cand[1].order, depth)
    assert.equal(#resolve(t, 'div > div'), depth - 1)
    assert.equal(resolve(t, 'span p'), {})
end

function testcase.empty_template_has_no_candidates()
    local t = compile_template('text only')
    assert.equal(resolve(t, '*'), {})
end

function testcase.resolve_requires_a_template_and_a_selector()
    local t = compile_template('<p></p>')
    local sel = assert(selector.compile('p'))
    assert.throws(selector.resolve, sel, sel)
    assert.throws(selector.resolve, t.ir, 'p')
    assert.throws(selector.resolve)
end

function testcase.resolve_reports_out_of_memory()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local selector = require('reflow.selector')
        local html = string.rep('<ul class="a"><li class="b">x</li></ul>', 64)
        local ir = assert(compile('page', html, 'x-', {}))
        local sel = assert(selector.compile('.a > .b, ul li, *'))
        local kinds = {}

        -- When so little memory is left that even the error cannot be
        -- created, the memory error is raised instead.
        for extra = 0, 256 * 1024, 16 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, cand, err = pcall(selector.resolve, ir, sel)
            memlimit.maxsize(0)
            if not called then
                assert(tostring(cand):find('not enough memory', 1, true),
                       tostring(cand))
            elseif cand then
                kinds.success = #cand == 128
                break
            else
                assert(errorlib.typeof(err) == errors.ESELECTOR)
                assert(err.message.message ==
                           'out of memory while resolving the selector',
                       err.message.message)
                kinds.oom = true
            end
        end
        return kinds
    ]])
    assert.is_true(ok, tostring(kinds))
    assert.equal(kinds, {
        oom = true,
        success = true,
    })
end
