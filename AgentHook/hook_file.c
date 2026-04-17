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

static inline void audit_transfer_event(const char *op, const char *from, const char *to)
{
    char buf[PATH_MAX * 2];
    snprintf(buf, sizeof(buf), "%s -> %s", from, to);
    record_file_event(op, buf);
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

/* open(2) — 打开/创建文件，沙箱接管写操作，O_CREAT/O_WRONLY/O_RDWR 时预建目录 */
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

/* creat(2) — 创建文件，等价于 open(path, O_CREAT|O_WRONLY|O_TRUNC, mode) */
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

/* stat(2) — 获取文件状态，优先查沙箱 */
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

/* lstat(2) — 获取符号链接本身状态，不追踪软链，沙箱优先 */
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

/* access(2) — 检查调用进程对文件的访问权限，沙箱优先 */
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

/* unlink(2) — 删除文件，沙箱内移至回收站 */
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

/* mkdir(2) — 创建目录，沙箱优先 */
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

/* rmdir(2) — 删除空目录，沙箱内移至回收站 */
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

/* rename(2) — 重命名/移动文件，沙箱接管写入端，目标目录预创建 */
static int hook_rename(const char *from, const char *to)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _rename(from, to);

    audit_transfer_event("rename", from, to);

    char old_sbox[PATH_MAX], new_sbox[PATH_MAX];
    const char *src = from;
    const char *dst = to;

    if (is_user_protected_path(from) &&
        sandboxize_path(from, old_sbox, sizeof(old_sbox))) {
        if (file_exists(old_sbox))
            src = old_sbox;
    }

    if (is_user_protected_path(to) &&
        sandboxize_path(to, new_sbox, sizeof(new_sbox))) {
        prepare_for_create(new_sbox);
        dst = new_sbox;
    }

    return _rename(src, dst);
}

/* renamex_np(2) — rename + flags，macOS 扩展，沙箱接管写入端 */
static int hook_renamex_np(const char *from, const char *to, unsigned int flags)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _renamex_np(from, to, flags);

    audit_transfer_event("renamex_np", from, to);

    char from_box[PATH_MAX], to_box[PATH_MAX];
    const char *src = from;
    const char *dst = to;

    if (is_user_protected_path(from) &&
        sandboxize_path(from, from_box, sizeof(from_box))) {
        if (file_exists(from_box))
            src = from_box;
    }

    if (is_user_protected_path(to) &&
        sandboxize_path(to, to_box, sizeof(to_box))) {
        prepare_for_create(to_box);
        dst = to_box;
    }

    return _renamex_np(src, dst, flags);
}

/* renameat(2) — POSIX 标准，oldpath/newpath 相对于 oldfd/newfd */
static int hook_renameat(int oldfd, const char *oldpath,
                         int newfd, const char *newpath)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _renameat(oldfd, oldpath, newfd, newpath);

    audit_transfer_event("renameat", oldpath, newpath);

    char old_buf[PATH_MAX], new_buf[PATH_MAX];
    const char *src = oldpath;
    const char *dst = newpath;

    if (is_user_protected_path(oldpath) &&
        sandboxize_path(oldpath, old_buf, sizeof(old_buf))) {
        if (file_exists(old_buf))
            src = old_buf;
    }

    if (is_user_protected_path(newpath) &&
        sandboxize_path(newpath, new_buf, sizeof(new_buf))) {
        prepare_for_create(new_buf);
        dst = new_buf;
    }

    return _renameat(AT_FDCWD, src, AT_FDCWD, dst);
}

/* renameatx_np(2) — renameat + flags，macOS 核心文件操作常用 */
static int hook_renameatx_np(int oldfd, const char *oldpath,
                             int newfd, const char *newpath, unsigned int flags)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _renameatx_np(oldfd, oldpath, newfd, newpath, flags);

    audit_transfer_event("renameatx_np", oldpath, newpath);

    char old_buf[PATH_MAX], new_buf[PATH_MAX];
    const char *src = oldpath;
    const char *dst = newpath;

    if (is_user_protected_path(oldpath) &&
        sandboxize_path(oldpath, old_buf, sizeof(old_buf))) {
        if (file_exists(old_buf))
            src = old_buf;
    }

    if (is_user_protected_path(newpath) &&
        sandboxize_path(newpath, new_buf, sizeof(new_buf))) {
        prepare_for_create(new_buf);
        dst = new_buf;
    }

    return _renameatx_np(AT_FDCWD, src, AT_FDCWD, dst, flags);
}

/* exchangedata(2) — macOS 原子交换两个文件的属性和数据 */
static int hook_exchangedata(const char *path1, const char *path2, unsigned int options)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _exchangedata(path1, path2, options);

    audit_transfer_event("exchangedata", path1, path2);

    char buf1[PATH_MAX], buf2[PATH_MAX];
    const char *p1 = path1;
    const char *p2 = path2;

    if (is_user_protected_path(path1) &&
        sandboxize_path(path1, buf1, sizeof(buf1))) {
        if (file_exists(buf1))
            p1 = buf1;
    }

    if (is_user_protected_path(path2) &&
        sandboxize_path(path2, buf2, sizeof(buf2))) {
        if (file_exists(buf2))
            p2 = buf2;
    }

    return _exchangedata(p1, p2, options);
}

/* copyfile(3) — 完整文件内容（含属性）复制 */
static int hook_copyfile(const char *src, const char *dst,
                         copyfile_state_t state, copyfile_flags_t flags)
{
    hook_init_guard();
    if (is_self_process() || IS_INSIDE_CALL())
        return _copyfile(src, dst, state, flags);

    audit_transfer_event("copyfile", src, dst);

    char src_buf[PATH_MAX], dst_buf[PATH_MAX];
    const char *src_path = src;
    const char *dst_path = dst;

    if (is_user_protected_path(src) &&
        sandboxize_path(src, src_buf, sizeof(src_buf))) {
        if (file_exists(src_buf))
            src_path = src_buf;
    }

    if (is_user_protected_path(dst) &&
        sandboxize_path(dst, dst_buf, sizeof(dst_buf))) {
        prepare_for_create(dst_buf);
        dst_path = dst_buf;
    }

    return _copyfile(src_path, dst_path, state, flags);
}


/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_open,         open)
DYLD_INTERPOSE(hook_creat,        creat)
DYLD_INTERPOSE(hook_stat,         stat)
DYLD_INTERPOSE(hook_lstat,        lstat)
DYLD_INTERPOSE(hook_access,       access)
DYLD_INTERPOSE(hook_unlink,       unlink)
DYLD_INTERPOSE(hook_mkdir,        mkdir)
DYLD_INTERPOSE(hook_rmdir,        rmdir)
DYLD_INTERPOSE(hook_rename,       rename)
DYLD_INTERPOSE(hook_renameat,     renameat)
DYLD_INTERPOSE(hook_renamex_np,   renamex_np)
DYLD_INTERPOSE(hook_renameatx_np, renameatx_np)
DYLD_INTERPOSE(hook_exchangedata, exchangedata)
DYLD_INTERPOSE(hook_copyfile,     copyfile)
