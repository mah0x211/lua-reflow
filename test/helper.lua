local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function read_first_line(cmd)
    local pipe = assert(io.popen(cmd))
    local line = pipe:read('*l')
    pipe:close()
    return line
end

local source = debug.getinfo(1, 'S').source
if source:sub(1, 1) == '@' then
    source = source:sub(2)
end

local test_dir = source:match('^(.+)/helper%.lua$') or 'test'
local root = test_dir:match('^/') and test_dir:gsub('/test$', '') or
                 read_first_line(
                     'cd ' .. shell_quote(test_dir .. '/..') .. ' && pwd -P')

package.path = table.concat({
    root .. '/lua/?.lua',
    root .. '/lua/?/init.lua',
    package.path,
}, ';')

local M = {
    root = root,
    shell_quote = shell_quote,
    read_first_line = read_first_line,
}

return M
