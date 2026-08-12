#include "epoll_handler.h"
#include "http_parser.h"
#include "http_request.h"
#include "http_response.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// 全局连接池，使用fd作为数组下标
static client_connection_t *connections[MAX_EVENTS];
static const server_config_t *g_config = NULL;

// 辅助函数

// 将socket设置为非阻塞模式
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 初始化连接管理模块
int epoll_handler_init(int epoll_fd, const server_config_t *config) {
    (void)epoll_fd;
    // 清空连接池
    for (int i = 0; i < MAX_EVENTS; i++) {
        connections[i] = NULL;
    }
    g_config = config;
    return 0;
}

// 清理所有连接资源
void epoll_handler_cleanup(void) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (connections[i] != NULL) {
            close(connections[i]->fd);
            free(connections[i]);
            connections[i] = NULL;
        }
    }
}

// epoll基础操作

// 向epoll实例添加监听fd
// epoll_fd：epoll实例fd
// fd：待监听文件描述符
// events：监听事件掩码
// return：成功返回0，失败返回-1
int epoll_add_fd(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl ADD 失败");
        return -1;
    }
    return 0;
}

// 修改已有fd的监听事件
int epoll_modify_fd(int epoll_fd, int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl MOD 失败");
        return -1;
    }
    return 0;
}

// 将fd从epoll监听列表移除
int epoll_remove_fd(int epoll_fd, int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl DEL 失败");
        return -1;
    }
    return 0;
}

// 事件处理函数

// 处理新连接，ET模式下循环accept直至无新连接
// epoll_fd：epoll实例
// listen_fd：服务器监听套接字
void epoll_handler_accept(int epoll_fd, int listen_fd) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("accept 失败");
            break;
        }

        // 设置客户端fd非阻塞
        if (set_nonblocking(client_fd) == -1) {
            close(client_fd);
            continue;
        }

        // 检查fd是否超出连接池上限
        if (client_fd >= MAX_EVENTS) {
            log_error("连接数超过限制: fd=%d >= %d", client_fd, MAX_EVENTS);
            close(client_fd);
            continue;
        }

        // 分配连接结构体
        client_connection_t *conn = (client_connection_t *)malloc(sizeof(client_connection_t));
        if (conn == NULL) {
            log_error("分配连接内存失败");
            close(client_fd);
            continue;
        }

        // 初始化连接信息
        conn->fd = client_fd;
        conn->read_pos = 0;
        conn->write_pos = 0;
        conn->write_len = 0;
        conn->last_active = time(NULL);
        conn->keep_alive = 1;
        memset(conn->read_buf, 0, BUFFER_SIZE);
        memset(conn->write_buf, 0, BUFFER_SIZE * 4);

        connections[client_fd] = conn;

        // 将客户端fd加入epoll，边缘触发监听读写事件
        if (epoll_add_fd(epoll_fd, client_fd, EPOLLIN | EPOLLOUT | EPOLLET) == -1) {
            close(client_fd);
            free(conn);
            connections[client_fd] = NULL;
            continue;
        }

        log_info("新连接: fd=%d, ip=%s, port=%d",
                 client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }
}

// 处理客户端可读事件，循环读取全部请求数据
void epoll_handler_read(int epoll_fd, int client_fd) {
    client_connection_t *conn = connections[client_fd];
    if (conn == NULL) return;

    conn->last_active = time(NULL);

    while (1) {
        int space_left = BUFFER_SIZE - conn->read_pos - 1;
        if (space_left <= 0) {
            log_error("fd=%d 读缓冲区已满", client_fd);
            epoll_handler_close(epoll_fd, client_fd);
            return;
        }

        int n = read(client_fd, conn->read_buf + conn->read_pos, space_left);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("read 失败");
            epoll_handler_close(epoll_fd, client_fd);
            return;
        }
        if (n == 0) {
            log_info("fd=%d 客户端关闭连接", client_fd);
            epoll_handler_close(epoll_fd, client_fd);
            return;
        }

        conn->read_pos += n;
        conn->read_buf[conn->read_pos] = '\0';
    }

    // 读取完成，解析HTTP请求
    if (conn->read_pos > 0) {
        http_request_t request;
        if (http_parse_request(conn->read_buf, conn->read_pos, &request) == 0) {
            http_handle_request(&request, conn, g_config);
            // 清空读缓冲区，支持长连接复用
            conn->read_pos = 0;
            memset(conn->read_buf, 0, BUFFER_SIZE);
            if (conn->write_len > 0) {
                epoll_modify_fd(epoll_fd, client_fd, EPOLLOUT | EPOLLET);
            }
        } else {
            log_error("fd=%d HTTP请求解析失败", client_fd);
            http_send_error(conn, 400, "Bad Request");
            epoll_modify_fd(epoll_fd, client_fd, EPOLLOUT | EPOLLET);
        }
    }
}

// 处理客户端可写事件，循环发送缓冲区响应数据
void epoll_handler_write(int epoll_fd, int client_fd) {
    client_connection_t *conn = connections[client_fd];
    if (conn == NULL) return;

    conn->last_active = time(NULL);

    while (conn->write_pos < conn->write_len) {
        int n = write(client_fd, conn->write_buf + conn->write_pos,
                      conn->write_len - conn->write_pos);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            perror("write 失败");
            epoll_handler_close(epoll_fd, client_fd);
            return;
        }
        conn->write_pos += n;
    }

    log_info("fd=%d 响应发送完成，大小: %d 字节", client_fd, conn->write_len);
    // 清空写缓冲区
    conn->write_pos = 0;
    conn->write_len = 0;
    memset(conn->write_buf, 0, BUFFER_SIZE * 4);

    if (!conn->keep_alive) {
        epoll_handler_close(epoll_fd, client_fd);
    } else {
        // 长连接，切换回监听可读事件
        epoll_modify_fd(epoll_fd, client_fd, EPOLLIN | EPOLLET);
    }
}

// 关闭指定客户端连接，释放资源并移除epoll监听
void epoll_handler_close(int epoll_fd, int client_fd) {
    client_connection_t *conn = connections[client_fd];
    if (conn == NULL) return;

    epoll_remove_fd(epoll_fd, client_fd);
    close(client_fd);
    free(conn);
    connections[client_fd] = NULL;
    log_info("fd=%d 连接已关闭", client_fd);
}

// 遍历所有连接，关闭超出空闲超时的连接
void epoll_handler_check_timeout(int epoll_fd) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_EVENTS; i++) {
        client_connection_t *conn = connections[i];
        if (conn == NULL) continue;
        if (now - conn->last_active > CONNECTION_TIMEOUT) {
            log_info("fd=%d 连接超时，关闭", i);
            epoll_handler_close(epoll_fd, i);
        }
    }
}