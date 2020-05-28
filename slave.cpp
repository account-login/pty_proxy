// system
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/select.h>
#include <sys/types.h>
// proj
#include "pty.h"
#include "protocol.h"
#include "util.h"


struct Context {
    pid_t pid = -1;
    int pty_fd = -1;
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

    Stream s;
    s.rfd = STDIN_FILENO;
    s.wfd = STDOUT_FILENO;
    s.base64 = arg_base64;

    Parser p;
    while (!p.eof) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctx.pty_fd, &fds);
        FD_SET(STDIN_FILENO, &fds);

        if (TEMP_FAILURE_RETRY(select(ctx.pty_fd + 1, &fds, NULL, NULL, NULL)) == -1) {
            log_err(errno, "select()");
            return -1;
        }

        // stdin --> pty
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (0 != feed_frame(p, &s, frame_cb, &ctx)) {
                return -1;
            }
        }

        // pty --> stdout
        if (FD_ISSET(ctx.pty_fd, &fds)) {
            char output_buf[MAX_FRAME_SIZE];
            char *buf = &output_buf[FRAME_HEADER_SIZE];
            const size_t k_output_buf_size = MAX_FRAME_SIZE - FRAME_HEADER_SIZE;
            int nread = TEMP_FAILURE_RETRY(read(ctx.pty_fd, buf, k_output_buf_size));
            if (nread < 0) {
                log_err(errno, "read(fd)");
                return -1;
            }
            if (nread == 0) {
                break;
            }

            if (0 != send_data(&s, buf, nread)) {
                return -1;
            }
        }
    }
    return 0;
}
