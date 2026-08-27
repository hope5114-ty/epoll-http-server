#ifndef EPOLL_HANDLER_H
#define EPOLL_HANDLER_H

#include "server.h"
#include <sys/epoll.h>

// 初始化epoll事件处理器
// epoll_fd：epoll实例描述符
// config：服务器配置
// return：成功返回0，失败返回‑1
int epoll_handler_init(int epoll_fd, const server_config_t *config);

// 设置关联的线程池实例
// pool：线程池指针，server_run内部调用
void epoll_handler_set_pool(void *pool);

// 清理全部客户端连接相关资源
void epoll_handler_cleanup(void);

// 将fd添加进epoll实例进行事件监听
int epoll_add_fd(int epoll_fd, int fd, uint32_t events);

// 修改epoll中fd对应的监听事件
int epoll_modify_fd(int epoll_fd, int fd, uint32_t events);

// 将fd从epoll实例中移除
int epoll_remove_fd(int epoll_fd, int fd);

// 处理新客户端连接，由主线程在listen_fd触发事件时调用
void epoll_handler_accept(int epoll_fd, int listen_fd);

// 把客户端fd任务派发至线程池执行
// epoll_fd：epoll实例
// client_fd：客户端socket描述符
void epoll_handler_dispatch(int epoll_fd, int client_fd);

// 关闭指定客户端连接
// note：调用方需要自行持有connections_mutex锁
void epoll_handler_close(int epoll_fd, int client_fd);

// 遍历检测，关闭超时的客户端连接，主线程定时调用
void epoll_handler_check_timeout(int epoll_fd);

/* 获取当前在线连接数（供 /status 接口调用） */
int epoll_handler_get_connection_count(void);

#endif // EPOLL_HANDLER_H
