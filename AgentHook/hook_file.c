/*
 * hook_file.c
 * AgentHook - 文件系统调用 Hook
 *
 * 通过 DYLD_INTERPOSE 拦截文件操作，实现沙箱重定向：
 *   - 读取：沙箱有则重定向，无则读原文件
 *   - 写入/修改：沙箱无则先拷贝，再重定向
 *   - 删除：移动到沙箱回收站
 *
 * 递归防护：
 *   - IS_INSIDE_CALL guard：send_event 调用的 hook 函数内部 bypass 时设置
 *   - path_utils.h 的 _stat/_open 等：内部通过 dlsym(__stat/__open) 获取原始
 *     libc 实现，绕过 DYLD_INTERPOSE，不触发 hook 递归
 *
 * Created by ConradSun on 2025/3/31.
 */

/* 在引入 CoreFoundation 链之前引入 stdio.h，避免 Darwin Foundation module 覆盖 rename */
#include <stdio.h>

#include "path_utils.h"
#include "hook_common.h"
#include "socket_client.h"

#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <libproc.h>

/* ============================================================================
 * 审计事件
 * ============================================================================ */

static inline void audit_event(const char *op, const char *path)
{
    record_file_event(op, path);
}

/* ============================================================================
 * 自身进程检测
 * ============================================================================ */

static int is_self_process(void)
{
    static int checked = 0;
    static int is_self = 0;
    if (checked) return is_self;
    checked = 1;
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(getpid(), path, sizeof(path)) <= 0) return 0;
    if (strstr(path, "AgentSandbox.app")) is_self = 1;
    return is_self;
}

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

static inline bool is_write_mode(int flags)
{
    return (flags & O_WRONLY) || (flags & O_RDWR);
}

static int file_exists(const char *path)
{
    if (!path) return 0;
    struct stat st;
    return _stat(path, &st) == 0;
}

static int prepare_for_write(const char *orig_path, const char *sbox_path)
{
    if (file_exists(sbox_path)) return 0;
    if (!file_exists(orig_path)) return 0;
    return copy_file_to_sandbox(orig_path, sbox_path);
}

static int prepare_for_create(const char *sbox_path)
{
    return ensure_parent_directory_exists(sbox_path);
}

static int move_to_trash(const char *orig_path)
{
    char trash_buf[PATH_MAX];
    if (!trash_path(orig_path, trash_buf, sizeof(trash_buf))) return -1;
    if (ensure_directory_exists(SANDBOX_TRASH_PATH) != 0) return -1;
    int r = _rename(orig_path, trash_buf);
    if (r == 0) return 0;
    if (errno == EXDEV) {
        if (copy_file_to_sandbox(orig_path, trash_buf) != 0) {
            _unlink(orig_path);
            return 0;
        }
    }
    return -1;
}

/* ============================================================================
 * Hook 函数
 * ============================================================================ */

static int hook_open(const char *path, int flags, ...)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL()) {
        va_list a; va_start(a, flags);
        mode_t mode = va_arg(a, int);
        va_end(a);
        return _open(path, flags, mode);
    }

    va_list a; va_start(a, flags);
    mode_t mode = va_arg(a, int);
    va_end(a);

    audit_event("open", path);

    if (!is_user_protected_path(path))
        return _open(path, flags, mode);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _open(path, flags, mode);

    if (flags & O_CREAT)
        prepare_for_create(sbox_path);
    else if (is_write_mode(flags))
        prepare_for_write(path, sbox_path);
    else if (!file_exists(sbox_path))
        return _open(path, flags, mode);

    return _open(sbox_path, flags, mode);
}

static int hook_creat(const char *path, mode_t mode)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);

    audit_event("creat", path);

    if (!is_user_protected_path(path))
        return _open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);

    prepare_for_create(sbox_path);
    return _open(sbox_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

static int hook_stat(const char *path, struct stat *buf)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _stat(path, buf);

    audit_event("stat", path);

    if (!is_user_protected_path(path))
        return _stat(path, buf);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _stat(path, buf);

    return _stat(file_exists(sbox_path) ? sbox_path : path, buf);
}

static int hook_lstat(const char *path, struct stat *buf)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _lstat(path, buf);

    audit_event("lstat", path);

    if (!is_user_protected_path(path))
        return _lstat(path, buf);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _lstat(path, buf);

    return _lstat(file_exists(sbox_path) ? sbox_path : path, buf);
}

static int hook_access(const char *path, int mode)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _access(path, mode);

    audit_event("access", path);

    if (!is_user_protected_path(path))
        return _access(path, mode);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _access(path, mode);

    return _access(file_exists(sbox_path) ? sbox_path : path, mode);
}

static int hook_unlink(const char *path)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _unlink(path);

    audit_event("unlink", path);

    if (!is_user_protected_path(path))
        return _unlink(path);

    int r = move_to_trash(path);
    if (r == 0) return 0;
    return _unlink(path);
}

static int hook_mkdir(const char *path, mode_t mode)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _mkdir(path, mode);

    audit_event("mkdir", path);

    if (!is_user_protected_path(path))
        return _mkdir(path, mode);

    char sbox_path[PATH_MAX];
    if (!sandboxize_path(path, sbox_path, sizeof(sbox_path)))
        return _mkdir(path, mode);

    prepare_for_create(sbox_path);
    return _mkdir(sbox_path, mode);
}

static int hook_rmdir(const char *path)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _rmdir(path);

    audit_event("rmdir", path);

    if (!is_user_protected_path(path))
        return _rmdir(path);

    int r = move_to_trash(path);
    if (r == 0) return 0;
    return _rmdir(path);
}

static int hook_rename(const char *oldpath, const char *newpath)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _rename(oldpath, newpath);

    audit_event("rename", oldpath);

    char old_sbox[PATH_MAX], new_sbox[PATH_MAX];
    const char *src = oldpath;
    const char *dst = newpath;

    if (is_user_protected_path(oldpath) &&
        sandboxize_path(oldpath, old_sbox, sizeof(old_sbox))) {
        if (file_exists(old_sbox))
            src = old_sbox;
    }

    if (is_user_protected_path(newpath) &&
        sandboxize_path(newpath, new_sbox, sizeof(new_sbox))) {
        prepare_for_create(new_sbox);
        dst = new_sbox;
    }

    return _rename(src, dst);
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
