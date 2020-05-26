#pragma once


#ifndef TEMP_FAILURE_RETRY
#   define TEMP_FAILURE_RETRY(exp)             \
    ({                                         \
        decltype(exp) _rc;                     \
        do {                                   \
            _rc = (exp);                       \
        } while (_rc == -1 && errno == EINTR); \
        _rc;                                   \
    })
#endif


void log_err(int errnum, const char *fmt, ...);
void log_dbg(const char *fmt, ...);
