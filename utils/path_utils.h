#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stddef.h>

/*
 * 路径工具模块职责：
 * 1. 确保目录存在，不存在时递归创建；
 * 2. 生成录像文件名；
 * 3. 生成截图文件名。
 */

int ensure_dir_exists(const char *dirpath);
int make_record_filename(char *out, size_t out_size, const char *dirpath);
int make_snapshot_filename(char *out, size_t out_size, const char *dirpath);

#endif