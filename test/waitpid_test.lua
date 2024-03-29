require('luacov')
local assert = require('assert')
local waitpid = require('waitpid')
local fork = require('fork')
local signal = require('signal')
local sleep = require('nanosleep.sleep')

local function test_wait()
    local p1 = assert(fork())
    if p1:is_child() then
        -- test that child process exit 123
        os.exit(123)
    end
    local pid1 = p1:pid()

    local p2 = assert(fork())
    if p2:is_child() then
        -- test that child process exit 123
        sleep(.1)
        os.exit(124)
    end
    local pid2 = p2:pid()

    -- test that wait specified child process exit
    local res, err, again = waitpid(pid2)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid2,
        exit = 124,
    })

    -- test that wait child process exit
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        exit = 123,
    })

    -- test that return error after all child process exit
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.is_nil(res)
end

local function test_wait_with_timeout()
    local p = assert(fork())
    if p:is_child() then
        -- test that child process exit 123 after 100ms
        sleep(.1)
        os.exit(123)
    end
    local pid = p:pid()

    -- test that return again=true
    local res, err, again = waitpid(nil, 0)
    assert.is_nil(err)
    assert.is_true(again)
    assert.is_nil(res)

    -- test that return result
    sleep(.6)
    res, err, again = waitpid()
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        exit = 123,
    })
end

local function test_wait_nohang()
    local p = assert(fork())
    if p:is_child() then
        sleep(0.1)
        os.exit(123)
    end
    local pid = p:pid()

    -- test that return timeout=true immediately
    local res, err, again = waitpid(pid, 1, 'nohang')
    assert.is_nil(err)
    assert.is_true(again)
    assert.is_nil(res)

    -- cleanup
    assert(waitpid(pid))
end

local function test_wait_untraced()
    local p = assert(fork())
    if p:is_child() then
        -- test that child process exit 123 after sig continued
        assert(signal.kill(signal.SIGSTOP))
        os.exit(123)
    end
    local pid = p:pid()

    -- test that res.sigstop=SIGSTOP
    local res, err, again = waitpid(nil, nil, 'untraced')
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        sigstop = signal.SIGSTOP,
    })

    -- cleanup
    assert(signal.kill(signal.SIGCONT, pid))
    waitpid()
end

local function test_wait_continued()
    local p1 = assert(fork())
    if p1:is_child() then
        -- child process exit 123 after sig continued
        assert(signal.kill(signal.SIGSTOP))
        sleep(.1)
        os.exit(123)
    end
    local pid1 = p1:pid()

    local p2 = assert(fork())
    if p2:is_child() then
        -- send SIGCONT signal after 100ms
        sleep(.2)
        assert(signal.kill(signal.SIGCONT, pid1))
        os.exit()
    end

    -- test that res.sigcont=true
    local res, err, again = waitpid(pid1, nil, 'continued')
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        sigcont = true,
    })

    -- cleanup
    waitpid()

    -- test that return exit status if continued process exit before wait
    p1 = assert(fork())
    if p1:is_child() then
        -- child process exit 123 after continued
        assert(signal.kill(signal.SIGSTOP))
        os.exit(123)
    end
    pid1 = p1:pid()

    p2 = assert(fork())
    if p2:is_child() then
        -- send SIGCONT signal after 100ms
        sleep(.1)
        assert(signal.kill(signal.SIGCONT, pid1))
        os.exit()
    end

    sleep(.3)
    res, err, again = waitpid(pid1)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid1,
        exit = 123,
    })

    -- cleanup
    waitpid()
end

local function test_wait_sigterm()
    local p = assert(fork())
    if p:is_child() then
        -- child process exit with sigterm after 100ms
        sleep(.1)
        assert(signal.kill(signal.SIGTERM))
        os.exit(123)
    end
    local pid = p:pid()

    -- test that res.sigterm=SIGTERM
    local res, err, again = waitpid(pid)
    assert.is_nil(err)
    assert.is_nil(again)
    assert.equal(res, {
        pid = pid,
        exit = 128 + signal.SIGTERM,
        sigterm = signal.SIGTERM,
    })
end

test_wait()
test_wait_with_timeout()
test_wait_nohang()
test_wait_untraced()
test_wait_continued()
test_wait_sigterm()
