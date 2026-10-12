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

#ifndef CRYOMNI3D_ATLANTIS_NPC_CLICK_H
#define CRYOMNI3D_ATLANTIS_NPC_CLICK_H

#include "common/array.h"
#include "common/str.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/wam_parser.h"

namespace CryOmni3D {
namespace Atlantis {

// One entry from the WAM's SPW sprite list (e.g. SPWATL1.TXT).
// NPC entries have type==0 and persoId>0; animated props (torches) have type!=0.
struct SpritePlaceEntry {
	uint16 placeId;
	int type;        // 0 = NPC, non-zero = animated prop / wspranim pose
	int frames;      // TXT frame field (animation length comes from the SPW header)
	int persoId;     // character ID (0 for props)
	// Byte 8 of the original's place entry: the absolute value of the fifth
	// token of a 6-token row, a 2-digit icon (OBJETS1.SPR sprite) for objects
	// (FUN_00424a20).  0 for 4-token rows.
	uint8 icon;
	Common::String spwFile;
	// FUN_00424a20: the id is kept only when written "-NNN" (else -1), and
	// the file name compared with the WAM NPC entries is the rest of the
	// line after the 2-digit field.
	bool idMarked = false;
	Common::String rawFile;
	// The NPC's "home" place — clickable here, look-only elsewhere.  Encoded
	// as a leading '-' on the angle token in the SPW list.
	bool interactive;
	// Trailing '-' on the placeId token marks the sprite as starting INACTIVE
	// (atlantis.exe sprite-record field +0x04 = 0): it only renders after a
	// /set(ShowPerso=N) (or wspranim activation, added later) flips it active.
	bool startActive;
	mutable bool active;   // runtime render flag; reset to startActive on place load
	mutable int  frame;    // current animation frame
};

// Parse one line of a sprite list (SPRLIST\\SPWATLn.TXT); false for comments
// and short lines.
bool parseSpritePlaceLine(const Common::String &line, SpritePlaceEntry &entry);

// A WAM NPC entry's click rectangle (block +0x62: x, y, w, h).
struct NpcClickRect {
	int16 x, y, w, h;
};

// The rectangles as stored in the WAM file.
void initNpcClickRects(const Common::Array<AtlantisNPCEntry> &npcs, Common::Array<NpcClickRect> &rects);

// Frame 0 of an SPW as FUN_00424a20 reads it: f = file + [file + 0x14];
// x = u16 f[0], y = u16 f[0xC], f4 = s32 f[4], f8 = s32 f[8].
struct SpwFrame0 {
	int16 x, y;
	int32 f4, f8;
};
bool readSpwFrame0(Common::SeekableReadStream &spw, SpwFrame0 &f);

// atlantis.exe FUN_00424a20 (0x00424c0b..0x00424ebe): a row with a "-NNN"
// id and the '-' click flag overwrites the rectangle of every NPC entry with
// the same id and file name (strcmp) from frame 0 of its SPW: x, y, and
// w = |f8 - x|, h = |f4 - y| (signed compare, 16-bit difference).  True if
// the row concerns some entry (its SPW is needed).
bool npcRowMatches(const Common::Array<AtlantisNPCEntry> &npcs, const SpritePlaceEntry &row);
void applyNpcRowRect(const Common::Array<AtlantisNPCEntry> &npcs, const SpritePlaceEntry &row,
                     const SpwFrame0 &f, Common::Array<NpcClickRect> &rects);

// atlantis.exe FUN_00422d5c: the id of the first WAM NPC entry whose views
// (those of the first entry with that id, FUN_0045240c) include vue, which
// has an active place entry of the same id and file name in vue, and whose
// rectangle holds (x, y), edges inclusive, unsigned compares; 0 if none.
uint32 npcHitTest(const Common::Array<AtlantisNPCEntry> &npcs, const Common::Array<NpcClickRect> &rects,
                  const Common::Array<SpritePlaceEntry> &entries, uint16 vue, int x, int y);

// atlantis.exe FUN_004278a0 (0x00427983..0x00427b85): the rows of view vue
// the object pick-up takes for a clicked object id, in list order: every row
// of that view with that id whose type is not 3.  The active flag is not
// tested, and the loop does not stop at the first row.
void objectPickupRows(const Common::Array<SpritePlaceEntry> &entries, uint16 vue, int id,
                      Common::Array<uint> &rows);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
