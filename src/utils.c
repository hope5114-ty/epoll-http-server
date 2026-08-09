#include "utils.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

// 打印日志
// 输出格式：[时间] [级别] [文件:行号] 消息
// level: 日志级别标识
// file: 源码文件路径
// line: 代码行号
// fmt: 格式化字符串
// ...: 可变参数列表
void log_print(const char *level, const char *file, int line,
               const char *fmt, ...) {
    // 获取当前系统时间
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // 截取文件名，剔除路径前缀
    const char *filename = strrchr(file, '/');
    if (filename) {
        filename++;
    } else {
        filename = file;
    }

    // 输出日志头部信息
    printf("[%s] [%s] [%s:%d] ", time_str, level, filename, line);

    // 输出日志正文
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
}

// 获取格式化当前时间字符串
void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}