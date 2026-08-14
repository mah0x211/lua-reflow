require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local function project_root()
    local pipe = assert(io.popen('pwd'))
    local cwd = assert(pipe:read('*l'))
    pipe:close()
    return (cwd:gsub('/test/?$', ''))
end

local ROOT = project_root()
local manifest = assert(loadfile(ROOT .. '/test/parity_manifest.lua'))()

local function read_file(relative)
    local file = assert(io.open(ROOT .. '/' .. relative, 'rb'))
    local content = assert(file:read('*a'))
    file:close()
    return content
end

local function assert_nonempty_string(value, label)
    if type(value) ~= 'string' or value == '' then
        error(label .. ' must be a non-empty string')
    end
end

local function check_test_mapping(mapping, label)
    assert_nonempty_string(mapping.file, label .. '.file')
    if type(mapping.cases) ~= 'table' or #mapping.cases == 0 then
        error(label .. '.cases must be a non-empty list')
    end
    local source = read_file(mapping.file)
    local seen = {}
    for index, name in ipairs(mapping.cases) do
        assert_nonempty_string(name, label .. '.cases[' .. index .. ']')
        if seen[name] then error(label .. '.cases must be unique') end
        seen[name] = true
        local declaration = 'function testcase.' .. name .. '()'
        if not source:find(declaration, 1, true) then
            error(('%s does not declare %s'):format(mapping.file, name))
        end
    end
end

function testcase.reference_identity_and_categories_are_complete()
    assert.equal(manifest.source.url,
                 'https://github.com/mah0x211/reflow.js.git')
    assert.equal(manifest.source.commit,
                 'b1f46947ac8516cf4f15070c6e01a5a3e1bb453c')
    assert.equal(#manifest.categories, 13)

    local seen = {}
    for index, category in ipairs(manifest.categories) do
        assert_nonempty_string(category.id,
                               ('categories[%d].id'):format(index))
        if seen[category.id] then
            error('categories must be unique by id')
        end
        seen[category.id] = true
        if type(category.reference) ~= 'table' or
           #category.reference == 0 then
            error(category.id .. ' must name reference behavior')
        end
        if category.lua == nil and category.fixture_manifest == nil then
            error(category.id .. ' has no Lua verification mapping')
        end
    end
end

function testcase.static_test_mappings_resolve_to_declared_cases()
    for _, category in ipairs(manifest.categories) do
        for index, mapping in ipairs(category.lua or {}) do
            check_test_mapping(mapping,
                               category.id .. '.lua[' .. index .. ']')
        end
    end
    for _, rule in ipairs(manifest.compatibility) do
        if rule.lua ~= nil then
            check_test_mapping(rule.lua, rule.id .. '.lua')
        end
    end
end

function testcase.dynamic_fixture_mapping_is_exact()
    local category = nil
    for _, item in ipairs(manifest.categories) do
        if item.id == 'golden-reference-fixtures' then category = item end
    end
    if category == nil then error('golden-reference-fixtures is missing') end
    local fixtures = assert(loadfile(ROOT .. '/' .. category.fixture_manifest))()
    assert.equal(category.fixture_runner, 'test/fixture_test.lua')
    assert.equal(category.expected.valid, #fixtures.valid)
    assert.equal(category.expected.invalid, #fixtures.invalid)
    assert.equal(category.expected.valid, 23)
    assert.equal(category.expected.invalid, 24)
    local override = fixtures.expectation_overrides
        ['invalid/x-each-non-array']
    assert.equal(override.compatibility, 'x-each-object-extension')
    assert.equal(override.source, 'expected array')
    assert.equal(override.value, 'array or object')
end

function testcase.compatibility_rules_and_exclusions_are_explicit()
    assert.equal(#manifest.compatibility, 5)
    assert.equal(#manifest.exclusions, 3)
    for _, list in ipairs({ manifest.compatibility, manifest.exclusions }) do
        local seen = {}
        for index, item in ipairs(list) do
            assert_nonempty_string(item.id, ('entry[%d].id'):format(index))
            assert_nonempty_string(item.rule or item.reason,
                                   item.id .. '.rule-or-reason')
            if seen[item.id] then error('duplicate entry id: ' .. item.id) end
            seen[item.id] = true
        end
    end
end
