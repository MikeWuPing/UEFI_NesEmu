#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#include "font.h"

//
// font.c bridges between the auto-generated bitmap tables in font_data.c
// and the NesColor framebuffer API. ASCII glyphs render at 8x16, CJK
// glyphs at 16x16, both scaled by an integer factor.
//
// The auto-generated tables are extern'd here rather than #include'd as a
// header so the .c stays a single TU.
//

extern CONST UINT8  g_ascii_font[96][ASCII_FONT_HEIGHT];
extern CONST UINT16 g_cn_ucs2[];
extern CONST UINT8  g_cn_font[][CN_FONT_WIDTH / 8 * CN_FONT_HEIGHT];

//
// g_cn_font_count is exported by font_data.c. Kept as a weak symbol so
// future variants of the generator (e.g. trimmed character set) just
// work without touching font.c.
//
extern CONST UINTN g_cn_font_count;

STATIC
CONST UINT8 *
LookupAscii (
    IN CHAR16  Ch
    )
{
    if (Ch < 0x20 || Ch > 0x7F) {
        return NULL;
    }
    return g_ascii_font[Ch - 0x20];
}

CONST UINT8 *
font_lookup_cjk (
    IN CHAR16  Ch
    )
{
    // g_cn_ucs2 has a fixed length derived from the GB2312 level-1 set.
    // We treat it as terminated by reading the array bound from the
    // adjacent g_cn_font table size, which is impractical without a
    // symbol count. The generator therefore exports an array-sized
    // g_cn_ucs2 - here we use a sentinel of "search until the value
    // passes Ch". The arrays are sorted by UCS-2, so binary search is
    // safe.
    UINTN N = (UINTN)g_cn_font_count;
    if (N == 0) {
        return NULL;
    }
    UINTN Lo = 0;
    UINTN Hi = N - 1;
    while (Lo <= Hi) {
        UINTN Mid = Lo + (Hi - Lo) / 2;
        UINT16 V = g_cn_ucs2[Mid];
        if (V == Ch) {
            return g_cn_font[Mid];
        }
        if (V < Ch) {
            Lo = Mid + 1;
        } else {
            if (Mid == 0) break;
            Hi = Mid - 1;
        }
    }
    return NULL;
}

STATIC
VOID
RenderGlyphBytes (
    IN INT32       X,
    IN INT32       Y,
    IN CONST UINT8 *Glyph,
    IN INT32       Width,
    IN INT32       Height,
    IN NesColor    *Color,
    IN INT32       Scale
    )
{
    INT32 BytesPerRow = (Width + 7) / 8;
    for (INT32 Row = 0; Row < Height; Row++) {
        CONST UINT8 *RowPtr = Glyph + Row * BytesPerRow;
        for (INT32 Col = 0; Col < Width; Col++) {
            INT32 ByteIdx   = Col / 8;
            INT32 BitInByte = 7 - (Col % 8);
            if (RowPtr[ByteIdx] & (1u << BitInByte)) {
                gfx_fill_rect(
                    X + Col * Scale,
                    Y + Row * Scale,
                    Scale, Scale,
                    Color);
            }
        }
    }
}

VOID font_draw_char (
    IN INT32     X,
    IN INT32     Y,
    IN CHAR16    Ch,
    IN NesColor  *Color,
    IN INT32     Scale
    )
{
    if (Scale <= 0 || Color == NULL) {
        return;
    }

    CONST UINT8 *Glyph;
    INT32 Width, Height;

    if (Ch >= 0x20 && Ch <= 0x7F) {
        Glyph = LookupAscii(Ch);
        Width = ASCII_FONT_WIDTH;
        Height = ASCII_FONT_HEIGHT;
    } else {
        Glyph = font_lookup_cjk(Ch);
        Width = CN_FONT_WIDTH;
        Height = CN_FONT_HEIGHT;
    }

    if (Glyph == NULL) {
        // Unknown codepoint: draw a solid box so it's obvious in the UI.
        gfx_fill_rect(X, Y, 8 * Scale, ASCII_FONT_HEIGHT * Scale, Color);
        return;
    }

    RenderGlyphBytes(X, Y, Glyph, Width, Height, Color, Scale);
}
