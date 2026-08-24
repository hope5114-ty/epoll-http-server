#include "http_parser.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 辅助函数

// 跳过字符串开头所有空白字符
// str: 原始字符串指针
// return: 跳过空白后的字符指针
static const char *skip_spaces(const char *str)
{
    while (*str && isspace((unsigned char)*str))
    {
        str++;
    }
    return str;
}

// 拷贝字符串内容，直到遇见指定分隔符
// dest: 目标缓冲区
// dest_size: 缓冲区最大容量
// src: 源字符串
// delimiter: 分隔符字符串
// return: 找到分隔符则返回分隔符位置，未找到返回NULL
static const char *copy_until(char *dest, size_t dest_size, const char *src, const char *delimiter)
{
    const char *end = strstr(src, delimiter);
    if (end == NULL)
    {
        // 未找到分隔符，拷贝全部内容
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
        return NULL;
    }

    size_t len = end - src;
    if (len >= dest_size)
    {
        len = dest_size - 1;
    }

    strncpy(dest, src, len);
    dest[len] = '\0';
    return end;
}

// 初始化与清理接口

// 初始化HTTP请求结构体，内存清零
void http_request_init(http_request_t *request)
{
    memset(request, 0, sizeof(http_request_t));
    request->content_length = 0;
    request->keep_alive = 1;
}

// 释放请求体内动态分配内存
void http_request_cleanup(http_request_t *request)
{
    if (request->body != NULL)
    {
        free(request->body);
        request->body = NULL;
    }
}

// 解析相关内部函数

// 解析请求行，格式：方法 URI 版本\r\n
// line: 请求行文本
// request: 输出解析结果
// return: 成功返回0，失败返回-1
static int parse_request_line(const char *line, http_request_t *request)
{
    const char *p = line;

    // 解析请求方法
    p = skip_spaces(p);
    const char *end = copy_until(request->method, MAX_METHOD_LENGTH, p, " ");
    if (end == NULL)
    {
        log_error("解析请求方法失败");
        return -1;
    }

    // 解析URI
    p = skip_spaces(end + 1);
    end = copy_until(request->uri, MAX_URI_LENGTH, p, " ");
    if (end == NULL)
    {
        log_error("解析URI失败");
        return -1;
    }

    // 解析HTTP版本
    p = skip_spaces(end + 1);
    end = copy_until(request->version, MAX_VERSION_LENGTH, p, "\r\n");
    if (request->version[0] == '\0')
    {
        // 无\r\n分隔符时拷贝剩余内容
        strncpy(request->version, p, MAX_VERSION_LENGTH - 1);
        // 剔除末尾换行符
        char *cr = strstr(request->version, "\r");
        if (cr)
            *cr = '\0';
    }

    return 0;
}

// 解析单条请求头，格式：Name: Value\r\n
// line: 单行头部文本
// header: 输出头部键值对
// return: 成功返回0，失败返回-1
static int parse_header_line(const char *line, http_header_t *header)
{
    // 查找名称与值的分隔冒号
    const char *colon = strchr(line, ':');
    if (colon == NULL)
    {
        return -1;
    }

    // 拷贝头部名称
    size_t name_len = colon - line;
    if (name_len >= MAX_HEADER_NAME)
    {
        name_len = MAX_HEADER_NAME - 1;
    }
    strncpy(header->name, line, name_len);
    header->name[name_len] = '\0';

    // 拷贝头部值，跳过冒号后的空格
    const char *value = skip_spaces(colon + 1);
    const char *end = strstr(value, "\r\n");
    size_t value_len;
    if (end)
    {
        value_len = end - value;
    }
    else
    {
        value_len = strlen(value);
    }

    if (value_len >= MAX_HEADER_VALUE)
    {
        value_len = MAX_HEADER_VALUE - 1;
    }
    strncpy(header->value, value, value_len);
    header->value[value_len] = '\0';

    return 0;
}

// 完整HTTP报文解析入口
// 流程：解析请求行 → 循环解析请求头 → 可选解析请求体
// raw_data: 原始HTTP报文缓冲区
// length: 报文有效长度
// request: 输出解析后的请求结构
// return: 成功返回0，失败返回-1
int http_parse_request(const char *raw_data, int length, http_request_t *request)
{
    // 初始化请求结构体
    http_request_init(request);

    // 定位请求行结束位置
    const char *line_end = strstr(raw_data, "\r\n");
    if (line_end == NULL)
    {
        log_error("未找到请求行结束符");
        return -1;
    }

    // 提取请求行字符串
    char request_line[1024];
    size_t line_len = line_end - raw_data;
    if (line_len >= sizeof(request_line))
    {
        line_len = sizeof(request_line) - 1;
    }
    strncpy(request_line, raw_data, line_len);
    request_line[line_len] = '\0';

    // 解析请求行
    if (parse_request_line(request_line, request) == -1)
    {
        return -1;
    }

    // 开始解析请求头
    const char *headers_start = line_end + 2; // 跳过\r\n换行
    const char *p = headers_start;

    while (*p)
    {
        // \r\n\r\n代表请求头结束
        if (strncmp(p, "\r\n", 2) == 0)
        {
            p += 2;
            break;
        }

        // 定位单行头部末尾
        line_end = strstr(p, "\r\n");
        if (line_end == NULL)
        {
            break;
        }

        // 提取单行头部文本
        char header_line[1024];
        line_len = line_end - p;
        if (line_len >= sizeof(header_line))
        {
            line_len = sizeof(header_line) - 1;
        }
        strncpy(header_line, p, line_len);
        header_line[line_len] = '\0';

        // 解析头部，同时缓存常用头部字段
        if (request->header_count < MAX_HEADERS)
        {
            if (parse_header_line(header_line, &request->headers[request->header_count]) == 0)
            {
                http_header_t *header = &request->headers[request->header_count];
                // 缓存Host头部
                if (strcasecmp(header->name, "Host") == 0)
                {
                    strncpy(request->host, header->value, MAX_HEADER_VALUE - 1);
                }
                else if (strcasecmp(header->name, "Connection") == 0)
                {
                    if (strcasecmp(header->value, "keep-alive") == 0)
                    {
                        request->keep_alive = 1;
                    }
                    else if (strcasecmp(header->value, "close") == 0)
                    {
                        request->keep_alive = 0;
                    }
                }
                else if (strcasecmp(header->name, "Content-Length") == 0)
                {
                    request->content_length = atoi(header->value);
                }
                request->header_count++;
            }
        }
        // 移动指针处理下一行头部
        p = line_end + 2;
    }

    // 解析请求体，仅Content-Length大于0时处理
    if (request->content_length > 0 && *p)
    {
        request->body = (char *)malloc(request->content_length + 1);
        if (request->body)
        {
            int body_len = length - (p - raw_data);
            if (body_len > request->content_length)
            {
                body_len = request->content_length;
            }
            memcpy(request->body, p, body_len);
            request->body[body_len] = '\0';
            request->body_length = body_len;
        }
    }

    // 打印解析结果调试信息
    http_request_print(request);

    return 0;
}

// 打印完整HTTP请求信息，用于调试
void http_request_print(const http_request_t *request)
{
    log_info("===== HTTP请求 =====");
    log_info("方法: %s", request->method);
    log_info("URI: %s", request->uri);
    log_info("版本: %s", request->version);
    log_info("Host: %s", request->host);
    log_info("Keep-Alive: %s", request->keep_alive ? "yes" : "no");
    log_info("头部数量: %d", request->header_count);

    for (int i = 0; i < request->header_count; i++)
    {
        log_info("  %s: %s", request->headers[i].name, request->headers[i].value);
    }

    if (request->body_length > 0)
    {
        log_info("请求体长度: %d", request->body_length);
    }
    log_info("===================");
}