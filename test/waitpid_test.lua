local testcase = require('testcase')
local assert = require('assert')
local waitpid = require('waitpid')
local fork = require('testcase.fork')
local signal = require('testcase.signal')
local timer = require('testcase.timer')
-- testcase replaces os.exit with a dummy that throws an error; use the real
-- os.exit in child processes so the parent sees the intended exit code.
local exit = require('testcase.exit').exit

local SIGTERM = assert(signal.tosignum('SIGTERM'),
                       'cannot detect SIGTERM signal number')
local SIGSTOP = assert(signal.tosignum('SIGSTOP'),
                       'cannot detect SIGSTOP signal number')

function testcase.test_wait()
    local p1 = assert(fork())
    if p1:is_child() then
        exit(123)
    end
    local pid1 = p1:pid()

    local p2 = assert(fork())
    if p2:is_child() then
        timer.sleep(0.1)
        exit(124)
    end
    local pid2 = p2:pid()

    -- wait for a specific child
    local res, err, again = waitpid(pid2)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid2,
        exit = 124,
    })

    -- wait for any remaining child
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        exit = 123,
    })

    -- no more children
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.is_nil(res)
end

function testcase.test_wait_with_timeout()
    local p = assert(fork())
    if p:is_child() then
        timer.sleep(0.1)
        exit(123)
    end
    local pid = p:pid()

    -- sec=0 causes an immediate poll timeout
    local res, err, again = waitpid(nil, 0)
    assert.is_nil(err)
    assert.is_true(again)
    assert.is_nil(res)

    -- wait until the child has exited
    timer.sleep(0.6)
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        exit = 123,
    })
end

function testcase.test_wait_nohang()
    local p = assert(fork())
    if p:is_child() then
        timer.sleep(0.1)
        exit(123)
    end
    local pid = p:pid()

    -- nohang returns immediately when no status is available
    local res, err, again = waitpid(pid, 1, 'nohang')
    assert.is_nil(err)
    assert.is_true(again)
    assert.is_nil(res)

    -- cleanup
    assert(waitpid(pid))
end

function testcase.test_wait_untraced()
    local p = assert(fork())
    if p:is_child() then
        -- stop self to trigger WSTOPPED
        signal.raise('SIGSTOP')
        exit(123)
    end
    local pid = p:pid()

    -- detect WSTOPPED event
    local res, err, again = waitpid(nil, nil, 'untraced')
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        sigstop = SIGSTOP,
    })

    -- let the child finish
    signal.kill('SIGCONT', pid)
    waitpid()
end

function testcase.test_wait_continued()
    local p1 = assert(fork())
    if p1:is_child() then
        signal.raise('SIGSTOP')
        timer.sleep(0.1)
        exit(123)
    end
    local pid1 = p1:pid()

    local p2 = assert(fork())
    if p2:is_child() then
        timer.sleep(0.2)
        signal.kill('SIGCONT', pid1)
        exit()
    end

    -- detect WCONTINUED event
    local res, err, again = waitpid(pid1, nil, 'continued')
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        sigcont = true,
    })

    -- cleanup
    waitpid()
end

function testcase.test_wait_continued_then_exit()
    -- verify that waitpid returns the exit status when the continued child
    -- exits before the parent calls waitpid
    local p1 = assert(fork())
    if p1:is_child() then
        signal.raise('SIGSTOP')
        exit(123)
    end
    local pid1 = p1:pid()

    local p2 = assert(fork())
    if p2:is_child() then
        timer.sleep(0.1)
        signal.kill('SIGCONT', pid1)
        exit()
    end

    -- wait until p1 has been continued and exited
    timer.sleep(0.3)
    local res, err, again = waitpid(pid1)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        exit = 123,
    })

    -- cleanup
    waitpid()
end

function testcase.test_wait_sigterm()
    local p = assert(fork())
    if p:is_child() then
        timer.sleep(0.1)
        -- deliver SIGTERM to self
        signal.raise('SIGTERM')
        exit(123)
    end
    local pid = p:pid()

    -- detect signal-terminated child
    local res, err, again = waitpid(pid)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        exit = 128 + SIGTERM,
        sigterm = SIGTERM,
    })
end
