// self
#include "pty.h"
#include "util.h"


int pty_master_open(int &fd, std::string &slave_name) {
    int err = 0;

    // Open pty master
    fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd == -1) {
        err = errno;
        log_err(err, "posix_openpt()");
        goto L_RETURN;
    }

    // Grant access to slave pty
    if (grantpt(fd) == -1) {
        err = errno;
        log_err(err, "grantpt()");
        goto L_RETURN;
    }

    // Unlock slave pty
    if (unlockpt(fd) == -1) {
        err = errno;
        log_err(err, "unlockpt()");
        goto L_RETURN;
    }

    // Get slave pty name
    if (const char *p = ptsname(fd)) {
        slave_name = p;
    } else {
        err = errno;
        log_err(err, "ptsname()");
        goto L_RETURN;
    }

L_RETURN:
    if (err) {
        (void)close(fd);
        fd = -1;
    }
    return err;
}

// http://www.man7.org/tlpi/code/online/book/pty/pty_fork.c.html
int pty_fork(
    pid_t &pid, int &fd, std::string &slave_name,
    const struct termios *slave_termios, const struct winsize *slave_ws)
{
    int err = 0;
    pid = -1;

    if ((err = pty_master_open(fd, slave_name))) {
        log_err(err, "pty_master_open()");
        return err;
    }

    pid = fork();
    // parent failed
    if (pid == -1) {
        err = errno;
        log_err(err, "fork()");
        (void)close(fd);
        fd = -1;
        return err;
    }

    // parent
    if (pid != 0) {
        return 0;
    }

    /* Child falls through to here */
    // Not needed in child
    (void)close(fd);
    int slave_fd = -1;

    // Start a new session
    if (setsid() == -1) {
        err = errno;
        log_err(err, "setsid()");
        goto L_RETURN;
    }

    // Becomes controlling tty
    slave_fd = open(slave_name.c_str(), O_RDWR);
    if (slave_fd == -1) {
        err = errno;
        log_err(err, "open(slave_name)");
        goto L_RETURN;
    }

// #ifdef TIOCSCTTY
//     // Acquire controlling tty on BSD
//     if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
//         err = errno;
//         log_err(err, "ioctl(slave_fd, TIOCSCTTY, 0)");
//         goto L_RETURN;
//     }
// #endif

    // Set slave tty attributes
    if (slave_termios != NULL) {
        if (tcsetattr(slave_fd, TCSANOW, slave_termios) == -1) {
            err = errno;
            log_err(err, "tcsetattr(slave_fd, TCSANOW, slave_termios)");
            goto L_RETURN;
        }
    }

    // Set slave tty window size
    if (slave_ws != NULL) {
        if (ioctl(slave_fd, TIOCSWINSZ, slave_ws) == -1) {
            err = errno;
            log_err(err, "ioctl(slave_fd, TIOCSWINSZ, slave_ws)");
            goto L_RETURN;
        }
    }

    /* Duplicate pty slave to be child's stdin, stdout, and stderr */
    if (dup2(slave_fd, STDIN_FILENO) == -1) {
        err = errno;
        log_err(err, "dup2(slave_fd, STDIN_FILENO)");
        goto L_RETURN;
    }
    if (dup2(slave_fd, STDOUT_FILENO) == -1) {
        err = errno;
        log_err(err, "dup2(slave_fd, STDOUT_FILENO)");
        goto L_RETURN;
    }
    if (dup2(slave_fd, STDERR_FILENO) == -1) {
        err = errno;
        log_err(err, "dup2(slave_fd, STDERR_FILENO)");
        goto L_RETURN;
    }

L_RETURN:
    // Safety check
    if (slave_fd > STDERR_FILENO) {
        // No longer need this fd
        (void)close(slave_fd);
    }
    return err;
}

/* Place terminal referred to by 'fd' in cbreak mode (noncanonical mode
   with echoing turned off). This function assumes that the terminal is
   currently in cooked mode (i.e., we shouldn't call it if the terminal
   is currently in raw mode, since it does not undo all of the changes
   made by the ttySetRaw() function below). Return 0 on success, or -1
   on error. If 'prevTermios' is non-NULL, then use the buffer to which
   it points to return the previous terminal settings. */
int tty_set_cbreak(int fd, struct termios *prev) {
    int err = 0;
    struct termios t = {};
    if (tcgetattr(fd, &t) == -1) {
        err = errno;
        goto L_RETURN;
    }

    if (prev != NULL) {
        *prev = t;
    }

    t.c_lflag &= ~(ICANON | ECHO);
    t.c_lflag |= ISIG;

    t.c_iflag &= ~ICRNL;

    t.c_cc[VMIN] = 1;                   /* Character-at-a-time input */
    t.c_cc[VTIME] = 0;                  /* with blocking */

    if (tcsetattr(fd, TCSAFLUSH, &t) == -1) {
        err = errno;
        goto L_RETURN;
    }

L_RETURN:
    return err;
}

/* Place terminal referred to by 'fd' in raw mode (noncanonical mode
   with all input and output processing disabled). Return 0 on success,
   or -1 on error. If 'prevTermios' is non-NULL, then use the buffer to
   which it points to return the previous terminal settings. */
int tty_set_raw(int fd, struct termios *prev) {
    int err = 0;
    struct termios t = {};
    if (tcgetattr(fd, &t) == -1) {
        err = errno;
        goto L_RETURN;
    }

    if (prev != NULL) {
        *prev = t;
    }

    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
                        /* Noncanonical mode, disable signals, extended
                           input processing, and echoing */

    t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
                      INPCK | ISTRIP | IXON | PARMRK);
                        /* Disable special handling of CR, NL, and BREAK.
                           No 8th-bit stripping or parity error handling.
                           Disable START/STOP output flow control. */

    t.c_oflag &= ~OPOST;                /* Disable all output processing */

    t.c_cc[VMIN] = 1;                   /* Character-at-a-time input */
    t.c_cc[VTIME] = 0;                  /* with blocking */

    if (tcsetattr(fd, TCSAFLUSH, &t) == -1) {
        err = errno;
        goto L_RETURN;
    }

L_RETURN:
    return err;
}
