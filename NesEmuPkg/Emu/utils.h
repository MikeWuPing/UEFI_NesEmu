#pragma once

#include <stdint.h>
#include <stddef.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#define TRACING_LOGS_ENABLED 0

#ifdef DEBUGGING_ENABLED
    #define EXIT_PAUSE 1
    #if TRACING_LOGS_ENABLED
    #define LOGLEVEL 0
    #else
    #define LOGLEVEL 1
#endif
#else
#define LOGLEVEL 2
#define EXIT_PAUSE 0
#endif

#define TRACER 0
#define PROFILE 0
#define PROFILE_STOP_FRAME 1
#define NAMETABLE_MODE 0

enum {
    BIT_7 = 1<<7,
    BIT_6 = 1<<6,
    BIT_5 = 1<<5,
    BIT_4 = 1<<4,
    BIT_3 = 1<<3,
    BIT_2 = 1<<2,
    BIT_1 = 1<<1,
    BIT_0 = 1
};

enum LogLevel{
    TRACE = 0,
    DEBUG,
    ERROR,
    WARN,
    INFO,
};

typedef enum ColorFormat {
    ARGB8888,
    ABGR8888
} ColorFormat;

size_t file_size(void *file);
void LOG(enum LogLevel logLevel, const char *fmt, ...);
void to_pixel_format(const uint32_t *restrict in, uint32_t *restrict out, size_t size, ColorFormat format);
uint64_t next_power_of_2(uint64_t num);
char *get_file_name(char *path);
void quit(int code);
