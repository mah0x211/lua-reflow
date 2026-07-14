require('luacov')
local testcase = require('testcase')
local assert = require('assert')

local Reflow = require('reflow')

-- ===================================================================
-- js-reflow fixture runner
--
-- Reads valid/invalid fixtures from ../js-reflow/test/fixtures/, drives
-- them through the Lua port, and asserts byte-identical output for
-- valid cases or matching error shape for invalid ones.
--
-- Some fixtures ship setup.js / helpers.js. Since we cannot execute
-- JavaScript, this runner looks for a Lua sibling under
-- test/fixture_overrides/<name>/ and skips the fixture (with a status
-- message) when the JS variant is present without a Lua counterpart.
-- ===================================================================

-- Absolute-ish paths are needed because testcase runs each case in the
-- test file's directory, not the project root.
local function project_root()
    local p = io.popen('pwd')
    local cwd = p:read('*l')
    p:close()
    -- If we were invoked from test/ specifically, strip that suffix.
    return (cwd:gsub('/test/?$', ''))
end

local ROOT = project_root()
local FIXTURE_ROOT = ROOT .. '/js-reflow/test/fixtures'
local OVERRIDE_ROOT = ROOT .. '/test/fixture_overrides'

local function file_exists(path)
    local f = io.open(path, 'r')
    if f == nil then return false end
    f:close()
    return true
end

local function read_file(path)
    local f = assert(io.open(path, 'rb'))
    local content = f:read('*a')
    f:close()
    return content
end

--- Extract a JSON string field from a small JSON document, handling
--- backslash escapes (\\, \", \n, etc.) so the value we hand to the
--- pattern matcher is the same one the fixture author wrote.
local function json_string_field(text, field)
    local i = text:find('"' .. field .. '"', 1, true)
    if not i then return nil end
    local _, colon_end = text:find(':%s*"', i)
    if not colon_end then return nil end
    local start = colon_end + 1
    local pos = start
    while pos <= #text do
        local c = text:sub(pos, pos)
        if c == '\\' then
            pos = pos + 2
        elseif c == '"' then
            local raw = text:sub(start, pos - 1)
            local decoded = raw:gsub('\\(.)', function(esc)
                if esc == 'n' then return '\n' end
                if esc == 't' then return '\t' end
                if esc == 'r' then return '\r' end
                return esc
            end)
            return decoded
        else
            pos = pos + 1
        end
    end
    return nil
end

--- Try to match a JS-style regex pattern against `text` using Lua
--- patterns. Supports the small subset present in fixture specs:
---   - plain substrings
---   - `A|B` top-level alternation
---   - `.*` between literal fragments (each fragment must appear in order)
local function match_js_pattern(text, pat)
    if text:find(pat, 1, true) then return true end
    for alt in pat:gmatch('[^|]+') do
        -- Split each alternate at `.*` and require each fragment in order.
        local search_from = 1
        local ok = true
        local scanned_any = false
        for frag in (alt .. '.*'):gmatch('(.-)%.%*') do
            if frag ~= '' then
                scanned_any = true
                local at = text:find(frag, search_from, true)
                if not at then ok = false; break end
                search_from = at + #frag
            end
        end
        if scanned_any and ok then return true end
    end
    return false
end

local function list_dir(path)
    local names = {}
    local p = io.popen('ls -1 "' .. path .. '" 2>/dev/null')
    if p == nil then return names end
    for line in p:lines() do
        if line ~= '' and not line:match('^%.') then
            names[#names + 1] = line
        end
    end
    p:close()
    return names
end

local function has_js_override(name)
    return file_exists(FIXTURE_ROOT .. '/valid/' .. name .. '/setup.js') or
           file_exists(FIXTURE_ROOT .. '/valid/' .. name .. '/helpers.js')
end

local function has_lua_override(name)
    return file_exists(OVERRIDE_ROOT .. '/' .. name .. '/setup.lua') or
           file_exists(OVERRIDE_ROOT .. '/' .. name .. '/helpers.lua')
end

--- Load an optional override Lua chunk.
local function load_override(name, kind)
    local path = OVERRIDE_ROOT .. '/' .. name .. '/' .. kind .. '.lua'
    if not file_exists(path) then return nil end
    local chunk, err = loadfile(path)
    if chunk == nil then
        error(('failed to load %s: %s'):format(path, err))
    end
    return chunk()
end

--- Run a single valid fixture and return {ok=bool, output=string, expected=string, err=string?}.
local function run_valid(name)
    local dir = FIXTURE_ROOT .. '/valid/' .. name
    local html = read_file(dir .. '/template.html')
    local expected = read_file(dir .. '/expected.html')
    local data = file_exists(dir .. '/data.json')
        and read_file(dir .. '/data.json') or nil

    local helpers = load_override(name, 'helpers') or {}
    local reflow = Reflow.new({ helpers = helpers })

    local setup = load_override(name, 'setup')
    if setup ~= nil then setup(reflow) end

    reflow:compile('main', html)
    local ok, out = pcall(function() return reflow:render('main', data) end)
    if not ok then
        return { ok = false, err = out }
    end
    return { ok = true, output = out, expected = expected }
end

--- Run a single invalid fixture and return {ok=bool, err=string, spec=table}.
local function run_invalid(name)
    local dir = FIXTURE_ROOT .. '/invalid/' .. name
    local html = read_file(dir .. '/template.html')
    local spec_text = read_file(dir .. '/expected-error.json')
    -- Parse expected-error.json fields (small hand-written JSON reader
    -- that respects backslash escapes).
    local pattern = json_string_field(spec_text, 'messagePattern')
    local phase = json_string_field(spec_text, 'phase')
    local cls = json_string_field(spec_text, 'class')
    local reason = json_string_field(spec_text, 'reason')

    local data = file_exists(dir .. '/data.json')
        and read_file(dir .. '/data.json') or nil
    local helpers = load_override(name, 'helpers') or {}
    local reflow = Reflow.new({ helpers = helpers })
    local setup = load_override(name, 'setup')
    if setup ~= nil then setup(reflow) end

    local err
    local ok, e = pcall(function() reflow:compile('main', html) end)
    if ok then
        ok, e = pcall(function() return reflow:render('main', data) end)
    end
    if ok then
        return { ok = true, err = nil,
                 spec = { phase = phase, class = cls,
                          messagePattern = pattern, reason = reason } }
    end
    err = tostring(e)
    return { ok = false, err = err,
             spec = { phase = phase, class = cls,
                      messagePattern = pattern, reason = reason } }
end

-- ===== dispatch =====

local skipped_valid = {}

for _, name in ipairs(list_dir(FIXTURE_ROOT .. '/valid')) do
    if has_js_override(name) and not has_lua_override(name) then
        skipped_valid[#skipped_valid + 1] = name
    else
        testcase['valid_' .. name:gsub('-', '_')] = function()
            local r = run_valid(name)
            if not r.ok then
                error(('render failed: %s'):format(r.err))
            end
            assert.equal(r.output, r.expected,
                ('%s output diverged'):format(name))
        end
    end
end

-- Fixtures the Lua port intentionally diverges from js-reflow on.
-- x-each in the Lua port also accepts objects (yielding value,key), so
-- the "collection must be array" error the JS version emits here is
-- not applicable.
local KNOWN_DIVERGENCES = {
    ['x-each-non-array'] = 'lua-reflow extends x-each to accept objects',
}

for _, name in ipairs(list_dir(FIXTURE_ROOT .. '/invalid')) do
    if not KNOWN_DIVERGENCES[name] then
        testcase['invalid_' .. name:gsub('-', '_')] = function()
            local r = run_invalid(name)
            if r.ok then
                error(('%s: expected compile/render to fail'):format(name))
            end
            if r.spec.messagePattern then
                local pat = r.spec.messagePattern
                if not match_js_pattern(r.err, pat) then
                    error(('%s: expected error to match /%s/, got %q')
                        :format(name, pat, r.err))
                end
            end
        end
    end
end

if #skipped_valid > 0 then
    testcase.report_skipped = function()
        io.write('\n[fixture-runner] skipped valid (JS-only setup): ',
                 table.concat(skipped_valid, ', '), '\n')
    end
end
