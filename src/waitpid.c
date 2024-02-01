/**
 * Copyright (C) 2023 Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <sys/types.h>
#include <sys/wait.h>
// lua
#include <lua_errno.h>

static inline int checkoptions(lua_State *L, int index)
{
    static const char *const options[] = {
        "nohang",
        "untraced",
        "continued",
        NULL,
    };
    int top  = lua_gettop(L);
    int opts = 0;

    for (; index <= top; index++) {
        switch (luaL_checkoption(L, index, NULL, options)) {
        case 0:
            opts |= WNOHANG;
            break;

        case 1:
            opts |= WUNTRACED;
            break;

        default:
#ifdef WCONTINUED
            opts |= WCONTINUED;
#endif
            break;
        }
    }

    return opts;
}

static int waitpid_lua(lua_State *L)
{
    int wpid    = luaL_optinteger(L, 1, -1);
    int opts    = checkoptions(L, 2);
    int wstatus = 0;
    pid_t pid   = waitpid(wpid, &wstatus, opts);

    switch (pid) {
    case -1:
        // got error
        lua_pushnil(L);
        if (errno == ECHILD) {
            // no child process exists
            return 1;
        }
        lua_errno_new(L, errno, "waitpid");
        return 2;

    case 0:
        // nonblock = WNOHANG
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushboolean(L, 1);
        return 3;
    }

    // push result
    lua_createtable(L, 0, 5);
    lauxh_pushint2tbl(L, "pid", pid);
    if (WIFEXITED(wstatus)) {
        // exit status
        lauxh_pushint2tbl(L, "exit", WEXITSTATUS(wstatus));
    } else if (WIFSIGNALED(wstatus)) {
        // exit by signal
        lauxh_pushint2tbl(L, "sigterm", WTERMSIG(wstatus));

#ifdef WCOREDUMP
        if (WCOREDUMP(wstatus)) {
            lauxh_pushbool2tbl(L, "coredump", 1);
        }
#endif

    } else if (WIFSTOPPED(wstatus)) {
        // stopped by signal
        lauxh_pushint2tbl(L, "sigstop", WSTOPSIG(wstatus));
    } else if (WIFCONTINUED(wstatus)) {
        // continued by signal
        lauxh_pushbool2tbl(L, "sigcont", 1);
    }

    return 1;
}

LUALIB_API int luaopen_waitpid(lua_State *L)
{
    lua_errno_loadlib(L);
    lua_pushcfunction(L, waitpid_lua);
    return 1;
}
