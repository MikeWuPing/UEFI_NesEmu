#pragma once

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <stdint.h>

//
// GraphicsContext replaces the SDL-backed GraphicsContext from the upstream
// emulator. It owns the GOP pointer, the cached framebuffer geometry, and the
// on-demand scaling buffer that maps the NES's 256x240 output onto whatever
// panel resolution OVMF happens to expose.
//
typedef struct GraphicsContext {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    int width;            // logical NES width  (256, or 512 in nametable mode)
    int height;           // logical NES height (240, or 480 in nametable mode)
    int scale;            // integer upscale factor chosen at init time
    int is_tv;

    UINT32 screen_width;          // current GOP mode horizontal resolution
    UINT32 screen_height;         // current GOP mode vertical resolution
    UINT32 pixels_per_scan_line;  // physical stride, may exceed screen_width
    EFI_GRAPHICS_PIXEL_FORMAT pixel_format;
    UINT8 *framebuffer;           // direct CPU pointer to the linear framebuffer

    // pre-allocated staging buffer sized for one scaled frame (screen_width*screen_height
    // is too large; we only need width*scale * height*scale and we Blt-center it)
    UINT32 *scaled_buffer;
    UINT32 scaled_width;
    UINT32 scaled_height;
} GraphicsContext;


void get_graphics_context(GraphicsContext *ctx);
void render_graphics(GraphicsContext *g_ctx, const uint32_t *buffer);
void free_graphics(GraphicsContext *ctx);

//
// Returns the most recently activated GraphicsContext (set by
// get_graphics_context), or NULL if none is active. Callers that need
// the current framebuffer geometry (e.g. to size a UI layout to the
// live panel resolution) should query through here rather than caching
// their own pointer.
//
GraphicsContext *gfx_active_context(VOID);

//
// UI helper API used by the ROM browser. Implemented on top of the same GOP.
//
typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} NesColor;

void gfx_clear(NesColor *c);
void gfx_fill_rect(INT32 x, INT32 y, INT32 width, INT32 height, NesColor *c);
void gfx_draw_text(INT32 x, INT32 y, const CHAR16 *text, NesColor *c, INT32 scale);
void gfx_present(VOID);
