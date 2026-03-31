/*
 * socket_client.c
 * AgentSandbox - Unix Domain Socket IPC 客户端
 *
 * 纯 C 实现，无任何运行时依赖。
 * dylib 在进程初始化极早期（_libxpc_initializer 阶段）就会被触发，
 * 此时 Swift/ObjC/Foundation 均未就绪，只能使用 POSIX API。
 *
 * Created by ConradSun on 2025/3/31.
 */

#include "socket_client.h"
#include "common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

/* ============================================================================
 * 静态变量
 * ============================================================================ */

static int             sock_fd        = -1;                    /* Socket 文件描述符 */
static pthread_mutex_t sock_mutex     = PTHREAD_MUTEX_INITIALIZER;  /* 互斥锁 */
static time_t          last_conn_time = 0;                     /* 最后连接时间 */

/* ============================================================================
 * 私有函数
 * ============================================================================ */

/*
 * set_nonblocking
 * 设置 socket 为非阻塞模式
 *
 * @param fd Socket 文件描述符
 * @return 原始 flags，用于恢复
 */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return flags;
}

/*
 * set_blocking
 * 恢复 socket 为阻塞模式并设置发送超时
 *
 * @param fd    Socket 文件描述符
 * @param flags 原始 flags
 */
static void set_blocking(int fd, int flags)
{
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv = { .tv_sec = 0, .tv_usec = SANDBOX_SEND_TIMEOUT * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/*
 * wait_connect
 * 等待非阻塞 connect 完成
 *
 * @param fd Socket 文件描述符
 * @return 连接是否成功
 */
static int wait_connect(int fd)
{
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };

    if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0)
        return 0;

    int err = 0;
    socklen_t len = sizeof(err);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
}

/*
 * try_connect
 * 建立新的 Unix Domain Socket 连接
 *
 * @return 成功返回 socket fd，失败返回 -1
 */
static int try_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int flags = set_nonblocking(fd);

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, SANDBOX_SOCK_PATH, sizeof(addr.sun_path) - 1);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    int connected = (ret == 0) || (ret < 0 && errno == EINPROGRESS && wait_connect(fd));

    if (!connected) {
        close(fd);
        return -1;
    }

    set_blocking(fd, flags);
    return fd;
}

/*
 * get_conn
 * 获取可用连接，超过 TTL 则重连
 *
 * @return 有效的 socket fd 或 -1
 */
static int get_conn(void)
{
    pthread_mutex_lock(&sock_mutex);

    time_t now = time(NULL);
    if (sock_fd >= 0 && (now - last_conn_time) < SANDBOX_CONN_TTL) {
        pthread_mutex_unlock(&sock_mutex);
        return sock_fd;
    }

    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }

    sock_fd = try_connect();
    if (sock_fd >= 0)
        last_conn_time = now;

    pthread_mutex_unlock(&sock_mutex);
    return sock_fd;
}

/*
 * reset_conn
 * 关闭当前连接，下次调用 get_conn 时重连
 */
static void reset_conn(void)
{
    pthread_mutex_lock(&sock_mutex);
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
    pthread_mutex_unlock(&sock_mutex);
}

/*
 * send_event
 * 发送事件消息到 Socket Server
 *
 * 消息格式：TYPE|pid|timestamp|op|detail\n
 *
 * @param type   事件类型（"FILE", "NETWORK", "PROCESS"）
 * @param op     操作名称
 * @param detail 操作详情
 */
static void send_event(const char *type, const char *op, const char *detail)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double ts = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;

    char msg[SANDBOX_MSG_MAX_LEN];
    int len = snprintf(msg, sizeof(msg), "%s|%d|%.6f|%s|%s\n",
                       type, (int)getpid(), ts,
                       op     ? op     : "",
                       detail ? detail : "");

    if (len < 0 || len >= (int)sizeof(msg))
        return;

    int fd = get_conn();
    if (fd < 0)
        return;

    if (send(fd, msg, len, MSG_NOSIGNAL) < 0) {
        reset_conn();
        fd = get_conn();
        if (fd >= 0)
            send(fd, msg, len, MSG_NOSIGNAL);
    }
}

/* ============================================================================
 * 公开接口
 * ============================================================================ */

void record_file_event(const char *op, const char *path)
{
    send_event("FILE", op, path);
}

void record_net_event(const char *op, const char *addr)
{
    send_event("NETWORK", op, addr);
}

void record_proc_event(const char *op, const char *target)
{
    send_event("PROCESS", op, target);
}
