/*
 * socket_client.c
 * AgentHook
 *
 * Unix Domain Socket IPC 客户端，纯 C 实现。
 *
 * 递归防护：
 *   - IS_INSIDE_CALL guard（pthread_key_t）：send_event 进入时设置为 true，
 *     所有 hook 函数检测到后直接调用真实 libc 函数，绕过记录逻辑
 *   - hook_network.c 内调用 socket() 等时，DYLD_INTERPOSE 不修改当前 dylib
 *     内部符号解析，直接走到真实 libSystem 实现，配合 IS_INSIDE_CALL 确保无递归
 */

#include "socket_client.h"
#include "common.h"
#include "hook_common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>

/* ============================================================================
 * IS_INSIDE_CALL guard（hook_common.h 声明，供所有模块共享）
 * ============================================================================ */

pthread_key_t g_inside_call_key;
int g_guard_inited = 0;

/* ============================================================================
 * 连接状态（原子操作，无锁）
 * ============================================================================ */

static int32_t g_sock_fd   = -1;
static int32_t g_conn_time = 0;

/* ============================================================================
 * 私有函数
 * ============================================================================ */

static int try_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int saved_flags = fcntl(fd, F_GETFL, 0);
    if (saved_flags < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, saved_flags | O_NONBLOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SANDBOX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    int connected = 0;
    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == 0) {
        connected = 1;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 2000 };
        int n = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (n > 0) {
            int err = 0;
            socklen_t err_len = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) == 0 && err == 0)
                connected = 1;
        }
    }

    if (!connected) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, saved_flags & ~O_NONBLOCK);

    struct timeval send_tv = { .tv_sec = 0, .tv_usec = SANDBOX_SEND_TIMEOUT * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));

    return fd;
}

static int connect_if_needed(void)
{
    int32_t now = (int32_t)time(NULL);
    int32_t fd = g_sock_fd;

    if (fd >= 0 && (now - g_conn_time) < SANDBOX_CONN_TTL)
        return fd;

    fd = try_connect();
    if (fd < 0) return -1;

    int32_t old = g_sock_fd;
    do {
        if (old >= 0) close(old);
    } while (!__sync_bool_compare_and_swap((volatile int32_t *)&g_sock_fd, old, fd));

    g_conn_time = now;
    return fd;
}

static int send_all(int fd, const char *msg, int len)
{
    int sent = 0;
    while (sent < len) {
        ssize_t n = sendto(fd, msg + sent, len - sent, MSG_NOSIGNAL, NULL, 0);
        if (n <= 0) return -1;
        sent += (int)n;
    }
    return 0;
}

/* ============================================================================
 * 发送事件
 * ============================================================================ */

static void send_event(const char *type, const char *op, const char *detail)
{
    char msg[SANDBOX_MSG_MAX_LEN];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double ts = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
    int len = snprintf(msg, sizeof(msg), "%s|%d|%.6f|%s|%s\n",
                       type, (int)getpid(), ts,
                       op     ? op     : "",
                       detail ? detail : "");

    if (len <= 0 || len >= (int)sizeof(msg)) return;

    ENTER_INSIDE_CALL();
    int fd = connect_if_needed();
    if (fd >= 0) {
        int r = send_all(fd, msg, len);
        if (r < 0) {
            /* 连接断开（Agent App 重启/崩溃），强制重连 */
            int32_t old = g_sock_fd;
            __sync_bool_compare_and_swap((volatile int32_t *)&g_sock_fd, old, -1);
            close(old);
            fd = connect_if_needed();
            if (fd >= 0) send_all(fd, msg, len);
        }
    }
    EXIT_INSIDE_CALL();
}

/* ============================================================================
 * 公开接口
 * ============================================================================ */

void record_file_event(const char *op, const char *path)
{
    send_event(SANDBOX_EVENT_FILE, op, path);
}

void record_net_event(const char *op, const char *addr)
{
    send_event(SANDBOX_EVENT_NETWORK, op, addr);
}

void record_proc_event(const char *op, const char *target)
{
    send_event(SANDBOX_EVENT_PROCESS, op, target);
}
