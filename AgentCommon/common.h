/*
 * common.h
 * AgentSandbox - 公共定义和宏
 *
 * 供 AgentHook 和主应用共同使用的常量和宏定义
 *
 * Created by ConradSun on 2025/3/31.
 */

#ifndef AGENT_SANDBOX_COMMON_H
#define AGENT_SANDBOX_COMMON_H

/* ============================================================================
 * Socket IPC 配置
 * ============================================================================ */

/* Unix Domain Socket 路径 */
#define SANDBOX_SOCK_PATH    "/tmp/sandbox_audit.sock"

/* 消息最大长度 */
#define SANDBOX_MSG_MAX_LEN  4096

/* 连接复用时间（秒） */
#define SANDBOX_CONN_TTL     5

/* 发送超时（毫秒） */
#define SANDBOX_SEND_TIMEOUT 10

/* ============================================================================
 * 沙箱域路径配置
 * ============================================================================ */

/* 沙箱根目录 */
#define SANDBOX_ROOT_PATH       "/Volumes/AgentSandbox"

/* 文件重定向目录 */
#define SANDBOX_FILES_PATH      SANDBOX_ROOT_PATH "/Files"

/* 二进制重定向目录 */
#define SANDBOX_BINARIES_PATH    SANDBOX_ROOT_PATH "/Binaries"

/* 回收站目录 */
#define SANDBOX_TRASH_PATH      SANDBOX_ROOT_PATH "/Trash"

/* 用户目录前缀（需要保护的区域） */
#define SANDBOX_USER_PREFIX     "/Users/"

/* 系统二进制目录前缀（不需要重定向） */
#define SANDBOX_SYSTEM_USR_PATH "/usr/"
#define SANDBOX_SYSTEM_BIN_PATH "/bin/"

/* ============================================================================
 * 事件类型定义
 * ============================================================================ */

/* 文件操作事件 */
#define SANDBOX_EVENT_FILE    "FILE"

/* 网络操作事件 */
#define SANDBOX_EVENT_NETWORK "NETWORK"

/* 进程操作事件 */
#define SANDBOX_EVENT_PROCESS "PROCESS"

#endif /* AGENT_SANDBOX_COMMON_H */
