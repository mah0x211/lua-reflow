-- Ports test/unit/escape.test.js of the JavaScript version through the
-- public API: text is escaped as x-text writes it, and attribute values as
-- x-bind writes them.
require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local reflow = require('reflow')

local function text(v)
    local out, err = reflow.render('<p x-text="$.v"></p>', {
        v = v,
    })
    assert.not_nil(out, err)
    return out:match('^<p>(.*)</p>$')
end

local function attr_value(v)
    local out, err = reflow.render('<p x-bind:title="$.v"></p>', {
        v = v,
    })
    assert.not_nil(out, err)
    return out:match('^<p title="(.*)"></p>$')
end

function testcase.text_escapes_lt_gt_amp_quote_and_apostrophe()
    assert.equal(text('a<b>c&d"e\'f'), 'a&lt;b&gt;c&amp;d&quot;e&#39;f')
end

function testcase.text_passes_plain_ascii_through()
    assert.equal(text('hello world 123'), 'hello world 123')
end

function testcase.text_writes_non_string_values()
    -- escapeText coerces its argument; x-text writes a number and a
    -- boolean as JavaScript strings, and null as nothing
    assert.equal(text(42), '42')
    assert.equal(text(true), 'true')
    assert.equal(reflow.render('<p x-text="null"></p>'), '<p></p>')
end

function testcase.attribute_escapes_lt_gt_amp_and_quote_but_not_apostrophe()
    assert.equal(attr_value('<>&"\''), '&lt;&gt;&amp;&quot;\'')
end

function testcase.text_handles_unicode()
    assert.equal(text('日本語 <em>'), '日本語 &lt;em&gt;')
end
