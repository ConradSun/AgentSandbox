/*
 * hook_file.c
 * AgentSandbox - 文件操作 Hook
 *
 * 通过 DYLD_INTERPOSE 拦截文件系统调用，记录审计事件。
 *
 * Created by ConradSun on 2025/3/31.
 */

#include "common.h"
#include "socket_client.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ============================================================================
 * Hook 函数
 * ============================================================================ */

/*
 * hook_open
 * 拦截 open() 系统调用
 *
 * @param path  文件路径
 * @param flags 打开标志
 * @return 原始 open() 的返回值
 */
static int hook_open(const char *path, int flags, ...)
{
    record_file_event("open", path);
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, int);
    va_end(args);
    return open(path, flags, mode);
}

/*
 * hook_creat
 * 拦截 creat() 系统调用
 *
 * @param path 文件路径
 * @param mode 文件权限
 * @return 原始 creat() 的返回值
 */
static int hook_creat(const char *path, mode_t mode)
{
    record_file_event("creat", path);
    return creat(path, mode);
}

/*
 * hook_stat
 * 拦截 stat() 系统调用
 *
 * @param path 文件路径
 * @param buf  stat 结构体指针
 * @return 原始 stat() 的返回值
 */
static int hook_stat(const char *path, struct stat *buf)
{
    record_file_event("stat", path);
    return stat(path, buf);
}

/*
 * hook_lstat
 * 拦截 lstat() 系统调用
 *
 * @param path 文件路径
 * @param buf  stat 结构体指针
 * @return 原始 lstat() 的返回值
 */
static int hook_lstat(const char *path, struct stat *buf)
{
    record_file_event("lstat", path);
    return lstat(path, buf);
}

/*
 * hook_access
 * 拦截 access() 系统调用
 *
 * @param path 文件路径
 * @param mode 访问模式
 * @return 原始 access() 的返回值
 */
static int hook_access(const char *path, int mode)
{
    record_file_event("access", path);
    return access(path, mode);
}

/*
 * hook_unlink
 * 拦截 unlink() 系统调用
 *
 * @param path 文件路径
 * @return 原始 unlink() 的返回值
 */
static int hook_unlink(const char *path)
{
    record_file_event("unlink", path);
    return unlink(path);
}

/*
 * hook_mkdir
 * 拦截 mkdir() 系统调用
 *
 * @param path 目录路径
 * @param mode 目录权限
 * @return 原始 mkdir() 的返回值
 */
static int hook_mkdir(const char *path, mode_t mode)
{
    record_file_event("mkdir", path);
    return mkdir(path, mode);
}

/*
 * hook_rmdir
 * 拦截 rmdir() 系统调用
 *
 * @param path 目录路径
 * @return 原始 rmdir() 的返回值
 */
static int hook_rmdir(const char *path)
{
    record_file_event("rmdir", path);
    return rmdir(path);
}

/*
 * hook_rename
 * 拦截 rename() 系统调用
 *
 * @param oldpath 原文件路径
 * @param newpath 新文件路径
 * @return 原始 rename() 的返回值
 */
static int hook_rename(const char *oldpath, const char *newpath)
{
    record_file_event("rename", oldpath);
    return rename(oldpath, newpath);
}

/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_open,   open)
DYLD_INTERPOSE(hook_creat,  creat)
DYLD_INTERPOSE(hook_stat,   stat)
DYLD_INTERPOSE(hook_lstat,  lstat)
DYLD_INTERPOSE(hook_access, access)
DYLD_INTERPOSE(hook_unlink, unlink)
DYLD_INTERPOSE(hook_mkdir,  mkdir)
DYLD_INTERPOSE(hook_rmdir,  rmdir)
DYLD_INTERPOSE(hook_rename, rename)
