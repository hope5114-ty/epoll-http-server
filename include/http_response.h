#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "server.h"

// 读取目标文件，组装完整HTTP响应报文存入连接发送缓冲区
// conn：客户端连接结构体
// file_path：本地文件路径
// content_type：对应MIME类型
// return：成功0，失败-1
int http_send_file(client_connection_t *conn, const char *file_path,
                   const char *content_type);

// 处理HEAD请求，仅返回HTTP响应头，不携带响应体
int http_send_head(client_connection_t *conn, const char *file_path,
                   const char *content_type);

// 构建错误状态响应报文，存入发送缓冲区
// status_code：HTTP状态码，message：状态描述文本
int http_send_error(client_connection_t *conn, int status_code, const char *message);

#endif // HTTP_RESPONSE_H