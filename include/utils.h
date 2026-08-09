#ifndef UTILS_H
#define UTILS_H

#include <time.h>

// 日志级别
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_ERROR 2

// 当前日志级别，可自行调整
#define CURRENT_LOG_LEVEL LOG_LEVEL_INFO

// 日志函数

// 打印调试日志
#define log_debug(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG) \
            log_print("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

// 打印信息日志
#define log_info(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO) \
            log_print("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

// 打印错误日志
#define log_error(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL <= LOG_LEVEL_ERROR) \
            log_print("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

// 底层日志输出函数，仅供内部调用
void log_print(const char *level, const char *file, int line,
               const char *fmt, ...);

// 工具函数

// 获取格式化当前时间字符串
// buffer: 存放时间文本的缓冲区
// size: 缓冲区最大长度
void get_current_time(char *buffer, size_t size);

#endif // UTILS_H