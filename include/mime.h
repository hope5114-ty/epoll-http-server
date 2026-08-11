#ifndef MIME_H
#define MIME_H

// 根据文件路径获取对应的MIME类型
// file_path：文件路径
// return：返回Content-Type字符串，未知后缀返回二进制默认类型
const char *mime_get_type(const char *file_path);

#endif // MIME_H