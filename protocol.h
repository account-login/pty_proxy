#pragma once

// system
#include <stdint.h>
#include <termios.h>

#define CMD_DATA 0
#define CMD_WS 1
#define FRAME_HEADER_SIZE 4
#define MAX_FRAME_SIZE 4096

const size_t k_input_buf_size = MAX_FRAME_SIZE * 4;

struct Parser {
    // private
    uint8_t input_buf[k_input_buf_size];
    size_t buf_pos = 0;
    // output
    uint8_t eof = 0;
    uint8_t cmd = 0;
    size_t size = 0;
    const uint8_t *payload = NULL;
};

int send_ws(int fd, const struct winsize &ws);
int send_data(int fd, const char *buf, size_t len);
int feed_frame(Parser &p, int fd, int cb(Parser &p, void *user), void *user);
