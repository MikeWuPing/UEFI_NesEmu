#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>

#include "utils.h"
#include <stdarg.h>


void LOG(enum LogLevel logLevel, const char *fmt, ...) {
    if (TRACER)
        return;
    if (logLevel < LOGLEVEL)
        return;

    static CONST CHAR8 *prefix[5] = {
        "TRACE > ", "DEBUG > ", "ERROR > ", "WARN  > ", "INFO  > "
    };
    if ((unsigned)logLevel < 5) {
        AsciiPrint("%a", prefix[logLevel]);
    }

    CHAR8 buf[512];
    VA_LIST ap;
    VA_START(ap, fmt);
    AsciiVSPrint(buf, sizeof(buf), fmt, ap);
    VA_END(ap);

    AsciiPrint("%a\r\n", buf);
}

void to_pixel_format(const uint32_t *restrict in, uint32_t *restrict out, size_t size, ColorFormat format) {
    for (size_t i = 0; i < size; i++) {
        switch (format) {
            case ARGB8888:
                out[i] = in[i];
                break;
            case ABGR8888:
                out[i] = (in[i] & 0xff000000) |
                         ((in[i] << 16) & 0x00ff0000) |
                         (in[i] & 0x0000ff00) |
                         ((in[i] >> 16) & 0x000000ff);
                break;
            default:
                LOG(DEBUG, "Unsupported format");
                quit(EXIT_FAILURE);
        }
    }
}

char *get_file_name(char *path) {
    char *pfile = path + AsciiStrLen(path);
    for (; pfile > path; pfile--) {
        if ((*pfile == '\\') || (*pfile == '/')) {
            pfile++;
            break;
        }
    }
    return pfile;
}

uint64_t next_power_of_2(uint64_t num) {
    uint64_t power = 1;
    while (power < num)
        power *= 2;
    return power;
}

size_t file_size(void *file) {
    // Not used in the UEFI port; ROM loading goes through uefi_read_file.
    (void)file;
    return 0;
}

void quit(int code) {
    // In UEFI there is no equivalent of exit(). We loop forever so the user can
    // read the screen before closing QEMU. The shell reclaims resources on
    // image unload.
    if (EXIT_PAUSE) {
        AsciiPrint("Press Ctrl-A X in QEMU to exit, or close the window.\r\n");
    }
    for (;;) {
        // halt
    }
}
