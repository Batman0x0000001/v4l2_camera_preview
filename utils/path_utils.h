#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include<stddef.h>

int ensure_dir_exists(const char* dirpath);
int make_record_filename(char* out,size_t out_size,const char* dirpath);
int make_snapshot_filename(char* out,size_t out_size,const char* dirpath);

#endif