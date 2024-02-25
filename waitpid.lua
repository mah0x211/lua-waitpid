--
-- Copyright (C) 2024 Masatoshi Fukunaga
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
local poll_wait_readable = require('gpoll').wait_readable
local waitpid_context = require('waitpid.context')

--- waitpid
--- @param pid number
--- @param sec? number
--- @param ... string
---| 'untraced' WUNTRACED
---| 'continued' WCONTINUED
--- @return table res
--- @return any err
--- @return boolean? timeout
local function waitpid(pid, sec, ...)
    assert(sec == nil or type(sec) == 'number', 'sec must be number')
    local ctx, err, again = waitpid_context(pid, ...)
    if not ctx then
        -- failed to create waitpid context
        return nil, err, again
    end

    local res
    res, err, again = ctx:waitpid()
    if not again then
        return res, err
    end

    -- wait for the thread to exit
    local fd = ctx:fd()
    local ok
    ok, err, again = poll_wait_readable(fd, sec)
    if not ok then
        if not again then
            -- got error
            return nil, err
        end

        -- cancel the thread
        ok, err = ctx:cancel()
        if not ok and err then
            -- failed to cancel the thread
            return nil, err
        end
    end

    -- wait for the thread to exit
    ok, err, again = poll_wait_readable(fd)
    if not ok then
        if again then
            error('failed to cancel the thread')
        end
        -- got error
        return nil, err
    end

    -- get the result
    return ctx:waitpid()
end

return waitpid
