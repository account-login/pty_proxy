// system
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>     // for basename
// proj
#include "util.h"


// linux only
static const char *_get_process_name(char *buf, size_t bufsize) {
    ssize_t len = ::readlink("/proc/self/exe", buf, bufsize);
    if (len < 0) {
        return "__CAN_NOT_READ_PROC_SELF_EXE__";
    }
    return ::basename(buf);
}


static char g_proc_name[1024 * 4];


static const char *get_process_name() {
    if (g_proc_name[0] == '\0') {
        const char *val = _get_process_name(g_proc_name, sizeof(g_proc_name));
        strcpy(g_proc_name, val);
    }
    return g_proc_name;
}


void log_err(int errnum, const char *fmt, ...) {
    fprintf(stderr, "[%s:%u] ", get_process_name(), getpid());

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (errnum != 0) {
        fprintf(stderr, " [errno:%d] %s", errnum, strerror(errnum));
    }

    fputs("\r\n", stderr);
}

static int g_is_debug = -1;

void log_dbg(const char *fmt, ...) {
    if (g_is_debug == -1) {
        const char *val = getenv("PTY_PROXY_DEBUG");
        g_is_debug = val ? atoi(val) : 0;
    }
    if (!g_is_debug) {
        return;
    }

    fprintf(stderr, "[%s:%u] ", get_process_name(), getpid());

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputs("\r\n", stderr);
}
