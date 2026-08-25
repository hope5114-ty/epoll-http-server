#ifndef EPOLL_HANDLER_H
#define EPOLL_HANDLER_H

#include "server.h"
#include <sys/epoll.h>

// 初始化epoll事件处理器
// epoll_fd：epoll实例fd
// config：服务器配置
// return：成功0，失败-1
int epoll_handler_init(int epoll_fd, const server_config_t *config);

// 设置线程池
void epoll_handler_set_poll(void *poll);

// 清理所有连接资源
void epoll_handler_cleanup(void);

// 将fd加入epoll监听
int epoll_add_fd(int epoll_fd, int fd, uint32_t events);

// 修改fd对应的监听事件
int epoll_modify_fd(int epoll_fd, int fd, uint32_t events);

// 将fd从epoll监听列表移除
int epoll_remove_fd(int epoll_fd, int fd);

// 处理新连接
void epoll_handler_accept(int epoll_fd, int listen_fd);

// 将客户端fd派发给线程池处理
void epoll_handler_dispatch(int epoll_fd,int client_fd);

//工作线程执行HTTP请求处理任务
static void worker_process_request(void *arg);

// 关闭客户端连接并释放资源
void epoll_handler_close(int epoll_fd, int client_fd);

// 遍历连接，关闭空闲超时连接
void epoll_handler_check_timeout(int epoll_fd);

#endif // EPOLL_HANDLER_H