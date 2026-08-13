# HTTP Server Makefile
# 目录结构：src/ 放 .c 文件，include/ 放 .h 文件

CC = gcc
CFLAGS = -Wall -Wextra -I./include
LDFLAGS =

# 源文件和目标文件
SRC_DIR = src
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = http_server

# 默认目标
all: $(TARGET)

# 链接所有 .o 文件生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译每个 .c 文件为 .o 文件
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理编译产物
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
