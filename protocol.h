#pragma once

// system
#include <stdint.h>
#include <stddef.h>
#include <termios.h>

#define CMD_DATA 0
#define CMD_WS 1
#define FRAME_HEADER_SIZE 4
#define MAX_FRAME_SIZE 4096

const size_t k_input_buf_size = MAX_FRAME_SIZE * 4;
const size_t k_base64_input_buf_size = MAX_FRAME_SIZE * 6;
const size_t k_base64_output_buf_size = MAX_FRAME_SIZE * 6;

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

struct Stream {
    // params
    int rfd = -1;
    int wfd = -1;
    int base64 = 0;
    // private
    size_t buflen = 0;
    uint8_t rbuf[k_base64_input_buf_size];
    uint8_t wbuf[k_base64_output_buf_size];
};

ssize_t stream_read(Stream *s, void *buf, size_t bufsize);
ssize_t stream_write(Stream *s, const void *buf, size_t bufsize);

int send_ws(Stream *s, const struct winsize &ws);
int send_data(Stream *s, const char *buf, size_t len);
int feed_frame(Parser &p, Stream *s, int cb(Parser &p, void *user), void *user);
