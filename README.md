# lua-waitpid

[![test](https://github.com/mah0x211/lua-waitpid/actions/workflows/test.yml/badge.svg)](https://github.com/mah0x211/lua-waitpid/actions/workflows/test.yml)
[![codecov](https://codecov.io/gh/mah0x211/lua-waitpid/branch/master/graph/badge.svg)](https://codecov.io/gh/mah0x211/lua-waitpid)

wait for process termination.

## Installation

```sh
luarocks install waitpid
```


## Usage

```lua
local waitpid = require('waitpid')
-- the following modules should be installed with luarocks
local fork = require('fork') -- luarocks install fork
local dump = require('dump') -- luarocks intall dump

local p = assert(fork())
if p:is_child() then
    os.exit(123)
end

local res, err = waitpid()
if err then
    error('failed to waitpid():', err)
end
print(dump(res))
-- {
--     exit = 123,
--     pid = <same as p:pid()>
-- }
```

## Error Handling

the functions/methods are return the error object created by https://github.com/mah0x211/lua-errno module.


## res, err, again = waitpid( [pid [, ...]] )

wait for process termination.  
please refer to `man 2 waitpid` for more details.

**Parameters**

- `pid:integer`: process id (defalut: `-1`).
    - `-1`: wait for any child process.
    - `0`: wait for any child process in the same process group as the caller.
    - `>0`: wait for the child whose process id is equal to the value of `pid`
    - `<-1`: wait for any child process whose process group id is equal to the absolute value of `pid`.
- `...:string`: waitpid options;  
    - `'nohang'`: return immediately if no child has exited.
    - `'untraced'`: also return if a child has stopped.
    - `'continued'`: also return if a stopped child has been resumed by delivery of `SIGCONT`.

**Returns**

- `res:table`: result table if succeeded.
    - `pid:integer`: process id.
    - `exit:integer`: value of `WEXITSTATUS(wstatus)` if `WIFEXITED(wstatus)` is true.
        - if `WIFSIGNALED(wstatus)` is true, then the value is `128 + WTERMSIG(wstatus)`.
    - `sigterm:integer`: value of `WTERMSIG(wstatus)` if `WIFSIGNALED(wstatus)` is true.
    - `sigstop:integer`: value of `WSTOPSIG(wstatus)` if `WIFSTOPPED(wstatus)` is true.
    - `sigcont:boolean`: `true` if `WIFCONTINUED(wstatus)` is true
- `err:any`: error object on failure.
    - `ECHILD` is ignored.
- `again:boolean`: `true` if `waitpid` returns `0`.
