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

#ifndef CRYOMNI3D_ATLANTIS_SAVELOAD_FORMAT_H
#define CRYOMNI3D_ATLANTIS_SAVELOAD_FORMAT_H

#include "common/array.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
class WriteStream;
}

namespace CryOmni3D {
namespace Atlantis {

// Named player profiles (the original's players.atl has 5 rows).
static const uint kSaveMaxPlayers = 5;
// Episode saves of player P live at slots P * stride + chapter.
static const uint kSavePlayerSlotStride = 100;

// The name of chapter N's checkpoint, as the original load picker extracts
// it from SPRLIST\EPI.TXT (FUN_0041cf78, 0x0041d3a1..0x0041d431).
Common::String epiEpisodeName(const byte *text, uint32 size, uint chapter);

// Read a players file: the port's own ('APLR') or an original players.atl
// (97 bytes: 5 names of 17 bytes, 6 chapter bytes, 6 letter bytes; rows end
// at "#New Player" or an empty name, 0x0041916b..0x004192e0).  Returns false
// for neither.
bool readPlayersFile(Common::SeekableReadStream &in, Common::String players[kSaveMaxPlayers],
                     int chapters[kSaveMaxPlayers]);
// Write the port's own players file.
void writePlayersFile(Common::WriteStream &out, const Common::String players[kSaveMaxPlayers],
                      const int chapters[kSaveMaxPlayers]);

// Whether a chapter change writes a checkpoint.  The original writes one on
// every CON chapter change (FUN_00427634 @ 0x0042765f -> FUN_0042d6b8),
// whatever its EPI name; the port's slot numbering bounds the chapter.
bool checkpointWritten(uint chapter);

// The save-file letters of an original players.atl (`p<letter><N>.gam`).
bool readOriginalPlayersLetters(Common::SeekableReadStream &in, char letters[kSaveMaxPlayers]);

// An original checkpoint p<L><N>.gam (atlantis-re formats/ui-gam.md, checked
// on 74 real files): the fields the port restores.
static const uint32 kOrigCheckpointSize = 2639;
struct OrigCheckpoint {
	struct Record {
		uint8 sprite;
		int32 id;
	};
	Common::String wamName;          // 0x000, file name of the area WAM
	uint chapter = 0;                // 0x104
	uint prevChapter = 0;            // 0x105
	int32 placeId = 0;               // 0x112
	int32 lastVue = 0;               // 0x116
	uint8 incPerso = 0;              // 0x11e
	Common::Array<Record> inventory; // 0x11f count, 0x123 records
	Common::Array<byte> conCounters; // 0x231, 0x5dc bytes
	int32 vars[7] = {};              // 0x80d, CON variables 0..6
	uint guestCount = 0;             // 0x829
	int32 guestId[4] = {};           // 0x82a
	uint8 guestSprite[4] = {};
	Common::Array<int> hiddenPersos; // 0x83e count, 0x842 int16 ids
};
bool parseOriginalCheckpoint(const byte *data, uint32 size, OrigCheckpoint &out);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
