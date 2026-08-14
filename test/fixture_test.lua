require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local lfs = require('lfs')

local Reflow = require('reflow')

-- Reference fixture data is repository-owned and validated before cases are
-- registered. This prevents a missing or partial corpus from becoming a
-- silently smaller successful test run.

local function project_root()
    local pipe = assert(io.popen('pwd'))
    local cwd = assert(pipe:read('*l'))
    pipe:close()
    return (cwd:gsub('/test/?$', ''))
end

local ROOT = project_root()
local FIXTURE_ROOT = ROOT .. '/test/fixtures/reference'
local MANIFEST_PATH = ROOT .. '/test/fixtures/manifest.lua'
local OVERRIDE_ROOT = ROOT .. '/test/fixture_overrides'

local function read_file(path)
    local file = assert(io.open(path, 'rb'))
    local content = assert(file:read('*a'))
    file:close()
    return content
end

local function file_exists(path)
    local file = io.open(path, 'rb')
    if file == nil then return false end
    file:close()
    return true
end

local function shell_quote(value)
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function sha256(path)
    local commands = {
        'sha256sum ' .. shell_quote(path),
        'shasum -a 256 ' .. shell_quote(path),
    }
    for _, command in ipairs(commands) do
        local pipe = io.popen(command .. ' 2>/dev/null')
        if pipe ~= nil then
            local output = pipe:read('*a') or ''
            pipe:close()
            local digest = output:match('^([0-9a-fA-F]+)')
            if digest ~= nil and #digest == 64 then
                return digest:lower()
            end
        end
    end
    error('fixture manifest verification requires sha256sum or shasum')
end

local function collect_tree(root)
    local files = {}
    local directories = {}

    local function walk(relative)
        local path = relative == '' and root or root .. '/' .. relative
        local iter, state = lfs.dir(path)
        if iter == nil then
            error(('cannot read fixture directory %s: %s'):format(path,
                                                                  state))
        end
        for name in iter, state do
            if name ~= '.' and name ~= '..' then
                local child_relative = relative == '' and name
                    or relative .. '/' .. name
                local child_path = root .. '/' .. child_relative
                local mode = lfs.symlinkattributes(child_path, 'mode')
                if mode == 'directory' then
                    directories[#directories + 1] = child_relative
                    walk(child_relative)
                elseif mode == 'file' then
                    files[#files + 1] = child_relative
                else
                    error(('unsupported fixture entry %s (%s)')
                        :format(child_relative, tostring(mode)))
                end
            end
        end
    end

    walk('')
    table.sort(files)
    table.sort(directories)
    return files, directories
end

local function load_manifest()
    local chunk, load_err = loadfile(MANIFEST_PATH)
    if chunk == nil then
        error(('cannot load fixture manifest: %s'):format(load_err))
    end
    local manifest = chunk()
    if type(manifest) ~= 'table' then
        error('fixture manifest must return a table')
    end
    return manifest
end

local function assert_sorted_unique(values, label)
    local previous = nil
    for index, value in ipairs(values) do
        if type(value) ~= 'string' or value == '' then
            error(('%s[%d] must be a non-empty string'):format(label,
                                                               index))
        end
        if previous ~= nil and previous >= value then
            error(('%s must be sorted and unique'):format(label))
        end
        previous = value
    end
end

local function validate_manifest(manifest)
    if type(manifest.source) ~= 'table' or
       manifest.source.url ~= 'https://github.com/mah0x211/reflow.js.git' or
       manifest.source.commit ~=
           'b1f46947ac8516cf4f15070c6e01a5a3e1bb453c' then
        error('fixture manifest has an unexpected reference identity')
    end
    if type(manifest.counts) ~= 'table' or
       manifest.counts.valid ~= 23 or
       manifest.counts.invalid ~= 24 or
       manifest.counts.files ~= 113 then
        error('fixture manifest counts must be valid=23, invalid=24, files=113')
    end
    if type(manifest.valid) ~= 'table' or #manifest.valid ~= 23 or
       type(manifest.invalid) ~= 'table' or #manifest.invalid ~= 24 then
        error('fixture manifest must list exactly 23 valid and 24 invalid cases')
    end
    assert_sorted_unique(manifest.valid, 'manifest.valid')
    assert_sorted_unique(manifest.invalid, 'manifest.invalid')

    local expected_directories = { valid = true, invalid = true }
    for _, name in ipairs(manifest.valid) do
        expected_directories['valid/' .. name] = true
    end
    for _, name in ipairs(manifest.invalid) do
        expected_directories['invalid/' .. name] = true
    end

    if type(manifest.files) ~= 'table' or
       #manifest.files ~= manifest.counts.files then
        error('fixture manifest file count does not match counts.files')
    end
    local expected_files = {}
    local previous_path = nil
    for index, record in ipairs(manifest.files) do
        if type(record) ~= 'table' or type(record.path) ~= 'string' or
           not record.path:match('^[%w%._/-]+$') or
           record.path:find('..', 1, true) or
           record.path:sub(1, 1) == '/' then
            error(('manifest.files[%d] has an unsafe path'):format(index))
        end
        if previous_path ~= nil and previous_path >= record.path then
            error('manifest.files must be sorted and unique by path')
        end
        previous_path = record.path
        if type(record.bytes) ~= 'number' or record.bytes < 0 or
           type(record.sha256) ~= 'string' or
           not record.sha256:match('^[0-9a-f]+$') or
           #record.sha256 ~= 64 then
            error(('manifest.files[%d] has invalid metadata'):format(index))
        end
        local path = FIXTURE_ROOT .. '/' .. record.path
        local content = read_file(path)
        if #content ~= record.bytes then
            error(('%s byte count mismatch: expected %d, got %d')
                :format(record.path, record.bytes, #content))
        end
        local actual_hash = sha256(path)
        if actual_hash ~= record.sha256 then
            error(('%s SHA-256 mismatch: expected %s, got %s')
                :format(record.path, record.sha256, actual_hash))
        end
        expected_files[record.path] = true
    end

    local actual_files, actual_directories = collect_tree(FIXTURE_ROOT)
    if #actual_files ~= manifest.counts.files then
        error(('fixture corpus contains %d files; expected %d')
            :format(#actual_files, manifest.counts.files))
    end
    for _, path in ipairs(actual_files) do
        if not expected_files[path] then
            error(('unexpected fixture file: %s'):format(path))
        end
    end
    for _, path in ipairs(actual_directories) do
        if not expected_directories[path] then
            error(('unexpected fixture directory: %s'):format(path))
        end
        expected_directories[path] = nil
    end
    for path in pairs(expected_directories) do
        error(('missing fixture directory: %s'):format(path))
    end

    if type(manifest.hooks) ~= 'table' or #manifest.hooks ~= 4 then
        error('fixture manifest must map exactly four upstream hooks')
    end
    for index, hook in ipairs(manifest.hooks) do
        if type(hook) ~= 'table' or
           type(hook.source_path) ~= 'string' or
           not hook.source_path:match('%.js$') or
           type(hook.source_sha256) ~= 'string' or
           #hook.source_sha256 ~= 64 or
           type(hook.override_path) ~= 'string' or
           type(hook.override_bytes) ~= 'number' or
           type(hook.override_sha256) ~= 'string' or
           #hook.override_sha256 ~= 64 then
            error(('manifest.hooks[%d] has invalid metadata'):format(index))
        end
        local path = ROOT .. '/' .. hook.override_path
        local content = read_file(path)
        if #content ~= hook.override_bytes or
           sha256(path) ~= hook.override_sha256 then
            error(('%s does not match its manifest entry')
                :format(hook.override_path))
        end
    end

    local overrides = manifest.expectation_overrides
    local each_override = type(overrides) == 'table' and
        overrides['invalid/x-each-non-array'] or nil
    if type(each_override) ~= 'table' or
       each_override.field ~= 'messagePattern' or
       each_override.source ~= 'expected array' or
       each_override.value ~= 'array or object' or
       each_override.compatibility ~= 'x-each-object-extension' then
        error('fixture manifest must declare the x-each object extension')
    end
    local override_count = 0
    for _ in pairs(overrides) do override_count = override_count + 1 end
    if override_count ~= 1 then
        error('fixture manifest contains an unexpected expectation override')
    end
end

local manifest = load_manifest()
validate_manifest(manifest)

local function json_string_field(text, field)
    local index = text:find('"' .. field .. '"', 1, true)
    if index == nil then return nil end
    local _, colon_end = text:find(':%s*"', index)
    if colon_end == nil then return nil end
    local start = colon_end + 1
    local position = start
    while position <= #text do
        local char = text:sub(position, position)
        if char == '\\' then
            position = position + 2
        elseif char == '"' then
            local raw = text:sub(start, position - 1)
            return (raw:gsub('\\(.)', function(escaped)
                if escaped == 'n' then return '\n' end
                if escaped == 't' then return '\t' end
                if escaped == 'r' then return '\r' end
                return escaped
            end))
        else
            position = position + 1
        end
    end
    return nil
end

-- Translate the literal/alternation/`.*` subset used by the approved
-- reference error specifications without pretending to implement JS RegExp.
local function match_js_pattern(text, pattern)
    if text:find(pattern, 1, true) then return true end
    for alternate in pattern:gmatch('[^|]+') do
        local search_from = 1
        local matched = true
        local has_fragment = false
        for fragment in (alternate .. '.*'):gmatch('(.-)%.%*') do
            if fragment ~= '' then
                has_fragment = true
                local at = text:find(fragment, search_from, true)
                if at == nil then
                    matched = false
                    break
                end
                search_from = at + #fragment
            end
        end
        if has_fragment and matched then return true end
    end
    return false
end

local function load_override(name, kind)
    local path = OVERRIDE_ROOT .. '/' .. name .. '/' .. kind .. '.lua'
    if not file_exists(path) then return nil end
    local chunk, load_err = loadfile(path)
    if chunk == nil then
        error(('failed to load %s: %s'):format(path, load_err))
    end
    return chunk()
end

local function run_valid(name)
    local directory = FIXTURE_ROOT .. '/valid/' .. name
    local html = read_file(directory .. '/template.html')
    local expected = read_file(directory .. '/expected.html')
        :gsub('\r\n', '\n')
    local data = file_exists(directory .. '/data.json')
        and read_file(directory .. '/data.json') or nil
    local reflow = Reflow.new({
        helpers = load_override(name, 'helpers') or {},
    })
    local setup = load_override(name, 'setup')
    if setup ~= nil then setup(reflow) end

    local _, compile_err = reflow:compile('main', html)
    if compile_err ~= nil then
        return nil, compile_err
    end
    local output, render_err = reflow:render('main', data)
    if output == nil then
        return nil, render_err
    end
    return output, expected
end

local function run_invalid(name)
    local directory = FIXTURE_ROOT .. '/invalid/' .. name
    local html = read_file(directory .. '/template.html')
    local spec_text = read_file(directory .. '/expected-error.json')
    local spec = {
        phase = json_string_field(spec_text, 'phase'),
        class = json_string_field(spec_text, 'class'),
        reason = json_string_field(spec_text, 'reason'),
        message_pattern = json_string_field(spec_text, 'messagePattern'),
    }
    local override = manifest.expectation_overrides['invalid/' .. name]
    if override ~= nil then
        if spec.message_pattern ~= override.source then
            error(('%s: expectation override source has drifted'):format(name))
        end
        spec.message_pattern = override.value
    end
    local data = file_exists(directory .. '/data.json')
        and read_file(directory .. '/data.json') or nil
    local reflow = Reflow.new({
        helpers = load_override(name, 'helpers') or {},
    })
    local setup = load_override(name, 'setup')
    if setup ~= nil then setup(reflow) end

    local _, compile_err = reflow:compile('main', html)
    if compile_err ~= nil then
        return compile_err, 'compile', spec
    end
    local _, render_err = reflow:render('main', data)
    return render_err, render_err ~= nil and 'render' or nil, spec
end

local function trim(value)
    return (value:gsub('^%s+', ''):gsub('%s+$', ''))
end

for _, name in ipairs(manifest.valid) do
    testcase['valid_' .. name:gsub('-', '_')] = function()
        local output, expected_or_err = run_valid(name)
        if output == nil then
            error(('%s: render failed: %s'):format(name,
                                                   tostring(expected_or_err)))
        end
        assert.equal(trim(output), trim(expected_or_err),
                     ('%s output diverged'):format(name))
    end
end

for _, name in ipairs(manifest.invalid) do
    testcase['invalid_' .. name:gsub('-', '_')] = function()
        local err, actual_phase, spec = run_invalid(name)
        if spec.phase ~= 'compile' and spec.phase ~= 'render' then
            error(('%s: invalid expected phase %q'):format(name,
                                                           tostring(spec.phase)))
        end
        if err == nil then
            error(('%s: expected %s to fail'):format(name, spec.phase))
        end
        assert.equal(actual_phase, spec.phase,
                     ('%s error phase mismatch'):format(name))
        if type(err) ~= 'table' then
            error(('%s: expected structured error, got %s')
                :format(name, type(err)))
        end
        assert.equal(err.type, spec.class,
                     ('%s error class mismatch'):format(name))
        if spec.reason ~= nil then
            assert.equal(err.reason, spec.reason,
                         ('%s error reason mismatch'):format(name))
        end
        if spec.message_pattern ~= nil and
           not match_js_pattern(tostring(err.message),
                                spec.message_pattern) then
            error(('%s: expected error to match /%s/, got %q')
                :format(name, spec.message_pattern, tostring(err.message)))
        end
    end
end
