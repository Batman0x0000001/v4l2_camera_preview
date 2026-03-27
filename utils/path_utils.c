#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "path_utils.h"

int ensure_dir_exists(const char *dirpath){
    struct stat st;

    if(!dirpath || dirpath[0] == '\0'){
        fprintf(stderr, "ensure_dir_exists: invalid empty dirpath\n");
        return -1;
    }

    if(stat(dirpath, &st) == 0){
        if(S_ISDIR(st.st_mode)){
            return 0;
        }

        fprintf(stderr,
                "ensure_dir_exists: path exists but is not a directory: %s\n",
                dirpath);
        return -1;
    }

    if(mkdir(dirpath, 0755) == 0){
        return 0;
    }

    if(errno == EEXIST){
        return 0;
    }

    fprintf(stderr,
            "ensure_dir_exists: mkdir failed for %s, errno=%d (%s)\n",
            dirpath,
            errno,
            strerror(errno));
    return -1;
}

static int make_timestamped_name(char *out,
                                 size_t out_size,
                                 const char *dirpath,
                                 const char *prefix,
                                 const char *ext){
    time_t now;
    struct tm tm_now;
    char timebuf[64];

    if(!out || out_size == 0 || !dirpath || !prefix || !ext){
        return -1;
    }

    now = time(NULL);
    localtime_r(&now, &tm_now);

    if(strftime(timebuf,
                sizeof(timebuf),
                "%Y%m%d_%H%M%S",
                &tm_now) == 0){
        return -1;
    }

    if(snprintf(out,
                out_size,
                "%s/%s_%s.%s",
                dirpath,
                prefix,
                timebuf,
                ext) >= (int)out_size){
        return -1;
    }

    return 0;
}

int make_record_filename(char *out,size_t out_size,const char *dirpath){
    return make_timestamped_name(out, out_size, dirpath, "record", "mp4");
}

int make_snapshot_filename(char *out,size_t out_size,const char *dirpath){
    return make_timestamped_name(out, out_size, dirpath, "snapshot", "ppm");
}