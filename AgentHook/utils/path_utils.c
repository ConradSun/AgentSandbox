/*
 * path_utils.c
 * AgentHook
 *
 * 路径判断与转换工具实现
 *
 * 递归防护：
 *   所有文件操作（stat/mkdir/open/close/unlink/rename）均通过 dlsym
 *   获取带 __ 前缀的 libc 符号（__stat/__open/__close 等），
 *   绕过 DYLD_INTERPOSE，不触发 hook 递归。
 */

#include "path_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <copyfile.h>

/* ============================================================================
 * 原始函数指针（绕过 interpose）
 *
 * 有 __ 前缀的符号不会被 DYLD_INTERPOSE 替换，dlsym 拿到真实 libc 实现。
 * ============================================================================ */

typedef int (*raw_stat_fn_t)(const char *, struct stat *);
typedef int (*raw_mkdir_fn_t)(const char *, mode_t);
typedef int (*raw_rmdir_fn_t)(const char *);
typedef int (*raw_access_fn_t)(const char *, int);
typedef int (*raw_open_fn_t)(const char *, int, int);
typedef int (*raw_close_fn_t)(int);
typedef int (*raw_unlink_fn_t)(const char *);
typedef int (*raw_rename_fn_t)(const char *, const char *);
typedef int (*raw_renameat_fn_t)(int, const char *, int, const char *);
typedef int (*raw_renamex_np_fn_t)(const char *, const char *, unsigned int);
typedef int (*raw_renameatx_np_fn_t)(int, const char *, int, const char *, unsigned int);
typedef int (*raw_exchangedata_fn_t)(const char *, const char *, unsigned long);
typedef int (*raw_copyfile_fn_t)(const char *, const char *, copyfile_state_t, copyfile_flags_t);

static raw_stat_fn_t         raw_stat   = NULL;
static raw_stat_fn_t         raw_lstat  = NULL;
static raw_mkdir_fn_t        raw_mkdir  = NULL;
static raw_rmdir_fn_t        raw_rmdir  = NULL;
static raw_access_fn_t       raw_access = NULL;
static raw_open_fn_t         raw_open   = NULL;
static raw_close_fn_t        raw_close  = NULL;
static raw_unlink_fn_t       raw_unlink = NULL;
static raw_rename_fn_t       raw_rename = NULL;
static raw_renameat_fn_t     raw_renameat = NULL;
static raw_renamex_np_fn_t   raw_renamex_np = NULL;
static raw_renameatx_np_fn_t raw_renameatx_np = NULL;
static raw_exchangedata_fn_t raw_exchangedata = NULL;
static raw_copyfile_fn_t     raw_copyfile = NULL;

__attribute__((constructor(100)))
static void init_raw_funcs(void)
{
    void *libc = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOLOAD);
    if (!libc) return;
    raw_stat     = (raw_stat_fn_t)        dlsym(libc, "__stat");
    raw_lstat    = (raw_stat_fn_t)        dlsym(libc, "__lstat");
    raw_mkdir    = (raw_mkdir_fn_t)       dlsym(libc, "__mkdir");
    raw_rmdir    = (raw_rmdir_fn_t)       dlsym(libc, "__rmdir");
    raw_access   = (raw_access_fn_t)      dlsym(libc, "__access");
    raw_open     = (raw_open_fn_t)        dlsym(libc, "__open");   /* fd + flags + mode */
    raw_close    = (raw_close_fn_t)       dlsym(libc, "__close");
    raw_unlink   = (raw_unlink_fn_t)      dlsym(libc, "__unlink");
    raw_rename   = (raw_rename_fn_t)      dlsym(libc, "__rename");
    raw_renameat      = (raw_renameat_fn_t)     dlsym(libc, "__renameat");
    raw_renamex_np   = (raw_renamex_np_fn_t)   dlsym(libc, "__renamex_np");
    raw_renameatx_np = (raw_renameatx_np_fn_t) dlsym(libc, "__renameatx_np");
    raw_exchangedata = (raw_exchangedata_fn_t) dlsym(libc, "__exchangedata");
    raw_copyfile     = (raw_copyfile_fn_t)     dlsym(libc, "__copyfile");
    dlclose(libc);
}

/* 安全包装：使用 int fd + int flags + int mode 签名（与 __open 对齐） */
int  _stat(const char *p, struct stat *s)    { return raw_stat   ? raw_stat(p, s)    : stat(p, s); }
int  _lstat(const char *p, struct stat *s)   { return raw_lstat  ? raw_lstat(p, s)   : lstat(p, s); }
int  _access(const char *p, int m)           { return raw_access ? raw_access(p, m)  : access(p, m); }
int  _mkdir(const char *p, mode_t m)         { return raw_mkdir  ? raw_mkdir(p, m)   : mkdir(p, m); }
int  _rmdir(const char *p)                   { return raw_rmdir  ? raw_rmdir(p)      : rmdir(p); }
int  _open(const char *p, int f, ...) {
    va_list a; va_start(a, f);
    int mode = va_arg(a, int);
    va_end(a);
    return raw_open ? raw_open(p, f, mode) : open(p, f, mode);
}
int  _close(int fd)                          { return raw_close  ? raw_close(fd)     : close(fd); }
int  _unlink(const char *p)                  { return raw_unlink ? raw_unlink(p)     : unlink(p); }
int  _rename(const char *o, const char *n)
    { return raw_rename       ? raw_rename(o, n)          : rename(o, n); }
int  _renameat(int oldfd, const char *o, int newfd, const char *n)
    { return raw_renameat     ? raw_renameat(oldfd, o, newfd, n) : renameat(oldfd, o, newfd, n); }
int  _renamex_np(const char *f, const char *t, unsigned int fl)
    { return raw_renamex_np   ? raw_renamex_np(f, t, fl)       : renamex_np(f, t, fl); }
int  _renameatx_np(int ofd, const char *op, int nfd, const char *np, unsigned int fl)
    { return raw_renameatx_np ? raw_renameatx_np(ofd, op, nfd, np, fl) : renameatx_np(ofd, op, nfd, np, fl); }
int  _exchangedata(const char *p1, const char *p2, unsigned int o)
    { return raw_exchangedata ? raw_exchangedata(p1, p2, o)  : exchangedata(p1, p2, o); }
int  _copyfile(const char *s, const char *d, copyfile_state_t st, copyfile_flags_t fl)
    { return raw_copyfile     ? raw_copyfile(s, d, st, fl)   : copyfile(s, d, st, fl); }

/* ============================================================================
 * 内部工具函数
 * ============================================================================ */

static bool str_starts_with(const char *str, const char *prefix)
{
    if (!str || !prefix) return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static char *get_basename(const char *path)
{
    if (!path) return NULL;
    /* basename 可能修改参数，用栈上副本避免堆分配 */
    char copy[PATH_MAX];
    strncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    return strdup(basename(copy));
}

static char *get_dirname(const char *path)
{
    if (!path) return NULL;
    char copy[PATH_MAX];
    strncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    return strdup(dirname(copy));
}

/* ============================================================================
 * 路径判断
 * ============================================================================ */

bool is_path_in_sandbox(const char *path)
{
    if (!path) return false;
    return str_starts_with(path, SANDBOX_ROOT_PATH);
}

bool is_user_protected_path(const char *path)
{
    if (!path) return false;
    return str_starts_with(path, SANDBOX_USER_PREFIX) &&
           !str_starts_with(path, SANDBOX_FILES_PATH);
}

/* ============================================================================
 * 路径转换
 * ============================================================================ */

char *sandboxize_path(const char *user_path, char *buf, size_t buf_size)
{
    if (!user_path || !buf || buf_size == 0) return NULL;
    int n = snprintf(buf, buf_size, SANDBOX_FILES_PATH "%s", user_path);
    if (n < 0 || (size_t)n >= buf_size) return NULL;
    return buf;
}

char *trash_path(const char *original_path, char *buf, size_t buf_size)
{
    if (!original_path || !buf || buf_size == 0) return NULL;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime((time_t *)&tv.tv_sec);
    if (!tm_info) return NULL;
    char *filename = get_basename(original_path);
    if (!filename) return NULL;
    int n = snprintf(buf, buf_size,
                     SANDBOX_TRASH_PATH "/%04d-%02d-%02d_%02d%02d%02d_%s",
                     tm_info->tm_year + 1900, tm_info->tm_mon + 1,
                     tm_info->tm_mday, tm_info->tm_hour,
                     tm_info->tm_min, tm_info->tm_sec, filename);
    free(filename);
    if (n < 0 || (size_t)n >= buf_size) return NULL;
    return buf;
}

/* ============================================================================
 * 目录与文件操作
 * ============================================================================ */

int ensure_directory_exists(const char *path)
{
    if (!path) return -1;
    struct stat st;
    if (_stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (_mkdir(path, 0755) == 0) return 0;
    if (errno == ENOENT) {
        char *parent = get_dirname(path);
        if (!parent) return -1;
        int ret = ensure_directory_exists(parent);
        free(parent);
        if (ret == 0) return _mkdir(path, 0755);
    }
    return -1;
}

int ensure_parent_directory_exists(const char *file_path)
{
    if (!file_path) return -1;
    char *parent = get_dirname(file_path);
    if (!parent) return -1;
    int ret = ensure_directory_exists(parent);
    free(parent);
    return ret;
}

int copy_file_to_sandbox(const char *src_path, const char *dst_path)
{
    if (!src_path || !dst_path) return -1;
    if (ensure_parent_directory_exists(dst_path) != 0) return -1;

    /* macOS copyfile() — 内核级拷贝，保留元数据，比 read/write 循环快得多 */
    if (copyfile(src_path, dst_path, NULL, COPYFILE_DATA | COPYFILE_STAT) == 0)
        return 0;

    /* fallback: copyfile 失败时（如跨卷），退回 read/write */
    int src_fd = _open(src_path, O_RDONLY, 0);
    if (src_fd < 0) return -1;

    struct stat st;
    int sr = raw_stat ? raw_stat(src_path, &st) : stat(src_path, &st);
    if (sr < 0) { _close(src_fd); return -1; }

    int dst_fd = _open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, (int)st.st_mode);
    if (dst_fd < 0) { _close(src_fd); return -1; }

    char buf[65536];  /* 64KB，比 8KB 减少系统调用次数 */
    ssize_t nr;
    while ((nr = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t nw = 0;
        while (nw < nr) {
            ssize_t ret = write(dst_fd, buf + nw, (size_t)(nr - nw));
            if (ret < 0) { _close(src_fd); _close(dst_fd); _unlink(dst_path); return -1; }
            nw += ret;
        }
    }

    _close(src_fd);
    _close(dst_fd);
    return 0;
}
