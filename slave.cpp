// system
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
// proj
#include "pty.h"
#include "protocol.h"
#include "util.h"


struct Context {
    pid_t pid = -1;
    int pty_fd = -1;
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int exit_flag = 0;
    int l2r = 0;
    int r2l = 0;
    Stream stream;
};


static int frame_cb(Parser &p, void *user) {
    Context &ctx = *(Context *)user;

    if (p.cmd == CMD_DATA) {
        if (TEMP_FAILURE_RETRY(write(ctx.pty_fd, p.payload, p.size)) != (ssize_t)p.size) {
            log_err(errno, "write(ctx.pty_fd, p.payload, p.size)");
            return -1;
        }
    } else if (p.cmd == CMD_WS) {
        if (p.size < 4) {
            log_err(0, "CMD_WS [size:%zu] < 4", p.size);
            return -1;
        }
        struct winsize ws = {};
        ws.ws_row = (uint16_t)p.payload[0] | ((uint16_t)p.payload[1] << 8);
        ws.ws_col = (uint16_t)p.payload[2] | ((uint16_t)p.payload[3] << 8);
        log_dbg("[frame_cb] got CMD_WS [row:%u][col:%u]", ws.ws_row, ws.ws_col);

        if (ioctl(ctx.pty_fd, TIOCSWINSZ, &ws) == -1) {
            log_err(errno, "ioctl(fd, TIOCSWINSZ, &ws)");
            return -1;
        }
        (void)kill(ctx.pid, SIGWINCH);
    } else {
        log_err(0, "Unknown cmd: %u", p.cmd);
        return -1;
    }

    return 0;
}

// stdin --> pty
static void *l2r(void *user) {
    Context &ctx = *(Context *)user;
    Parser p;
    int ret = 0;
    while (!p.eof && 0 == (ret = feed_frame(p, &ctx.stream, frame_cb, &ctx))) {}

    pthread_mutex_lock(&ctx.mu);
    ctx.exit_flag |= 1;
    ctx.l2r = ret;
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mu);
    return NULL;
}

// pty --> stdout
static void *r2l(void *user) {
    Context &ctx = *(Context *)user;
    int ret = 0;
    while (1) {
        char output_buf[MAX_FRAME_SIZE];
        char *buf = &output_buf[FRAME_HEADER_SIZE];
        const size_t k_output_buf_size = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
        int nread = TEMP_FAILURE_RETRY(read(ctx.pty_fd, buf, k_output_buf_size));
        if (nread < 0) {
            log_err(errno, "read(fd)");
            ret = -1;
            break;
        }
        if (nread == 0) {
            break;
        }

        if (0 != (ret = send_data(&ctx.stream, buf, nread))) {
            break;
        }
    }

    pthread_mutex_lock(&ctx.mu);
    ctx.exit_flag |= 2;
    ctx.r2l = ret;
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mu);
    return NULL;
}

int main(int argc, char *const *argv) {
    // parse args
    int arg_base64 = 0;
    int arg_greeting = 0;
    struct option long_options[] = {
        /* These options set a flag. */
        {"base64", no_argument, &arg_base64, 1},
        {"greeting", no_argument, &arg_greeting, 1},
        {0, 0, 0, 0}
    };

    int option_index = -1;
    while (-1 != getopt_long(argc, argv, "", long_options, &option_index)) {}
    char *const cmd_argv_default[] = {(char *)"/bin/sh", NULL};
    char *const *cmd_argv = argc > optind ? &argv[optind] : cmd_argv_default;

    // fork
    Context ctx;
    std::string slave_name;
    int err = pty_fork(ctx.pid, ctx.pty_fd, slave_name, NULL, NULL);
    if (err) {
        log_err(err, "pty_fork()");
        return -1;
    }

    if (ctx.pid == 0) {
        // child
        (void)execvp(cmd_argv[0], cmd_argv);
        log_err(errno, "execvp()");
        return -1;
    }

    // parent
    if (isatty(STDIN_FILENO)) {
        // prevent echoing
        (void)tty_set_raw(STDIN_FILENO, NULL);
    }
    // ready to accept input and produce output
    if (arg_greeting) {
        const char *k_greeting = "PTY_SLAVE_GREETING";
        (void)write(STDOUT_FILENO, k_greeting, strlen(k_greeting));
    }

    // init stream
    ctx.stream.rfd = STDIN_FILENO;
    ctx.stream.wfd = STDOUT_FILENO;
    ctx.stream.base64 = arg_base64;

    // start threads
    pthread_attr_t attr;
    if (0 != pthread_attr_init(&attr)) {
        log_err(errno, "pthread_attr_init()");
        return -1;
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread_l2r;
    pthread_t thread_r2l;
    if (0 != pthread_create(&thread_l2r, &attr, &l2r, &ctx)) {
        log_err(errno, "pthread_create(&thread_l2r, &attr, &l2r, &ctx)");
        return -1;
    }
    if (0 != pthread_create(&thread_r2l, &attr, &r2l, &ctx)) {
        log_err(errno, "pthread_create(&thread_r2l, &attr, &r2l, &ctx)");
        return -1;
    }

    // wait for threads
    pthread_mutex_lock(&ctx.mu);
    while (!ctx.exit_flag) {
        pthread_cond_wait(&ctx.cond, &ctx.mu);
    }
    log_dbg("[exit_flag:%d] [l2r:%d][r2l:%d]", ctx.exit_flag, ctx.l2r, ctx.r2l);
    pthread_mutex_unlock(&ctx.mu);
    return ctx.l2r ? ctx.l2r : ctx.r2l;
}
