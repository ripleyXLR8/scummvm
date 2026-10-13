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

#ifndef CRYOMNI3D_ATLANTIS_CREDITS_LAYOUT_H
#define CRYOMNI3D_ATLANTIS_CREDITS_LAYOUT_H

#include "common/array.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// atlantis.exe FUN_0041a72c, the credits, as a list of events: the bytes
// of SPRLIST\CREDITS.TXT are walked one at a time (from offset 3, past the
// leading `$01`); the text is drawn into a work buffer over the picture,
// which reaches the screen only at the next `$NN` marker, after the page
// time.  Ticks are the 200 Hz timer's; the character loop takes no time.
struct CreditsEvent {
	enum Kind { kImage, kGlyph, kPresent, kClear };
	Kind kind;
	int image;          // kImage: images\creditNN.tga
	bool blue;          // kGlyph: CREDBLEU.SPR, else CREDBLAN.SPR
	int x, y, frame;    // kGlyph: FUN_00410430 at (x, y), frame c - 0x21
	uint32 tick;        // kPresent / kClear
};

class CreditsLayout {
public:
	enum { kPageTicks = 0x556, kFirstWaitTicks = 0x556 - 0x53c };

	// widths: the byte table 0x004966fc (indexed by the raw byte).
	static void build(const byte *t, uint32 size, const byte widths[256],
	                  Common::Array<CreditsEvent> &ev) {
		uint32 tick = 0;
		uint32 timer = tick - 0x53c;                 // 0x006cd810
		add(ev, CreditsEvent::kImage, 1);            // FUN_0041ad7c(1)
		int x = 0x140, y = 0;
		bool blue = false;
		for (uint32 p = 3; p < size; p++) {
			byte c = t[p];
			bool draw = true;
			if (c == '<') {
				p += 2;
				if (p < size && t[p] == '\\') {
					p++;
					blue = true;
				}
				x = centre(t, size, p, widths);      // FUN_0042073c
				c = p < size ? t[p] : 0;
			}
			if (c == '\r') {
				blue = false;
				y += 16;
				x = 0x140;
				p++;                                 // the LF
				draw = false;
			} else if (c == '\\') {
				x -= widths[0x5c];
				blue = true;
				draw = false;
			} else if (c == '$') {
				x = 0x140;
				y = 0;
				if (tick - timer < (uint32)kPageTicks)
					tick = timer + kPageTicks;
				timer = tick;
				CreditsEvent e = event(CreditsEvent::kPresent);
				e.tick = tick;
				ev.push_back(e);
				add(ev, CreditsEvent::kImage, atoi(t, size, p + 1));
				p += 2;
				draw = false;
			}
			if (draw && c != ' ') {
				CreditsEvent e = event(CreditsEvent::kGlyph);
				e.blue = blue;
				e.x = x;
				e.y = y;
				e.frame = (int)c - 0x21;
				ev.push_back(e);
				x += widths[c];
			} else {
				x += 6;
			}
		}
		if (tick - timer < (uint32)kPageTicks)
			tick = timer + kPageTicks;
		CreditsEvent e = event(CreditsEvent::kClear);
		e.tick = tick;
		ev.push_back(e);
	}

	// FUN_0042073c: centre the line from p to the next CR on x = 480.
	static int centre(const byte *t, uint32 size, uint32 p, const byte widths[256]) {
		int w = 0;
		for (; p < size && t[p] != '\r'; p++)
			w += t[p] == ' ' ? 6 : widths[t[p]];
		return (0x3c0 - w) / 2;
	}

private:
	static CreditsEvent event(CreditsEvent::Kind k) {
		CreditsEvent e;
		e.kind = k;
		e.image = 0;
		e.blue = false;
		e.x = e.y = e.frame = 0;
		e.tick = 0;
		return e;
	}
	static void add(Common::Array<CreditsEvent> &ev, CreditsEvent::Kind k, int image) {
		CreditsEvent e = event(k);
		e.image = image;
		ev.push_back(e);
	}
	// FUN_00451b7a, atoi.
	static int atoi(const byte *t, uint32 size, uint32 p) {
		while (p < size && (t[p] == ' ' || t[p] == '\t'))
			p++;
		bool neg = false;
		if (p < size && (t[p] == '-' || t[p] == '+'))
			neg = t[p++] == '-';
		int n = 0;
		for (; p < size && t[p] >= '0' && t[p] <= '9'; p++)
			n = n * 10 + (t[p] - '0');
		return neg ? -n : n;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
