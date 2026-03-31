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
 * 事件类型定义
 * ============================================================================ */

/* 文件操作事件 */
#define SANDBOX_EVENT_FILE    "FILE"

/* 网络操作事件 */
#define SANDBOX_EVENT_NETWORK "NETWORK"

/* 进程操作事件 */
#define SANDBOX_EVENT_PROCESS "PROCESS"

/* ============================================================================
 * DYLD_INTERPOSE 宏
 * ============================================================================ */

/*
 * DYLD_INTERPOSE
 * 替换系统函数的宏，用于 dylib 注入时拦截系统调用
 */
#define DYLD_INTERPOSE(_new, _old) \
    __attribute__((used)) \
    static struct { const void *r; const void *o; } \
    _interpose_##_old \
    __attribute__((section("__DATA,__interpose"))) = { \
        (const void *)(unsigned long)&_new, \
        (const void *)(unsigned long)&_old  \
    };

#endif /* AGENT_SANDBOX_COMMON_H */
