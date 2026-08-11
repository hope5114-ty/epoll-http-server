#include "mime.h"
#include <string.h>

// MIME类型映射结构体，存储后缀与对应Content-Type
typedef struct {
    const char *extension;    // 文件扩展名
    const char *content_type; // 对应的MIME类型
} mime_entry_t;

// 常用文件后缀与MIME类型对照表
static const mime_entry_t mime_table[] = {
    // 文本类型
    {".html", "text/html; charset=utf-8"},
    {".htm",  "text/html; charset=utf-8"},
    {".css",  "text/css; charset=utf-8"},
    {".js",   "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".xml",  "application/xml; charset=utf-8"},
    {".txt",  "text/plain; charset=utf-8"},

    // 图片类型
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png",  "image/png"},
    {".gif",  "image/gif"},
    {".ico",  "image/x-icon"},
    {".svg",  "image/svg+xml"},
    {".webp", "image/webp"},

    // 音频类型
    {".mp3",  "audio/mpeg"},
    {".wav",  "audio/wav"},
    {".ogg",  "audio/ogg"},

    // 视频类型
    {".mp4",  "video/mp4"},
    {".webm", "video/webm"},
    {".avi",  "video/x-msvideo"},

    // 应用文件类型
    {".pdf",  "application/pdf"},
    {".zip",  "application/zip"},
    {".gz",   "application/gzip"},
    {".tar",  "application/x-tar"},

    // 字体类型
    {".woff", "font/woff"},
    {".woff2","font/woff2"},
    {".ttf",  "font/ttf"},

    // 数组结束标记
    {NULL, NULL}
};

// 从文件名提取后缀，返回带.的扩展名，无后缀返回NULL
static const char *get_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return NULL;
    }
    return dot;
}

// 根据文件路径查找对应的MIME类型
// 使用strcmp精确匹配小写后缀，未知文件默认返回二进制流类型
const char *mime_get_type(const char *file_path) {
    // 获取文件后缀
    const char *ext = get_extension(file_path);
    if (ext == NULL) {
        return "application/octet-stream";
    }

    // 遍历映射表进行匹配
    for (int i = 0; mime_table[i].extension != NULL; i++) {
        if (strcmp(ext, mime_table[i].extension) == 0) {
            return mime_table[i].content_type;
        }
    }

    // 没有匹配到后缀，返回默认二进制类型
    return "application/octet-stream";
}