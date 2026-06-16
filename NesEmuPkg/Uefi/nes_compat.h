#pragma once

//
// Libc compatibility shim. The NES emulator core was lifted verbatim from
// the SDL-based upstream and still uses malloc/free/memset/memcpy/strlen/
// strncmp and friends. UEFI applications do not link against the C runtime,
// so we redirect those identifiers to their BaseMemoryLib / MemoryAllocationLib
// / BaseLib equivalents.
//
// We use a macro-redirect pattern (#define malloc __nesc_malloc) plus inline
// function definitions, rather than direct function-style macros, so that
// the identifiers continue to work in arbitrary positions (function names,
// declarations, etc.) without disrupting the parser.
//
// This file is force-included (/FI) into every translation unit in the
// NesEmu module via NesEmu.inf's [BuildOptions].
//

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

//
// size_t is normally provided by stddef.h. Since we block libc headers
// (the upstream code does not #include them any more), we provide the
// alias ourselves.
//
typedef UINTN size_t;

//
// Rename the upstream identifiers so we can define inline replacements
// without colliding with system declarations. The NES source keeps using
// the original names (malloc, free, ...) and the preprocessor rewrites
// them at use sites.
//
#define malloc   __nesc_malloc
#define calloc   __nesc_calloc
#define realloc  __nesc_realloc
#define free     __nesc_free
#define memset   __nesc_memset
#define memcpy   __nesc_memcpy
#define memmove  __nesc_memmove
#define memcmp   __nesc_memcmp
#define strlen   __nesc_strlen
#define strncmp  __nesc_strncmp
#define strcmp   __nesc_strcmp
#define strstr   __nesc_strstr
#define strchr   __nesc_strchr

static __inline void *__nesc_malloc(size_t s) {
    return AllocatePool((UINTN)s);
}

static __inline void *__nesc_calloc(size_t n, size_t s) {
    return AllocateZeroPool((UINTN)n * (UINTN)s);
}

static __inline void *__nesc_realloc(void *p, size_t s) {
    return ReallocatePool(0, (UINTN)s, p);
}

static __inline void __nesc_free(void *p) {
    if (p != NULL) {
        FreePool(p);
    }
}

static __inline void *__nesc_memset(void *s, int c, size_t n) {
    return SetMem(s, (UINTN)n, (UINT8)c);
}

static __inline void *__nesc_memcpy(void *d, const void *s, size_t n) {
    return CopyMem(d, s, (UINTN)n);
}

static __inline void *__nesc_memmove(void *d, const void *s, size_t n) {
    return CopyMem(d, s, (UINTN)n);
}

static __inline int __nesc_memcmp(const void *a, const void *b, size_t n) {
    INTN r = (INTN)CompareMem(a, b, (UINTN)n);
    if (r == 0) return 0;
    return (r > 0) ? 1 : -1;
}

static __inline size_t __nesc_strlen(const char *s) {
    return (size_t)AsciiStrLen((CONST CHAR8 *)s);
}

static __inline int __nesc_strncmp(const char *a, const char *b, size_t n) {
    INTN r = (INTN)AsciiStrnCmp((CONST CHAR8 *)a, (CONST CHAR8 *)b, (UINTN)n);
    if (r == 0) return 0;
    return (r > 0) ? 1 : -1;
}

static __inline int __nesc_strcmp(const char *a, const char *b) {
    INTN r = (INTN)AsciiStrCmp((CONST CHAR8 *)a, (CONST CHAR8 *)b);
    if (r == 0) return 0;
    return (r > 0) ? 1 : -1;
}

static __inline char *__nesc_strstr(const char *s, const char *sub) {
    return (char *)AsciiStrStr((CONST CHAR8 *)s, (CONST CHAR8 *)sub);
}

static __inline char *__nesc_strchr(const char *s, int c) {
    CONST char *p = s;
    while (*p != 0) {
        if (*p == (char)c) return (char *)p;
        p++;
    }
    return (c == 0) ? (char *)p : NULL;
}
