#include "server.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// 程序版本号
#define VERSION "1.0.0"

// 打印命令行帮助使用说明
// program_name：程序可执行文件名
static void print_usage(const char *program_name)
{
    printf("Simple HTTP Server v%s\n\n", VERSION);
    printf("用法: %s [选项]\n\n", program_name);
    printf("选项:\n");
    printf("  -p, --port <端口>       监听端口（默认8080）\n");
    printf("  -r, --root <目录>       网站根目录（默认./www）\n");
    printf("  -m, --max <连接数>      最大连接数（默认1024）\n");
    printf("  -t, --threads <线程数>  工作线程数（默认4）\n");
    printf("  -h, --help              显示帮助信息\n");
    printf("\n示例:\n");
    printf("  %s -p 8080 -r ./www\n", program_name);
    printf("  %s --port 9000 --root /var/www/html\n", program_name);
}

// 程序入口，解析命令行参数、填充配置、校验目录、启动http服务器
int main(int argc, char *argv[])
{
    // 填充服务器默认配置
    server_config_t config = {
        .port = DEFAULT_PORT,
        .max_connections = MAX_EVENTS,
        .thread_count = DEFAULT_THREAD_COUNT
    };
    strncpy(config.web_root, DEFAULT_WEB_ROOT, sizeof(config.web_root) - 1);

    // 手动解析命令行参数，不依赖getopt_long库
    int i = 1;
    while (i < argc)
    {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0)
        {
            // 解析监听端口
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误: -p/--port 需要指定端口号\n");
                return 1;
            }
            i++;
            config.port = atoi(argv[i]);
            if (config.port <= 0 || config.port > 65535)
            {
                fprintf(stderr, "错误: 端口号无效（1-65535）\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--root") == 0)
        {
            // 解析网页根目录
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误: -r/--root 需要指定目录路径\n");
                return 1;
            }
            i++;
            strncpy(config.web_root, argv[i], sizeof(config.web_root) - 1);
        }
        else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max") == 0)
        {
            // 解析最大并发连接数
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误: -m/--max 需要指定连接数\n");
                return 1;
            }
            i++;
            config.max_connections = atoi(argv[i]);
            if (config.max_connections <= 0 || config.max_connections > MAX_EVENTS)
            {
                fprintf(stderr, "错误: 最大连接数无效（1-%d）\n", MAX_EVENTS);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0)
        {
            // 解析线程池工作线程数量
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误: -t/--threads 需要指定线程数\n");
                return 1;
            }
            i++;
            config.thread_count = atoi(argv[i]);
            if (config.thread_count <= 0 || config.thread_count > 64)
            {
                fprintf(stderr, "错误: 线程数无效（1-64）\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "错误: 未知参数 %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        i++;
    }

    // 输出服务器启动配置信息
    log_info("===========================================");
    log_info("  Simple HTTP Server v%s", VERSION);
    log_info("===========================================");
    log_info("端口: %d", config.port);
    log_info("根目录: %s", config.web_root);
    log_info("最大连接: %d", config.max_connections);
    log_info("工作线程: %d", config.thread_count);
    log_info("-------------------------------------------");

    // open校验网站根目录是否可读，不使用access函数
    int dir_fd = open(config.web_root, O_RDONLY);
    if (dir_fd == -1)
    {
        perror("网站根目录不存在或不可读");
        log_error("请创建目录并放入网页文件，或使用-r参数指定其他目录");
        return 1;
    }
    close(dir_fd);

    // 创建socket、bind、listen得到监听fd
    int listen_fd = server_init(&config);
    if (listen_fd == -1)
    {
        log_error("服务器初始化失败");
        return 1;
    }

    // 进入epoll主循环，函数阻塞直到收到退出信号
    server_run(listen_fd, &config);

    return 0;
}
