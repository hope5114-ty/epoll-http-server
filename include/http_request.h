#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include "http_parser.h"
#include "server.h"

//处理HTTP请求
//request:解析HTTP请求
//conn:客户端连接信息
//config:服务器配置
int http_handle_request(const http_request_t *request, client_connection_t *conn, 
                        const server_config_t *config);

#endif