#include "http_request.h"
#include "http_response.h"
#include "mime.h"
#include "utils.h"
#include "epoll_handler.h" 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* 服务器第一次收到请求的时间，用来计算运行时长（uptime） */
static time_t server_start_time = 0;

/* 累计处理的请求总数，多线程下用原子操作递增，不需要加锁 */
static long total_requests = 0;

// 辅助函数

// 拼接完整文件路径，防御目录遍历攻击
// web_root：网站根目录
// uri：客户端请求URI
// path：输出完整文件路径缓冲区
// path_size：缓冲区容量
// return：成功返回0，检测到非法路径返回-1
static int build_file_path(const char *web_root, const char *uri,
                           char *path, size_t path_size) {
    // 拦截包含 .. 的目录遍历请求
    if (strstr(uri, "..") != NULL) {
        log_error("检测到目录遍历攻击: %s", uri);
        return -1;
    }

    // 跳过URI最前方的斜杠
    const char *rel_path = uri;
    if (rel_path[0] == '/') {
        rel_path++;
    }

    // 访问根路径时默认指向 index.html
    if (rel_path[0] == '\0' || strcmp(rel_path, "/") == 0) {
        snprintf(path, path_size, "%s/index.html", web_root);
    } else {
        snprintf(path, path_size, "%s/%s", web_root, rel_path);
    }

    return 0;
}

// 获取文件大小，使用open + lseek实现
// file_path：文件路径
// out_fd：输出打开后的文件描述符
// return：成功返回文件字节大小，失败返回-1
static long get_file_size(const char *file_path, int *out_fd) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    // lseek移动至文件末尾，返回值即为文件大小
    long size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        close(fd);
        return -1;
    }

    // 文件指针复位到起始位置，方便后续读取
    lseek(fd, 0, SEEK_SET);

    *out_fd = fd;
    return size;
}

// 处理GET请求，读取文件资源并返回完整响应
// uri：请求资源路径
// conn：客户端连接对象
// config：服务器全局配置
// return：成功返回0，失败返回-1
static int handle_get(const char *uri, client_connection_t *conn,
                      const server_config_t *config) {
    // 拼装本地文件路径
    char file_path[1024];
    if (build_file_path(config->web_root, uri, file_path, sizeof(file_path)) == -1) {
        http_send_error(conn, 403, "Forbidden");
        return -1;
    }

    // 直接open探测文件，不提前使用stat判断
    int fd;
    long file_size = get_file_size(file_path, &fd);

    if (file_size == -1) {
        // 目标为目录，尝试自动拼接 index.html
        char index_path[1024];
        snprintf(index_path, sizeof(index_path), "%s/index.html", file_path);
        file_size = get_file_size(index_path, &fd);
        if (file_size == -1) {
            http_send_error(conn, 404, "Not Found");
            return -1;
        }
        strncpy(file_path, index_path, sizeof(file_path) - 1);
    }

    // 校验文件大小，超出缓冲区上限返回错误
    if (file_size > BUFFER_SIZE * 4 - 512) {
        log_error("文件太大: %ld 字节", file_size);
        close(fd);
        http_send_error(conn, 413, "Payload Too Large");
        return -1;
    }

    // 获取文件对应的MIME类型
    const char *content_type = mime_get_type(file_path);

    // 组装HTTP响应头
    int header_len = snprintf(conn->write_buf, BUFFER_SIZE * 4,
        "HTTP/1.1 200 OK\r\n"
        "Server: SimpleHTTP/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        content_type,
        file_size,
        conn->keep_alive ? "keep-alive" : "close");

    // 将文件内容读取到缓冲区，紧跟响应头之后
    int bytes_read = read(fd, conn->write_buf + header_len, file_size);
    close(fd);

    if (bytes_read == -1) {
        perror("读取文件失败");
        http_send_error(conn, 500, "Internal Server Error");
        return -1;
    }

    // 更新连接写缓冲区信息
    conn->write_pos = 0;
    conn->write_len = header_len + bytes_read;

    log_info("准备发送文件: %s, 大小: %ld 字节", file_path, file_size);
    return 0;
}

// 处理HEAD请求，仅返回响应头，不传输响应体
// uri：请求资源路径
// conn：客户端连接对象
// config：服务器全局配置
// return：成功返回0，失败返回-1
static int handle_head(const char *uri, client_connection_t *conn,
                       const server_config_t *config) {
    // 拼装本地文件路径
    char file_path[1024];
    if (build_file_path(config->web_root, uri, file_path, sizeof(file_path)) == -1) {
        http_send_error(conn, 403, "Forbidden");
        return -1;
    }

    // 打开文件获取大小，HEAD不需要读取内容
    int fd;
    long file_size = get_file_size(file_path, &fd);
    if (file_size == -1) {
        // 目标为目录，尝试拼接 index.html
        char index_path[1024];
        snprintf(index_path, sizeof(index_path), "%s/index.html", file_path);
        file_size = get_file_size(index_path, &fd);
        if (file_size == -1) {
            http_send_error(conn, 404, "Not Found");
            return -1;
        }
        strncpy(file_path, index_path, sizeof(file_path) - 1);
    }
    close(fd);

    // 获取文件MIME类型
    const char *content_type = mime_get_type(file_path);

    // 构造响应头，填充Content-Length，无响应体
    int header_len = snprintf(conn->write_buf, BUFFER_SIZE * 4,
        "HTTP/1.1 200 OK\r\n"
        "Server: SimpleHTTP/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        content_type,
        file_size,
        conn->keep_alive ? "keep-alive" : "close");

    conn->write_pos = 0;
    conn->write_len = header_len;

    return 0;
}

// 请求处理入口，根据请求方法分发至对应处理函数
// request：解析完成的HTTP请求结构体
// conn：客户端连接
// config：服务器配置
// return：成功返回0，失败返回-1
int http_handle_request(const http_request_t *request, client_connection_t *conn, const server_config_t *config) {

    /* ===== 服务器启动时间：第一次收到请求时记录 ===== */
    if (server_start_time == 0) {
        server_start_time = time(NULL);
    }

    /* 总请求数 +1（原子操作，多线程安全，下面单独讲） */
    __atomic_fetch_add(&total_requests, 1, __ATOMIC_SEQ_CST);

    log_info("处理请求: %s %s", request->method, request->uri);

    /* ===== /status 接口：返回服务器运行状态（JSON格式）===== */
    if (strcmp(request->method, "GET") == 0 &&
        strcmp(request->uri, "/status") == 0) {

        /* 从连接表获取当前在线连接数 */
        int current_conns = epoll_handler_get_connection_count();

        /* 计算服务器已运行秒数 */
        long uptime = (long)(time(NULL) - server_start_time);

        /* 原子读取总请求数 */
        long req_count = __atomic_load_n(&total_requests, __ATOMIC_SEQ_CST);

        /* 拼 JSON 响应体 */
        char json_body[256];
        int body_len = snprintf(json_body, sizeof(json_body),
            "{\"connections\":%d,\"total_requests\":%ld,"
            "\"thread_pool_size\":%d,\"uptime\":%ld}",
            current_conns, req_count, config->thread_count, uptime);

        /* 拼 HTTP 响应头 */
        int header_len = snprintf(conn->write_buf, BUFFER_SIZE * 4,
            "HTTP/1.1 200 OK\r\n"
            "Server: SimpleHTTP/1.0\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: %s\r\n"
            "\r\n",
            body_len,
            conn->keep_alive ? "keep-alive" : "close");

        // 把 JSON body 拷到响应头后面
        memcpy(conn->write_buf + header_len, json_body, body_len);

        // 设置写缓冲区长度，worker 线程会发出去
        conn->write_pos = 0;
        conn->write_len = header_len + body_len;

        log_info("返回状态: 连接数=%d, 总请求=%ld", current_conns, req_count);
        return 0;
    }

    if (strcmp(request->method, "GET") == 0) {
        return handle_get(request->uri, conn, config);
    } else if (strcmp(request->method, "HEAD") == 0) {
        return handle_head(request->uri, conn, config);
    } else if (strcmp(request->method, "POST") == 0) {
        http_send_error(conn, 501, "Not Implemented");
        return -1;
    } else {
        http_send_error(conn, 405, "Method Not Allowed");
        return -1;
    }
}