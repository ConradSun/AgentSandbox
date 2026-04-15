/*
 * hook_common.h
 * AgentHook - 公共头文件
 *
 * IS_INSIDE_CALL 递归防护：
 *   - pthread_key_t 由 socket_client.c 在库加载时创建
 *   - 所有 hook 模块共用在同一 key，确保 send_event 内部的 socket/open 调用
 *     不会再次进入 hook 记录逻辑
 *
 * Include 顺序：common.h → hook_common.h → 其他 hook 模块
 */

#ifndef HOOK_COMMON_H
#define HOOK_COMMON_H

#include "common.h"

#include <pthread.h>

/* ============================================================================
 * DYLD_INTERPOSE 宏
 * ============================================================================ */

#ifndef DYLD_INTERPOSE
#define DYLD_INTERPOSE(_replacement, _replacee) \
    __attribute__((used)) static struct { const void *r; const void *o; } \
        _interpose_##_replacee \
    __attribute__ ((section ("__DATA,__interpose"))) = { \
        (const void *)(unsigned long)&_replacement, \
        (const void *)(unsigned long)&_replacee };
#endif

/* ============================================================================
 * IS_INSIDE_CALL — 递归防护（socket_client.c 统一初始化）
 *
 * 每个 hook 函数用法：
 *   hook_init_guard();
 *   if (IS_INSIDE_CALL()) return xxx(...);  // bypass，不进记录逻辑
 *   ENTER_INSIDE_CALL();
 *   ... record_xxx() ...
 *   EXIT_INSIDE_CALL();
 * ============================================================================ */

extern pthread_key_t g_inside_call_key;
extern volatile int g_guard_inited;

static void _init_guard_key(void)
{
    pthread_key_create(&g_inside_call_key, NULL);
    __sync_synchronize();  /* 确保 key 写入对其他线程可见 */
}

static inline void hook_init_guard(void)
{
    if (__builtin_expect(!g_guard_inited, 0)) {
        static pthread_once_t once = PTHREAD_ONCE_INIT;
        pthread_once(&once, _init_guard_key);
        g_guard_inited = 1;
    }
}

#define ENTER_INSIDE_CALL()  pthread_setspecific(g_inside_call_key, (void *)1)
#define EXIT_INSIDE_CALL()   pthread_setspecific(g_inside_call_key, (void *)0)
#define IS_INSIDE_CALL()     (pthread_getspecific(g_inside_call_key) != NULL)

#endif /* HOOK_COMMON_H */
