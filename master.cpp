// system
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
// proj
#include "pty.h"
#include "util.h"
#include "protocol.h"


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
    (void)user;
    if (p.cmd == CMD_DATA) {
        if (TEMP_FAILURE_RETRY(write(STDOUT_FILENO, p.payload, p.size)) != (ssize_t)p.size) {
            log_err(errno, "write(STDOUT_FILENO, p.payload, p.size)");
            return -1;
        }
    } else {
        log_err(0, "Unknown cmd: %u", p.cmd);
        return -1;
    }
    return 0;
}

int main(int argc, char *const *argv) {
    // check argv
    if (argc <= 1) {
        log_err(0, "usage: pty_proxy_master SLAVE_CMD ARGS...");
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
        (void)execv(argv[1], argv + 1);
        log_err(errno, "execv()");
        return -1;
    }

    // parent
    (void)close(child_r);
    (void)close(child_w);

    // setup sigwinch
    (void)signal(SIGWINCH, &set_winch);

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

    Parser p;
    while (!p.eof) {
        // sigwinch
        if (g_winch) {
            g_winch = 0;
            struct winsize ws = {};
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) {
                log_err(errno, "ioctl(STDIN_FILENO, TIOCGWINSZ, &ws)");
                return -1;
            }
            if (0 != send_ws(parent_w, ws)) {
                return -1;
            }
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(parent_r, &fds);

        if (select(parent_r + 1, &fds, NULL, NULL, NULL) == -1) {
            if (errno != EINTR) {
                log_err(errno, "select()");
                return -1;
            }
            // else may be sigwinch
            FD_ZERO(&fds);
        }

        // stdin --> child
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char bufstore[MAX_FRAME_SIZE];
            const size_t k_buf_size = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
            char *buf = &bufstore[FRAME_HEADER_SIZE];

            ssize_t nread = TEMP_FAILURE_RETRY(read(STDIN_FILENO, buf, k_buf_size));
            if (nread < 0) {
                log_err(errno, "read(STDIN_FILENO)");
                return -1;
            }
            if (nread == 0) {
                break;
            }

            if (0 != send_data(parent_w, buf, nread)) {
                return -1;
            }
        }

        // child --> stdout
        if (FD_ISSET(parent_r, &fds)) {
            if (0 != feed_frame(p, parent_r, frame_cb, NULL)) {
                return -1;
            }
        }
    }   // while (true)

    return 0;
}
