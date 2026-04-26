#ifndef FBFONT_H
#define FBFONT_H

#include <kernel/types.h>

// Font sizes
#define FONT_WIDTH  8
#define FONT_HEIGHT 12

#include <fbfont/eng.h>
#include <fbfont/num.h>
#include <fbfont/sym.h>

// Get glyph by symbol
const u8 (*font_get_glyph(char c))[12][1];

#endif
