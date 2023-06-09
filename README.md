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

- `pid:integer`: process id.
- `...:string`: waitpid options;  
    - `'nohang'`: return immediately if no child has exited.
    - `'untraced'`: also return if a child has stopped.
    - `'continued'`: also return if a stopped child has been resumed by delivery of `SIGCONT`.

**Returns**

- `res:table`: result table if succeeded.
    - `pid:integer` = process id.
    - `exit:integer` = value of `WEXITSTATUS(wstatus)` if `WIFEXITED(wstatus)` is true.
    - `sigterm:integer` = value of `WTERMSIG(wstatus)` if `WIFSIGNALED(wstatus)` is true.
    - `sigstop:integer` = value of `WSTOPSIG(wstatus)` if `WIFSTOPPED(wstatus)` is true.
    - `sigcont:boolean` = `true` if `WIFCONTINUED(wstatus)` is true
- `err:error`: `nil` on success, or error object on failure.
- `again:boolean`: `true` if `waitpid` returns `0`.
