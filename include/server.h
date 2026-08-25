#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <time.h>

// 默认端口
#define DEFAULT_PORT 8080
// 默认网页资源根目录
#define DEFAULT_WEB_ROOT "./www"
// 默认线程池工作线程数量
#define DEFAULT_THREAD_COUNT 4
// epoll一次返回的最大事件数量
#define MAX_EVENTS 1024
// IO读写缓冲区大小
#define BUFFER_SIZE 4096
// HTTP请求头部最大允许长度
#define MAX_HEADER_SIZE 8192
// 客户端连接超时秒数
#define CONNECTION_TIMEOUT 60

// 服务器运行配置结构体
// 保存端口、资源目录、线程数等运行参数
typedef struct {
    uint16_t port;         // 监听端口
    char web_root[256];    // 网页根目录路径
    int max_connections;   // 最大并发连接数
    int thread_count;      // 线程池工作线程数
} server_config_t;

// 单个客户端连接上下文
// 保存socket、读写缓冲区、超时、keep‑alive状态
typedef struct {
    int in_use;                      // 连接是否被占用，0空闲，1正在处理
    int fd;                          // 客户端socket描述符
    char read_buf[BUFFER_SIZE];      // 请求读缓冲区
    int read_pos;                    // 读缓冲区有效数据偏移
    char write_buf[BUFFER_SIZE * 4]; // 响应写缓冲区
    int write_pos;                   // 写缓冲区发送偏移
    int write_len;                   // 写缓冲区有效数据总长度
    time_t last_active;              // 最后活跃时间，用于超时判定
    int keep_alive;                  // HTTP长连接标记，1开启，0关闭
} client_connection_t;

// 初始化TCP服务器
// config：服务器配置参数
// return：成功返回listen监听fd，失败返回‑1
int server_init(const server_config_t *config);

// 启动epoll事件主循环，处理客户端连接
// listen_fd：监听套接字
// config：服务器配置
void server_run(int listen_fd, const server_config_t *config);

#endif // SERVER_H
