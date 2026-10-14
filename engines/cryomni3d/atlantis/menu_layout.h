/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CRYOMNI3D_ATLANTIS_MENU_LAYOUT_H
#define CRYOMNI3D_ATLANTIS_MENU_LAYOUT_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/stream.h"

namespace CryOmni3D {
namespace Atlantis {

// Parsed entry from a SPRLIST\*MENU.TXT layout file (original Atlantis menu
// data).  The format is line-oriented:
//
//   <X,Y>TEXT          — anchor a FONTMAX label at (X, Y_txt).  X may be
//                        empty, e.g. "<>,140>SELECT PLAYER NAME", meaning
//                        the label is horizontally centred on the 640-wide
//                        screen.  Y is the y_txt baseline (same convention
//                        as drawFontMaxText).
//   /spr=I,HX,HY,W,H   — composite SPRMENU.SPR sprite index I with its
//                        hotspot at screen (HX, HY).  W/H are size hints kept
//                        in w/h (slider tracks use them); the sprite still
//                        carries its real pixel size for blitting.
//   <X,Y>//A&&B[&&C…]   — a toggle: a text anchor whose value cycles through
//                        the &&-separated option labels.  Parsed as kToggle.
//   /fin or /FIN       — end of file.  Anything after is ignored.
//
// Blank lines and CR characters are tolerated.
struct MenuItem {
	enum Kind { kText, kSprite, kToggle };

	Kind kind;

	// Text item fields (anchorX/anchorY/centerX also apply to kToggle).
	Common::String text;
	bool centerX;       // true → ignore anchorX and centre on screen.
	int  anchorX;
	int  anchorY;       // y_txt baseline.
	// Character count after '>' up to end of line, BEFORE any trimming or
	// toggle splitting (for "//A&&B" toggles this counts the whole spec).
	// The original sizes a positioned text item's hit rect as 22 px per raw
	// character (atlantis.exe FUN_0041fb6c: W = chars_to_CR * 0x16).
	int  rawLen;

	// Sprite item fields.  w/h come from the /spr size hint.
	int sprIdx;
	int hx, hy;
	int w, h;

	// Toggle item fields (kToggle).  `options` holds the label of each
	// state; `selected` indexes the currently active one.
	Common::Array<Common::String> options;
	int selected;

	MenuItem()
	    : kind(kText), centerX(false), anchorX(0), anchorY(0), rawLen(0),
	      sprIdx(0), hx(0), hy(0), w(0), h(0), selected(0) {}
};

// atlantis.exe FUN_0041fb6c, the menu walk: the text is not parsed ahead of
// time; every draw walks it byte by byte and rebuilds the menu's hit table
// (0x006126b0 + 0xa1 * menu: a count, then {s16 x, y, w, h} records) in the
// same pass.  At each position, in this order: `/fin` ends; `<>,Y>` centred
// text (FUN_004206c8); `<X,Y>` positioned text; `//` and `&&` the options
// toggles (menu 2, lines 3 and 5); `/sprj=` `/sprr=` `/sprc=` `/spr=`
// sprite records (FUN_004207c0); then the byte itself: CR (the next byte is
// consumed), space and other control bytes (pen + 22), or a FONTMAX glyph.
struct MenuWalkRecord {
	int16 x, y, w, h;
};
struct MenuWalkOp {
	bool sprite;        // a sprite of SPRMENU.SPR (or of the alternate bank)
	bool altBank;       // /sprj= /sprr= /sprc=: bank 0x006cd874
	bool hoverFont;     // glyph in FONTMAX2 (the hovered line)
	int16 x, y;         // pen (glyph) or record position (sprite)
	int index;          // glyph c - 0x20, or the sprite index
	int line;           // the walk's line counter at the op
	int record;         // the record the op belongs to (-1 none yet)
};
struct MenuWalk {
	// In: the records of the previous draw (a sprite record keeps a non-zero
	// x / y, FUN_004207c0); out: this draw's hit table.
	Common::Array<MenuWalkRecord> records;
	Common::Array<MenuWalkOp> ops;
};
// hoverLine: DAT_006cd850; toggleA / toggleB: DAT_004960a8 / DAT_004960ac.
void walkMenuText(const byte *text, uint32 size, int menu, int hoverLine,
                  int toggleA, int toggleB, MenuWalk &walk);
// atlantis.exe FUN_004210c0: the first record strictly containing the
// mouse, coordinates read as u16 (a negative x / y is never hit), sprite
// records included; -1 for none (the original's 0xff).
int menuWalkHit(const Common::Array<MenuWalkRecord> &records, int mx, int my);
// The original's menu number of a SPRLIST menu file ("OPTMENU.TXT" → 2).
int menuNumber(const char *fileName);

struct MenuLayout {
	Common::Array<MenuItem> items;
	// The file's bytes as read, walked by walkMenuText on every draw, and the
	// menu number the original gives it (the order of FUN_0041c838's table:
	// MAINMENU 0 ... ERROR3 14; -1 unknown).  `walk` keeps the hit table
	// between draws as the original's static table does.
	Common::Array<byte> raw;
	int menu;
	mutable MenuWalk walk;

	MenuLayout() : menu(-1) {}

	// Parse the menu-text stream.  Returns false only when the buffer is
	// unreadable; an empty / truncated file yields an empty layout and true.
	bool loadFromStream(Common::ReadStream &s);
};

// The FONTMIN pen advance table (atlantis.exe 0x00496697), glyph index
// c - 0x20 (control characters use index 0x20).
int fontMinAdvance(uint idx);

// One FONTMIN glyph blit: pen x, baseline y, glyph index.
struct FontMinBlit {
	int x, y;
	uint8 idx;
};

// The three FONTMIN text renderers of atlantis.exe:
//  - player list (FUN_00420d8c): a space advances 20, not drawn; '#' is
//    neither drawn nor advanced; other glyphs are drawn and advance by the
//    table;
//  - name field (FUN_00420f60): a space advances 20, not drawn; other
//    glyphs are drawn and advance by the table, glyph 0x2e by 20;
//  - load list (FUN_00420b48): glyph 0x20 is not drawn and advances 6;
//    other glyphs (the space included) are drawn and advance by the table.
enum FontMinRenderer {
	kFontMinPlayerList,
	kFontMinNameField,
	kFontMinLoadList
};
void fontMinBlits(FontMinRenderer r, const Common::String &text, int x, int y,
                  Common::Array<FontMinBlit> &out);

// atlantis.exe FUN_004211f0: the cell under (mx, my) of a cols x rows grid
// at (x0, y0) with cells cellW x cellH, bounds INCLUSIVE on both sides (so
// the far edge column / row gives an index past the grid); column-major
// (index = col * rows + row) or row-major; -1 outside.
int menuGridHit(int x0, int y0, int cellW, int cellH, int cols, int rows, bool colMajor,
                int mx, int my);

// atlantis.exe sound menu OK: a knob at x gives the AIL volume
// (x - 305) / 213 * 127, rounded toward zero (FUN_00451d6e), 0..127.
int menuVolumeFromKnob(int x);

// atlantis.exe options menu, SUBTITLES item: on = !on; counter + 1; when
// the counter is 2, on = 0; when it is 3, it returns to 0 (the byte counter
// DAT_00496098).  Subtitles are drawn when on is set (FUN_00430178).
void subtitleToggleClick(bool &on, uint8 &counter);

// FONTMAX measuring widths (atlantis.exe @ 0x0049660e indexed by char - 0x20,
// i.e. 0x004965ee indexed by the raw byte):
// FUN_004206c8 centres labels and sizes hit rects with it, and the pen
// advances by it while drawing (0x004202cc).
extern const uint8 kFontMaxMeasureWidths[137];
int fontMaxMeasureWidth(const Common::String &text);

// Draw x of a text item's label: (640 - measured width) / 2 when centred
// (FUN_004206c8), else its anchor.
int menuTextDrawX(const MenuItem &it, const Common::String &label);

// Hit box of a text record (FUN_0041fb6c): x - 11, y - 28, height 28; width
// is the measured string when centred, 22 per raw spec character otherwise.
// The original tests strict inside, hence the left/top + 1.
Common::Rect menuTextHitRect(const MenuItem &it, int drawX);

// Pen of a glyph-sprite font (FONTMAX, FONTMIN): a space advances
// spaceAdvance and draws nothing; glyph c - 0x20 (below glyphWidths.size())
// is drawn at the pen, which then advances by advanceTable when given, else
// by the glyph's width.
struct SprFontGlyph {
	int x, idx;
};
void sprFontPen(const Common::String &text, int anchorX, int spaceAdvance,
                const Common::Array<int> &glyphWidths, const uint8 *advanceTable,
                uint advanceCount, Common::Array<SprFontGlyph> &out);

} // namespace Atlantis
} // namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_MENU_LAYOUT_H
