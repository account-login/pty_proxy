// system
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
// self
#include "protocol.h"
#include "base64.h"
#include "util.h"


ssize_t stream_read(Stream *s, void *buf, size_t bufsize) {
    assert(bufsize > 0);
    if (!s->base64) {
        return TEMP_FAILURE_RETRY(read(s->rfd, buf, bufsize));
    }

    assert(s->buflen < sizeof(s->rbuf));
    size_t read_limit = sizeof(s->rbuf) - s->buflen;
    if (read_limit > bufsize + bufsize / 3) {
        read_limit = bufsize + bufsize / 3;
    }
    ssize_t raw_read = TEMP_FAILURE_RETRY(read(s->rfd, &s->rbuf[s->buflen], read_limit));
    if (raw_read <= 0) {
        return raw_read;
    }
    s->buflen += (size_t)raw_read;
    assert(s->buflen <= sizeof(s->rbuf));

    size_t insize = s->buflen;
    size_t outsize = bufsize;
    if (0 != b64_decode(s->rbuf, &insize, (uint8_t *)buf, &outsize)) {
        return -1;
    }
    assert(insize <= s->buflen && outsize <= bufsize);
    memmove(s->rbuf, &s->rbuf[insize], s->buflen - insize);
    s->buflen -= insize;
    log_dbg("[stream_read] [insize:%zu][outsize:%zu] [remain:%zu]", insize, outsize, s->buflen);

    return (ssize_t)outsize;
}

ssize_t stream_write(Stream *s, const void *buf, size_t bufsize) {
    assert(bufsize > 0);
    if (!s->base64) {
        return TEMP_FAILURE_RETRY(write(s->wfd, buf, bufsize));
    }

    const size_t k_max_stream_write = k_input_buf_size;
    assert(b64_encoded_size(k_max_stream_write) <= sizeof(s->wbuf));

    const uint8_t *input_buf = (const uint8_t *)buf;
    for (size_t remain = bufsize; remain > 0; ) {
        size_t block_size = remain > k_max_stream_write ? k_max_stream_write : remain;
        size_t outsize = b64_encoded_size(block_size);
        b64_encode(input_buf, block_size, s->wbuf);
        ssize_t raw_write = TEMP_FAILURE_RETRY(write(s->wfd, s->wbuf, outsize));
        if (raw_write < 0) {
            return raw_write;
        }
        if (raw_write < (ssize_t)block_size) {
            log_err(0, "[stream_write] short write [raw_write:%zd] < [block_size:%zu]", raw_write, block_size);
            return -1;
        }

        input_buf += block_size;
        remain -= block_size;
    }
    return (ssize_t)bufsize;
}

static uint8_t g_send_seq = 0;
static uint8_t g_recv_seq = 0;

int send_ws(Stream *s, const struct winsize &ws) {
    log_dbg("[send_ws] [row:%u][col:%u]", ws.ws_row, ws.ws_col);

    char buf[4 + 4];
    buf[0] = 4;
    buf[1] = 0;
    buf[2] = CMD_WS;
    buf[3] = g_send_seq++;
    buf[4] = (uint8_t)ws.ws_row;
    buf[5] = (uint8_t)(ws.ws_row >> 8);
    buf[6] = (uint8_t)ws.ws_col;
    buf[7] = (uint8_t)(ws.ws_col >> 8);

    if (stream_write(s, buf, sizeof(buf)) != sizeof(buf)) {
        log_err(errno, "send_ws()");
        return -1;
    }
    return 0;
}

int send_data(Stream *s, const char *buf, size_t len) {
    assert(0 < len && len + FRAME_HEADER_SIZE <= MAX_FRAME_SIZE);
    log_dbg("[send_data] [seq:%u][len:%zu]", g_send_seq, len);

    char *head = (char *)(buf - FRAME_HEADER_SIZE);
    head[0] = (uint8_t)(len & 0xff);
    head[1] = (uint8_t)(len >> 8);
    head[2] = CMD_DATA;
    head[3] = g_send_seq++;

    size_t write_len = FRAME_HEADER_SIZE + len;
    if (stream_write(s, head, write_len) != (ssize_t)write_len) {
        log_err(errno, "send_data()");
        return -1;
    }
    return 0;
}

int send_eof(Stream *s) {
    char buf[4];
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = CMD_EOF;
    buf[3] = g_send_seq++;

    if (stream_write(s, buf, sizeof(buf)) != sizeof(buf)) {
        log_err(errno, "send_eof()");
        return -1;
    }
    return 0;
}

int feed_frame(Parser &p, Stream *s, int cb(Parser &p, void *user), void *user) {
    assert(!p.eof);
    log_dbg("[feed_frame] called");

    ssize_t nread = stream_read(s, &p.input_buf[p.buf_len], k_input_buf_size - p.buf_len);
    if (nread < 0) {
        log_err(errno, "feed_frame() read(fd)");
        return -1;
    }
    if (nread == 0) {
        p.eof = 1;
        return 0;
    }

    // parse each frame
    size_t buf_end = p.buf_len + (size_t)nread;
    size_t buf_pos = 0;
    while (buf_pos < buf_end) {
        if (buf_pos + FRAME_HEADER_SIZE > buf_end) {
            log_dbg("[feed_frame] not enough header [nread:%zd]", nread);
            break;
        }
        const uint8_t *data = &p.input_buf[buf_pos];
        size_t size = (size_t)data[0] | ((size_t)data[1] << 8);
        uint8_t cmd = data[2];
        uint8_t seq = data[3];
        const uint8_t *payload = data + FRAME_HEADER_SIZE;
        if (FRAME_HEADER_SIZE + size > MAX_FRAME_SIZE) {
            log_err(0, "[feed_frame] frame too large [seq:%u][size:%zu][cmd:%u] [pos:%zu]", seq, size, cmd, buf_pos);
            return -1;
        }
        if (buf_pos + FRAME_HEADER_SIZE + size > buf_end) {
            log_dbg("[feed_frame] not enough payload [nread:%zd] [seq:%u][cmd:%u][size:%zu] [pos:%zu][end:%zu]",
                nread, seq, cmd, size, buf_pos, buf_end);
            break;
        }
        if (seq != g_recv_seq++) {
            log_err(0, "[feed_frame] [seq:%u] != [expected:%u] [size:%zu][cmd:%u] [pos:%zu]", seq, g_recv_seq - 1, size, cmd, buf_pos);
            return -1;
        }

        // output
        log_dbg("[feed_frame] [seq:%u][size:%zu][cmd:%u] [pos:%zu]", seq, size, cmd, buf_pos);
        p.size = size;
        p.cmd = cmd;
        p.payload = payload;
        int err = cb(p, user);
        if (err) {
            return err;
        }

        // next
        buf_pos += FRAME_HEADER_SIZE + size;
    }

    // move incomplete frame
    if (buf_pos < buf_end) {
        memmove(p.input_buf, p.input_buf + buf_pos, buf_end - buf_pos);
    }
    p.buf_len = buf_end - buf_pos;
    return 0;
}
