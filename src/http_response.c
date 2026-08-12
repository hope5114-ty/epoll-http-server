#include "http_response.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// 根据状态码获取对应的文本描述
static const char *get_status_description(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Unknown";
    }
}

// 组装HTTP响应头部，返回头部字节长度
static int build_response_header(char *buffer, size_t buffer_size,
                                 int status_code, const char *content_type,
                                 long content_length, int keep_alive) {
    int len = snprintf(buffer, buffer_size,
        "HTTP/1.1 %d %s\r\n"
        "Server: SimpleHTTP/1.0\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status_code, get_status_description(status_code),
        content_type,
        content_length,
        keep_alive ? "keep-alive" : "close");

    return len;
}

// 打开本地文件，组装完整响应头+文件内容存入发送缓冲区
int http_send_file(client_connection_t *conn, const char *file_path,
                   const char *content_type) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("打开文件失败");
        log_error("文件路径: %s", file_path);
        http_send_error(conn, 403, "Forbidden");
        return -1;
    }

    // 通过lseek获取文件大小
    long file_size = lseek(fd, 0, SEEK_END);
    if (file_size == -1) {
        perror("获取文件大小失败");
        close(fd);
        http_send_error(conn, 500, "Internal Server Error");
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    // 判断文件是否超出缓冲区上限
    if (file_size > BUFFER_SIZE * 4 - 512) {
        log_error("文件太大: %ld 字节", file_size);
        close(fd);
        http_send_error(conn, 413, "Payload Too Large");
        return -1;
    }

    int header_len = build_response_header(conn->write_buf, BUFFER_SIZE * 4,
                                           200, content_type, file_size,
                                           conn->keep_alive);

    int bytes_read = read(fd, conn->write_buf + header_len, file_size);
    close(fd);

    if (bytes_read == -1) {
        perror("读取文件失败");
        http_send_error(conn, 500, "Internal Server Error");
        return -1;
    }

    conn->write_pos = 0;
    conn->write_len = header_len + bytes_read;

    log_info("准备发送文件: %s, 大小: %ld 字节", file_path, file_size);
    return 0;
}

// 处理HEAD请求，仅返回响应头，不携带文件内容
int http_send_head(client_connection_t *conn, const char *file_path,
                   const char *content_type) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        http_send_error(conn, 404, "Not Found");
        return -1;
    }

    long file_size = lseek(fd, 0, SEEK_END);
    close(fd);

    if (file_size == -1) {
        http_send_error(conn, 404, "Not Found");
        return -1;
    }

    int header_len = build_response_header(conn->write_buf, BUFFER_SIZE * 4,
                                           200, content_type, file_size,
                                           conn->keep_alive);

    conn->write_pos = 0;
    conn->write_len = header_len;

    return 0;
}

// 构造错误页面响应，存入发送缓冲区
int http_send_error(client_connection_t *conn, int status_code, const char *message) {
    char body[1024];
    int body_len = snprintf(body, sizeof(body),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>%d %s</title></head>\n"
        "<body>\n"
        "<h1>%d %s</h1>\n"
        "<hr>\n"
        "<p>SimpleHTTP/1.0</p>\n"
        "</body>\n"
        "</html>\n",
        status_code, message, status_code, message);

    int header_len = build_response_header(conn->write_buf, BUFFER_SIZE * 4,
                                           status_code, "text/html", body_len,
                                           0);

    memcpy(conn->write_buf + header_len, body, body_len);

    conn->write_pos = 0;
    conn->write_len = header_len + body_len;

    log_info("发送错误响应: %d %s", status_code, message);
    return 0;
}