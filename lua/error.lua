--
-- Copyright (C) 2026 Masatoshi Fukunaga
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
-- THE SOFTWARE.
--
--- assign to local
local error = error
local type = type
local errorlib = require('error')
local new_error_message = errorlib.message.new
local error_type = errorlib.type

--- error types registered at module load
local EREFLOW = error_type.new('reflow.Error')
local ECOMPILE = error_type.new('reflow.CompileError')
local ERUNTIME = error_type.new('reflow.RuntimeError')
local EINCLUDE = error_type.new('reflow.IncludeError')
local ESELECTOR = error_type.new('reflow.SelectorError')

--- create a typed reflow error that directly wraps the given cause.
--- @param errt error.type
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error? previously raised error object to wrap; must be nil or an error object
--- @param meta table?
--- @return error err
local function new_error(errt, op, message, cause, meta)
    if type(op) ~= 'string' then
        error('op must be string', 3)
    elseif type(message) ~= 'string' then
        error('message must be string', 3)
    elseif meta ~= nil and type(meta) ~= 'table' then
        error('meta must be table', 3)
    end
    -- errt:new validates that cause is nil or an error object
    local err = errt:new(new_error_message(message, op), cause, 3)
    err.meta = meta
    return err
end

local M = {
    EREFLOW = EREFLOW,
    ECOMPILE = ECOMPILE,
    ERUNTIME = ERUNTIME,
    EINCLUDE = EINCLUDE,
    ESELECTOR = ESELECTOR,
}

--- create a reflow.Error.
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error?
--- @param meta table?
--- @return error err
function M.new(op, message, cause, meta)
    return new_error(EREFLOW, op, message, cause, meta)
end

--- create a reflow.CompileError.
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error?
--- @param meta table?
--- @return error err
function M.new_compile(op, message, cause, meta)
    return new_error(ECOMPILE, op, message, cause, meta)
end

--- create a reflow.RuntimeError.
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error?
--- @param meta table?
--- @return error err
function M.new_runtime(op, message, cause, meta)
    return new_error(ERUNTIME, op, message, cause, meta)
end

--- create a reflow.IncludeError.
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error?
--- @param meta table?
--- @return error err
function M.new_include(op, message, cause, meta)
    return new_error(EINCLUDE, op, message, cause, meta)
end

--- create a reflow.SelectorError.
--- @param op string operation name where the error occurred
--- @param message string
--- @param cause error?
--- @param meta table?
--- @return error err
function M.new_selector(op, message, cause, meta)
    return new_error(ESELECTOR, op, message, cause, meta)
end

return M
