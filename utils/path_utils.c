#include "path_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static int path_is_dir(const char *path)
{
    struct stat st;

    if (!path || path[0] == '\0') {
        return 0;
    }

    if (stat(path, &st) != 0) {
        return 0;
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int path_make_single_dir(const char *path)
{
    if (!path || path[0] == '\0') {
        return -1;
    }

    if (mkdir(path, 0755) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        return path_is_dir(path) ? 0 : -1;
    }

    return -1;
}

int ensure_dir_exists(const char *dirpath)
{
    char tmp[512];
    size_t len;
    size_t i;

    if (!dirpath || dirpath[0] == '\0') {
        fprintf(stderr, "ensure_dir_exists: invalid empty dirpath\n");
        return -1;
    }

    len = strlen(dirpath);
    if (len >= sizeof(tmp)) {
        fprintf(stderr, "ensure_dir_exists: dirpath too long: %s\n", dirpath);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s", dirpath);

    /*
     * 去掉末尾多余的 '/'
     * 例如 "recordings///" -> "recordings"
     * 但保留根目录 "/" 本身。
     */
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }

    if (path_is_dir(tmp)) {
        return 0;
    }

    /*
     * 递归创建：
     *   a/b/c
     * 先尝试 a，再 a/b，最后 a/b/c
     */
    for (i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';

            if (tmp[0] != '\0' && !path_is_dir(tmp)) {
                if (path_make_single_dir(tmp) < 0) {
                    fprintf(stderr,
                            "ensure_dir_exists: mkdir failed for %s, errno=%d (%s)\n",
                            tmp,
                            errno,
                            strerror(errno));
                    return -1;
                }
            }

            tmp[i] = '/';
        }
    }

    if (!path_is_dir(tmp)) {
        if (path_make_single_dir(tmp) < 0) {
            fprintf(stderr,
                    "ensure_dir_exists: mkdir failed for %s, errno=%d (%s)\n",
                    tmp,
                    errno,
                    strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int path_join_filename(char *out,
                              size_t out_size,
                              const char *dirpath,
                              const char *filename)
{
    size_t dir_len;

    if (!out || out_size == 0 || !dirpath || !filename) {
        return -1;
    }

    dir_len = strlen(dirpath);

    if (dir_len > 0 && dirpath[dir_len - 1] == '/') {
        if (snprintf(out, out_size, "%s%s", dirpath, filename) >= (int)out_size) {
            return -1;
        }
    } else {
        if (snprintf(out, out_size, "%s/%s", dirpath, filename) >= (int)out_size) {
            return -1;
        }
    }

    return 0;
}

static int make_timestamped_name(char *out,
                                 size_t out_size,
                                 const char *dirpath,
                                 const char *prefix,
                                 const char *ext)
{
    time_t now;
    struct tm tm_now;
    char timebuf[64];
    char filename[128];

    if (!out || out_size == 0 || !dirpath || !prefix || !ext) {
        return -1;
    }

    now = time(NULL);
    localtime_r(&now, &tm_now);

    if (strftime(timebuf,
                 sizeof(timebuf),
                 "%Y%m%d_%H%M%S",
                 &tm_now) == 0) {
        return -1;
    }

    if (snprintf(filename,
                 sizeof(filename),
                 "%s_%s.%s",
                 prefix,
                 timebuf,
                 ext) >= (int)sizeof(filename)) {
        return -1;
    }

    return path_join_filename(out, out_size, dirpath, filename);
}

int make_record_filename(char *out, size_t out_size, const char *dirpath)
{
    return make_timestamped_name(out, out_size, dirpath, "record", "mp4");
}

int make_snapshot_filename(char *out, size_t out_size, const char *dirpath)
{
    return make_timestamped_name(out, out_size, dirpath, "snapshot", "ppm");
}