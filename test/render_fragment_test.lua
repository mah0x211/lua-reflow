require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local compile = require('reflow.compile')
local interpret = require('reflow.interpret')
local selector = require('reflow.selector')
local newstate = require('newstate')

local function helper_names(helpers)
    local names = {}
    for name in pairs(helpers or {}) do
        names[name] = true
    end
    return names
end

-- Compile every template and render the fragment of name matched by source.
local function fragment(sources, name, source, data, max_include_depth, helpers)
    local templates = {}
    for k, html in pairs(sources) do
        templates[k] = assert(compile(k, html, 'x-', helper_names(helpers)))
    end
    local sel = assert(selector.compile(source))
    return interpret.render(name, templates[name], data, helpers, templates,
                            max_include_depth, sel)
end

local function assert_fragment(expected, ...)
    local out, err = fragment(...)
    assert.equal(out, expected, err)
end

local function assert_reason(reason, ...)
    local out, err = fragment(...)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
    assert.equal(err.meta.reason, reason)
    return err
end

local function assert_error_type(errt, ...)
    local out, err = fragment(...)
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), errt)
    return err
end

-- The following cases port test/interpret.selector.test.js of the
-- JavaScript version.

function testcase.simple_id_extraction_from_a_static_template()
    assert_fragment('<header id="hdr"><h1>Hello</h1></header>', {
        page = '<div><header id="hdr"><h1>Hello</h1></header><main>body</main></div>',
    }, 'page', '#hdr')
end

function testcase.renders_text_and_bind_inside_the_matched_subtree()
    assert_fragment(
        '<article id="post" data-slug="x"><h1>T</h1><p>B</p></article>', {
            page = '<section><article id="post" x-bind:data-slug="$.slug">' ..
                '<h1 x-text="$.title"></h1><p x-text="$.body"></p></article></section>',
        }, 'page', '#post', {
            slug = 'x',
            title = 'T',
            body = 'B',
        })
end

function testcase.candidate_inside_data_ancestor_sees_the_scope()
    assert_fragment('<span id="tag">A</span>', {
        page = [[
            <div x-data="page: { title: 'A' }">
                <span id="tag" x-text="@page.title"></span>
            </div>
        ]],
    }, 'page', '#tag')
end

function testcase.candidate_inside_if_branch()
    local sources = {
        page = '<div><p x-if="$.show" id="p">visible</p></div>',
    }
    assert_fragment('<p id="p">visible</p>', sources, 'page', '#p', {
        show = true,
    })
    assert_reason('no_match', sources, 'page', '#p', {
        show = false,
    })
end

function testcase.chain_evaluation_picks_the_truthy_branch()
    local sources = {
        page = [[
            <div>
                <p x-if="$.mode == 'a'" class="msg">A</p>
                <p x-elseif="$.mode == 'b'" class="msg">B</p>
                <p x-else class="msg">other</p>
            </div>
        ]],
    }
    for mode, expected in pairs({
        a = '<p class="msg">A</p>',
        b = '<p class="msg">B</p>',
        c = '<p class="msg">other</p>',
    }) do
        assert_fragment(expected, sources, 'page', '.msg', {
            mode = mode,
        })
    end
end

function testcase.match_selects_the_matching_case()
    local sources = {
        page = [[
            <div id="wrap" x-match="$.status">
                <span x-case="'ok'" class="msg">OK</span>
                <span x-case="'fail'" class="msg">FAIL</span>
                <span x-nocase class="msg">unknown</span>
            </div>
        ]],
    }
    for status, expected in pairs({
        ok = '<span class="msg">OK</span>',
        fail = '<span class="msg">FAIL</span>',
        other = '<span class="msg">unknown</span>',
    }) do
        assert_fragment(expected, sources, 'page', '.msg', {
            status = status,
        })
    end
end

function testcase.each_iterations_are_matches()
    local sources = {
        page = '<ul><li x-each="u in $.users" class="user" x-text=".u"></li></ul>',
    }
    assert_reason('multiple_matches', sources, 'page', '.user', {
        users = {
            'a',
            'b',
        },
    })
    assert_fragment('<li class="user">only</li>', sources, 'page', '.user', {
        users = {
            'only',
        },
    })
    assert_reason('no_match', sources, 'page', '.user', {
        users = {},
    })
end

function testcase.nth_child_selects_the_runtime_iteration()
    assert_fragment('<li class="u">b</li>', {
        page = '<ul id="users"><li x-each="u in $.users" class="u" x-text=".u"></li></ul>',
    }, 'page', '#users > li:nth-child(2)', {
        users = {
            'a',
            'b',
            'c',
        },
    })
end

function testcase.nth_child_respects_mixed_static_and_iterated_siblings()
    local sources = {
        page = [[
            <ul>
                <li>head</li>
                <li x-each="u in $.users" class="u" x-text=".u"></li>
                <li>tail</li>
            </ul>
        ]],
    }
    assert_fragment('<li class="u">A</li>', sources, 'page', 'li:nth-child(2)',
                    {
        users = {
            'A',
            'B',
        },
    })
    assert_fragment('<li>tail</li>', sources, 'page', 'li:nth-child(2)', {
        users = {},
    })
end

function testcase.first_last_and_only_child()
    local sources = {
        page = '<ul><li x-each="u in $.users" x-text=".u"></li></ul>',
    }
    local abc = {
        users = {
            'a',
            'b',
            'c',
        },
    }
    assert_fragment('<li>a</li>', sources, 'page', 'li:first-child', abc)
    assert_fragment('<li>c</li>', sources, 'page', 'li:last-child', abc)
    assert_fragment('<li>only</li>', sources, 'page', 'li:only-child', {
        users = {
            'only',
        },
    })
    assert_reason('no_match', sources, 'page', 'li:only-child', {
        users = {
            'a',
            'b',
        },
    })
end

function testcase.first_last_and_nth_of_type()
    local sources = {
        page = '<section><h2>title</h2><p>one</p><p>two</p><p>three</p><h2>tail</h2></section>',
    }
    assert_fragment('<p>one</p>', sources, 'page', 'p:first-of-type')
    assert_fragment('<p>three</p>', sources, 'page', 'p:last-of-type')
    assert_fragment('<p>two</p>', sources, 'page', 'p:nth-of-type(2)')
    assert_reason('no_match', sources, 'page', 'h2:only-of-type')
end

function testcase.positional_condition_counts_the_selected_case()
    assert_fragment('<li><span>OK</span></li>', {
        page = [[
            <ul id="status" x-match="$.status">
                <li x-case="'ok'"><span>OK</span></li>
                <li x-case="'fail'"><span>Fail</span></li>
            </ul>
        ]],
    }, 'page', '#status li:first-child', {
        status = 'ok',
    })
end

function testcase.include_is_searched_without_candidates()
    assert_fragment('<div id="target"><p>hello</p></div>', {
        layout = '<html><body><main x-include="$.body"></main></body></html>',
        page = '<section><div id="target"><p>hello</p></div></section>',
    }, 'layout', '#target', {
        body = 'page',
    })
end

function testcase.include_is_not_searched_with_candidates()
    assert_fragment('<span id="here"></span>', {
        layout = '<div><span id="here"></span><main x-include="\'inner\'"></main></div>',
        inner = '<span id="here">from inside</span>',
    }, 'layout', '#here')
end

function testcase.matches_across_includes_are_counted_together()
    local err = assert_reason('multiple_matches', {
        layout = '<div><main x-include="\'inner1\'"></main>' ..
            '<aside x-include="\'inner2\'"></aside></div>',
        inner1 = '<div id="dupe">1</div>',
        inner2 = '<div id="dupe">2</div>',
    }, 'layout', '#dupe')
    assert.equal(err.meta.matches, {
        {
            template_name = 'inner1',
        },
        {
            template_name = 'inner2',
        },
    })
end

function testcase.include_errors_propagate_from_the_search()
    local err = assert_error_type(reflow_error.EINCLUDE, {
        layout = '<div><main x-include="\'missing\'"></main></div>',
    }, 'layout', '#anything')
    assert.equal(err.meta.reason, 'not_found')
    err = assert_error_type(reflow_error.EINCLUDE, {
        a = '<div><main x-include="\'b\'"></main></div>',
        b = '<div><main x-include="\'a\'"></main></div>',
    }, 'a', '#nothing')
    assert.equal(err.meta.reason, 'cycle')
    err = assert_error_type(reflow_error.EINCLUDE, {
        layout = '<div><main x-include="$.bad"></main></div>',
    }, 'layout', '#anything', {
        bad = 42,
    })
    assert.equal(err.meta.reason, 'invalid')
    err = assert_error_type(reflow_error.EINCLUDE, {
        a = '<div><main x-include="\'b\'"></main></div>',
        b = '<div><main x-include="\'c\'"></main></div>',
        c = '<div><main x-include="\'d\'"></main></div>',
        d = '<div id="ok">hi</div>',
    }, 'a', '#ok', nil, 2)
    assert.equal(err.meta.reason, 'depth_exceeded')
end

function testcase.multiple_and_missing_matches()
    assert_reason('multiple_matches', {
        page = '<div><span id="x">1</span><span id="x">2</span></div>',
    }, 'page', '#x')
    assert_reason('no_match', {
        page = '<div><span></span></div>',
    }, 'page', '#absent')
end

function testcase.bind_and_helpers_apply_to_the_matched_element()
    assert_fragment('<a id="l" href="/x">click</a>', {
        page = '<div><a id="l" x-bind:href="$.u">click</a></div>',
    }, 'page', '#l', {
        u = '/x',
    })
    assert_fragment('<span id="s">HI</span>', {
        t = '<div><span id="s" x-text="upper($.name)"></span></div>',
    }, 't', '#s', {
        name = 'hi',
    }, nil, {
        upper = function(s)
            return tostring(s):upper()
        end,
    })
end

function testcase.runtime_error_inside_the_matched_subtree()
    assert_error_type(reflow_error.ERUNTIME, {
        page = '<div><p id="p" x-text=".missing.field"></p></div>',
    }, 'page', '#p')
end

-- The following cases port the partial rendering cases of
-- test/selector-coverage.test.js of the JavaScript version.

function testcase.top_level_positional_candidate()
    local sources = {
        t = '<div id="a"></div><span id="b"></span><p id="c"></p>',
    }
    assert_fragment('<div id="a"></div>', sources, 't', 'div:first-child')
    assert_fragment('<p id="c"></p>', sources, 't', 'p:last-child')
end

function testcase.positional_candidate_under_a_parent_with_frames()
    assert_fragment('<span>T</span>', {
        t = [[
            <div id="wrap" x-data="page: { title: 'T' }">
                <span x-text="@page.title">a</span>
                <span>b</span>
            </div>
        ]],
    }, 't', '#wrap > span:first-child')
    assert_fragment('<span class="a">1</span>', {
        t = '<div x-for="i = 1, 1"><span class="a" x-text=".i"></span>' ..
            '<span class="b">tail</span></div>',
    }, 't', 'span.a:first-child')
    assert_fragment('<li class="body">B</li>', {
        t = '<ul x-each="row in $.rows"><li class="head" x-text=".row.h"></li>' ..
            '<li class="body" x-text=".row.b"></li></ul>',
    }, 't', 'li.body:last-child', {
        rows = {
            {
                h = 'H',
                b = 'B',
            },
        },
    })
end

function testcase.positional_count_skips_text_and_comments()
    assert_fragment('<li class="target">x</li>', {
        t = '<ul> <!--c--> <li class="target">x</li> <!--tail--> </ul>',
    }, 't', 'li:first-child')
end

function testcase.positional_count_of_unexecuted_siblings()
    local chain = {
        t = '<ul><li x-if="$.show">optional</li><li class="tgt">target</li></ul>',
    }
    assert_fragment('<li class="tgt">target</li>', chain, 't',
                    'li.tgt:nth-child(2)', {
        show = true,
    })
    assert_fragment('<li class="tgt">target</li>', chain, 't',
                    'li.tgt:first-child', {
        show = false,
    })
    local each = {
        t = '<ul><li x-each="u in $.users">u</li><li class="tail">tail</li></ul>',
    }
    assert_fragment('<li class="tail">tail</li>', each, 't',
                    'li.tail:nth-child(4)', {
        users = {
            'a',
            'b',
            'c',
        },
    })
    assert_error_type(reflow_error.ERUNTIME, each, 't', 'li.tail:nth-child(2)',
                      {
        users = 'not-an-array',
    })
    assert_fragment('<li class="tail">tail</li>', {
        t = '<ul><li x-for="i = 1, 3">i</li><li class="tail">tail</li></ul>',
    }, 't', 'li.tail:nth-child(4)')
end

function testcase.ancestor_loops_are_executed()
    assert_fragment('<span id="x">value</span>', {
        t = '<div x-for="i = 1, 1"><span id="x">value</span></div>',
    }, 't', '#x')
    local each = {
        t = '<ul x-each="u in $.us"><li id="target" x-text=".u"></li></ul>',
    }
    assert_error_type(reflow_error.ERUNTIME, each, 't', '#target', {
        us = 'not-an-array',
    })
    assert_fragment('<li id="target">only</li>', each, 't', '#target', {
        us = {
            'only',
        },
    })
    assert_reason('no_match', each, 't', '#target', {
        us = {},
    })
    assert_error_type(reflow_error.ERUNTIME, {
        t = '<ul x-each="u in $.us"><li class="t">x</li></ul>',
    }, 't', 'li.t:first-child', {
        us = 'not-an-array',
    })
end

function testcase.positional_candidate_under_match()
    assert_fragment('<section>B</section>', {
        t = '<div x-match="$.k"><section x-case="\'a\'">A</section>' ..
            '<section x-case="\'b\'">B</section></div>',
    }, 't', 'section:first-of-type', {
        k = 'b',
    })
    assert_reason('no_match', {
        t = '<div x-match="$.k"><section x-case="\'a\'">A</section></div>',
    }, 't', 'section:first-of-type', {
        k = 'other',
    })
end

function testcase.candidate_loop_with_a_break_inside()
    assert_fragment('<li>only</li>', {
        t = '<ul id="list"><li x-each="u in $.users" x-text=".u">' ..
            '<span x-break-if="true"></span></li></ul>',
    }, 't', '#list > li', {
        users = {
            'only',
        },
    })
end

function testcase.candidate_with_its_own_loop()
    assert_fragment('<span id="tag">5</span>', {
        t = '<div><span id="tag" x-for="i = 5, 5" x-text=".i"></span></div>',
    }, 't', '#tag')
    assert_reason('multiple_matches', {
        t = '<div><span class="t" x-for="i = 1, 3"></span></div>',
    }, 't', '.t')
    assert_reason('no_match', {
        t = '<div><span class="t" x-each="u in $.us"></span></div>',
    }, 't', '.t', {
        us = {},
    })
    assert_fragment('<li class="t">x</li>', {
        t = '<ul><li class="t" x-each="u in $.us" x-text=".u"></li><li>tail</li></ul>',
    }, 't', '.t:first-child', {
        us = {
            'x',
        },
    })
    assert_fragment('<li class="t">7</li>', {
        t = '<ul><li class="t" x-for="i = 7, 7" x-text=".i"></li><li>tail</li></ul>',
    }, 't', '.t:first-child')
end

function testcase.candidate_of_an_unselected_branch()
    local match = {
        t = '<div x-match="$.k"><section id="a" x-case="\'a\'">A</section>' ..
            '<section id="b" x-case="\'b\'">B</section></div>',
    }
    assert_reason('no_match', match, 't', '#b', {
        k = 'a',
    })
    assert_reason('no_match', match, 't', '#a', {
        k = 'other',
    })
    local chain = {
        t = '<div><section x-if="$.show"><span id="target">v</span></section></div>',
    }
    assert_fragment('<span id="target">v</span>', chain, 't', '#target', {
        show = true,
    })
    assert_reason('no_match', chain, 't', '#target', {
        show = false,
    })
    local chain_else = {
        t = '<div><section x-if="$.show"><span id="a">A</span></section>' ..
            '<section x-else><span id="b">B</span></section></div>',
    }
    assert_fragment('<span id="b">B</span>', chain_else, 't', '#b', {
        show = false,
    })
    assert_reason('no_match', chain_else, 't', '#b', {
        show = true,
    })
    local cases = {
        t = '<div x-match="$.k"><section x-case="\'a\'"><span id="ax">A</span></section>' ..
            '<section x-nocase><span id="fallback">F</span></section></div>',
    }
    assert_fragment('<span id="ax">A</span>', cases, 't', '#ax', {
        k = 'a',
    })
    assert_reason('no_match', cases, 't', '#ax', {
        k = 'b',
    })
    assert_fragment('<span id="fallback">F</span>', cases, 't', '#fallback', {
        k = 'x',
    })
end

function testcase.errors_in_candidate_loops_surface()
    assert_error_type(reflow_error.ERUNTIME, {
        t = '<ul id="tag" x-each="u in $.us"><li class="row" x-text=".u"></li></ul>',
    }, 't', '#tag', {
        us = 'not-array',
    })
    for _, source in ipairs({
        '#tag',
        '.tgt:first-child',
    }) do
        assert_error_type(reflow_error.ERUNTIME, {
            t = '<div><ul id="tag" class="tgt" x-for="i = 1, 3">' ..
                '<li class="row" x-text=".none.name"></li></ul></div>',
        }, 't', source)
        assert_error_type(reflow_error.ERUNTIME, {
            t = '<div><ul id="tag" class="tgt" x-each="u in $.us">' ..
                '<li class="row" x-text=".none.name"></li></ul></div>',
        }, 't', source, {
            us = {
                'x',
            },
        })
    end
end

function testcase.positional_candidate_loop_with_a_break_inside()
    -- The second emission of each ul is the second of its tag name, so only
    -- the first one matches :first-of-type.
    assert_fragment('<ul class="tgt"><li class="row">i</li></ul>', {
        t = '<div><ul class="tgt" x-for="i = 1, 5"><li class="row" ' ..
            'x-break-if=".i == 2">i</li></ul></div>',
    }, 't', '.tgt:first-of-type')
    assert_fragment('<ul class="tgt"><li class="row">ok</li></ul>', {
        t = '<div><ul class="tgt" x-each="u in $.us"><li class="row" ' ..
            'x-break-if=".u == \'stop\'" x-text=".u"></li></ul></div>',
    }, 't', '.tgt:first-of-type', {
        us = {
            'ok',
            'stop',
            'ignored',
        },
    })
end

function testcase.rendering_stops_at_the_second_match()
    assert_reason('multiple_matches', {
        t = '<ul id="tag" x-each="u in $.us" x-text=".u"></ul>',
    }, 't', '#tag', {
        us = {
            'a',
            'b',
            'c',
        },
    })
    assert_reason('multiple_matches', {
        t = '<div x-for="i = 1, 3"><span id="target" x-text=".i"></span></div>',
    }, 't', '#target')
    local calls = 0
    assert_reason('multiple_matches', {
        t = '<div x-each="u in $.us"><span id="target" x-text="count(.u)"></span></div>',
    }, 't', '#target', {
        us = {
            'a',
            'b',
            'c',
        },
    }, nil, {
        count = function(v)
            calls = calls + 1
            return v
        end,
    })
    assert.equal(calls, 2)
end

-- The following cases port test/x-with-selector.test.js of the JavaScript
-- version.

function testcase.with_bindings_reach_the_candidate()
    assert_fragment('<header id="head"><h1>Hello</h1></header>', {
        t = '<article x-with="post = { title: $.raw, body: $.text }">' ..
            '<header id="head"><h1 x-text="@post.title"></h1></header>' ..
            '<section><p x-text="@post.body"></p></section></article>',
    }, 't', '#head', {
        raw = 'Hello',
        text = 'World',
    })
    assert_fragment('<h2 id="ph">Fragment</h2>', {
        panel = '<section><h2 id="ph" x-text="@title"></h2><p x-text="@body"></p></section>',
        layout = '<div><article x-include="\'panel\'" x-with="title = $.t, body = $.b">' ..
            '</article></div>',
    }, 'layout', '#ph', {
        t = 'Fragment',
        b = 'inside include',
    })
    assert_fragment('<span id="target">BOB</span>', {
        t = '<ul><li x-each="row in $.rows" x-with="upname = upper(.row.name)">' ..
            '<span x-if=".row.name == \'bob\'" id="target" x-text="@upname"></span>' ..
            '</li></ul>',
    }, 't', '#target', {
        rows = {
            {
                name = 'alice',
            },
            {
                name = 'bob',
            },
        },
    }, nil, {
        upper = function(s)
            return tostring(s):upper()
        end,
    })
end

-- evalPositional of test/selector/resolve.test.js, through runtime
-- emissions.

function testcase.positional_conditions_follow_eval_positional()
    local sources = {
        t = '<div><p>1</p><i>2</i><p>3</p><i>4</i><p>5</p></div>',
    }
    for source, expected in pairs({
        ['p:first-child'] = '<p>1</p>',
        ['p:last-child'] = '<p>5</p>',
        ['i:nth-child(4)'] = '<i>4</i>',
        ['p:nth-last-child(3)'] = '<p>3</p>',
        ['i:first-of-type'] = '<i>2</i>',
        ['i:last-of-type'] = '<i>4</i>',
        ['p:nth-of-type(2)'] = '<p>3</p>',
        ['p:nth-last-of-type(1)'] = '<p>5</p>',
        ['i:nth-last-of-type(2)'] = '<i>2</i>',
    }) do
        assert_fragment(expected, sources, 't', source)
    end
    for _, source in ipairs({
        'p:only-child',
        'i:only-of-type',
        'p:nth-child(6)',
        'p:nth-last-child(9)',
        'i:nth-of-type(3)',
        'i:nth-last-of-type(3)',
    }) do
        assert_reason('no_match', sources, 't', source)
    end
    assert_fragment('<b>x</b>', {
        t = '<div><b>x</b></div>',
    }, 't', 'b:only-child')
    assert_fragment('<b>x</b>', {
        t = '<div><b>x</b><i></i></div>',
    }, 't', 'b:only-of-type')
end

-- Cases where the JavaScript version differs from its documents
-- (DESIGN-09): the matches of a partial render are the emissions of the
-- full render.

function testcase.breaking_iteration_of_a_candidate_is_a_match()
    -- selector-coverage.test.js expects one match from the JavaScript
    -- version, which drops the iteration that breaks.
    assert_reason('multiple_matches', {
        t = '<ul id="tag" x-for="i = 1, 3"><li class="row" x-break-if=".i == 2">i</li></ul>',
    }, 't', '#tag')
    assert_reason('multiple_matches', {
        t = '<ul id="tag" x-each="u in $.us"><li class="row" ' ..
            'x-break-if=".u == \'stop\'" x-text=".u"></li></ul>',
    }, 't', '#tag', {
        us = {
            'ok',
            'stop',
            'never-reached',
        },
    })
end

function testcase.break_of_a_candidate_ends_its_ancestor_loop()
    assert_fragment('<li id="t" class="c">x</li>', {
        t = '<ul x-for="i = 1, 3"><li id="t" class="c" x-break-if=".i == 1">x</li></ul>',
    }, 't', '#t')
end

function testcase.break_of_a_sibling_stops_later_emissions()
    local sources = {
        t = '<ul x-for="i = 1, 2"><li class="a" x-break-if=".i == 1">a</li>' ..
            '<li id="t" class="b">b</li></ul>',
    }
    assert_reason('no_match', sources, 't', '.b:last-child')
    assert_reason('no_match', sources, 't', '#t')
    -- a sibling whose loop breaks is counted by its emissions
    assert_fragment('<li class="tail">tail</li>', {
        t = '<ul><li x-for="i = 1, 5"><b x-break-if=".i == 2">i</b></li>' ..
            '<li class="tail">tail</li></ul>',
    }, 't', 'li.tail:nth-child(3)')
end

function testcase.children_replaced_by_content_are_not_matched()
    assert_reason('no_match', {
        t = '<div x-text="\'T\'"><p id="c">x</p></div>',
    }, 't', '#c')
    assert_reason('no_match', {
        t = '<div x-html="\'<b>H</b>\'"><p id="c">x</p></div>',
    }, 't', '#c')
    assert_reason('no_match', {
        t = '<div x-include="\'u\'"><p id="c">x</p></div>',
        u = '<i>u</i>',
    }, 't', '#c')
    assert_reason('no_match', {
        t = '<ul x-text="\'T\'"><li>a</li><li>b</li></ul>',
    }, 't', 'li:first-child')
end

-- Lua specific cases.

function testcase.selector_errors_follow_the_javascript_messages()
    local err = assert_reason('no_match', {
        page = '<div></div>',
    }, 'page', '#absent')
    assert.equal(err.op, 'reflow.interpret.render')
    assert.equal(err.message.message,
                 'selector "#absent" matched no elements in template "page"')
    assert.equal(err.meta, {
        reason = 'no_match',
        template_name = 'page',
        source = '#absent',
    })
    err = assert_reason('multiple_matches', {
        page = '<p class="x"></p><p class="x"></p><p class="x"></p>',
    }, 'page', '.x')
    assert.equal(err.message.message,
                 'selector ".x" matched 2+ elements; the single-fragment ' ..
                     'contract requires exactly one')
    assert.equal(err.meta, {
        reason = 'multiple_matches',
        template_name = 'page',
        source = '.x',
        matches = {
            {
                template_name = 'page',
            },
            {
                template_name = 'page',
            },
        },
    })
end

function testcase.ancestor_positional_condition_is_rejected()
    local err = assert_reason('unsupported', {
        t = '<ul><li><p>x</p></li></ul>',
    }, 't', 'li:first-child p')
    assert.equal(err.message.message,
                 'positional pseudo-class ":first-child" is only supported ' ..
                     'on the rightmost compound selector')
    assert.equal(err.meta, {
        reason = 'unsupported',
        feature = 'pseudo-ancestor:first-child',
        source = 'li:first-child p',
    })
    -- also in a searched template
    err = assert_reason('unsupported', {
        t = '<div x-include="\'u\'"></div>',
        u = '<ul><li><p>x</p></li></ul>',
    }, 't', 'li:nth-child(1) > p')
    assert.equal(err.meta.feature, 'pseudo-ancestor:nth-child')
end

function testcase.positional_count_of_object_iteration()
    assert_fragment('<li class="tail">tail</li>', {
        t = '<ul><li x-each="v in $.o">v</li><li class="tail">tail</li></ul>',
    }, 't', 'li.tail:nth-child(3)', {
        o = {
            a = 1,
            b = 2,
        },
    })
    assert_fragment('<li class="tail">tail</li>', {
        t = '<ul><li x-each="v in $.a">v</li><li class="tail">tail</li></ul>',
    }, 't', 'li.tail:nth-child(3)', {
        a = {
            1,
            2,
            nil,
            4,
        },
    })
end

function testcase.positional_count_of_a_huge_range_does_not_iterate()
    local sources = {
        t = '<ul><li x-for="i = 1, 9007199254740991">i</li>' ..
            '<li class="tail">tail</li></ul>',
    }
    assert_reason('no_match', sources, 't', 'li.tail:nth-child(2)')
    assert_fragment('<li class="tail">tail</li>', sources, 't',
                    'li.tail:last-child')
end

function testcase.positional_candidate_at_the_top_of_a_searched_template()
    assert_fragment('<p>2</p>', {
        t = '<main x-include="\'u\'"></main>',
        u = '<p>1</p><p>2</p>',
    }, 't', 'p:last-child')
end

function testcase.nested_candidates_are_separate_matches()
    assert_reason('multiple_matches', {
        t = '<div class="a"><div class="a">x</div></div>',
    }, 't', '.a')
    -- A template included by a candidate is not searched (DESIGN-07).
    assert_fragment('<p class="a"><b><i class="a">x</i></b></p>', {
        t = '<p class="a"><b x-include="\'u\'"></b></p>',
        u = '<i class="a">x</i>',
    }, 't', '.a')
end

function testcase.ancestors_are_not_written()
    -- The x-bind of an ancestor is not evaluated, as in the JavaScript
    -- version.
    assert_fragment('<p id="p">x</p>', {
        t = '<div x-bind:title="boom()"><p id="p">x</p></div>',
    }, 't', '#p', nil, nil, {
        boom = function()
            error('boom')
        end,
    })
end

function testcase.invisible_markers_are_not_emitted()
    assert_reason('no_match', {
        t = '<ul x-for="i = 1, 2"><span x-break-if=".i == 2"></span></ul>',
    }, 't', 'span')
end

function testcase.selector_argument_must_be_a_compiled_selector()
    local ir = assert(compile('t', '<p></p>', 'x-', {}))
    local out, err = interpret.render('t', ir, nil, nil, nil, nil, 'p')
    assert.is_nil(out)
    assert.rawequal(errorlib.typeof(err), reflow_error.ERUNTIME)
    assert.equal(err.message.message,
                 'selector must be a compiled selector or nil')
    -- nil renders the whole template
    assert.equal(interpret.render('t', ir, nil, nil, nil, nil, nil), '<p></p>')
end

-- With only :first-child and :nth-child(n) conditions, the children of a
-- parent after the last position that can match are not counted, as the
-- JavaScript computeForwardBound does: their loops and conditions are not
-- evaluated.
function testcase.forward_bound_leaves_out_later_siblings()
    local html = '<ul><li>a</li><li>b</li><li x-each="x in $.nope">c</li>' ..
                     '<li x-if="boom()">d</li></ul>'
    local helpers = {
        boom = function()
            error('boom')
        end,
    }
    local data = {
        nope = 1,
    }
    assert_fragment('<li>a</li>', {
        t = html,
    }, 't', 'li:first-child', data, nil, helpers)
    assert_fragment('<li>b</li>', {
        t = html,
    }, 't', 'li:nth-child(2)', data, nil, helpers)
    -- The first complex selector an element matches decides its conditions.
    assert_fragment('<li>a</li>', {
        t = html,
    }, 't', 'li:nth-child(1), li:only-child', data, nil, helpers)
    -- Other conditions need every child to be counted.
    assert_error_type(reflow_error.ERUNTIME, {
        t = html,
    }, 't', 'li:last-child', data, nil, helpers)
    assert_error_type(reflow_error.ERUNTIME, {
        t = html,
    }, 't', 'li:first-of-type', data, nil, helpers)
end

function testcase.forward_bound_ends_the_loop_of_a_candidate()
    assert_fragment('<li>3</li>', {
        t = '<ul><li x-for="i = 1, 1000000" x-text=".i"></li></ul>',
    }, 't', 'li:nth-child(3)')
    assert_fragment('<li>2</li>', {
        t = '<ul><li>a</li><li x-for="i = 1, 1000000" x-text=".i"></li></ul>',
    }, 't', 'li:nth-child(3)')
end

function testcase.forward_bound_keeps_what_can_still_match()
    -- a candidate without positional conditions
    assert_reason('multiple_matches', {
        t = '<ul><li>a</li><li>b</li><p class="x">c</p></ul>',
    }, 't', 'li:first-child, p.x')
    -- a candidate inside a later sibling
    assert_reason('multiple_matches', {
        t = '<ul><li>a</li><li>b</li><li><b>c</b></li></ul>',
    }, 't', 'li:first-child, li b')
    -- a break of a later sibling still ends the loop, so the second
    -- iteration is not emitted
    assert_fragment('<p>1</p>', {
        t = '<div x-for="i = 1, 3"><p x-text=".i"></p>' ..
            '<span x-break-if=".i == 1"></span></div>',
    }, 't', 'p:first-child')
end

function testcase.partial_render_reports_out_of_memory()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local compile = require('reflow.compile')
        local interpret = require('reflow.interpret')
        local selector = require('reflow.selector')
        local html = '<ul>' .. string.rep('<li class="a">x</li>', 32) ..
                     '<li class="t" x-each="u in $.us" x-text=".u"></li></ul>'
        local ir = assert(compile('t', html, 'x-', {}))
        local sel = assert(selector.compile('li.t:nth-child(34)'))
        local data = { us = { 'a', 'b', 'c' } }
        local kinds = {}

        for extra = 0, 512 * 1024, 64 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, out, err = pcall(interpret.render, 't', ir, data,
                                           nil, nil, nil, sel)
            memlimit.maxsize(0)
            if not called then
                assert(tostring(out):find('not enough memory', 1, true),
                       tostring(out))
            elseif out then
                kinds.success = out == '<li class="t">b</li>'
                break
            else
                assert(errorlib.typeof(err) == errors.ERUNTIME, tostring(err))
                kinds.oom = true
            end
        end
        return kinds
    ]])
    assert.is_true(ok, tostring(kinds))
    -- Whether a failure is returned as an error or raised as a memory error
    -- depends on the Lua version: the library does not collect garbage, and
    -- Lua 5.1 and LuaJIT have no emergency collection.
    assert.is_true(kinds.success)
end
