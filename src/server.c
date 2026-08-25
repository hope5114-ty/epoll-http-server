#include "server.h"
#include "epoll_handler.h"
#include "thread_pool.h"
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

// 服务器运行标志，信号处理会修改该变量
static volatile int running = 1;

// 信号处理函数
// sig：接收到的信号编号
// 捕获SIGINT、SIGTERM，实现服务器优雅退出
static void signal_handler(int sig)
{
    log_info("收到信号 %d，正在关闭服务器...", sig);
    running = 0;
}

// 注册信号处理器
// 忽略SIGPIPE，防止对端断开后write触发信号导致进程直接退出
static void setup_signals(void)
{
    signal(SIGINT, signal_handler);   // Ctrl+C触发
    signal(SIGTERM, signal_handler);  // kill命令触发
    signal(SIGPIPE, SIG_IGN);         // 忽略管道断裂信号
}

// 将fd设置为非阻塞模式
// fd：待设置的文件描述符
// return：成功返回0，失败返回-1
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl F_GETFL 失败");
        return -1;
    }

    // 追加O_NONBLOCK非阻塞标志
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl F_SETFL 失败");
        return -1;
    }

    return 0;
}

// 初始化TCP服务器
// config：服务器配置，包含端口等参数
// return：成功返回listen监听fd，失败返回-1
int server_init(const server_config_t *config)
{
    // 创建IPv4 TCP流式套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("创建socket失败");
        return -1;
    }
    log_info("创建socket成功，fd = %d", listen_fd);

    // 设置SO_REUSEADDR端口复用，避免重启被TIME_WAIT阻塞
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("设置SO_REUSEADDR失败");
        close(listen_fd);
        return -1;
    }

    // 填充服务端地址结构体
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;          // IPv4协议
    addr.sin_addr.s_addr = INADDR_ANY;  // 监听本机所有网卡
    addr.sin_port = htons(config->port); // 端口转为网络字节序

    // 绑定地址与端口
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("绑定端口失败");
        log_error("端口: %d", config->port);
        close(listen_fd);
        return -1;
    }
    log_info("绑定端口 %d 成功", config->port);

    // 开启监听，128为待处理连接队列上限
    if (listen(listen_fd, 128) == -1)
    {
        perror("监听失败");
        close(listen_fd);
        return -1;
    }
    log_info("服务器开始监听端口 %d", config->port);

    return listen_fd;
}

// 服务器主事件循环
// listen_fd：监听套接字
// config：服务器配置参数
void server_run(int listen_fd, const server_config_t *config)
{
    // 将监听socket设置非阻塞
    if (set_nonblocking(listen_fd) == -1)
    {
        close(listen_fd);
        return;
    }

    // 注册信号捕获
    setup_signals();

    // 创建epoll实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("epoll_create1 失败");
        close(listen_fd);
        return;
    }
    log_info("创建epoll实例成功，epoll_fd = %d", epoll_fd);

    // 初始化epoll连接管理模块
    if (epoll_handler_init(epoll_fd, config) == -1)
    {
        close(epoll_fd);
        close(listen_fd);
        return;
    }

    // 创建线程池，取配置线程数或默认值
    int thread_count = config->thread_count > 0 ? config->thread_count : DEFAULT_THREAD_COUNT;
    thread_pool_t *pool = thread_pool_create(thread_count);
    if (pool == NULL)
    {
        log_error("线程池创建失败");
        close(epoll_fd);
        close(listen_fd);
        return;
    }
    epoll_handler_set_pool(pool);

    // 将listen_fd加入epoll，边缘触发，监听可读事件
    if (epoll_add_fd(epoll_fd, listen_fd, EPOLLIN | EPOLLET) == -1)
    {
        close(epoll_fd);
        close(listen_fd);
        return;
    }

    log_info("服务器启动成功，工作线程数: %d，等待连接...", thread_count);

    struct epoll_event events[MAX_EVENTS];
    time_t last_timeout_check = time(NULL);

    // 主循环，running被信号修改用于退出
    while (running)
    {
        // epoll_wait等待IO事件，超时1000ms
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds == -1)
        {
            // 被信号打断，继续循环
            if (errno == EINTR)
            {
                continue;
            }
            perror("epoll_wait 失败");
            break;
        }

        // 遍历处理所有触发的事件
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            if (fd == listen_fd)
            {
                // 监听fd触发事件，处理新连接
                epoll_handler_accept(epoll_fd, listen_fd);
            }
            else
            {
                // 客户端fd事件，派发给线程池工作线程处理
                epoll_handler_dispatch(epoll_fd, fd);
            }
        }

        // 每秒执行一次超时连接检测
        time_t now = time(NULL);
        if (now - last_timeout_check >= 1)
        {
            epoll_handler_check_timeout(epoll_fd);
            last_timeout_check = now;
        }
    }

    // 服务器退出，按顺序释放资源
    log_info("正在关闭服务器...");
    thread_pool_destroy(pool);  // 先销毁线程池，等待工作线程全部退出
    epoll_handler_cleanup();    // 清理所有客户端连接
    close(epoll_fd);
    close(listen_fd);
    log_info("服务器已关闭");
}
