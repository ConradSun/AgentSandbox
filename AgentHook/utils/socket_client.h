/*
 * socket_client.h
 * AgentHook
 *
 * Unix Domain Socket IPC 客户端接口。
 *
 * 递归防护机制：
 *   - IS_INSIDE_CALL guard（pthread_key_t，定义在 socket_client.c）
 *   - send_event 调用前设置 ENTER_INSIDE_CALL()，hook 函数检测到后
 *     直接调用真实 libc 函数，绕过记录逻辑，不产生递归
 *   - hook_network.c 内调 socket() 时，由于 DYLD_INTERPOSE 不修改
 *     当前 dylib 内部符号解析，直接走到真实 libSystem 实现
 */

#ifndef SOCKET_CLIENT_H
#define SOCKET_CLIENT_H

#include <sys/socket.h>

void record_file_event(const char *op, const char *path);
void record_net_event(const char *op, const char *addr);
void record_proc_event(const char *op, const char *target);

#endif /* SOCKET_CLIENT_H */
