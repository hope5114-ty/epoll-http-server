# Simple HTTP Server

基于 epoll 事件驱动模型的高性能 HTTP 服务器，使用纯 C 语言实现。

## 项目简介

这是一个为嵌入式 Linux 校招打造的 HTTP 服务器项目，核心亮点是使用 **epoll 事件驱动模型**实现高并发连接处理。

### 技术特点

- **epoll 事件驱动**：使用边缘触发（ET）模式 + 非阻塞 IO
- **高并发支持**：单线程处理数千并发连接
- **HTTP/1.1 支持**：GET、HEAD 方法，Keep-Alive 连接
- **MIME 自动识别**：根据文件扩展名自动设置 Content-Type
- **连接超时管理**：自动清理僵尸连接
- **信号处理**：优雅关闭服务器

## 快速开始

### 环境要求

- Linux 系统（Ubuntu 24.04）
- GCC 编译器
- Make 工具

### 编译

```bash
# 编译程序
make

# 或编译调试版本（带调试符号，无优化）
make debug
```

### 运行

```bash
# 使用默认配置运行（端口8080，根目录./www）
./http_server

# 或指定端口和根目录
./http_server -p 9000 -r ./www

# 查看帮助
./http_server --help
```

### 测试

在浏览器中访问：
```
http://localhost:8080
```

或使用 curl 测试：
```bash
# GET 请求
curl http://localhost:8080

# HEAD 请求
curl -I http://localhost:8080

# 测试 404
curl http://localhost:8080/notexist.html
```

### 停止服务器

按 `Ctrl+C` 优雅关闭服务器。

## 项目结构

```
http_server/
├── src/
│   ├── main.c              # 主入口：解析命令行参数、启动服务器
│   ├── server.c            # 服务器初始化：socket/bind/listen
│   ├── server.h
│   ├── epoll_handler.c     # epoll事件循环：连接管理、超时检测
│   ├── epoll_handler.h
│   ├── http_parser.c       # HTTP请求解析：请求行、请求头
│   ├── http_parser.h
│   ├── http_request.c      # 请求处理：路由、文件读取
│   ├── http_request.h
│   ├── http_response.c     # HTTP响应构建：状态行、响应头
│   ├── http_response.h
│   ├── thread_pool.c       # 线程池任务处理
│   ├── thread_pool.h
│   ├── mime.c              # MIME类型映射
│   ├── mime.h
│   ├── utils.c             # 工具函数：日志
│   └── utils.h
├── www/                    # 静态文件根目录
│   ├── index.html          # 首页
│   └── 404.html            # 404错误页
├── Makefile
├── README.md
└── INTERVIEW_GUIDE.md      # 面试要点文档
```

## 核心模块说明

### 1. epoll_handler（事件处理）

**核心职责**：管理所有客户端连接，处理读写事件

**关键函数**：
- `epoll_handler_accept()`: 处理新连接
- `epoll_handler_read()`: 读取客户端请求
- `epoll_handler_write()`: 发送响应
- `epoll_handler_check_timeout()`: 检测超时连接

**面试要点**：
- 为什么用 epoll 而不是 select/poll？
- ET 模式和 LT 模式的区别？
- 为什么 ET 模式要循环 read/accept？

### 2. http_parser（请求解析）

**核心职责**：解析 HTTP 请求报文

**解析流程**：
1. 解析请求行（方法、URI、版本）
2. 解析请求头（键值对）
3. 解析请求体（POST 请求）

### 3. http_response（响应构建）

**核心职责**：构建 HTTP 响应报文

**响应格式**：
```
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 1234\r\n
\r\n
<html>...</html>
```

## 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-p, --port` | 监听端口 | 8080 |
| `-r, --root` | 网站根目录 | ./www |
| `-m, --max` | 最大连接数 | 1024 |
| `-h, --help` | 显示帮助 | - |

## 性能测试

### 使用 ab（Apache Bench）

```bash
# 1000个请求，100个并发
ab -n 1000 -c 100 http://localhost:8080/
```

### 使用 wrk

```bash
# 30秒测试，100个并发，10个线程
wrk -t10 -c100 -d30s http://localhost:8080/
```

## 已知限制

1. **不支持 POST 请求**：只支持 GET 和 HEAD
2. **不支持 HTTPS**：纯 HTTP，无加密
3. **不支持 CGI**：只能返回静态文件
4. **文件大小限制**：响应内容不能超过缓冲区大小（16KB）
5. **不支持虚拟主机**：只能服务一个网站根目录

## 扩展方向

如果继续完善这个项目，可以添加：

1. **POST 方法支持**：解析请求体，处理表单数据
2. **Keep-Alive 长连接**：一个连接服务多个请求
3. **断点续传**：支持 Range 请求头
4. **CGI 支持**：调用外部程序处理动态请求
5. **HTTPS 支持**：集成 OpenSSL
6. **日志系统**：记录访问日志到文件
7. **配置文件**：从配置文件读取参数
8. **性能优化**：使用 sendfile 零拷贝、内存池

## 许可证

MIT License