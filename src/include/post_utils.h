#ifndef POST_UTILS_H
#define POST_UTILS_H

#include "m_pd.h"
#include "p_sheetmidi_types.h"
#include <stdarg.h>

// Debug post function that only posts if debug is enabled
static inline void debug_post(t_p_sheetmidi *x, const char *fmt, ...) {
    if (!x || !x->debug_enabled) return;
    
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    post("%s", buf);
}

// Regular post function for non-debug messages
static inline void info_post(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    post("%s", buf);
}

#endif // POST_UTILS_H 