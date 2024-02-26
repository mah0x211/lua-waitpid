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

#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
// lua
#include <lua_errno.h>

#define WAITPID_CONTEXT_MT "waitpid.context"

typedef struct {
    pthread_t tid;
    pthread_mutex_t mutex;
    int pipefd[2];
    pid_t pid;
    pid_t wpid;
    int status;
    int options;
    int errnum;
    int is_nohang;
} waitpid_ctx_t;

static int pushresult_lua(lua_State *L, waitpid_ctx_t *ctx)
{
    // check result
    if (ctx->wpid == 0) {
        // thread is canceled or waitpid with WNOHANG option
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushboolean(L, 1);
        return 3;
    }

    if (ctx->wpid == -1) {
        // got error
        lua_pushnil(L);
        if (ctx->errnum == ECHILD) {
            // no child process exists
            return 1;
        }
        lua_errno_new(L, ctx->errnum, "waitpid");
        return 2;
    }

    // push result
    lua_createtable(L, 0, 5);
    lauxh_pushint2tbl(L, "pid", ctx->wpid);
    if (WIFEXITED(ctx->status)) {
        // exit status
        lauxh_pushint2tbl(L, "exit", WEXITSTATUS(ctx->status));
    } else if (WIFSIGNALED(ctx->status)) {
        int signo = WTERMSIG(ctx->status);
        // exit by signal
        lauxh_pushint2tbl(L, "sigterm", signo);
        lauxh_pushint2tbl(L, "exit", 128 + signo);

#ifdef WCOREDUMP
        if (WCOREDUMP(ctx->status)) {
            lauxh_pushbool2tbl(L, "coredump", 1);
        }
#endif

    } else if (WIFSTOPPED(ctx->status)) {
        // stopped by signal
        lauxh_pushint2tbl(L, "sigstop", WSTOPSIG(ctx->status));
    } else if (WIFCONTINUED(ctx->status)) {
        // continued by signal
        lauxh_pushbool2tbl(L, "sigcont", 1);
    }

    return 1;
}

static int waitpid_with_thread(lua_State *L, waitpid_ctx_t *ctx)
{
    char buf[2] = {0};
    ssize_t len = read(ctx->pipefd[0], buf, sizeof(buf));

    lua_settop(L, 1);
    switch (len) {
    case -1:
        // got error
        lua_pushnil(L);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            lua_pushnil(L);
            lua_pushboolean(L, 1);
            return 3;
        }
        lua_errno_new(L, errno, "read");
        return 2;

    default:
        // got signal
        close(ctx->pipefd[0]);
        ctx->pipefd[0] = -1;
    }

    return pushresult_lua(L, ctx);
}

static int waitpid_lua(lua_State *L)
{
    waitpid_ctx_t *ctx = lauxh_checkudata(L, 1, WAITPID_CONTEXT_MT);

    if (ctx->is_nohang) {
        ctx->wpid = waitpid(ctx->pid, &ctx->status, ctx->options);
        if (ctx->wpid == -1) {
            ctx->errnum = errno;
        }
        return pushresult_lua(L, ctx);
    }
    return waitpid_with_thread(L, ctx);
}

static int cancel_lua(lua_State *L)
{
    waitpid_ctx_t *ctx = lauxh_checkudata(L, 1, WAITPID_CONTEXT_MT);

    if (ctx->is_nohang) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "cannot cancel waitpid with nohang option");
        return 2;
    }

    errno = pthread_mutex_lock(&ctx->mutex);
    if (errno) {
        lua_pushboolean(L, 0);
        lua_errno_new(L, errno, "pthread_mutex_lock");
        return 2;
    }

    if (ctx->pipefd[1] != -1) {
        errno = pthread_cancel(ctx->tid);
    }
    pthread_mutex_unlock(&ctx->mutex);

    lua_pushboolean(L, errno == 0);
    return 1;
}

static int is_nohang_lua(lua_State *L)
{
    waitpid_ctx_t *ctx = lauxh_checkudata(L, 1, WAITPID_CONTEXT_MT);
    lua_pushboolean(L, ctx->is_nohang);
    return 1;
}

static int fd_lua(lua_State *L)
{
    waitpid_ctx_t *ctx = lauxh_checkudata(L, 1, WAITPID_CONTEXT_MT);
    // return read-end of pipe
    lua_pushinteger(L, ctx->pipefd[0]);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, WAITPID_CONTEXT_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    waitpid_ctx_t *ctx = lauxh_checkudata(L, 1, WAITPID_CONTEXT_MT);
    // close pipe
    if (ctx->pipefd[0] != -1) {
        close(ctx->pipefd[0]);
    }
    if (ctx->pipefd[1] != -1) {
        close(ctx->pipefd[1]);
    }
    return 0;
}

static void waitpid_thread_cleanup(void *arg)
{
    waitpid_ctx_t *ctx = (waitpid_ctx_t *)arg;

    // send signal to main thread
    pthread_mutex_lock(&ctx->mutex);
    if (ctx->wpid == -2) {
        ctx->wpid = 0;
    }
    close(ctx->pipefd[1]);
    ctx->pipefd[1] = -1;
    pthread_mutex_unlock(&ctx->mutex);
}

static void *waitpid_thread(void *arg)
{
    waitpid_ctx_t *ctx = (waitpid_ctx_t *)arg;

    pthread_cleanup_push(waitpid_thread_cleanup, ctx);
    ctx->wpid = waitpid(ctx->pid, &ctx->status, ctx->options);

    if (ctx->wpid == -1) {
        // keep errno
        ctx->errnum = errno;
    }
    pthread_mutex_lock(&ctx->mutex);
    close(ctx->pipefd[1]);
    ctx->pipefd[1] = -1;
    pthread_mutex_unlock(&ctx->mutex);

    pthread_cleanup_pop(1);
    return NULL;
}

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

static int new_lua(lua_State *L)
{
    pid_t pid          = luaL_optinteger(L, 1, -1);
    int options        = checkoptions(L, 2);
    waitpid_ctx_t *ctx = lua_newuserdata(L, sizeof(waitpid_ctx_t));

    // initialize context
    ctx->mutex     = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    ctx->pipefd[0] = -1;
    ctx->pipefd[1] = -1;
    ctx->pid       = pid;
    ctx->wpid      = -2;
    ctx->status    = 0;
    ctx->options   = options;
    ctx->errnum    = 0;
    ctx->is_nohang = options & WNOHANG;
    if (ctx->is_nohang) {
        // waitpid without thread if WNOHANG is set
        lauxh_setmetatable(L, WAITPID_CONTEXT_MT);
        return 1;
    }

    // create pipe with O_NONBLOCK
    if (pipe(ctx->pipefd) == -1) {
        // failed to create pipe
        lua_pushnil(L);
        lua_errno_new(L, errno, "pipe");
        return 2;
    } else if (fcntl(ctx->pipefd[0], F_SETFL, O_NONBLOCK) == -1) {
        close(ctx->pipefd[0]);
        close(ctx->pipefd[1]);
        // failed to set nonblock flag
        lua_pushnil(L);
        lua_errno_new(L, errno, "fcntl");
        return 2;
    }

    // create thread
    errno = pthread_create(&ctx->tid, NULL, waitpid_thread, ctx);
    if (errno) {
        close(ctx->pipefd[0]);
        close(ctx->pipefd[1]);
        // failed to create thread
        lua_pushnil(L);
        if (errno == EAGAIN) {
            // too many threads are running
            lua_pushnil(L);
            lua_pushboolean(L, 1);
            return 3;
        }
        lua_errno_new(L, errno, "pthread_create");
        return 2;
    }
    errno = pthread_detach(ctx->tid);
    if (errno) {
        close(ctx->pipefd[0]);
        close(ctx->pipefd[1]);
        // failed to detach thread
        lua_pushnil(L);
        lua_errno_new(L, errno, "pthread_detach");
        return 2;
    }

    lauxh_setmetatable(L, WAITPID_CONTEXT_MT);
    return 1;
}

LUALIB_API int luaopen_waitpid_context(lua_State *L)
{
    struct luaL_Reg mmethods[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg methods[] = {
        {"fd",        fd_lua       },
        {"is_nohang", is_nohang_lua},
        {"cancel",    cancel_lua   },
        {"waitpid",   waitpid_lua  },
        {NULL,        NULL         }
    };

    // create metatable
    luaL_newmetatable(L, WAITPID_CONTEXT_MT);
    for (int i = 0; mmethods[i].name; i++) {
        lauxh_pushfn2tbl(L, mmethods[i].name, mmethods[i].func);
    }
    lua_newtable(L);
    for (int i = 0; methods[i].name; i++) {
        lauxh_pushfn2tbl(L, methods[i].name, methods[i].func);
    }
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_errno_loadlib(L);
    lua_pushcfunction(L, new_lua);
    return 1;
}
