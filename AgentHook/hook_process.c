/*
 * hook_process.c
 * AgentHook
 *
 * 进程调用 Hook。通过 DYLD_INTERPOSE 拦截进程操作并上报审计事件。
 */

#include "hook_common.h"
#include "socket_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <spawn.h>
#include <dlfcn.h>

/* ============================================================================
 * Hook 函数
 * ============================================================================ */

/*
 * hook_execve — 拦截 execve(2)
 */
int hook_execve(const char *path, char *const argv[], char *const envp[])
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return execve(path, argv, envp);

    ENTER_INSIDE_CALL();
    record_proc_event("execve", path);
    EXIT_INSIDE_CALL();
    return execve(path, argv, envp);
}

/*
 * hook_posix_spawn — 拦截 posix_spawn(2)
 */
int hook_posix_spawn(pid_t *pid, const char *path,
                     const posix_spawn_file_actions_t *file_actions,
                     const posix_spawnattr_t *attrp,
                     char *const argv[], char *const envp[])
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return posix_spawn(pid, path, file_actions, attrp, argv, envp);

    ENTER_INSIDE_CALL();
    record_proc_event("posix_spawn", path);
    EXIT_INSIDE_CALL();
    return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

/*
 * hook_fork — 拦截 fork(2)
 */
pid_t hook_fork(void)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return fork();

    ENTER_INSIDE_CALL();
    record_proc_event("fork", NULL);
    EXIT_INSIDE_CALL();
    return fork();
}

/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_execve,      execve)
DYLD_INTERPOSE(hook_posix_spawn, posix_spawn)
DYLD_INTERPOSE(hook_fork,        fork)
