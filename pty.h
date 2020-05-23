#pragma once

// system
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <string>


int pty_master_open(int &fd, std::string &slave_name);
int pty_fork(
    pid_t &pid, int &fd, std::string &slave_name,
    const struct termios *slave_termios, const struct winsize *slave_ws
);
int tty_set_cbreak(int fd, struct termios *prev);
int tty_set_raw(int fd, struct termios *prev);
