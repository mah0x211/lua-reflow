local rockspec = ...

local function shell_quote(value)
    value = tostring(value)
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function run(command)
    local ok, _, code = os.execute(command)
    if type(ok) == "number" then
        if ok ~= 0 then
            error("command failed (" .. ok .. "): " .. command)
        end
    elseif not ok then
        error("command failed (" .. tostring(code) .. "): " .. command)
    end
end

if type(rockspec) ~= "table" then
    error("rockspec table is required")
end
local source_dir = "deps/lexbor"
local build_dir = source_dir .. "/build"

run(table.concat({
    "set -e",
    "cd " .. shell_quote(source_dir),
    "cmake -E make_directory build",
    "cd build",
    "cmake ..",
    "make lexbor_static",
}, "\n"))

rockspec.variables = rockspec.variables or {}
rockspec.variables.LEXBOR_INCDIR = source_dir .. "/source"
rockspec.variables.LEXBOR_LIBDIR = build_dir
rockspec.variables.LEXBOR_LIB = "lexbor_static"
