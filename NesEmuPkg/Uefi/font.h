#pragma once

#include <Uefi.h>
#include "uefi_gfx.h"

//
// Glyph metrics. ASCII characters are 8x16 (1 byte per row, 8 columns).
// Chinese (CJK) characters are 16x16 (2 bytes per row, 16 columns). The
// runtime scales both by an integer factor via font_draw_char.
//
#define ASCII_FONT_WIDTH   8
#define ASCII_FONT_HEIGHT  16
#define CN_FONT_WIDTH      16
#define CN_FONT_HEIGHT     16

//
// Render a single UCS-2 codepoint at (X, Y). The codepoint is dispatched
// to either the ASCII table (0x20-0x7F) or the GB2312 level-1 Chinese
// table (binary search over g_cn_ucs2). Characters outside both ranges
// render as a filled box so omissions are obvious.
//
VOID font_draw_char (
    IN INT32     X,
    IN INT32     Y,
    IN CHAR16    Ch,
    IN NesColor  *Color,
    IN INT32     Scale
    );

//
// Look up a UCS-2 codepoint in g_cn_ucs2 via binary search. Returns the
// pointer to the 32-byte glyph buffer, or NULL if not present.
//
CONST UINT8 *
font_lookup_cjk (
    IN CHAR16  Ch
    );
