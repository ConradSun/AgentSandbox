/*
 * socket_client.h
 * AgentSandbox - Unix Domain Socket IPC 接口
 *
 * Created by ConradSun on 2025/3/31.
 */

#ifndef SOCKET_CLIENT_H
#define SOCKET_CLIENT_H

/*
 * record_file_event
 * 记录文件操作事件
 *
 * @param op   操作类型（如 "open", "read", "write"）
 * @param path 文件路径
 */
void record_file_event(const char *op, const char *path);

/*
 * record_net_event
 * 记录网络操作事件
 *
 * @param op   操作类型（如 "connect", "send", "recv"）
 * @param addr 网络地址信息
 */
void record_net_event(const char *op, const char *addr);

/*
 * record_proc_event
 * 记录进程操作事件
 *
 * @param op     操作类型（如 "execve", "fork"）
 * @param target 目标进程或路径
 */
void record_proc_event(const char *op, const char *target);

#endif /* SOCKET_CLIENT_H */
