// system
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
// self
#include "protocol.h"
#include "util.h"


int send_ws(int fd, const struct winsize &ws) {
    log_dbg("[send_ws] [row:%u][col:%u]", ws.ws_row, ws.ws_col);

    char buf[4 + 4];
    buf[0] = 4;
    buf[1] = 0;
    buf[2] = CMD_WS;
    buf[3] = 0;
    buf[4] = (uint8_t)ws.ws_row;
    buf[5] = (uint8_t)(ws.ws_row >> 8);
    buf[6] = (uint8_t)ws.ws_col;
    buf[7] = (uint8_t)(ws.ws_col >> 8);

    if (TEMP_FAILURE_RETRY(write(fd, buf, sizeof(buf))) != sizeof(buf)) {
        log_err(errno, "send_ws()");
        return -1;
    }
    return 0;
}

int send_data(int fd, const char *buf, size_t len) {
    assert(0 < len && len + FRAME_HEADER_SIZE <= MAX_FRAME_SIZE);
    log_dbg("[send_data] [len:%zu]", len);

    char *head = (char *)(buf - FRAME_HEADER_SIZE);
    head[0] = (uint8_t)(len & 0xff);
    head[1] = (uint8_t)(len >> 8);
    head[2] = CMD_DATA;
    head[3] = 0;

    size_t write_len = FRAME_HEADER_SIZE + len;
    if (TEMP_FAILURE_RETRY(write(fd, head, write_len)) != (ssize_t)write_len) {
        log_err(errno, "send_data()");
        return -1;
    }
    return 0;
}

int feed_frame(Parser &p, int fd, int cb(Parser &p, void *user), void *user) {
    assert(!p.eof);
    log_dbg("[free_frame] called");

    ssize_t nread = TEMP_FAILURE_RETRY(read(fd, &p.input_buf[p.buf_pos], k_input_buf_size - p.buf_pos));
    if (nread < 0) {
        log_err(errno, "feed_frame() read(fd)");
        return -1;
    }
    if (nread == 0) {
        p.eof = 1;
        return 0;
    }

    // parse each frame
    size_t buf_end = p.buf_pos + (size_t)nread;
    while (p.buf_pos < buf_end) {
        if (p.buf_pos + FRAME_HEADER_SIZE > buf_end) {
            log_dbg("[read_frame] not enough header [nread:%zd]", nread);
            break;
        }
        const uint8_t *data = &p.input_buf[p.buf_pos];
        size_t size = (size_t)data[0] | ((size_t)data[1] << 8);
        uint8_t cmd = data[2];
        (void)data[3];
        const uint8_t *payload = data + FRAME_HEADER_SIZE;
        if (p.buf_pos + FRAME_HEADER_SIZE + size > buf_end) {
            log_dbg("[read_frame] not enough payload [nread:%zd] [cmd:%u][size:%zu]", cmd, nread, size);
            break;
        }

        // output
        log_dbg("[feed_frame] [size:%zu][cmd:%u]", size, cmd);
        p.size = size;
        p.cmd = cmd;
        p.payload = payload;
        int err = cb(p, user);
        if (err) {
            return err;
        }

        // next
        p.buf_pos += FRAME_HEADER_SIZE + size;
    }

    // move incomplete frame
    if (p.buf_pos < buf_end) {
        memmove(p.input_buf, p.input_buf + p.buf_pos, buf_end - p.buf_pos);
    }
    p.buf_pos = buf_end - p.buf_pos;
    return 0;
}
