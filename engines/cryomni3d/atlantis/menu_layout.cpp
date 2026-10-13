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

} // namespace Atlantis
} // namespace CryOmni3D
