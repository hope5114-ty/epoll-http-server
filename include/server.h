#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <time.h>

// 配置常量
#define DEFAULT_PORT 8080           // 默认端口
#define DEFAULT_WEB_ROOT "./www"    // 默认网站根目录
#define MAX_EVENTS 1024             // epoll最大监听事件数
#define BUFFER_SIZE 4096            // 读写缓冲区大小
#define MAX_HEADER_SIZE 8192        // HTTP头部最大大小
#define CONNECTION_TIMEOUT 60       // 连接超时时间（秒）

// 数据结构

// 服务器配置，存储服务器运行参数，支持命令行修改
typedef struct {
    uint16_t port;          // 监听端口
    char web_root[256];     // 网站根目录路径
    int max_connections;    // 最大连接数
} server_config_t;

// 客户端连接信息，每个客户端对应一个结构体，保存连接状态、缓冲区、超时信息
typedef struct {
    int fd;                         // 客户端socket文件描述符
    char read_buf[BUFFER_SIZE];     // 读缓冲区
    int read_pos;                   // 读缓冲区当前位置
    char write_buf[BUFFER_SIZE * 4]; // 写缓冲区（响应数据可能较大）
    int write_pos;                  // 写缓冲区当前位置
    int write_len;                  // 写缓冲区有效数据长度
    time_t last_active;             // 最后活跃时间，用于超时检测
    int keep_alive;                 // 是否长连接，HTTP/1.1默认开启
} client_connection_t;

// 函数声明

// 初始化服务器，创建socket、绑定地址并监听
// config: 服务器配置参数
// return: 成功返回监听socket fd，失败返回-1
int server_init(const server_config_t *config);

// 启动服务器主循环，基于epoll处理各类事件与客户端连接
// listen_fd: 监听socket文件描述符
// config: 服务器配置参数
void server_run(int listen_fd, const server_config_t *config);

#endif // SERVER_H