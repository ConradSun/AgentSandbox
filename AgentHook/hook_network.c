/*
 * hook_network.c
 * AgentSandbox - 网络操作 Hook
 *
 * 通过 DYLD_INTERPOSE 拦截网络系统调用，记录审计事件。
 *
 * Created by ConradSun on 2025/3/31.
 */

#include "common.h"
#include "socket_client.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

/*
 * fmt_addr
 * 将 sockaddr 格式化为 "ip:port" 字符串
 *
 * @param addr    socket 地址结构
 * @param len     地址结构长度
 * @param buf     输出缓冲区
 * @param bufsize 缓冲区大小
 * @return 格式化后的地址字符串
 */
static const char *fmt_addr(const struct sockaddr *addr, socklen_t len,
                             char *buf, size_t bufsize)
{
    buf[0] = '\0';
    if (!addr)
        return buf;

    if (addr->sa_family == AF_INET && len >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
        snprintf(buf, bufsize, "%s:%d", inet_ntoa(in->sin_addr), ntohs(in->sin_port));
    } else if (addr->sa_family == AF_INET6 && len >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)addr;
        char tmp[64] = {0};
        inet_ntop(AF_INET6, &in6->sin6_addr, tmp, sizeof(tmp));
        snprintf(buf, bufsize, "[%s]:%d", tmp, ntohs(in6->sin6_port));
    } else if (addr->sa_family == AF_UNIX) {
        snprintf(buf, bufsize, "unix");
    }
    return buf;
}

/* ============================================================================
 * Hook 函数
 * ============================================================================ */

/*
 * hook_socket
 * 拦截 socket() 系统调用
 *
 * @param domain   协议族（AF_INET, AF_INET6 等）
 * @param type     socket 类型（SOCK_STREAM, SOCK_DGRAM 等）
 * @param protocol 协议类型
 * @return 原始 socket() 的返回值
 */
static int hook_socket(int domain, int type, int protocol)
{
    char detail[64];
    snprintf(detail, sizeof(detail), "d=%d t=%d p=%d", domain, type, protocol);
    record_net_event("socket", detail);
    return socket(domain, type, protocol);
}

/*
 * hook_connect
 * 拦截 connect() 系统调用
 *
 * @param sockfd   socket 文件描述符
 * @param addr     目标地址
 * @param addrlen  地址长度
 * @return 原始 connect() 的返回值
 */
static int hook_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    char buf[256] = {0};
    record_net_event("connect", fmt_addr(addr, addrlen, buf, sizeof(buf)));
    return connect(sockfd, addr, addrlen);
}

/*
 * hook_bind
 * 拦截 bind() 系统调用
 *
 * @param sockfd   socket 文件描述符
 * @param addr     绑定地址
 * @param addrlen  地址长度
 * @return 原始 bind() 的返回值
 */
static int hook_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    char buf[256] = {0};
    record_net_event("bind", fmt_addr(addr, addrlen, buf, sizeof(buf)));
    return bind(sockfd, addr, addrlen);
}

/*
 * hook_listen
 * 拦截 listen() 系统调用
 *
 * @param sockfd  socket 文件描述符
 * @param backlog 监听队列最大长度
 * @return 原始 listen() 的返回值
 */
static int hook_listen(int sockfd, int backlog)
{
    record_net_event("listen", NULL);
    return listen(sockfd, backlog);
}

/*
 * hook_accept
 * 拦截 accept() 系统调用
 *
 * @param sockfd   socket 文件描述符
 * @param addr     客户端地址（输出）
 * @param addrlen  地址长度指针
 * @return 原始 accept() 的返回值
 */
static int hook_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int fd = accept(sockfd, addr, addrlen);
    record_net_event("accept", NULL);
    return fd;
}

/*
 * hook_send
 * 拦截 send() 系统调用
 *
 * @param sockfd socket 文件描述符
 * @param buf    发送缓冲区
 * @param len    发送数据长度
 * @param flags  发送标志
 * @return 原始 send() 的返回值
 */
static ssize_t hook_send(int sockfd, const void *buf, size_t len, int flags)
{
    record_net_event("send", NULL);
    return send(sockfd, buf, len, flags);
}

/*
 * hook_recv
 * 拦截 recv() 系统调用
 *
 * @param sockfd socket 文件描述符
 * @param buf    接收缓冲区
 * @param len    缓冲区长度
 * @param flags  接收标志
 * @return 原始 recv() 的返回值
 */
static ssize_t hook_recv(int sockfd, void *buf, size_t len, int flags)
{
    record_net_event("recv", NULL);
    return recv(sockfd, buf, len, flags);
}

/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_socket,  socket)
DYLD_INTERPOSE(hook_connect, connect)
DYLD_INTERPOSE(hook_bind,    bind)
DYLD_INTERPOSE(hook_listen,  listen)
DYLD_INTERPOSE(hook_accept,  accept)
DYLD_INTERPOSE(hook_send,    send)
DYLD_INTERPOSE(hook_recv,    recv)
