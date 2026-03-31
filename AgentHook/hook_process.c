/*
 * hook_process.c
 * AgentSandbox - 进程操作 Hook
 *
 * 通过 DYLD_INTERPOSE 拦截进程系统调用，记录审计事件。
 *
 * Created by ConradSun on 2025/3/31.
 */

#include "common.h"
#include "socket_client.h"

#include <unistd.h>
#include <spawn.h>

/* ============================================================================
 * Hook 函数
 * ============================================================================ */

/*
 * hook_execve
 * 拦截 execve() 系统调用
 *
 * @param path  可执行文件路径
 * @param argv  命令行参数数组
 * @param envp  环境变量数组
 * @return 原始 execve() 的返回值
 */
static int hook_execve(const char *path, char *const argv[], char *const envp[])
{
    record_proc_event("execve", path);
    return execve(path, argv, envp);
}

/*
 * hook_posix_spawn
 * 拦截 posix_spawn() 系统调用
 *
 * @param pid           新进程 ID（输出）
 * @param path          可执行文件路径
 * @param file_actions  文件操作配置
 * @param attrp         进程属性配置
 * @param argv          命令行参数数组
 * @param envp          环境变量数组
 * @return 原始 posix_spawn() 的返回值
 */
static int hook_posix_spawn(pid_t *pid, const char *path,
                             const posix_spawn_file_actions_t *file_actions,
                             const posix_spawnattr_t *attrp,
                             char *const argv[], char *const envp[])
{
    record_proc_event("posix_spawn", path);
    return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

/*
 * hook_fork
 * 拦截 fork() 系统调用
 *
 * @return 原始 fork() 的返回值
 */
static pid_t hook_fork(void)
{
    record_proc_event("fork", NULL);
    return fork();
}

/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_execve,      execve)
DYLD_INTERPOSE(hook_posix_spawn, posix_spawn)
DYLD_INTERPOSE(hook_fork,        fork)
