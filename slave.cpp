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
    int pty_fd = -1;        // rw
    int no_tty = 0;
    int child_in = -1;      // w
    int child_out = -1;     // r
    int child_err = -1;     // r
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int exit_flag = 0;
    int l2r = 0;
    int r2l = 0;
    int msg_eof = 0;
    Stream stream;
};


static int frame_cb(Parser &p, void *user) {
    Context &ctx = *(Context *)user;

    if (ctx.msg_eof) {
        log_err(0, "got msg after CMD_EOF");
        return 0;
    }

    if (p.cmd == CMD_DATA) {
        int wfd = ctx.no_tty ? ctx.child_in : ctx.pty_fd;
        if (TEMP_FAILURE_RETRY(write(wfd, p.payload, p.size)) != (ssize_t)p.size) {
            log_err(errno, "write(wfd, p.payload, p.size)");
            return -1;
        }
    } else if (p.cmd == CMD_EOF) {
        log_dbg("[frame_cb] EOF msg received");
        if (ctx.no_tty) {
            (void)close(ctx.child_in);
        } else {
            // ctrl+d
            // NOTE: not working if sending EOF too early
            (void)write(ctx.pty_fd, "\x04", 1);
        }
        ctx.msg_eof = 1;
        return 0;
    } else if (p.cmd == CMD_WS) {
        if (p.size < 4) {
            log_err(0, "CMD_WS [size:%zu] < 4", p.size);
            return -1;
        }
        if (ctx.no_tty) {
            return 0;
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
    while (!p.eof && !ctx.msg_eof && 0 == (ret = feed_frame(p, &ctx.stream, frame_cb, &ctx))) {}

    pthread_mutex_lock(&ctx.mu);
    ctx.exit_flag |= 1;
    ctx.l2r = ret;
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mu);
    return NULL;
}

// pty --> stdout
static void r2l_fd(Context &ctx, int fd, uint8_t cmd) {
    int ret = 0;
    while (1) {
        char output_buf[MAX_FRAME_SIZE];
        char *buf = &output_buf[FRAME_HEADER_SIZE];
        const size_t k_output_buf_size = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
        int nread = TEMP_FAILURE_RETRY(read(fd, buf, k_output_buf_size));
        if (nread < 0) {
            log_err(errno, "read(fd)");
            ret = -1;
            break;
        }
        if (nread == 0) {
            break;
        }

        if (0 != (ret = send_payload(&ctx.stream, cmd, buf, nread))) {
            break;
        }
    }

    if (cmd == CMD_DATA) {
        // eof
        (void)send_eof(&ctx.stream);

        pthread_mutex_lock(&ctx.mu);
        ctx.exit_flag |= 2;
        ctx.r2l = ret;
        pthread_cond_signal(&ctx.cond);
        pthread_mutex_unlock(&ctx.mu);
    }
}

static void *r2l_pty(void *user) {
    Context &ctx = *(Context *)user;
    r2l_fd(ctx, ctx.pty_fd, CMD_DATA);
    return NULL;
}

static void *r2l_out(void *user) {
    Context &ctx = *(Context *)user;
    r2l_fd(ctx, ctx.child_out, CMD_DATA);
    return NULL;
}

static void *r2l_err(void *user) {
    Context &ctx = *(Context *)user;
    r2l_fd(ctx, ctx.child_err, CMD_ERR);
    return NULL;
}

static int pipe_fork(pid_t &pid, int &child_in, int &child_out, int &child_err) {
    int fd_list[6] = {-1, -1, -1, -1, -1, -1};
    int &parent_r_out = fd_list[0];
    int &child_w_out = fd_list[1];
    int &child_r_in = fd_list[2];
    int &parent_w_in = fd_list[3];
    int &parent_r_err = fd_list[4];
    int &child_w_err = fd_list[5];

    // stdout
    int pipe_fd[2] = {-1, -1};
    if (0 != pipe(pipe_fd)) {
        log_err(errno, "pipe()");
        goto L_ERR;
    }
    parent_r_out = pipe_fd[0];
    child_w_out = pipe_fd[1];
    // stdin
    if (0 != pipe(pipe_fd)) {
        log_err(errno, "pipe()");
        goto L_ERR;
    }
    child_r_in = pipe_fd[0];
    parent_w_in = pipe_fd[1];
    // stderr
    if (0 != pipe(pipe_fd)) {
        log_err(errno, "pipe()");
        goto L_ERR;
    }
    parent_r_err = pipe_fd[0];
    child_w_err = pipe_fd[1];

    // fork
    pid = fork();
    if (pid < 0) {
        log_err(errno, "fork()");
        goto L_ERR;
    }

    if (pid == 0) {
        // child
        (void)close(parent_w_in);
        (void)close(parent_r_out);
        (void)close(parent_r_err);

        if (dup2(child_r_in, STDIN_FILENO) == -1) {
            log_err(errno, "dup2(child_r_in, STDIN_FILENO)");
            return -1;
        }
        if (dup2(child_w_out, STDOUT_FILENO) == -1) {
            log_err(errno, "dup2(child_w_out, STDOUT_FILENO)");
            return -1;
        }
        if (dup2(child_w_err, STDERR_FILENO) == -1) {
            log_err(errno, "dup2(child_w_err, STDERR_FILENO)");
            return -1;
        }
        if (child_r_in > STDERR_FILENO) {
            (void)close(child_r_in);
        }
        if (child_w_out > STDERR_FILENO) {
            (void)close(child_w_out);
        }
        if (child_w_err > STDERR_FILENO) {
            (void)close(child_w_err);
        }
        return 0;
    } else {
        // parent
        (void)close(child_r_in);
        (void)close(child_w_out);
        (void)close(child_w_err);

        child_in = parent_w_in;
        child_out = parent_r_out;
        child_err = parent_r_err;
        return 0;
    }

L_ERR:
    for (size_t i = 0; i < 6; ++i) {
        if (fd_list[i] >= 0) {
            (void)close(fd_list[i]);
        }
    }
    return -1;
}

int main(int argc, char *const *argv) {
    // parse args
    int arg_base64 = 0;
    int arg_greeting = 0;
    int arg_no_tty = 0;
    struct option long_options[] = {
        /* These options set a flag. */
        {"base64", no_argument, &arg_base64, 1},
        {"greeting", no_argument, &arg_greeting, 1},
        {"no-tty", no_argument, &arg_no_tty, 1},
        {0, 0, 0, 0}
    };

    int option_index = -1;
    while (-1 != getopt_long(argc, argv, "", long_options, &option_index)) {}
    char *const cmd_argv_default[] = {(char *)"/bin/sh", NULL};
    char *const *cmd_argv = argc > optind ? &argv[optind] : cmd_argv_default;

    // fork
    Context ctx;
    ctx.no_tty = arg_no_tty;
    int err = 0;
    if (ctx.no_tty) {
        err = pipe_fork(ctx.pid, ctx.child_in, ctx.child_out, ctx.child_err);
    } else {
        err = pty_fork(ctx.pid, ctx.pty_fd, NULL, NULL);
    }
    if (err) {
        log_err(err, "pty_fork() or pipe_fork()");
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
    pthread_t thread_id;
    if (0 != pthread_create(&thread_id, &attr, &l2r, &ctx)) {
        log_err(errno, "pthread_create(&thread_id, &attr, &l2r, &ctx)");
        return -1;
    }
    if (ctx.no_tty) {
        if (0 != pthread_create(&thread_id, &attr, &r2l_out, &ctx)) {
            log_err(errno, "pthread_create(&thread_id, &attr, &r2l_out, &ctx)");
            return -1;
        }
        if (0 != pthread_create(&thread_id, &attr, &r2l_err, &ctx)) {
            log_err(errno, "pthread_create(&thread_id, &attr, &r2l_err, &ctx)");
            return -1;
        }
    } else {
        if (0 != pthread_create(&thread_id, &attr, &r2l_pty, &ctx)) {
            log_err(errno, "pthread_create(&thread_id, &attr, &r2l_pty, &ctx)");
            return -1;
        }
    }

    // wait for remote exit
    pthread_mutex_lock(&ctx.mu);
    while (!(ctx.exit_flag & 2)) {
        pthread_cond_wait(&ctx.cond, &ctx.mu);
    }
    log_dbg("[exit_flag:%d] [l2r:%d][r2l:%d]", ctx.exit_flag, ctx.l2r, ctx.r2l);
    pthread_mutex_unlock(&ctx.mu);
    return ctx.l2r ? ctx.l2r : ctx.r2l;
}
