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

#ifndef CRYOMNI3D_ATLANTIS_WAM_PARSER_H
#define CRYOMNI3D_ATLANTIS_WAM_PARSER_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace CryOmni3DEngine {
class Omni3DManager;
}

namespace CryOmni3D {
namespace Atlantis {

// A clickable zone on the panoramic view.  Coordinates are in panorama space
// (x: 0-2047, y: 0-767). action is the place ID to transition to.
struct AtlantisZone {
	int16 x, y, w, h;
	int16 action;
};

// A transition between two places.  Plays a UBB/HNM video then loads dstPlaceId.
struct AtlantisTransition {
	Common::String videoName;  // e.g. "at001013.UBB"
	uint16 dstPlaceId;
	uint8 flags;               // 0x01 = normal transition
	float srcAlpha, srcBeta;   // camera angle when leaving source
	float dstAlpha, dstBeta;   // camera angle when entering destination
};

// A sub-WAM reference: cross-level transition pointing to another WAM file.
struct AtlantisSubWAM {
	uint16 srcPlaceId;
	uint16 dstPlaceId;
	Common::String wamName;    // e.g. "atlan2.wam"
};

// An NPC entry from the WAM tail: identifies a character active in this WAM area.
struct AtlantisNPCEntry {
	uint32 persoId;            // character ID referenced by CON scripts as ClicPerso
	Common::String spriteFile; // canonical SPW file (others found via SPWATL*.TXT)
	// Click rectangle at block +0x62 (x, y, w, h), as stored in the file;
	// atlantis.exe FUN_00424a20 overwrites it from the sprite list.
	int16 rectX = 0, rectY = 0, rectW = 0, rectH = 0;
	// Views the NPC appears in: block +0x1e, 17 dwords used as a bitmap (bit b
	// of dword k = view 32 * k + b), in the order atlantis.exe FUN_0045240c
	// enumerates them.  View 0 is left out: in the original a set view-0
	// bit ends the list at once (its low word 0 reads as the end), so the
	// entry has no view, and the enumerator's state then makes the next
	// entry continue this bitmap; no block of the shipped WAMs sets that
	// bit, so neither effect is reproduced (atlantis-re
	// functions/graphics-FUN_00422d5c.md).
	Common::Array<uint16> vues;
};

// A place's reference to an ambient sound object (17 bytes after the zones;
// FUN_0043187c binds it by id, FUN_00450ed8 reads t, m; c is copied to the
// object's +0x26).  See atlantis-re formats/archive-wam.md.
struct AtlantisAmbientRef {
	uint8 objectId;   // 0xff: empty
	float t;          // +5: the non-directional part of the gains
	float m;          // +9: loudness, times the object's
	float c;          // +0xd
};

// A 3D ambient sound object (100 bytes after the NPC records).  The position
// is the file's; the original rotates it at load (x' = -y, y' = x, z' = -z).
struct AtlantisAmbientObject {
	uint8 id;
	Common::String name;
	float pos[3];             // +0x0e
	float loudness;           // +0x1a
	uint8 weights[4];         // +0x2c: of 101; above 100, weight - 100 and loop
	Common::String variants[4];   // +0x30: wav\<name>; empty = absent
};

// The listener position of a place: a `frame N` / `camera v1 v2 v3` entry of
// the WAM's centre .vue file, stored as (-v2, v1, -v3) (FUN_004315b4).
struct AtlantisCentrePos {
	float x, y, z;
};

// FUN_004315b4's walk of a centre .vue file into table (never cleared by the
// original: entries from earlier files stay).
void parseCentreVue(const byte *buf, uint32 size, Common::HashMap<int32, AtlantisCentrePos> &table);

struct AtlantisPlace {
	uint16 placeId;
	Common::String cycloHnm;   // panoramic view HNM file, e.g. "atl16001.hnm"
	Common::Array<AtlantisTransition> transitions;
	Common::Array<AtlantisZone> zones;
	Common::Array<AtlantisAmbientRef> ambientRefs;

	// Returns the transition to dstPlaceId, or nullptr.
	const AtlantisTransition *findTransition(uint16 dstPlaceId) const;

	// Returns the action for the point (panorama coordinates), or 0.  Matches
	// atlantis.exe FUN_00422bb0: inclusive bounds compared as unsigned values,
	// largest action of all the zones that contain the point.
	uint16 hitTest(int16 x, int16 y) const;
};

// The panorama point the original hit-tests (zones and NPCs) for an
// Omni3DManager::mapMouseCoords() result: FUN_0045166f returns y = 0x2ff - v.
// See atlantis-re: functions/FUN_00422bb0.md.
inline Common::Point hitTestPoint(const Common::Point &mapped) {
	return Common::Point(mapped.x, 767 - mapped.y);
}

class AtlantisWAMParser {
public:
	void loadStream(Common::SeekableReadStream &stream);
	void clear();

	const AtlantisPlace *findPlaceById(uint16 placeId) const;
	const Common::Array<AtlantisSubWAM> &getSubWAMs() const { return _subWAMs; }
	const Common::Array<AtlantisPlace> &getPlaces() const { return _places; }
	const Common::Array<AtlantisNPCEntry> &getNPCEntries() const { return _npcEntries; }
	const Common::Array<AtlantisAmbientObject> &getAmbientObjects() const { return _ambientObjects; }
	// The file references by position (the original reads the centre .vue
	// as entry 2 whatever its type, FUN_004315b4 @ 0x00431601).
	const Common::Array<Common::String> &getFileRefs() const { return _fileRefs; }

	// Sprite and view list filenames embedded in the WAM.
	Common::String spwListFile;   // e.g. "spwatl1.txt"
	Common::String spfListFile;   // e.g. "spfatl1.txt"
	Common::String centVueFile;   // e.g. "atl1cent.vue"

private:
	Common::Array<AtlantisPlace> _places;
	Common::Array<AtlantisSubWAM> _subWAMs;
	Common::Array<AtlantisNPCEntry> _npcEntries;
	Common::Array<AtlantisAmbientObject> _ambientObjects;
	Common::Array<Common::String> _fileRefs;
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_WAM_PARSER_H
