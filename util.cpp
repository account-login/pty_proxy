// system
#include <assert.h>
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
    if ((size_t)len >= bufsize) {
        len = bufsize - 1;
    }
    buf[len] = '\0';
    return ::basename(buf);
}


static char g_proc_name[1024 * 4] = {};


static const char *get_process_name() {
    // FIXME: thread unsafe
    if (g_proc_name[0] == '\0') {
        const char *val = _get_process_name(g_proc_name, sizeof(g_proc_name));
        strcpy(g_proc_name, val);
    }
    return g_proc_name;
}

static void do_vlog(int errnum, const char *fmt, va_list args) {
    char buf[4096];
    char *cur = &buf[0];
    char *end = cur + sizeof(buf);
    int n = snprintf(cur, end - cur, "[%s:%u]", get_process_name(), getpid());
    assert(n > 0);
    cur += n;

    if (cur < end) {
        n = vsnprintf(cur, end - cur, fmt, args);
        if (n < 0) {
            return;
        }
        cur += n;
    }

    if (errnum != 0 && cur < end) {
        // FIXME: thread unsafe
        n = snprintf(cur, end - cur, " [errno:%d] %s", errnum, strerror(errnum));
        assert(n > 0);
        cur += n;
    }
    if (cur > end - 2) {
        cur = end - 2;
    }
    cur[0] = '\r';
    cur[1] = '\n';

    (void)write(2, buf, cur - buf + 2);
}

void log_err(int errnum, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_vlog(errnum, fmt, args);
    va_end(args);
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

    va_list args;
    va_start(args, fmt);
    do_vlog(0, fmt, args);
    va_end(args);
}
