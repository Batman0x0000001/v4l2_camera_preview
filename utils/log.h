#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * 轻量级 header-only 日志模块
 *
 * 目标：
 * 1. 不引入 log.c，避免改动当前构建方式；
 * 2. 保持现有调用方式不变：
 *      LOG_INFO("x=%d", x);
 * 3. 输出统一格式，便于排查线程、时间戳、状态切换问题。
 *
 * 输出格式示例：
 *   [2026-03-27 15:20:31][INFO][main.c:42] startup ok
 */

#ifdef __cplusplus
extern "C" {
#endif

static inline const char *log_basename(const char *path)
{
    const char *slash1;
    const char *slash2;
    const char *base;

    if (!path) {
        return "(null)";
    }

    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');

    base = path;
    if (slash1 && slash2) {
        base = (slash1 > slash2) ? (slash1 + 1) : (slash2 + 1);
    } else if (slash1) {
        base = slash1 + 1;
    } else if (slash2) {
        base = slash2 + 1;
    }

    return base;
}

static inline void log_format_now(char *buf, size_t buf_size)
{
    time_t now;
    struct tm tm_now;

    if (!buf || buf_size == 0) {
        return;
    }

    now = time(NULL);
    localtime_r(&now, &tm_now);

    if (strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_now) == 0) {
        snprintf(buf, buf_size, "0000-00-00 00:00:00");
    }
}

static inline void log_vwrite(FILE *fp,
                              const char *level,
                              const char *file,
                              int line,
                              const char *fmt,
                              va_list ap)
{
    char timebuf[32];

    if (!fp || !level || !fmt) {
        return;
    }

    log_format_now(timebuf, sizeof(timebuf));

    fprintf(fp,
            "[%s][%s][%s:%d] ",
            timebuf,
            level,
            log_basename(file),
            line);

    vfprintf(fp, fmt, ap);
    fputc('\n', fp);
    fflush(fp);
}

static inline void log_write(FILE *fp,
                             const char *level,
                             const char *file,
                             int line,
                             const char *fmt,
                             ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_vwrite(fp, level, file, line, fmt, ap);
    va_end(ap);
}

#define LOG_INFO(fmt, ...) \
    log_write(stdout, "INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    log_write(stderr, "WARN", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    log_write(stderr, "ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif