/*
 * hook_network.c
 * AgentHook
 *
 * 网络调用 Hook。通过 DYLD_INTERPOSE 拦截 socket 操作并上报审计事件。
 *
 * 递归防护：
 *   - IS_INSIDE_CALL guard：send_event 调用前设置 ENTER_INSIDE_CALL()，
 *     各 hook 函数检测到后直接调真实 libc 函数，绕过记录逻辑
 *   - hook 内调用 socket()/connect() 等时，由于 DYLD_INTERPOSE 不修改
 *     当前 dylib 内部符号解析，直接走到真实 libSystem 实现
 */

#include "hook_common.h"
#include "socket_client.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

static const char *fmt_addr(const struct sockaddr *addr, socklen_t addrlen,
                            char *buf, size_t bufsize)
{
    buf[0] = '\0';
    if (!addr) return buf;

    if (addr->sa_family == AF_INET && addrlen >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in4 = (const struct sockaddr_in *)addr;
        snprintf(buf, bufsize, "%s:%d", inet_ntoa(in4->sin_addr), ntohs(in4->sin_port));

    } else if (addr->sa_family == AF_INET6 && addrlen >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)addr;
        char addr_str[64] = {0};
        inet_ntop(AF_INET6, &in6->sin6_addr, addr_str, sizeof(addr_str));
        snprintf(buf, bufsize, "[%s]:%d", addr_str, ntohs(in6->sin6_port));

    } else if (addr->sa_family == AF_UNIX) {
        snprintf(buf, bufsize, "unix");
    }

    return buf;
}

/* ============================================================================
 * Hook 函数
 *
 * 每个 hook 模式：
 *   int hook_xxx(...) {
 *       hook_init_guard();
 *       if (IS_INSIDE_CALL()) return xxx(...);   // bypass
 *       ENTER_INSIDE_CALL();
 *       record_net_event(...);
 *       int r = xxx(...);
 *       EXIT_INSIDE_CALL();
 *       return r;
 *   }
 *
 * bypass 时直接调 xxx()：DYLD_INTERPOSE 不修改当前 dylib 内部调用，
 * 配合 IS_INSIDE_CALL=true，hook 函数直接返回，不产生递归。
 * ============================================================================ */

int hook_socket(int domain, int type, int protocol)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return socket(domain, type, protocol);

    char detail[64];
    snprintf(detail, sizeof(detail), "d=%d t=%d p=%d", domain, type, protocol);

    ENTER_INSIDE_CALL();
    record_net_event("socket", detail);
    int fd = socket(domain, type, protocol);
    EXIT_INSIDE_CALL();
    return fd;
}

int hook_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return connect(sockfd, addr, addrlen);

    char buf[256] = {0};
    ENTER_INSIDE_CALL();
    record_net_event("connect", fmt_addr(addr, addrlen, buf, sizeof(buf)));
    int r = connect(sockfd, addr, addrlen);
    EXIT_INSIDE_CALL();
    return r;
}

int hook_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return bind(sockfd, addr, addrlen);

    char buf[256] = {0};
    ENTER_INSIDE_CALL();
    record_net_event("bind", fmt_addr(addr, addrlen, buf, sizeof(buf)));
    int r = bind(sockfd, addr, addrlen);
    EXIT_INSIDE_CALL();
    return r;
}

int hook_listen(int sockfd, int backlog)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return listen(sockfd, backlog);

    ENTER_INSIDE_CALL();
    record_net_event("listen", NULL);
    int r = listen(sockfd, backlog);
    EXIT_INSIDE_CALL();
    return r;
}

int hook_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return accept(sockfd, addr, addrlen);

    int fd = accept(sockfd, addr, addrlen);
    if (fd >= 0) {
        char buf[256] = {0};
        ENTER_INSIDE_CALL();
        record_net_event("accept", fmt_addr(addr, addrlen ? *addrlen : 0, buf, sizeof(buf)));
        EXIT_INSIDE_CALL();
    }
    return fd;
}

ssize_t hook_send(int sockfd, const void *buf, size_t len, int flags)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return send(sockfd, buf, len, flags);

    ENTER_INSIDE_CALL();
    record_net_event("send", NULL);
    ssize_t r = send(sockfd, buf, len, flags);
    EXIT_INSIDE_CALL();
    return r;
}

ssize_t hook_recv(int sockfd, void *buf, size_t len, int flags)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return recv(sockfd, buf, len, flags);

    ENTER_INSIDE_CALL();
    record_net_event("recv", NULL);
    ssize_t r = recv(sockfd, buf, len, flags);
    EXIT_INSIDE_CALL();
    return r;
}

ssize_t hook_sendto(int sockfd, const void *buf, size_t len, int flags,
                    const struct sockaddr *addr, socklen_t addrlen)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return sendto(sockfd, buf, len, flags, addr, addrlen);

    char addrbuf[256] = {0};
    ENTER_INSIDE_CALL();
    record_net_event("sendto", addr ? fmt_addr(addr, addrlen, addrbuf, sizeof(addrbuf)) : NULL);
    ssize_t r = sendto(sockfd, buf, len, flags, addr, addrlen);
    EXIT_INSIDE_CALL();
    return r;
}

ssize_t hook_recvfrom(int sockfd, void *buf, size_t len, int flags,
                      struct sockaddr *addr, socklen_t *addrlen)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return recvfrom(sockfd, buf, len, flags, addr, addrlen);

    ENTER_INSIDE_CALL();
    record_net_event("recvfrom", NULL);
    ssize_t r = recvfrom(sockfd, buf, len, flags, addr, addrlen);
    EXIT_INSIDE_CALL();
    return r;
}

int hook_shutdown(int sockfd, int how)
{
    hook_init_guard();
    if (IS_INSIDE_CALL()) return shutdown(sockfd, how);

    ENTER_INSIDE_CALL();
    record_net_event("shutdown", NULL);
    int r = shutdown(sockfd, how);
    EXIT_INSIDE_CALL();
    return r;
}

/* ============================================================================
 * DYLD_INTERPOSE 注册
 * ============================================================================ */

DYLD_INTERPOSE(hook_socket,    socket)
DYLD_INTERPOSE(hook_connect,   connect)
DYLD_INTERPOSE(hook_bind,      bind)
DYLD_INTERPOSE(hook_listen,    listen)
DYLD_INTERPOSE(hook_accept,    accept)
DYLD_INTERPOSE(hook_send,      send)
DYLD_INTERPOSE(hook_recv,      recv)
DYLD_INTERPOSE(hook_sendto,    sendto)
DYLD_INTERPOSE(hook_recvfrom,  recvfrom)
DYLD_INTERPOSE(hook_shutdown,  shutdown)
