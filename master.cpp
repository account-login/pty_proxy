// system
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
// proj
#include "pty.h"
#include "util.h"
#include "protocol.h"


struct Context {
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int exit_flag = 0;
    int l2r = 0;
    int r2l = 0;
    int msg_eof = 0;
    Stream stream;
};


volatile static sig_atomic_t g_winch = 1;
static struct termios g_tty_orig;

// Reset terminal mode on program exit
static void tty_reset(void) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_tty_orig);
}

static void set_winch(int) {
    g_winch = 1;
}

static int frame_cb(Parser &p, void *user) {
    Context &ctx = *(Context *)user;
    if (p.cmd == CMD_DATA) {
        if (TEMP_FAILURE_RETRY(write(STDOUT_FILENO, p.payload, p.size)) != (ssize_t)p.size) {
            log_err(errno, "write(STDOUT_FILENO, p.payload, p.size)");
            return -1;
        }
    } else if (p.cmd == CMD_EOF) {
        log_dbg("[frame_cb] EOF msg received");
        ctx.msg_eof = 1;
        return 0;
    } else {
        log_err(0, "Unknown cmd: %u", p.cmd);
        return -1;
    }
    return 0;
}

// stdin --> child
static void *l2r(void *user) {
    Context &ctx = *(Context *)user;
    int ret = 0;

    // unblock sigwinch for me
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGWINCH);
    if (0 != pthread_sigmask(SIG_UNBLOCK, &sigset, NULL)) {
        log_err(errno, "pthread_sigmask(SIG_UNBLOCK, &sigset, NULL)");
    }
    // setup sigwich
    struct sigaction sa = {};
    sa.sa_handler = &set_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGWINCH, &sa, NULL);

    while (1) {
        // sigwinch
        if (g_winch) {
            g_winch = 0;
            struct winsize ws = {};
            if (0 != (ret = ioctl(STDIN_FILENO, TIOCGWINSZ, &ws))) {
                log_err(errno, "ioctl(STDIN_FILENO, TIOCGWINSZ, &ws)");
                break;
            }
            if (0 != (ret = send_ws(&ctx.stream, ws))) {
                break;
            }
        }

        char bufstore[MAX_FRAME_SIZE];
        const size_t k_buf_size = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
        char *buf = &bufstore[FRAME_HEADER_SIZE];

        ssize_t nread = read(STDIN_FILENO, buf, k_buf_size);
        if (nread < 0) {
            if (errno == EINTR) {
                log_dbg("got EINTR, winch: %d", g_winch);
                continue;   // maybe sigwinch
            }

            log_err(errno, "read(STDIN_FILENO)");
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
    ctx.exit_flag |= 1;
    ctx.l2r = ret;
    pthread_cond_signal(&ctx.cond);
    pthread_mutex_unlock(&ctx.mu);
    return NULL;
}

// child --> stdout
static void *r2l(void *user) {
    Context &ctx = *(Context *)user;
    int ret = 0;
    Parser p;
    while (!p.eof && !ctx.msg_eof && 0 == (ret = feed_frame(p, &ctx.stream, frame_cb, &ctx))) {}

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
    struct option long_options[] = {
        /* These options set a flag. */
        {"base64", no_argument, &arg_base64, 1},
        {0, 0, 0, 0}
    };

    int option_index = -1;
    while (-1 != getopt_long(argc, argv, "", long_options, &option_index)) {}
    int slave_cmd_argc = argc - optind;
    char *const *slave_cmd_argv = &argv[optind];
    if (slave_cmd_argc < 1) {
        log_err(0, "usage: pty_proxy_master [--base64] -- SLAVE_CMD ARGS...");
        return 1;
    }

    // pipes
    int pipe_fd[2] = {-1, -1};
    if (0 != pipe(pipe_fd)) {
        log_err(errno, "pipe()");
        return -1;
    }
    int parent_r = pipe_fd[0];
    int child_w = pipe_fd[1];
    if (0 != pipe(pipe_fd)) {
        log_err(errno, "pipe()");
        return -1;
    }
    int child_r = pipe_fd[0];
    int parent_w = pipe_fd[1];

    // fork
    pid_t pid = fork();
    if (pid < 0) {
        log_err(errno, "fork()");
        return -1;
    }

    // child
    if (pid == 0) {
        (void)close(parent_r);
        (void)close(parent_w);
        if (dup2(child_r, STDIN_FILENO) == -1) {
            log_err(errno, "dup2(child_r, STDIN_FILENO)");
            return -1;
        }
        if (dup2(child_w, STDOUT_FILENO) == -1) {
            log_err(errno, "dup2(child_w, STDOUT_FILENO)");
            return -1;
        }
        if (child_r > STDERR_FILENO) {
            (void)close(child_r);
        }
        if (child_w > STDERR_FILENO) {
            (void)close(child_w);
        }
        (void)execvp(slave_cmd_argv[0], slave_cmd_argv);
        log_err(errno, "execvp()");
        return -1;
    }

    // parent
    (void)close(child_r);
    (void)close(child_w);

    // block sigwinch for all threads
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGWINCH);
    if (0 != pthread_sigmask(SIG_BLOCK, &sigset, NULL)) {
        log_err(errno, "pthread_sigmask(SIG_BLOCK, &sigset, NULL)");
        return -1;
    }

    // reset tty on exit
    if (atexit(tty_reset) != 0) {
        log_err(errno, "atexit(tty_reset)");
        return -1;
    }

    // Place terminal in raw mode so that we can pass all terminal
    // input to the pseudoterminal master untouched
    if (int err = tty_set_raw(STDIN_FILENO, &g_tty_orig)) {
        log_err(err, "tty_set_raw(STDIN_FILENO)");
        return -1;
    }

    // init ctx
    Context ctx;
    ctx.stream.rfd = parent_r;
    ctx.stream.wfd = parent_w;
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
