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

#ifndef CRYOMNI3D_ATLANTIS_OGRE_DRAW_H
#define CRYOMNI3D_ATLANTIS_OGRE_DRAW_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/util.h"

#include "cryomni3d/atlantis/ogre_rules.h"

namespace CryOmni3D {
namespace Atlantis {

// The drawing of the ogre game (StartPuzzle 2) as atlantis.exe does it, as
// lists of events: sprites of PUZCYCLO.SPR ('P') or CURSEURS.SPR ('C'), the
// background copy, presents, sounds, the standard cursor and SetCursorPos.
struct OgreEvent {
	enum Kind { kSprite, kBackground, kPresent, kSound, kStdCursor, kSetCursorPos, kWait };
	Kind kind;
	char set;               // kSprite
	int x, y, frame;        // kSprite; x, y for kSetCursorPos; frame = ticks for kWait
	int sound;              // kSound: 1..5 for cyclo1..cyclo5
};

class OgreDraw {
public:
	typedef Common::Array<OgreEvent> Events;

	static int pieceX(int x) { return x * 60 + 0x71; }
	static int pieceY(int y) { return y * 60 + 0x1d; }

	// FUN_004432f8 / FUN_00443944: cell (x, y), plain or highlighted.
	static void cell(Events &ev, int x, int y, bool lit) {
		sprite(ev, 'P', OgreRules::colEdge(x), OgreRules::rowEdge(y), y * 7 + x + 0x34 - (lit ? 0x31 : 0));
	}

	// The hero, 10 pixels right when he stands on the queen's cell.
	static void hero(Events &ev, const OgreRules &r) {
		const bool onQueen = r.p[0].x == r.p[1].x && r.p[0].y == r.p[1].y;
		sprite(ev, 'P', pieceX(r.p[0].x) + (onQueen ? 10 : 0), pieceY(r.p[0].y), 0);
	}

	// FUN_00442b88: the background; while held, FUN_00443f94's cells (the
	// elbow and destination lit on a legal hover, else the destination plain,
	// then the hero's cell lit; nothing off the board); the hero; records
	// 1..3 with a flag, frame min(i, 2).  r.buildMoveMask runs first.
	static void board(Events &ev, OgreRules &r, bool held, int mx, int my) {
		background(ev);
		if (held) {
			const int cx = OgreRules::cellColumn(mx), cy = OgreRules::cellRow(my);
			if (cx < 7 && cy < 7) {
				r.buildMoveMask(mx, my);
				const int d = ABS(cx - r.p[0].x) + ABS(cy - r.p[0].y);
				if (d >= 1 && d <= 2) {
					cell(ev, r.pathX[1], r.pathY[1], true);
					cell(ev, cx, cy, true);
				} else {
					cell(ev, cx, cy, false);
				}
				cell(ev, r.p[0].x, r.p[0].y, true);
			}
		}
		if (r.p[0].flag != 0)
			hero(ev, r);
		for (int i = 1; i <= 3; i++)
			if (r.p[i].flag != 0)
				sprite(ev, 'P', pieceX(r.p[i].x), pieceY(r.p[i].y), MIN(i, 2));
	}

	// FUN_004439c4.  Held: the hero follows the mouse minus the grab offset,
	// kept in [0x5a, 0x1f4] x [0x16, 0x190] by moving the system cursor.
	// Free: CURSEURS frame 11 outside x 0x32..0x24e, else the standard cursor.
	static void cursor(Events &ev, bool held, int gx, int gy, int &mx, int &my) {
		if (held) {
			int dx = mx - gx, dy = my - gy;
			if (dx < 0x5a || dx > 0x1f4 || dy < 0x16 || dy > 0x190) {
				dx = CLIP(dx, 0x5a, 0x1f4);
				dy = CLIP(dy, 0x16, 0x190);
				mx = dx + gx;
				my = dy + gy;
				OgreEvent e = event(OgreEvent::kSetCursorPos);
				e.x = mx;
				e.y = my;
				ev.push_back(e);
			}
			sprite(ev, 'P', dx, dy, 0);
		} else if (mx < 0x32 || mx > 0x24e) {
			sprite(ev, 'C', mx, my, 11);
		} else {
			ev.push_back(event(OgreEvent::kStdCursor));
		}
	}

	// FUN_004436f4: nothing when the queen shares an ogre's cell; else cyclo4,
	// then 3 frames: the board, her cell plain / lit / plain, the queen,
	// present, 200 ticks.
	static void reveal(Events &ev, OgreRules &r) {
		for (int o = 2; o <= 3; o++)
			if (r.p[1].x == r.p[o].x && r.p[1].y == r.p[o].y)
				return;
		sound(ev, 4);
		bool t = false;
		for (int i = 0; i < 3; i++) {
			board(ev, r, false, 0, 0);
			cell(ev, r.p[1].x, r.p[1].y, t);
			sprite(ev, 'P', pieceX(r.p[1].x), pieceY(r.p[1].y), 1);
			t = !t;
			present(ev, 0xc8);
		}
	}

	// FUN_00443378(0): 5 frames: the three path cells (lit on odd frames),
	// the hero, the queen, records 2..3 with a flag; 150 ticks each.
	static void win(Events &ev, const OgreRules &r) {
		bool t = false;
		for (int i = 0; i < 5; i++) {
			background(ev);
			for (int k = 0; k < 3; k++)
				cell(ev, r.pathX[k], r.pathY[k], t);
			hero(ev, r);
			sprite(ev, 'P', pieceX(r.p[1].x), pieceY(r.p[1].y), 1);
			for (int o = 2; o <= 3; o++)
				if (r.p[o].flag != 0)
					sprite(ev, 'P', pieceX(r.p[o].x), pieceY(r.p[o].y), 2);
			t = !t;
			present(ev, 0x96);
		}
	}

	// FUN_00442d78(0): 5 cycles of two frames: all 49 cells plain (column by
	// column), the hero's cell (lit on odd frames), the hero, records 2..3
	// with a flag; the second frame draws the hero again on top; 100 ticks
	// each.
	static void lose(Events &ev, const OgreRules &r) {
		bool t = false;
		for (int i = 0; i < 5; i++) {
			for (int second = 0; second < 2; second++) {
				background(ev);
				for (int x = 0; x < 7; x++)
					for (int y = 0; y < 7; y++)
						cell(ev, x, y, false);
				cell(ev, r.p[0].x, r.p[0].y, t);
				sprite(ev, 'P', pieceX(r.p[0].x), pieceY(r.p[0].y), 0);
				for (int o = 2; o <= 3; o++)
					if (r.p[o].flag != 0)
						sprite(ev, 'P', pieceX(r.p[o].x), pieceY(r.p[o].y), 2);
				if (second)
					sprite(ev, 'P', pieceX(r.p[0].x), pieceY(r.p[0].y), 0);
				t = !t;
				present(ev, 0x64);
			}
		}
	}

private:
	static OgreEvent event(OgreEvent::Kind k) {
		OgreEvent e;
		e.kind = k;
		e.set = 0;
		e.x = e.y = e.frame = 0;
		e.sound = 0;
		return e;
	}
	static void sprite(Events &ev, char set, int x, int y, int frame) {
		OgreEvent e = event(OgreEvent::kSprite);
		e.set = set;
		e.x = x;
		e.y = y;
		e.frame = frame;
		ev.push_back(e);
	}
	static void background(Events &ev) { ev.push_back(event(OgreEvent::kBackground)); }
	static void sound(Events &ev, int n) {
		OgreEvent e = event(OgreEvent::kSound);
		e.sound = n;
		ev.push_back(e);
	}
	// The frame copy to the surface (FUN_00410f36 again), the present
	// (FUN_0041afd0) and the busy wait.
	static void present(Events &ev, int ticks) {
		background(ev);
		ev.push_back(event(OgreEvent::kPresent));
		OgreEvent w = event(OgreEvent::kWait);
		w.frame = ticks;
		ev.push_back(w);
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
