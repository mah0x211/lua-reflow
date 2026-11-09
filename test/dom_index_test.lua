require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local compile = require('reflow.compile')

local function compile_table(html)
    local ir, err = compile('page', html, 'x-', {})
    assert.not_nil(ir, err)
    return ir:to_table()
end

-- List the elements in document order without recursion, descending into
-- chain and x-match branches.
local function elements(root)
    local out = {}
    local stack = {
        root,
    }
    while #stack > 0 do
        local node = table.remove(stack)
        local kids = {}
        if node.type == 'element' then
            out[#out + 1] = node
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

-- Render annotations as "order:tag:parent:depth:branch".
local function annotations(t)
    local out = {}
    for i, e in ipairs(elements(t.root)) do
        out[i] = table.concat({
            e.order,
            e.tag_name,
            e.parent or '-',
            e.depth,
            e.chain_branch and ('c' .. e.chain_branch) or
                (e.match_branch and ('m' .. e.match_branch) or ''),
        }, ':')
    end
    return table.concat(out, ' ')
end

function testcase.dom_index_annotates_structure_like_the_javascript_version()
    -- Expected annotations were produced by the JavaScript version.
    local cases = {
        ['<div id="a" class="x y"><p class="y">t</p><span></span></div>'] = '0:div:-:0: 1:p:0:1: 2:span:0:1:',
        ['<p x-if="$.a" id="i">A</p><p x-elseif="$.b">B</p>' ..
            '<p x-else class="e">C</p>'] = '0:p:-:0:c0 1:p:-:0:c1 2:p:-:0:c2',
        ['<section><p x-if="$.a">A</p><p x-else><b>B</b></p></section>'] = '0:section:-:0: 1:p:0:1:c0 2:p:0:1:c1 3:b:2:2:',
        ['<div x-match="$.v"><p x-case="1"><i>A</i></p>' ..
            '<p x-nocase class="n">C</p></div>'] = '0:div:-:0: 1:p:0:1:m0 ' ..
            '2:i:1:2: 3:p:0:1:m1',
        ['<div x-match="$.v"><p x-case="1"><div x-match="$.w">' ..
            '<b x-case="2"></b></div></p></div>'] = '0:div:-:0: 1:p:0:1:m0 ' ..
            '2:div:1:2: 3:b:2:3:m0',
        ['<div><p x-if="$.a">A</p> <p x-else>B</p><span x-if="$.c"></span>' ..
            '</div>'] = '0:div:-:0: 1:p:0:1:c0 2:p:0:1:c1 3:span:0:1:c0',
        ['text<br><!-- c --><img src="a">'] = '0:br:-:0: 1:img:-:0:',
        ['<div><div><div><b></b></div></div></div><i></i>'] = '0:div:-:0: ' ..
            '1:div:0:1: 2:div:1:2: 3:b:2:3: 4:i:-:0:',
    }

    for html, expected in pairs(cases) do
        assert.equal(annotations(compile_table(html)), expected)
    end
end

function testcase.dom_index_lists_static_attributes_in_document_order()
    -- Expected lists were produced by the JavaScript version.
    local index = compile_table('<div id="a" class="x y"><p class="y">t</p>' ..
                                    '<span></span></div>').index
    assert.equal(index.by_id, {
        a = {
            0,
        },
    })
    assert.equal(index.by_class, {
        x = {
            0,
        },
        y = {
            0,
            1,
        },
    })
    assert.equal(index.by_tag, {
        div = {
            0,
        },
        p = {
            1,
        },
        span = {
            2,
        },
    })
    assert.equal(index.by_attr_name, {
        id = {
            0,
        },
        class = {
            0,
            1,
        },
    })
    assert.equal(index.elements, {
        0,
        1,
        2,
    })
    assert.equal(index.includes, {})
end

function testcase.dom_index_keeps_repeated_entries_and_skips_empty_values()
    -- The JavaScript version lists an element once per occurrence.
    local index = compile_table('<p class="a a" class="b" id="" ' ..
                                    'data-x="1" data-x="2"></p>' ..
                                    '<p class=""></p><p id="z"></p>' ..
                                    '<p id="z"></p>').index
    assert.equal(index.by_class, {
        a = {
            0,
            0,
        },
        b = {
            0,
        },
    })
    assert.equal(index.by_id, {
        z = {
            2,
            3,
        },
    })
    assert.equal(index.by_attr_name, {
        class = {
            0,
            0,
            1,
        },
        id = {
            0,
            2,
            3,
        },
        ['data-x'] = {
            0,
            0,
        },
    })
end

function testcase.dom_index_splits_classes_on_javascript_whitespace()
    local index = compile_table('<p class="  a\tb\194\160c\227\128\128d  ">' ..
                                    '</p>').index
    assert.equal(index.by_class, {
        a = {
            0,
        },
        b = {
            0,
        },
        c = {
            0,
        },
        d = {
            0,
        },
    })

    -- Tag and attribute names arrive in lower case; values keep their case.
    index = compile_table('<P CLASS="A" ID="B" Data-X="1"></P>').index
    assert.equal(index.by_tag, {
        p = {
            0,
        },
    })
    assert.equal(index.by_id, {
        B = {
            0,
        },
    })
    assert.equal(index.by_class, {
        A = {
            0,
        },
    })
end

function testcase.dom_index_ignores_dynamic_attributes_and_lists_includes()
    local index = compile_table('<div x-include="\'x\'"></div>' ..
                                    '<ul x-for="i = 1, 2"><li x-include=' ..
                                    '"\'y\'" x-bind:id="$.a"></li></ul>' ..
                                    '<p x-bind:class="$.c" x-text="1"></p>').index
    assert.equal(index.includes, {
        0,
        2,
    })
    assert.equal(index.by_id, {})
    assert.equal(index.by_class, {})
    assert.equal(index.by_attr_name, {})
end

function testcase.dom_index_handles_many_keys_and_deep_trees_without_c_recursion()
    local list = {}
    for i = 1, 1000 do
        list[i] = '<p id="id' .. i .. '" class="c' .. i % 7 .. '"></p>'
    end
    local index = compile_table(table.concat(list)).index
    for i = 1, 1000 do
        assert.equal(index.by_id['id' .. i], {
            i - 1,
        })
    end
    assert.equal(#index.by_class.c0, 142)
    assert.equal(#index.elements, 1000)

    local depth = 2000
    local html = string.rep('<div x-if="$.a"><section x-match="$.v">' ..
                                '<p x-case="1">', depth) ..
                     string.rep('</p></section></div>', depth)
    local t = compile_table(html)
    local all = elements(t.root)
    assert.equal(#all, depth * 3)
    local last = all[#all]
    assert.equal(last.tag_name, 'p')
    assert.equal(last.order, depth * 3 - 1)
    assert.equal(last.depth, depth * 3 - 1)
    assert.equal(last.parent, depth * 3 - 2)
    assert.equal(last.match_branch, 0)
    assert.equal(all[#all - 2].chain_branch, 0)
    assert.equal(all[#all - 2].depth, depth * 3 - 3)
end

function testcase.dom_index_belongs_to_each_compiled_result()
    local html = '<p id="a"></p>'
    local first = assert(compile('first', html, 'x-', {}))
    do
        local second = assert(compile('second', html, 'x-', {}))
        local a = first:to_table()
        local b = second:to_table()
        assert.equal(a.name, 'first')
        assert.equal(b.name, 'second')
        assert.equal(a.index, b.index)
        assert.not_rawequal(a.index, b.index)
    end
    collectgarbage('collect')
    collectgarbage('collect')
    assert.equal(first:to_table().index.by_id, {
        a = {
            0,
        },
    })
end
