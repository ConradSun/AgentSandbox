/*
 * path_utils.h
 * AgentHook
 *
 * 路径判断与转换工具
 */

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <copyfile.h>

/* ============================================================================
 * 路径判断
 * ============================================================================ */

bool is_user_protected_path(const char *path);
bool is_path_in_sandbox(const char *path);

/* ============================================================================
 * 路径转换
 * ============================================================================ */

char *sandboxize_path(const char *user_path, char *buf, size_t buf_size);
char *trash_path(const char *original_path, char *buf, size_t buf_size);

/* ============================================================================
 * 目录与文件操作
 * ============================================================================ */

int ensure_directory_exists(const char *path);
int ensure_parent_directory_exists(const char *file_path);
int copy_file_to_sandbox(const char *src_path, const char *dst_path);

/* ============================================================================
 * 原始系统调用包装（绕过 interpose）
 *
 * 原理：DYLD_INTERPOSE 替换的是不带 __ 前缀的符号，
 * dlsym(RTLD_NOLOAD, "__xxx") 可直接拿到 libc 真实实现。
 * 用于 hook 内部调用，避免触发递归。
 * ============================================================================ */

int  _stat(const char *path, struct stat *buf);
int  _lstat(const char *path, struct stat *buf);
int  _access(const char *path, int mode);
int  _mkdir(const char *path, mode_t mode);
int  _rmdir(const char *path);
int  _open(const char *path, int flags, ...);
int  _close(int fd);
int  _unlink(const char *path);
int  _rename(const char *oldpath, const char *newpath);
int  _renameat(int oldfd, const char *oldpath, int newfd, const char *newpath);
int  _renamex_np(const char *from, const char *to, unsigned int flags);
int  _renameatx_np(int oldfd, const char *oldpath, int newfd, const char *newpath, unsigned int flags);
int  _exchangedata(const char *path1, const char *path2, unsigned int options);
int  _copyfile(const char *src, const char *dst, copyfile_state_t state, copyfile_flags_t flags);

#endif /* PATH_UTILS_H */
