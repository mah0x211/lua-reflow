require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local errorlib = require('error')
local reflow_error = require('reflow.error')
local selector = require('reflow.selector')
local newstate = require('newstate')

-- Remove the source from a parse result so that it can be compared with
-- the AST of the JavaScript version.
local function ast_of(sel)
    local t = sel:to_table()
    t.source = nil
    return t
end

function testcase.compile_matches_the_javascript_version()
    -- Accepted selectors give the AST of the JavaScript version; rejected
    -- ones give its message, reason, position (in UTF-16 code units), and
    -- feature.
    local cases = {
        {
            "   ",
            'err',
            "selector syntax error: empty selector",
            "syntax",
            0,
            nil,
        },
        {
            "  a  ,   b  >   c   ",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    tag = "c",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "",
            'err',
            "selector syntax error: empty selector",
            "syntax",
            0,
            nil,
        },
        {
            "[a=\"b\" i]",
            'err',
            "selector unsupported: attribute case-sensitivity flag is not supported",
            "unsupported",
            7,
            "attr-case-flag",
        },
        {
            "[a=\"b\" S]",
            'err',
            "selector unsupported: attribute case-sensitivity flag is not supported",
            "unsupported",
            7,
            "attr-case-flag",
        },
        {
            "[a=\"he said \\\"hi\\\"\"]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "=",
                                            value = "he said \"hi\"",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a=\"line\010break\"]",
            'err',
            "selector syntax error: unterminated string literal",
            "syntax",
            8,
            nil,
        },
        {
            "[a=\"cr\013x\"]",
            'err',
            "selector syntax error: unterminated string literal",
            "syntax",
            6,
            nil,
        },
        {
            "[a=\"unterminated",
            'err',
            "selector syntax error: unterminated string literal",
            "syntax",
            16,
            nil,
        },
        {
            "[a='x\\",
            'err',
            "selector syntax error: unterminated string escape",
            "syntax",
            6,
            nil,
        },
        {
            "[data-role]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "data-role",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[ns|attr]",
            'err',
            "selector unsupported: attribute namespaces are not supported",
            "unsupported",
            1,
            "attr-namespace",
        },
        {
            "*",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "#a > .b[c=\"d\"]:first-child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    id = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    classes = {
                                        "b",
                                    },
                                    attrs = {
                                        {
                                            name = "c",
                                            op = "=",
                                            value = "d",
                                        },
                                    },
                                    pseudos = {
                                        {
                                            name = "first-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "#a#b",
            'err',
            "selector syntax error: multiple \"#id\" in the same compound selector",
            "syntax",
            3,
            nil,
        },
        {
            "#status li:nth-child(2)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    id = "status",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-child",
                                            n = 2,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a )",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            "a || b",
            'err',
            "selector unsupported: column combinator \"||\" is not supported",
            "unsupported",
            2,
            "combinator:||",
        },
        {
            "a, b, c#id",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "c",
                                    id = "id",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "div p",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "div",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "p",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "div",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "div",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "DIV",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "div",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "div#foo.bar.baz",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "div",
                                    id = "foo",
                                    classes = {
                                        "bar",
                                        "baz",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:First-Child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "first-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:first-child(1)",
            'err',
            "selector syntax error: \":first-child\" does not take an argument",
            "syntax",
            14,
            nil,
        },
        {
            "li:nth-child",
            'err',
            "selector syntax error: \":nth-child\" requires an integer argument, e.g. \":nth-child(3)\"",
            "syntax",
            12,
            nil,
        },
        {
            "li:nth-child(0)",
            'err',
            "selector syntax error: \":nth-child(n)\" argument must be a positive integer (>= 1)",
            "syntax",
            13,
            nil,
        },
        {
            "p::before",
            'err',
            "selector unsupported: pseudo-elements (\"::\") are not supported",
            "unsupported",
            1,
            "pseudo-element",
        },
        {
            "section > article .body p",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "section",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    tag = "article",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    classes = {
                                        "body",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "p",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "ul > li",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "ul",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[data-x='hello world']",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "data-x",
                                            op = "=",
                                            value = "hello world",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:first-child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "first-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:last-child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "last-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:only-child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "only-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:first-of-type",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "first-of-type",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:last-of-type",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "last-of-type",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:only-of-type",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "only-of-type",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-child(3)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-child",
                                            n = 3,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-last-child(3)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-last-child",
                                            n = 3,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-of-type(3)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-of-type",
                                            n = 3,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-last-of-type(3)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-last-of-type",
                                            n = 3,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:NTH-CHILD( 12 )",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-child",
                                            n = 12,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:NTH-LAST-CHILD( 12 )",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-last-child",
                                            n = 12,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:NTH-OF-TYPE( 12 )",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-of-type",
                                            n = 12,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:NTH-LAST-OF-TYPE( 12 )",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-last-of-type",
                                            n = 12,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-child(2n+1)",
            'err',
            "selector unsupported: \":nth-child\" accepts only a positive integer literal; formulas (An+B, odd, even) are not supported",
            "unsupported",
            13,
            "pseudo-arg:nth-child",
        },
        {
            "li:nth-child(odd)",
            'err',
            "selector unsupported: \":nth-child\" accepts only a positive integer literal; formulas (An+B, odd, even) are not supported",
            "unsupported",
            13,
            "pseudo-arg:nth-child",
        },
        {
            "li:nth-child(-1)",
            'err',
            "selector unsupported: \":nth-child\" accepts only a positive integer literal; formulas (An+B, odd, even) are not supported",
            "unsupported",
            13,
            "pseudo-arg:nth-child",
        },
        {
            "li:nth-child(3 4)",
            'err',
            "selector unsupported: \":nth-child\" accepts only a positive integer literal; formulas (An+B, odd, even) are not supported",
            "unsupported",
            13,
            "pseudo-arg:nth-child",
        },
        {
            "li:nth-child(007)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "li",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-child",
                                            n = 7,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "li:nth-child(9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999)",
            'err',
            "selector syntax error: \":nth-child(n)\" argument must be a positive integer (>= 1)",
            "syntax",
            13,
            nil,
        },
        {
            "li:nth-child(3",
            'err',
            "selector unsupported: \":nth-child\" accepts only a positive integer literal; formulas (An+B, odd, even) are not supported",
            "unsupported",
            13,
            "pseudo-arg:nth-child",
        },
        {
            "li:hover",
            'err',
            "selector unsupported: pseudo-class \":hover\" is not supported (supported: :first-child, :last-child, :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(n), :nth-last-child(n), :nth-of-type(n), :nth-last-of-type(n))",
            "unsupported",
            2,
            "pseudo:hover",
        },
        {
            "li:not(.a)",
            'err',
            "selector unsupported: pseudo-class \":not\" is not supported (supported: :first-child, :last-child, :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(n), :nth-last-child(n), :nth-of-type(n), :nth-last-of-type(n))",
            "unsupported",
            2,
            "pseudo:not",
        },
        {
            "li:Is(.a)",
            'err',
            "selector unsupported: pseudo-class \":Is\" is not supported (supported: :first-child, :last-child, :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(n), :nth-last-child(n), :nth-of-type(n), :nth-last-of-type(n))",
            "unsupported",
            2,
            "pseudo:is",
        },
        {
            ":",
            'err',
            "selector syntax error: expected pseudo-class name after \":\"",
            "syntax",
            1,
            nil,
        },
        {
            "a:1",
            'err',
            "selector syntax error: expected pseudo-class name after \":\"",
            "syntax",
            2,
            nil,
        },
        {
            "a + b",
            'err',
            "selector unsupported: combinator \"+\" is not supported (only descendant \" \" and child \">\" are supported)",
            "unsupported",
            2,
            "combinator:+",
        },
        {
            "a ~ b",
            'err',
            "selector unsupported: combinator \"~\" is not supported (only descendant \" \" and child \">\" are supported)",
            "unsupported",
            2,
            "combinator:~",
        },
        {
            "a~b",
            'err',
            "selector unsupported: combinator \"~\" is not supported (only descendant \" \" and child \">\" are supported)",
            "unsupported",
            1,
            "combinator:~",
        },
        {
            "a+b",
            'err',
            "selector unsupported: combinator \"+\" is not supported (only descendant \" \" and child \">\" are supported)",
            "unsupported",
            1,
            "combinator:+",
        },
        {
            "a>b",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a > > b",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            4,
            nil,
        },
        {
            "a >",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            3,
            nil,
        },
        {
            "> a",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            0,
            nil,
        },
        {
            "a,",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            ",a",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            0,
            nil,
        },
        {
            "a,,b",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            "a , b",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a\009b\010c\012d\013e",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "c",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "d",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "e",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a\194\160b",
            'err',
            "selector syntax error: unexpected \"\194\160\"",
            "syntax",
            1,
            nil,
        },
        {
            "\194\160",
            'err',
            "selector syntax error: empty selector",
            "syntax",
            0,
            nil,
        },
        {
            "\227\128\128a",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            0,
            nil,
        },
        {
            "[a=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[ a = b ]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a~=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "~=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a|=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "|=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a^=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "^=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a$=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "$=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a*=b]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "*=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a!=b]",
            'err',
            "selector syntax error: expected attribute operator or \"]\"",
            "syntax",
            2,
            nil,
        },
        {
            "[a~b]",
            'err',
            "selector syntax error: expected \"=\" after \"~\" in attribute selector",
            "syntax",
            3,
            nil,
        },
        {
            "[a]b",
            'err',
            "selector syntax error: unexpected \"b\"",
            "syntax",
            3,
            nil,
        },
        {
            "[a",
            'err',
            "selector syntax error: expected attribute operator or \"]\"",
            "syntax",
            2,
            nil,
        },
        {
            "[",
            'err',
            "selector syntax error: expected attribute name",
            "syntax",
            1,
            nil,
        },
        {
            "[1]",
            'err',
            "selector syntax error: expected attribute name",
            "syntax",
            1,
            nil,
        },
        {
            "[a=1]",
            'err',
            "selector syntax error: expected attribute value after \"=\"",
            "syntax",
            3,
            nil,
        },
        {
            "[a=]",
            'err',
            "selector syntax error: expected attribute value after \"=\"",
            "syntax",
            3,
            nil,
        },
        {
            "[a=\"x\"y]",
            'err',
            "selector syntax error: expected \"]\" to close attribute selector",
            "syntax",
            6,
            nil,
        },
        {
            "[a|b]",
            'err',
            "selector unsupported: attribute namespaces are not supported",
            "unsupported",
            1,
            "attr-namespace",
        },
        {
            "[a||b]",
            'err',
            "selector syntax error: expected \"=\" after \"|\" in attribute selector",
            "syntax",
            3,
            nil,
        },
        {
            "[a|=b] ",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                            op = "|=",
                                            value = "b",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[Data-X=\"Y\"]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "Data-X",
                                            op = "=",
                                            value = "Y",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "[a=\"\195\169\"]x!",
            'err',
            "selector syntax error: unexpected \"x\"",
            "syntax",
            7,
            nil,
        },
        {
            "[a=\"\240\159\152\128\"] !",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            9,
            nil,
        },
        {
            "[a=\"\240\159\152\128\"]\240\159\152\128",
            'err',
            "selector syntax error: unexpected \"\239\191\189\"",
            "syntax",
            8,
            nil,
        },
        {
            "#",
            'err',
            "selector syntax error: expected identifier after \"#\"",
            "syntax",
            1,
            nil,
        },
        {
            ".",
            'err',
            "selector syntax error: expected identifier after \".\"",
            "syntax",
            1,
            nil,
        },
        {
            "#1",
            'err',
            "selector syntax error: expected identifier after \"#\"",
            "syntax",
            1,
            nil,
        },
        {
            ".1",
            'err',
            "selector syntax error: expected identifier after \".\"",
            "syntax",
            1,
            nil,
        },
        {
            "#-a",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    id = "-a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            ".-",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {
                                        "-",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "._x",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {
                                        "_x",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "#a.b#c",
            'err',
            "selector syntax error: multiple \"#id\" in the same compound selector",
            "syntax",
            5,
            nil,
        },
        {
            "a#b c.d",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    id = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "c",
                                    classes = {
                                        "d",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "A.B#C",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    id = "C",
                                    classes = {
                                        "B",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "*.x",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {
                                        "x",
                                    },
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "**",
            'err',
            "selector syntax error: unexpected \"*\"",
            "syntax",
            1,
            nil,
        },
        {
            "*#a",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    id = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "*[a]",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {},
                                    attrs = {
                                        {
                                            name = "a",
                                        },
                                    },
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a*",
            'err',
            "selector syntax error: unexpected \"*\"",
            "syntax",
            1,
            nil,
        },
        {
            "a b*",
            'err',
            "selector syntax error: unexpected \"*\"",
            "syntax",
            3,
            nil,
        },
        {
            "a b *",
            'ok',
            {
                has_positional = false,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "a",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "b",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    classes = {},
                                    attrs = {},
                                    pseudos = {},
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "\195\169",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            0,
            nil,
        },
        {
            "a\195\169",
            'err',
            "selector syntax error: unexpected \"\195\169\"",
            "syntax",
            1,
            nil,
        },
        {
            "a \195\169",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            "a,\195\169",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            "\240\159\152\128",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            0,
            nil,
        },
        {
            "a \240\159\152\128",
            'err',
            "selector syntax error: expected selector",
            "syntax",
            2,
            nil,
        },
        {
            "a|b",
            'err',
            "selector syntax error: unexpected \"|\"",
            "syntax",
            1,
            nil,
        },
        {
            "a::after",
            'err',
            "selector unsupported: pseudo-elements (\"::\") are not supported",
            "unsupported",
            1,
            "pseudo-element",
        },
        {
            "a:hover::after",
            'err',
            "selector unsupported: pseudo-class \":hover\" is not supported (supported: :first-child, :last-child, :only-child, :first-of-type, :last-of-type, :only-of-type, :nth-child(n), :nth-last-child(n), :nth-of-type(n), :nth-last-of-type(n))",
            "unsupported",
            1,
            "pseudo:hover",
        },
        {
            "#a:first-child.b",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    id = "a",
                                    classes = {
                                        "b",
                                    },
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "first-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            ".a:nth-of-type(2):last-child",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    classes = {
                                        "a",
                                    },
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-of-type",
                                            n = 2,
                                        },
                                        {
                                            name = "last-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
        {
            "a\\b",
            'err',
            "selector syntax error: unexpected \"\\\"",
            "syntax",
            1,
            nil,
        },
        {
            "a\"b\"",
            'err',
            "selector syntax error: unexpected \"\"\"",
            "syntax",
            1,
            nil,
        },
        {
            "x:first-child > y:last-of-type z:only-child, w:nth-last-of-type(9)",
            'ok',
            {
                has_positional = true,
                selectors = {
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "x",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "first-child",
                                        },
                                    },
                                },
                            },
                            {
                                combinator = ">",
                                compound = {
                                    tag = "y",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "last-of-type",
                                        },
                                    },
                                },
                            },
                            {
                                combinator = " ",
                                compound = {
                                    tag = "z",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "only-child",
                                        },
                                    },
                                },
                            },
                        },
                    },
                    {
                        parts = {
                            {
                                compound = {
                                    tag = "w",
                                    classes = {},
                                    attrs = {},
                                    pseudos = {
                                        {
                                            name = "nth-last-of-type",
                                            n = 9,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
    }
    -- A character outside the BMP is quoted whole, where the JavaScript
    -- version quotes its high surrogate.
    local whole = {
        ['[a="\240\159\152\128"]\240\159\152\128'] = 'selector syntax error: unexpected "\240\159\152\128"',
    }
    for _, case in ipairs(cases) do
        local source, kind = case[1], case[2]
        local sel, err = selector.compile(source)
        if kind == 'ok' then
            assert(sel, err)
            assert.equal(ast_of(sel), case[3], source)
            assert.equal(selector.source(sel), source)
        else
            assert.is_nil(sel, source)
            assert.rawequal(errorlib.typeof(err), reflow_error.ESELECTOR)
            assert.equal(err.message.message, whole[source] or case[3], source)
            assert.equal(err.meta.reason, case[4], source)
            assert.equal(err.meta.position, case[5], source)
            assert.equal(err.meta.feature, case[6], source)
            assert.equal(err.meta.source, source)
        end
    end
end

function testcase.compile_locates_errors_in_bytes_and_characters()
    -- position counts UTF-16 code units from 0, as in JavaScript; byte is
    -- for string.sub and char counts code points, both from 1.
    local source = '[a="\195\169\240\159\152\128"]!'
    local sel, err = selector.compile(source)
    assert.is_nil(sel)
    assert.equal(err.meta.position, 9)
    assert.equal(err.meta.byte, 13)
    assert.equal(err.meta.char, 9)
    assert.equal(source:sub(err.meta.byte, err.meta.byte), '!')
end

function testcase.compile_reads_the_source_by_its_length()
    -- A NUL byte inside a quoted value is part of the value.
    local sel = assert(selector.compile('[a="x\0y"]'))
    assert.equal(sel:to_table().selectors[1].parts[1].compound.attrs[1].value,
                 'x\0y')
    assert.equal(selector.source(sel), '[a="x\0y"]')

    -- Outside a string it is an unexpected byte.
    local _, err = selector.compile('a\0b')
    assert.equal(err.message.message, 'selector syntax error: unexpected "\0"')
    assert.equal(err.meta.position, 1)
end

function testcase.compile_rejects_values_that_are_not_strings()
    for _, v in ipairs({
        1,
        true,
        {},
        print,
    }) do
        local sel, err = selector.compile(v)
        assert.is_nil(sel)
        assert.equal(err.message.message,
                     'selector syntax error: selector source must be a string')
        assert.equal(err.meta.reason, 'syntax')
        assert.equal(err.meta.position, 0)
        assert.rawequal(err.meta.source, v)
    end
    local sel, err = selector.compile()
    assert.is_nil(sel)
    assert.equal(err.meta.reason, 'syntax')
end

function testcase.is_compiled_tells_selectors_from_other_values()
    local sel = assert(selector.compile('a'))
    assert.is_true(selector.is_compiled(sel))
    for _, v in ipairs({
        'a',
        1,
        {},
        io.tmpfile(),
        newproxy and newproxy(true) or coroutine.create(print),
    }) do
        assert.is_false(selector.is_compiled(v))
    end
    assert.is_false(selector.is_compiled())
    assert.match(select(2, pcall(selector.source, 'a')), 'reflow.selector')
end

function testcase.compiled_selectors_are_independent_and_collectable()
    local a = assert(selector.compile('ul > li.item:nth-child(2)'))
    local b = assert(selector.compile('ul > li.item:nth-child(2)'))
    assert.not_rawequal(a, b)
    assert.equal(a:to_table(), b:to_table())
    -- The metatable cannot be replaced or read.
    assert.equal(getmetatable(a), 'reflow.selector')

    local refs = setmetatable({}, {
        __mode = 'v',
    })
    refs[1] = assert(selector.compile('a'))
    collectgarbage('collect')
    collectgarbage('collect')
    assert.is_nil(refs[1])
end

function testcase.compile_reports_out_of_memory()
    local ok, kinds = newstate.new():dostring([[
        local memlimit = require('memlimit')
        local errorlib = require('error')
        local errors = require('reflow.error')
        local selector = require('reflow.selector')
        local source = 'section > article.a.b[c~="d"]:nth-of-type(3), ' ..
                       'ul li[data-x="' .. string.rep('v', 256) .. '"]'
        local kinds = {}

        -- When so little memory is left that even the error cannot be
        -- created, the memory error is raised instead.
        for extra = 512, 256 * 1024, 16 do
            collectgarbage('collect')
            local used = memlimit.used()
            memlimit.maxsize(math.max(used, memlimit.minsize()) + extra)
            local called, sel, err = pcall(selector.compile, source)
            memlimit.maxsize(0)
            if not called then
                assert(tostring(sel):find('not enough memory', 1, true),
                       tostring(sel))
            elseif sel then
                kinds.success = selector.source(sel) == source
                break
            else
                assert(errorlib.typeof(err) == errors.ESELECTOR)
                assert(err.message.message ==
                           'out of memory while compiling the selector',
                       err.message.message)
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
