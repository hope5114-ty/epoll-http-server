#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stdint.h>

// 常量定义
#define MAX_METHOD_LENGTH 16    // 请求方法最大长度
#define MAX_URI_LENGTH 2048     // URI最大长度
#define MAX_VERSION_LENGTH 16   // 协议版本最大长度
#define MAX_HEADERS 32          // 最大头部数量
#define MAX_HEADER_NAME 64      // 头部名称最大长度
#define MAX_HEADER_VALUE 256    // 头部值最大长度

// 数据结构

// HTTP请求头键值对
typedef struct {
    char name[MAX_HEADER_NAME];   // 头部名称，如 "Host"
    char value[MAX_HEADER_VALUE]; // 头部值，如 "localhost:8080"
} http_header_t;

// HTTP请求结构体，存储解析后的HTTP请求信息
typedef struct {
    // 请求行
    char method[MAX_METHOD_LENGTH];       // 请求方法：GET, POST, HEAD等
    char uri[MAX_URI_LENGTH];             // 请求URI：/index.html
    char version[MAX_VERSION_LENGTH];     // HTTP版本：HTTP/1.1
    
    // 请求头
    http_header_t headers[MAX_HEADERS];   // 头部数组
    int header_count;                     // 头部数量
    
    // 请求体（POST请求）
    char *body;                           // 请求体内容
    int body_length;                      // 请求体长度
    
    // 常用头部的快捷访问
    char host[MAX_HEADER_VALUE];          // Host头部
    int keep_alive;    // Connection头部（keep-alive/close）
    int content_length;                   // Content-Length头部
} http_request_t;

// 函数声明

// 解析HTTP请求，将原始HTTP请求报文解析为结构化的http_request_t
// raw_data: 原始HTTP报文数据
// length: 数据长度
// request: 输出参数，解析后的请求结构
// return: 成功返回0，失败返回-1
int http_parse_request(const char *raw_data, int length, http_request_t *request);

// 初始化HTTP请求结构
// request: 要初始化的请求结构
void http_request_init(http_request_t *request);

// 清理HTTP请求结构，释放动态分配的内存
// request: 要清理的请求结构
void http_request_cleanup(http_request_t *request);

// 打印HTTP请求信息，调试使用
// request: 要打印的请求结构
void http_request_print(const http_request_t *request);

#endif // HTTP_PARSER_H