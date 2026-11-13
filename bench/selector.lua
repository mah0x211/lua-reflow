-- Selector fragment benchmarks, ported from bench/selector.js of the
-- JavaScript version: renders small fragments of the full dashboard and
-- compares them with the full render.
--
-- Run after building the module (lua-cjson is required for null):
--   lua bench/selector.lua [iterations]
local clock = os.clock
local format = string.format
local floor = math.floor
local reflow = require('reflow')

local DIR = (arg and arg[0] or ''):match('^(.*)[/\\]') or '.'
local ITERATIONS = tonumber(arg and arg[1]) or 2000
local TEMPLATES = {
    'services-list',
    'alert-feed',
    'user-management',
    'metrics-overview',
    'audit-log',
    'dashboard',
}
local SELECTORS = {
    {
        'nav.sidebar (shallow)',
        '.sidebar',
    },
    {
        'nav > ul > li.active',
        '.sidebar li.active',
    },
    {
        'main.main-content',
        '.main-content',
    },
    {
        'first panel (positional)',
        '.main-content > section:first-child',
    },
    {
        'title tag',
        'title',
    },
}

-- The helpers of the JavaScript benchmark.
local HELPERS = {
    fmtDate = function(ms)
        return os.date('!%Y-%m-%d %H:%M:%S', floor(ms / 1000))
    end,
    fmtBytes = function(b)
        if b >= 1048576 then
            return format('%.1f MB', b / 1048576)
        end
        return format('%.0f KB', b / 1024)
    end,
    pct = function(n, total)
        if total == 0 then
            return 0
        end
        return math.min(100, floor(n / total * 100 + 0.5))
    end,
    ceil = function(n, d)
        return math.ceil(n / d)
    end,
    plural = function(n, word)
        return format('%d %s%s', n, word, n == 1 and '' or 's')
    end,
    cat = function(...)
        local parts = {}
        for i = 1, select('#', ...) do
            parts[i] = tostring((select(i, ...)))
        end
        return table.concat(parts)
    end,
    pad2 = function(n)
        return format('%02d', n)
    end,
}

--- @param path string
--- @return string
local function read(path)
    local f = assert(io.open(path))
    local s = f:read('*a')
    f:close()
    return s
end

--- Print the mean CPU time of one call of fn over ITERATIONS calls, after a
--- tenth of them as warm-up.
--- @param label string
--- @param fn function
local function bench(label, fn)
    for _ = 1, floor(ITERATIONS / 10) do
        fn()
    end
    local t0 = clock()
    for _ = 1, ITERATIONS do
        fn()
    end
    print(format('  %-46s %10.2f us', label, (clock() - t0) / ITERATIONS * 1e6))
end

local data = dofile(DIR .. '/data.lua').dashboard
local engine = assert(reflow.new({
    helpers = HELPERS,
}))
for _, name in ipairs(TEMPLATES) do
    assert(engine:compile(name, read(format('%s/templates/%s.html', DIR, name))))
end

print('=== Environment ===')
print('Lua      : ' .. (jit and jit.version or _VERSION))
print(format('Iterations: %d', ITERATIONS))
print()

print('=== Fragment sizes ===')
for _, s in ipairs(SELECTORS) do
    local html = assert(engine:render('dashboard', data, s[2]))
    print(format('  %-30s (%-38s): %d bytes', s[1], s[2], #html))
end
print(format('  full dashboard render: %d bytes',
             #assert(engine:render('dashboard', data))))
print()

print('=== Render ===')
bench('dashboard (full)', function()
    return engine:render('dashboard', data)
end)
for _, s in ipairs(SELECTORS) do
    local source = s[2]
    bench(s[1] .. ' [string]', function()
        return engine:render('dashboard', data, source)
    end)
end
for _, s in ipairs(SELECTORS) do
    local sel = assert(reflow.compile_selector(s[2]))
    bench(s[1] .. ' [precompiled]', function()
        return engine:render('dashboard', data, sel)
    end)
end
