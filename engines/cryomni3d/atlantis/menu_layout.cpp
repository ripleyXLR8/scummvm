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

#include "cryomni3d/atlantis/menu_layout.h"

#include "common/textconsole.h"
#include "common/util.h"

namespace CryOmni3D {
namespace Atlantis {

// Parse one ASCII integer starting at *p, advance *p past it.
// Empty (no digits) → returns false; *outVal untouched.
static bool consumeInt(const char *&p, int &outVal) {
	bool neg = false;
	if (*p == '-') { neg = true; ++p; }
	if (*p < '0' || *p > '9')
		return false;
	int v = 0;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (*p - '0');
		++p;
	}
	outVal = neg ? -v : v;
	return true;
}

static bool startsWithIgnoreCase(const Common::String &s, const char *prefix) {
	uint n = strlen(prefix);
	if (s.size() < n) return false;
	for (uint i = 0; i < n; i++) {
		char a = s[i], b = prefix[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return false;
	}
	return true;
}

bool MenuLayout::loadFromStream(Common::ReadStream &s) {
	items.clear();
	raw.clear();
	walk.records.clear();
	walk.ops.clear();

	// Read the whole stream into a heap buffer.  Menu files are tiny
	// (< 500 bytes typical, < 5 KB for credits), so this is fine.
	Common::Array<byte> buf;
	byte chunk[256];
	while (!s.eos()) {
		uint32 got = s.read(chunk, sizeof(chunk));
		if (got == 0)
			break;
		for (uint32 i = 0; i < got; i++)
			buf.push_back(chunk[i]);
	}
	raw = buf;
	buf.push_back(0); // null-terminate so c_str scans are safe.

	const char *cur = (const char *)buf.data();
	while (*cur) {
		// Extract one line (terminator-tolerant).
		Common::String line;
		while (*cur && *cur != '\n' && *cur != '\r') {
			line += *cur;
			++cur;
		}
		while (*cur == '\n' || *cur == '\r') ++cur;

		if (line.empty())
			continue;

		// /fin terminator (case-insensitive).
		if (startsWithIgnoreCase(line, "/fin"))
			break;

		// /spr=I,HX,HY,W,H
		if (startsWithIgnoreCase(line, "/spr=")) {
			const char *p = line.c_str() + 5;
			int idx, hx, hy, w, h;
			if (!consumeInt(p, idx)) continue;
			if (*p != ',') continue;
			++p;
			if (!consumeInt(p, hx))  continue;
			if (*p != ',') continue;
			++p;
			if (!consumeInt(p, hy))  continue;
			if (*p != ',') continue;
			++p;
			if (!consumeInt(p, w))   continue;
			if (*p != ',') continue;
			++p;
			if (!consumeInt(p, h))   continue;
			MenuItem it;
			it.kind = MenuItem::kSprite;
			it.sprIdx = idx;
			it.hx = hx;
			it.hy = hy;
			it.w = w;
			it.h = h;
			items.push_back(it);
			continue;
		}

		// <X,Y>TEXT — X may be empty meaning "centered".
		// The original game encodes the empty form as a literal "<>", then
		// continues with ",Y>TEXT" (so the full line is e.g. "<>,230>LOAD
		// GAME").  Confirmed against shipped MAINMENU.TXT / SELEMENU.TXT.
		if (line[0] == '<') {
			const char *p = line.c_str() + 1;
			bool centerX = false;
			int  ax = 0, ay = 0;
			if (*p == '>') {
				centerX = true;
				++p;
			} else if (*p == ',') {
				// Legacy/permissive: bare "<,Y>" with no opening number.
				centerX = true;
			} else {
				if (!consumeInt(p, ax))
					continue;
			}
			if (*p != ',') continue;
			++p;
			if (!consumeInt(p, ay)) continue;
			if (*p != '>') continue;
			++p;

			MenuItem it;
			it.kind    = MenuItem::kText;
			it.centerX = centerX;
			it.anchorX = ax;
			it.anchorY = ay;
			it.text    = Common::String(p);
			it.rawLen  = (int)it.text.size();
			// Strip stray trailing whitespace.
			while (!it.text.empty()) {
				char c = it.text[it.text.size() - 1];
				if (c == ' ' || c == '\t')
					it.text.deleteLastChar();
				else
					break;
			}

			// A "//A&&B" value is a toggle: split the &&-separated option
			// labels into MenuItem::options and mark the item as kToggle.
			if (it.text.hasPrefix("//") && it.text.contains("&&")) {
				const Common::String body(it.text.c_str() + 2);
				uint segStart = 0;
				for (uint q = 0; q < body.size(); ) {
					if (q + 1 < body.size() &&
					    body[q] == '&' && body[q + 1] == '&') {
						it.options.push_back(
						    Common::String(body.c_str() + segStart, q - segStart));
						q += 2;
						segStart = q;
					} else {
						++q;
					}
				}
				it.options.push_back(Common::String(body.c_str() + segStart));
				it.kind = MenuItem::kToggle;
				it.text.clear();
			}

			items.push_back(it);
			continue;
		}

		// Unknown directive — silently skip (credits use $NN section markers,
		// \ italics, etc.).
	}

	return true;
}

int menuVolumeFromKnob(int x) {
	int a = (int)(((double)(x - 305)) / 213.0 * 127.0);
	return CLIP(a, 0, 127);
}

void subtitleToggleClick(bool &on, uint8 &counter) {
	on = !on;
	counter++;
	if (counter == 2)
		on = false;
	if (counter == 3)
		counter = 0;
}

static const uint8 kFontMinAdvance[137] = {
	15, 8,13, 6,13,20,17, 6,13,20,11, 6, 4,20,15,12,17, 8,16,16,
	14,13,15,13,16,15, 7, 6, 6, 6, 6,20, 6,17,19,18,19,16,16,19,
	18,12,12,17,16,24,20,23,17,22,17,15,19,20,17,23,22,18,17, 7,
	16, 6,10, 6, 7,16,16,15,15,16,12,16,17, 8, 9,16, 7,19,16,17,
	16,15,13,12,10,17,15,19,17,15,14,16, 6,17,11,16,17,17,15,16,
	15,16,16,16,15,15,15,11,10,10,18,18,16,17,17,17,17,15,23,19,
	15,19, 0,21,17,16,10,17,17,10,20,14,14,13, 3, 5, 8
};

int fontMinAdvance(uint idx) {
	return idx < ARRAYSIZE(kFontMinAdvance) ? kFontMinAdvance[idx] : 0;
}

void fontMinBlits(FontMinRenderer r, const Common::String &text, int x, int y,
                  Common::Array<FontMinBlit> &out) {
	out.clear();
	int cx = x;
	for (uint i = 0; i < text.size(); i++) {
		const byte c = (byte)text[i];
		if (r != kFontMinLoadList && c == ' ') {
			cx += 20;
			continue;
		}
		if (r == kFontMinPlayerList && c == '#')
			continue;
		const uint8 idx = (c < 0x20) ? 0x20 : (uint8)(c - 0x20);
		if (r == kFontMinLoadList && idx == 0x20) {
			cx += 6;
			continue;
		}
		FontMinBlit b = { cx, y, idx };
		out.push_back(b);
		cx += (r == kFontMinNameField && idx == 0x2e) ? 20 : fontMinAdvance(idx);
	}
}

int menuGridHit(int x0, int y0, int cellW, int cellH, int cols, int rows, bool colMajor,
                int mx, int my) {
	if (mx < x0 || mx > x0 + cols * cellW || my < y0 || my > y0 + rows * cellH)
		return -1;
	const int col = (mx - x0) / cellW, row = (my - y0) / cellH;
	return colMajor ? col * rows + row : row * cols + col;
}

// FONTMAX *measuring* width table (atlantis.exe @ 0x4965ee, glyph idx = char).
// The original uses it only to centre "<>,Y>" labels and size their hit rects
// (FUN_004206c8); the same table advances the pen while drawing (0x004202cc).
const uint8 kFontMaxMeasureWidths[137] = {
	22,10,15,15,16,24,20, 8,16,24,13, 8, 8,24, 7,14,20, 9,19,19,
	17,16,18,17,20,18, 7, 8, 8, 8, 8,18, 8,21,23,21,23,19,20,22,
	22,14,15,20,19,28,24,28,20,27,21,19,24,23,21,28,26,22,21, 8,
	19, 8,11, 8, 8,18,19,17,19,18,14,19,19,10,11,18,10,23,20,20,
	19,19,15,15,12,20,18,23,20,19,17,18, 7,20,13,18,21,20,18,18,
	18,18,18,19,18,18,18,13,11,13,21,21,19,26,32,20,20,20,20,20,
	19,28,23,17,24, 0,25,20,18,13,20,20,20,24, 2,12,16
};

int fontMaxMeasureWidth(const Common::String &text) {
	int w = 0;
	for (uint i = 0; i < text.size(); ++i) {
		int idx = (int)(unsigned char)text[i] - 0x20;
		if (idx >= 0 && idx < (int)ARRAYSIZE(kFontMaxMeasureWidths))
			w += kFontMaxMeasureWidths[idx];
	}
	return w;
}

// Exe-exact menu text hit rect (FUN_0041fb6c).  A text record's clickable box
// is X-11 / Y-28, height 28; width is the measured string for centred items
// but 22 px per raw spec character for positioned ones.  The original tests
// strict inside (left < x < left+W), which a Common::Rect reproduces with its
// bounds shrunk by one on the left/top.
Common::Rect menuTextHitRect(const MenuItem &it, int drawX) {
	int w;
	if (it.centerX)
		w = fontMaxMeasureWidth(it.text);
	else
		w = it.rawLen * 22;
	int left = drawX - 11;
	int top  = it.anchorY - 28;
	return Common::Rect(left + 1, top + 1, left + w, top + 28);
}

int menuTextDrawX(const MenuItem &it, const Common::String &label) {
	return it.centerX ? (640 - fontMaxMeasureWidth(label)) / 2 : it.anchorX;
}

void sprFontPen(const Common::String &text, int anchorX, int spaceAdvance,
                const Common::Array<int> &glyphWidths, const uint8 *advanceTable,
                uint advanceCount, Common::Array<SprFontGlyph> &out) {
	out.clear();
	int cx = anchorX;
	for (uint i = 0; i < text.size(); ++i) {
		const unsigned char c = (unsigned char)text[i];
		if (c == ' ') {
			cx += spaceAdvance;
			continue;
		}
		const int idx = (int)c - 0x20;
		if (idx < 0 || idx >= (int)glyphWidths.size())
			continue;
		SprFontGlyph g;
		g.x = cx;
		g.idx = idx;
		out.push_back(g);
		cx += (advanceTable && (uint)idx < advanceCount) ? advanceTable[idx] : glyphWidths[idx];
	}
}

namespace {

// FUN_00451b7a (atoi): leading spaces, a sign, digits.
int walkAtoi(const byte *t, uint32 size, uint32 p) {
	while (p < size && (t[p] == ' ' || t[p] == '\t'))
		p++;
	bool neg = false;
	if (p < size && (t[p] == '-' || t[p] == '+'))
		neg = (t[p++] == '-');
	int v = 0;
	while (p < size && t[p] >= '0' && t[p] <= '9')
		v = v * 10 + (t[p++] - '0');
	return neg ? -v : v;
}

// The digit steps of FUN_0041fb6c / FUN_004207c0: base, + 1 above 9,
// + 1 above 99 (a 4-digit or negative number desynchronises the walk).
uint32 walkStep(uint32 p, int v, int base, bool threeDigits = true) {
	p += base;
	if (v > 9)
		p++;
	if (threeDigits && v > 99)
		p++;
	return p;
}

bool walkAt(const byte *t, uint32 size, uint32 p, const char *word) {
	const uint32 n = strlen(word);
	return p + n <= size && memcmp(t + p, word, n) == 0;
}

MenuWalkRecord &walkSlot(MenuWalk &walk, uint n) {
	while (walk.records.size() <= n) {
		MenuWalkRecord r = { 0, 0, 0, 0 };
		walk.records.push_back(r);
	}
	return walk.records[n];
}

} // End of anonymous namespace

int menuWalkHit(const Common::Array<MenuWalkRecord> &records, int mx, int my) {
	for (uint k = 0; k < records.size(); k++) {
		const int x = (uint16)records[k].x, y = (uint16)records[k].y;
		const int w = (uint16)records[k].w, h = (uint16)records[k].h;
		if (x < mx && mx < x + w && y < my && my < y + h)
			return (int)k;
	}
	return -1;
}

int menuNumber(const char *fileName) {
	static const char *const kMenus[] = {
		"MAINMENU", "LOADMENU", "OPTMENU", "NEWMENU", "SELEMENU", "SONMENU",
		"SUREMENU", "CD1MENU", "CD2MENU", "CD3MENU", "CD4MENU", "WAIT",
		"ERROR1", "ERROR2", "ERROR3" };
	Common::String base(fileName);
	const size_t dot = base.findFirstOf('.');
	if (dot != Common::String::npos)
		base = Common::String(base.c_str(), dot);
	for (uint k = 0; k < ARRAYSIZE(kMenus); k++)
		if (base.equalsIgnoreCase(kMenus[k]))
			return (int)k;
	return -1;
}

void walkMenuText(const byte *t, uint32 size, int menu, int hoverLine,
                  int toggleA, int toggleB, MenuWalk &walk) {
	walk.ops.clear();
	uint count = 0;               // 0x006126b0 + 0xa1 * menu = 0
	uint32 p = 0;
	int line = 0;                 // EBP-0x14
	int penX = 0, penY = 0;       // EBP-0x24, EBP-0x20
	auto toCr = [&](uint32 q) {
		while (q < size && t[q] != 0x0d)
			q++;
		return q;
	};
	while (p < size && !walkAt(t, size, p, "/fin")) {
		bool draw = true;         // EBP-0x18
		bool hover = (line == hoverLine && line != 0) || (line == hoverLine && menu == 0);
		if ((line == 2 || line == 4) && menu == 2)
			hover = false;
		if (walkAt(t, size, p, "<>")) {             // 0x0041fd08
			p += 3;
			const int y = walkAtoi(t, size, p);
			p = walkStep(p, y, 2);
			int w = 0;                               // FUN_004206c8
			for (uint32 q = p; q < size && t[q] != 0x0d; q++)
				w += (t[q] >= 0x20 && t[q] - 0x20 < (int)ARRAYSIZE(kFontMaxMeasureWidths)) ? kFontMaxMeasureWidths[t[q] - 0x20] : 0;
			penX = (640 - w) / 2;
			penY = y;
			MenuWalkRecord &r = walkSlot(walk, count);
			r.x = (int16)(penX - 11);
			r.y = (int16)(y - 28);
			r.w = (int16)w;
			r.h = 28;
			count++;
		}
		if (walkAt(t, size, p, "<")) {              // 0x0041fe42
			p++;
			const int x = walkAtoi(t, size, p);
			p = walkStep(p, x, 2);
			const int y = walkAtoi(t, size, p);
			p = walkStep(p, y, 2);
			penX = x;
			penY = y;
			MenuWalkRecord &r = walkSlot(walk, count);
			r.x = (int16)(x - 11);
			r.y = (int16)(y - 28);
			r.w = (int16)(0x16 * (int)(toCr(p) - p));
			r.h = 28;
			count++;
		}
		if (walkAt(t, size, p, "//")) {             // 0x0041ffb7
			p += 2;
			for (int k = 0; k < 2; k++) {
				const bool on = (menu == 2) && (k == 0 ? (line == 3 && toggleA == 0) : (line == 5 && toggleB == 0));
				if (!on)
					continue;
				uint32 q = p;                         // strchr(p, '&') + 2
				while (q < size && t[q] != '&')
					q++;
				p = q + 2;
			}
		}
		if (walkAt(t, size, p, "&&")) {             // 0x0042002d
			if (menu == 2 && ((line == 3 && toggleA == 1) || (line == 5 && toggleB == 1))) {
				p = toCr(p) + 1;
				line++;
			}
		}
		static const char *const kSpr[] = { "/sprj=", "/sprr=", "/sprc=", "/spr=" };
		for (int v = 0; v < 4; v++) {               // 0x0042009d..0x004201be
			if (!walkAt(t, size, p, kSpr[v]))
				continue;
			p += (v < 3) ? 6 : 5;
			// FUN_004207c0: I (1-2 digits), X, Y, W (1-3 digits), H
			const int idx = walkAtoi(t, size, p);
			p = walkStep(p, idx, 2, false);
			const int x = walkAtoi(t, size, p);
			p = walkStep(p, x, 2);
			const int y = walkAtoi(t, size, p);
			p = walkStep(p, y, 2);
			const int w = walkAtoi(t, size, p);
			p = walkStep(p, w, 2);
			const int h = walkAtoi(t, size, p);
			MenuWalkRecord &r = walkSlot(walk, count);
			int bx = x;
			if (r.x == 0)
				r.x = (int16)x;
			else
				bx = r.x;                             // a slider keeps its knob
			if (r.y == 0)
				r.y = (int16)y;
			r.w = (int16)w;
			r.h = (int16)h;
			MenuWalkOp op = { true, v < 3, false, (int16)bx, (int16)y, idx, line, (int)count };
			walk.ops.push_back(op);
			count++;
			p = toCr(p) + 1;                          // past the CR
			draw = false;
			line++;
		}
		if (p >= size)
			break;
		int c = t[p];                                 // 0x004201c1
		if (c == 0x0d) {
			p++;
			penX = 0;
			penY += 28;
			draw = false;
			line++;
		}
		if (c < 0x20 && c != 0x0d)
			c = 0x20;
		if (c == 0x20) {
			penX += 0x16;
			draw = false;
		}
		c -= 0x20;
		if (draw) {
			MenuWalkOp op = { false, false, hover, (int16)penX, (int16)penY, c, line, (int)count - 1 };
			walk.ops.push_back(op);
			penX += (c >= 0 && c < (int)ARRAYSIZE(kFontMaxMeasureWidths)) ? kFontMaxMeasureWidths[c] : 0;
		}
		p++;
	}
	walk.records.resize(count);
}

} // namespace Atlantis
} // namespace CryOmni3D
