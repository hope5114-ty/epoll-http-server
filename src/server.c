#include "server.h"
#include "epoll_handler.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

// 全局变量
static volatile int running = 1;  // 服务器运行标志

// 信号处理相关

// 信号回调函数，捕获SIGINT、SIGTERM实现优雅退出
// sig：触发的信号编号
static void signal_handler(int sig) {
    log_info("收到信号 %d，正在关闭服务器...", sig);
    running = 0;
}

// 注册信号处理函数
// 捕获Ctrl+C、kill终止信号；忽略SIGPIPE，防止客户端断开造成进程退出
static void setup_signals(void) {
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill终止信号
    signal(SIGPIPE, SIG_IGN);         // 忽略管道破裂信号
}

// 服务器初始化工具函数

// 将fd设置为非阻塞IO模式
// 使用fcntl获取原有标志，追加O_NONBLOCK，配合epoll实现并发
// fd：待设置的文件描述符
// return：成功返回0，失败返回-1
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL 失败");
        return -1;
    }

    // 添加非阻塞标志
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL 失败");
        return -1;
    }

    return 0;
}

// 初始化TCP监听socket
// 流程：创建socket → 开启端口复用 → bind → listen
// config：服务器配置参数
// return：成功返回监听fd，失败返回-1
int server_init(const server_config_t *config) {
    // 创建IPv4 TCP套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("创建socket失败");
        return -1;
    }
    log_info("创建socket成功，fd = %d", listen_fd);

    // 开启SO_REUSEADDR，允许快速重启复用端口
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("设置SO_REUSEADDR失败");
        close(listen_fd);
        return -1;
    }

    // 填充服务器地址结构，监听0.0.0.0
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config->port);

    // 绑定端口
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("绑定端口失败");
        log_error("端口: %d", config->port);
        close(listen_fd);
        return -1;
    }
    log_info("绑定端口 %d 成功", config->port);

    // 开启监听，等待队列长度128
    if (listen(listen_fd, 128) == -1) {
        perror("监听失败");
        close(listen_fd);
        return -1;
    }
    log_info("服务器开始监听端口 %d", config->port);

    return listen_fd;
}

// 服务器主事件循环
// 创建epoll实例，注册监听fd，循环epoll_wait处理连接事件
// listen_fd：监听套接字
// config：服务器配置
void server_run(int listen_fd, const server_config_t *config) {
    // 监听socket设置为非阻塞
    if (set_nonblocking(listen_fd) == -1) {
        close(listen_fd);
        return;
    }

    // 注册信号处理
    setup_signals();

    // 创建epoll实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 失败");
        close(listen_fd);
        return;
    }
    log_info("创建epoll实例成功，epoll_fd = %d", epoll_fd);

    // 初始化epoll连接管理模块
    if (epoll_handler_init(epoll_fd, config) == -1) {
        close(epoll_fd);
        close(listen_fd);
        return;
    }

    // 将监听fd加入epoll，边缘触发，监听可读事件
    if (epoll_add_fd(epoll_fd, listen_fd, EPOLLIN | EPOLLET) == -1) {
        close(epoll_fd);
        close(listen_fd);
        return;
    }

    log_info("服务器启动成功，等待连接...");

    struct epoll_event events[MAX_EVENTS];
    time_t last_timeout_check = time(NULL);

    // 主循环
    while (running) {
        // 等待IO事件，超时1000ms
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds == -1) {
            if (errno == EINTR) {
                // 被信号中断，继续循环
                continue;
            }
            perror("epoll_wait 失败");
            break;
        }

        // 遍历处理所有就绪事件
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                // 监听到新连接
                epoll_handler_accept(epoll_fd, listen_fd);
            } else {
                // 客户端连接事件
                if (events[i].events & EPOLLIN) {
                    epoll_handler_read(epoll_fd, fd);
                }
                if (events[i].events & EPOLLOUT) {
                    epoll_handler_write(epoll_fd, fd);
                }
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    epoll_handler_close(epoll_fd, fd);
                }
            }
        }

        // 每秒执行一次连接超时清理
        time_t now = time(NULL);
        if (now - last_timeout_check >= 1) {
            epoll_handler_check_timeout(epoll_fd);
            last_timeout_check = now;
        }
    }

    // 退出，释放资源
    log_info("正在关闭服务器...");
    epoll_handler_cleanup();
    close(epoll_fd);
    close(listen_fd);
    log_info("服务器已关闭");
}