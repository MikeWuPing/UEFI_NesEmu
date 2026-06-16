#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>

#include "uefi_gfx.h"
#include "../Emu/utils.h"
#include "font.h"

static GraphicsContext *g_active_ctx = NULL;

STATIC
VOID
PickBestMode (
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    OUT UINT32                        *BestMode
    )
{
    UINTN SizeOfInfo;
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

    UINT32 Best4By3 = 0;
    UINT32 BestFallback = Gop->Mode->Mode;
    BOOLEAN Found4By3 = FALSE;

    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++) {
        Status = Gop->QueryMode(Gop, i, &SizeOfInfo, &Info);
        if (EFI_ERROR(Status)) {
            continue;
        }
        UINT32 W = Info->HorizontalResolution;
        UINT32 H = Info->VerticalResolution;
        // Prefer a 4:3 resolution that comfortably fits the NES 256x240 frame
        // with at least 2x scaling. 640x480 is the sweet spot for OVMF + QEMU.
        if (W == 640 && H == 480) {
            *BestMode = i;
            FreePool(Info);
            return;
        }
        if (!Found4By3 && W * 3 == H * 4 && W >= 640 && H >= 480) {
            Best4By3 = i;
            Found4By3 = TRUE;
        }
        if (W >= 640 && H >= 480 && BestFallback == Gop->Mode->Mode) {
            BestFallback = i;
        }
        FreePool(Info);
    }
    *BestMode = Found4By3 ? Best4By3 : BestFallback;
}

EFI_STATUS
InitGopInternal (
    IN OUT GraphicsContext *Ctx
    )
{
    EFI_STATUS Status;
    UINTN NumHandles;
    EFI_HANDLE *Handles;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;

    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        &NumHandles,
        &Handles);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    for (UINTN i = 0; i < NumHandles; i++) {
        Status = gBS->HandleProtocol(
            Handles[i],
            &gEfiGraphicsOutputProtocolGuid,
            (VOID **)&Gop);
        if (!EFI_ERROR(Status) && Gop != NULL) {
            break;
        }
    }
    FreePool(Handles);
    if (Gop == NULL) {
        return EFI_NOT_FOUND;
    }

    Ctx->gop = Gop;

    UINT32 BestMode;
    PickBestMode(Gop, &BestMode);
    if (BestMode != Gop->Mode->Mode) {
        Status = Gop->SetMode(Gop, BestMode);
        if (EFI_ERROR(Status)) {
            LOG(WARN, "GOP SetMode to %d failed: %r", BestMode, Status);
        }
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = Gop->Mode->Info;
    Ctx->screen_width = Info->HorizontalResolution;
    Ctx->screen_height = Info->VerticalResolution;
    Ctx->pixels_per_scan_line = Info->PixelsPerScanLine;
    Ctx->pixel_format = Info->PixelFormat;
    Ctx->framebuffer = (UINT8 *)(UINTN)Gop->Mode->FrameBufferBase;

    LOG(INFO, "GOP %dx%d (stride=%u) fmt=%d fb=0x%p",
        Ctx->screen_width, Ctx->screen_height,
        Ctx->pixels_per_scan_line, Ctx->pixel_format, Ctx->framebuffer);

    // Pick an integer scale that lets the 256x240 image fit inside the panel.
    INT32 ScaleW = (INT32)(Ctx->screen_width / (UINT32)Ctx->width);
    INT32 ScaleH = (INT32)(Ctx->screen_height / (UINT32)Ctx->height);
    INT32 Scale = ScaleW < ScaleH ? ScaleW : ScaleH;
    if (Scale < 1) {
        Scale = 1;
    }
    // The browser/UI also uses scale for text rendering, but the emulator
    // itself uses Ctx->scale for scaling.
    if (Ctx->scale <= 0) {
        Ctx->scale = Scale;
    }

    Ctx->scaled_width = (UINT32)Ctx->width * (UINT32)Ctx->scale;
    Ctx->scaled_height = (UINT32)Ctx->height * (UINT32)Ctx->scale;
    Ctx->scaled_buffer = AllocateZeroPool(Ctx->scaled_width * Ctx->scaled_height * sizeof(UINT32));

    return EFI_SUCCESS;
}

void get_graphics_context(GraphicsContext *ctx) {
    InitGopInternal(ctx);
    g_active_ctx = ctx;

    NesColor black = {0, 0, 0, 0};
    gfx_clear(&black);
}

GraphicsContext *gfx_active_context(VOID) {
    return g_active_ctx;
}

void free_graphics(GraphicsContext *ctx) {
    if (ctx->scaled_buffer != NULL) {
        FreePool(ctx->scaled_buffer);
        ctx->scaled_buffer = NULL;
    }
    if (g_active_ctx == ctx) {
        g_active_ctx = NULL;
    }
}

static __inline
UINT32
ArgbToBgraUint32 (
    IN UINT32 Argb
    )
{
    // ARGB8888 packed value (0xAARRGGBB) has memory layout [B, G, R, A] on
    // little-endian. The BGRA framebuffer reads each pixel as a UINT32, so
    // the value matches directly.
    return Argb;
}

void render_graphics(GraphicsContext *g_ctx, const uint32_t *buffer) {
    INT32 Scale = g_ctx->scale;
    INT32 SrcW = g_ctx->width;
    INT32 SrcH = g_ctx->height;
    INT32 DstX = (INT32)((g_ctx->screen_width - SrcW * Scale) / 2);
    INT32 DstY = (INT32)((g_ctx->screen_height - SrcH * Scale) / 2);
    INT32 Stride = (INT32)g_ctx->pixels_per_scan_line;

    if (g_ctx->pixel_format == PixelBlueGreenRedReserved8BitPerColor) {
        UINT8 *fb = g_ctx->framebuffer;
        for (INT32 y = 0; y < SrcH; y++) {
            INT32 src_row = y * SrcW;
            for (INT32 sy = 0; sy < Scale; sy++) {
                INT32 out_y = DstY + y * Scale + sy;
                if (out_y < 0 || out_y >= (INT32)g_ctx->screen_height) {
                    continue;
                }
                UINT32 *row = (UINT32 *)(fb + (out_y * Stride + DstX) * 4);
                for (INT32 x = 0; x < SrcW; x++) {
                    UINT32 px = ArgbToBgraUint32(buffer[src_row + x]);
                    for (INT32 sx = 0; sx < Scale; sx++) {
                        row[x * Scale + sx] = px;
                    }
                }
            }
        }
    } else if (g_ctx->pixel_format == PixelRedGreenBlueReserved8BitPerColor) {
        UINT8 *fb = g_ctx->framebuffer;
        for (INT32 y = 0; y < SrcH; y++) {
            INT32 src_row = y * SrcW;
            for (INT32 sy = 0; sy < Scale; sy++) {
                INT32 out_y = DstY + y * Scale + sy;
                if (out_y < 0 || out_y >= (INT32)g_ctx->screen_height) {
                    continue;
                }
                UINT8 *row = fb + (out_y * Stride + DstX) * 4;
                for (INT32 x = 0; x < SrcW; x++) {
                    UINT32 px = buffer[src_row + x];
                    // ARGB -> R, G, B, A then write as R, G, B, Rsv
                    UINT8 B = (UINT8)(px & 0xff);
                    UINT8 G = (UINT8)((px >> 8) & 0xff);
                    UINT8 R = (UINT8)((px >> 16) & 0xff);
                    for (INT32 sx = 0; sx < Scale; sx++) {
                        UINT8 *p = row + (x * Scale + sx) * 4;
                        p[0] = R;
                        p[1] = G;
                        p[2] = B;
                        p[3] = 0;
                    }
                }
            }
        }
    } else {
        // Bitmask or Blt-only: fall back to EFI_GRAPHICS_OUTPUT_PROTOCOL.Blt.
        // Build the scaled buffer first, then Blt centered.
        UINT32 *Scaled = g_ctx->scaled_buffer;
        if (Scaled == NULL) {
            return;
        }
        for (INT32 y = 0; y < SrcH; y++) {
            INT32 src_row = y * SrcW;
            for (INT32 sy = 0; sy < Scale; sy++) {
                INT32 out_y = y * Scale + sy;
                UINT32 *dst_row = Scaled + out_y * g_ctx->scaled_width;
                for (INT32 x = 0; x < SrcW; x++) {
                    UINT32 px = buffer[src_row + x];
                    for (INT32 sx = 0; sx < Scale; sx++) {
                        dst_row[x * Scale + sx] = px;
                    }
                }
            }
        }
        g_ctx->gop->Blt(
            g_ctx->gop,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Scaled,
            EfiBltBufferToVideo,
            0, 0,
            (UINTN)DstX, (UINTN)DstY,
            g_ctx->scaled_width, g_ctx->scaled_height,
            0);
    }
}

//
// UI helpers
//

VOID
NesSetPixel (
    IN INT32 X,
    IN INT32 Y,
    IN NesColor *Color
    )
{
    GraphicsContext *ctx = g_active_ctx;
    if (ctx == NULL || X < 0 || Y < 0 ||
        X >= (INT32)ctx->screen_width || Y >= (INT32)ctx->screen_height) {
        return;
    }
    UINT8 *fb = ctx->framebuffer;
    UINTN offset = (Y * ctx->pixels_per_scan_line + X) * 4;
    if (ctx->pixel_format == PixelBlueGreenRedReserved8BitPerColor) {
        fb[offset]     = Color->Blue;
        fb[offset + 1] = Color->Green;
        fb[offset + 2] = Color->Red;
        fb[offset + 3] = 0;
    } else if (ctx->pixel_format == PixelRedGreenBlueReserved8BitPerColor) {
        fb[offset]     = Color->Red;
        fb[offset + 1] = Color->Green;
        fb[offset + 2] = Color->Blue;
        fb[offset + 3] = 0;
    } else {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
        px.Blue = Color->Blue;
        px.Green = Color->Green;
        px.Red = Color->Red;
        px.Reserved = 0;
        ctx->gop->Blt(ctx->gop, &px, EfiBltVideoFill, 0, 0, X, Y, 1, 1, 0);
    }
}

VOID gfx_clear(NesColor *c) {
    GraphicsContext *ctx = g_active_ctx;
    if (ctx == NULL) return;
    if (ctx->gop != NULL) {
        // EfiBltVideoFill is the most reliable way to clear: it's a single
        // GOP call that the firmware implementation is free to optimise
        // (OVMF uses a tight memcpy on the linear framebuffer) and it
        // covers the whole panel in one go regardless of pixel format.
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
        px.Blue    = c->Blue;
        px.Green   = c->Green;
        px.Red     = c->Red;
        px.Reserved = 0;
        ctx->gop->Blt(ctx->gop, &px, EfiBltVideoFill,
                      0, 0, 0, 0,
                      ctx->screen_width, ctx->screen_height, 0);
        return;
    }
    gfx_fill_rect(0, 0, (INT32)ctx->screen_width, (INT32)ctx->screen_height, c);
}

VOID gfx_fill_rect(INT32 x, INT32 y, INT32 width, INT32 height, NesColor *c) {
    for (INT32 j = y; j < y + height; j++) {
        for (INT32 i = x; i < x + width; i++) {
            NesSetPixel(i, j, c);
        }
    }
}

VOID gfx_draw_text(INT32 x, INT32 y, const CHAR16 *text, NesColor *c, INT32 scale) {
    if (text == NULL || scale <= 0) {
        return;
    }
    INT32 cursor_x = x;
    for (CONST CHAR16 *p = text; *p; p++) {
        CHAR16 Ch = *p;
        font_draw_char(cursor_x, y, Ch, c, scale);
        // Advance by glyph width + 1 px of padding. ASCII (8 wide) and
        // CJK (16 wide) get distinct advances so proportional layout
        // stays correct when the two are mixed in one string.
        if (Ch >= 0x20 && Ch <= 0x7F) {
            cursor_x += (ASCII_FONT_WIDTH + 1) * scale;
        } else {
            cursor_x += (CN_FONT_WIDTH + 1) * scale;
        }
    }
}

VOID gfx_present(VOID) {
    // Direct framebuffer rendering means present is a no-op.
}
