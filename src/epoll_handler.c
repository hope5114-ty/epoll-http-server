#include "epoll_handler.h"
#include "http_parser.h"
#include "http_request.h"
#include "http_response.h"
#include "utils.h"
#include "thread_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <strings.h>
#include <time.h>

// 全局连接池，使用fd作为数组下标存储客户端连接上下文
static client_connection_t *connections[MAX_EVENTS];
// 全局服务配置
static const server_config_t *g_config = NULL;
// 线程池实例
static thread_pool_t *g_pool = NULL;

// 保护connections[]数组的互斥锁
// epoll_ctl本身线程安全，连接数组多线程读写必须加锁
static pthread_mutex_t connections_mutex = PTHREAD_MUTEX_INITIALIZER;

// 工作线程任务参数，传递epoll_fd与客户端fd
typedef struct
{
    int epoll_fd;
    int client_fd;
} work_arg_t;

// 将socket设置为非阻塞模式
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 检查HTTP请求报文是否接收完整
// buf:接收缓冲区，len:已读字节数，out_content_length:输出解析得到的Content‑Length
// 返回1请求完整，0请求不完整(TCP半包/粘包)
static int is_request_complete(const char *buf, int len, int *out_content_length)
{
    // 查找请求头结束标记\r\n\r\n
    const char *header_end = strstr(buf, "\r\n\r\n");
    if (header_end == NULL)
    {
        return 0;
    }
    int header_len = (int)(header_end - buf) + 4;
    int content_length = 0;

    // 解析Content‑Length请求头
    const char *p = buf;
    while (p < header_end)
    {
        if (strncasecmp(p, "Content‑Length:", 15) == 0)
        {
            p += 15;
            while (*p == ' ' || *p == '\t')
                p++;
            content_length = atoi(p);
            break;
        }
        const char *crlf = strstr(p, "\r\n");
        if (crlf == NULL)
            break;
        p = crlf + 2;
    }
    if (out_content_length)
    {
        *out_content_length = content_length;
    }
    // 判断body是否全部接收完成
    if (len < header_len + content_length)
    {
        return 0;
    }
    return 1;
}

// 初始化连接管理模块，清空连接池数组
int epoll_handler_init(int epoll_fd, const server_config_t *config)
{
    (void)epoll_fd;
    for (int i = 0; i < MAX_EVENTS; i++)
    {
        connections[i] = NULL;
    }
    g_config = config;
    return 0;
}

// 设置全局线程池实例
void epoll_handler_set_pool(void *pool)
{
    g_pool = (thread_pool_t *)pool;
}

// 清理所有连接资源，服务退出时调用，工作线程已退出无需加锁
void epoll_handler_cleanup(void)
{
    for (int i = 0; i < MAX_EVENTS; i++)
    {
        if (connections[i] != NULL)
        {
            close(connections[i]->fd);
            free(connections[i]);
            connections[i] = NULL;
        }
    }
}

// ==================== epoll基础操作 ====================

// 向epoll实例添加监听fd
// epoll_fd：epoll实例fd
// fd：待监听文件描述符
// events：监听事件掩码
// return：成功返回0，失败返回-1
int epoll_add_fd(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl ADD 失败");
        return -1;
    }
    return 0;
}

// 修改已有fd的监听事件
int epoll_modify_fd(int epoll_fd, int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        perror("epoll_ctl MOD 失败");
        return -1;
    }
    return 0;
}

// 将fd从epoll监听列表移除
int epoll_remove_fd(int epoll_fd, int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        perror("epoll_ctl DEL 失败");
        return -1;
    }
    return 0;
}

// ==================== 事件处理函数 ====================

// 处理新连接，ET模式下循环accept直至无新连接
// epoll_fd：epoll实例
// listen_fd：服务器监听套接字
void epoll_handler_accept(int epoll_fd, int listen_fd)
{
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1)
        {
            // ET模式，已经没有待接受连接
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            perror("accept 失败");
            break;
        }

        // 设置客户端fd非阻塞
        if (set_nonblocking(client_fd) == -1)
        {
            close(client_fd);
            continue;
        }

        // 关闭Nagle算法，降低小包延迟
        int opt = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        // 检查fd是否超出连接池上限
        if (client_fd >= MAX_EVENTS)
        {
            log_error("连接数超过限制: fd=%d >= %d", client_fd, MAX_EVENTS);
            close(client_fd);
            continue;
        }

        // 分配连接结构体
        client_connection_t *conn = (client_connection_t *)malloc(sizeof(client_connection_t));
        if (conn == NULL)
        {
            log_error("分配连接内存失败");
            close(client_fd);
            continue;
        }

        // 初始化连接信息
        conn->fd = client_fd;
        conn->in_use = 0;
        conn->read_pos = 0;
        conn->write_pos = 0;
        conn->write_len = 0;
        conn->last_active = time(NULL);
        conn->keep_alive = 1;
        memset(conn->read_buf, 0, BUFFER_SIZE);
        memset(conn->write_buf, 0, BUFFER_SIZE * 4);

        // 加锁存入连接池
        pthread_mutex_lock(&connections_mutex);
        connections[client_fd] = conn;
        pthread_mutex_unlock(&connections_mutex);

        // 将客户端fd加入epoll
        // EPOLLONESHOT：触发一次事件后自动禁用fd，保证同一fd只被一个线程处理
        if (epoll_add_fd(epoll_fd, client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP) == -1)
        {
            pthread_mutex_lock(&connections_mutex);
            close(client_fd);
            free(conn);
            connections[client_fd] = NULL;
            pthread_mutex_unlock(&connections_mutex);
            continue;
        }

        log_info("新连接: fd=%d, ip=%s, port=%d",
                 client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }
}

static void worker_process_request(void *arg);

// 主线程派发任务：把客户端IO任务交给线程池
void epoll_handler_dispatch(int epoll_fd, int client_fd)
{
    if (g_pool == NULL)
    {
        log_error("线程池未初始化");
        epoll_handler_close(epoll_fd, client_fd);
        return;
    }
    work_arg_t *arg = (work_arg_t *)malloc(sizeof(work_arg_t));
    if (arg == NULL)
    {
        log_error("分配任务参数失败");
        pthread_mutex_lock(&connections_mutex);
        epoll_handler_close(epoll_fd, client_fd);
        pthread_mutex_unlock(&connections_mutex);
        return;
    }
    arg->epoll_fd = epoll_fd;
    arg->client_fd = client_fd;
    if (thread_pool_add_task(g_pool, worker_process_request, arg) != 0)
    {
        log_error("添加线程池任务失败");
        free(arg);
        pthread_mutex_lock(&connections_mutex);
        epoll_handler_close(epoll_fd, client_fd);
        pthread_mutex_unlock(&connections_mutex);
    }
}

// 工作线程执行HTTP请求处理任务
// 流程：循环读socket → 判断报文完整 → 解析HTTP → 业务处理 → 写响应 → keep‑alive重激活/关闭连接
static void worker_process_request(void *arg)
{
    work_arg_t *work_arg = (work_arg_t *)arg;
    int epoll_fd = work_arg->epoll_fd;
    int client_fd = work_arg->client_fd;
    free(work_arg);

    // 加锁获取连接指针，标记in_use，表示正在被工作线程处理
    pthread_mutex_lock(&connections_mutex);
    client_connection_t *conn = connections[client_fd];
    if (conn == NULL)
    {
        pthread_mutex_unlock(&connections_mutex);
        return;
    }
    conn->in_use = 1;
    pthread_mutex_unlock(&connections_mutex);

    // 更新连接最后活跃时间
    conn->last_active = time(NULL);

    int close_conn = 0;
    // ET边沿触发，循环read直到EAGAIN，读完内核缓冲区全部数据
    while (1)
    {
        int space_left = BUFFER_SIZE - conn->read_pos - 1;
        if (space_left <= 0)
        {
            log_error("fd=%d 读缓冲区已满", client_fd);
            close_conn = 1;
            break;
        }

        int n = read(client_fd, conn->read_buf + conn->read_pos, space_left);
        if (n == -1)
        {
            // 非阻塞socket，数据全部读取完毕
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            perror("read 失败");
            close_conn = 1;
            break;
        }
        // read返回0代表对端关闭socket
        if (n == 0)
        {
            log_info("fd=%d 客户端关闭连接", client_fd);
            close_conn = 1;
            break;
        }
        conn->read_pos += n;
        conn->read_buf[conn->read_pos] = '\0';
    }

    // IO异常或客户端关闭，直接清理连接
    if (close_conn)
    {
        pthread_mutex_lock(&connections_mutex);
        epoll_handler_close(epoll_fd, client_fd);
        pthread_mutex_unlock(&connections_mutex);
        return;
    }

    // 检查HTTP请求是否完整，处理TCP半包粘包场景
    int content_length = 0;
    if (!is_request_complete(conn->read_buf, conn->read_pos, &content_length))
    {
        log_info("fd=%d 请求不完整（%d字节），等待更多数据", client_fd, conn->read_pos);
        conn->in_use = 0;
        // 重新激活EPOLLIN，等待后续报文到达
        epoll_modify_fd(epoll_fd, client_fd,
                        EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
        return;
    }

    // 解析HTTP请求报文
    http_request_t request;
    if (http_parse_request(conn->read_buf, conn->read_pos, &request) != 0)
    {
        log_error("fd=%d HTTP请求解析失败", client_fd);
        http_send_error(conn, 400, "Bad Request");
    }
    else
    {
        // 根据请求头更新keep‑alive状态
        conn->keep_alive = request.keep_alive;
        http_handle_request(&request, conn, g_config);
    }
    // 释放请求内部动态分配资源
    http_request_cleanup(&request);

    // 发送HTTP响应回客户端
    if (conn->write_len > 0)
    {
        int total = conn->write_len;
        int sent = 0;
        int retry = 0;
        while (sent < total)
        {
            int n = write(client_fd, conn->write_buf + sent, total - sent);
            if (n == -1)
            {
                // 内核发送缓冲区满，简单重试机制，仅适配小响应
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (++retry > 1000)
                    {
                        log_error("fd=%d 发送重试超限，关闭连接", client_fd);
                        close_conn = 1;
                        break;
                    }
                    usleep(1000);
                    continue;
                }
                perror("write 失败");
                close_conn = 1;
                break;
            }
            sent += n;
            retry = 0;
        }
        if (!close_conn)
        {
            log_info("fd=%d 响应发送完成，大小: %d 字节", client_fd, total);
        }
    }

    // 清空写缓冲区
    conn->write_pos = 0;
    conn->write_len = 0;
    memset(conn->write_buf, 0, BUFFER_SIZE * 4);

    pthread_mutex_lock(&connections_mutex);
    if (close_conn || !conn->keep_alive)
    {
        // 需要关闭连接，释放资源
        epoll_handler_close(epoll_fd, client_fd);
    }
    else
    {
        // keep‑alive长连接：清空读缓冲区，重新注册epoll事件等待下一次http请求
        conn->read_pos = 0;
        memset(conn->read_buf, 0, BUFFER_SIZE);
        conn->in_use = 0;
        epoll_modify_fd(epoll_fd, client_fd,
                        EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
    }
    pthread_mutex_unlock(&connections_mutex);
}

// 关闭指定客户端连接，释放资源并移除epoll监听
// 调用方需要持有connections_mutex锁
void epoll_handler_close(int epoll_fd, int client_fd)
{
    client_connection_t *conn = connections[client_fd];
    if (conn == NULL)
        return;

    epoll_remove_fd(epoll_fd, client_fd);
    close(client_fd);
    free(conn);
    connections[client_fd] = NULL;
    log_info("fd=%d 连接已关闭", client_fd);
}

// 遍历所有连接，关闭超出空闲超时的连接，主线程定时调用
void epoll_handler_check_timeout(int epoll_fd)
{
    time_t now = time(NULL);
    pthread_mutex_lock(&connections_mutex);
    for (int i = 0; i < MAX_EVENTS; i++)
    {
        client_connection_t *conn = connections[i];
        if (conn == NULL)
            continue;
        // 跳过正在工作线程处理中的连接
        if (conn->in_use)
            continue;
        if (now - conn->last_active > CONNECTION_TIMEOUT)
        {
            log_info("fd=%d 连接超时，关闭", i);
            epoll_handler_close(epoll_fd, i);
        }
    }
    pthread_mutex_unlock(&connections_mutex);
}
