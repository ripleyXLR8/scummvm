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

#include "common/stream.h"

#include "cryomni3d/atlantis/saveload_format.h"

namespace CryOmni3D {
namespace Atlantis {

static const uint32 kPlayersMagic   = 0x41504c52u;  // 'APLR'
static const uint32 kPlayersVersion = 2;             // v2 appends resume chapters
static const uint32 kOrigPlayersSize = 97;

Common::String epiEpisodeName(const byte *text, uint32 size, uint chapter) {
	// For chapter 1 the original scan only stops if the file does not start
	// with CR; EPI.TXT does, so chapter 1 has no name here.
	if (chapter < 2)
		return Common::String();
	// Each CR counts and also skips the byte after it (the LF of a CR/LF
	// pair); the name runs from there to the next CR.  The original reads
	// past the end of the text; this stops there.
	const uint32 wanted = chapter - 1;
	uint32 count = 0, i = 0;
	do {
		if (i >= size)
			return Common::String();
		if (text[i] == '\r') {
			i++;
			count++;
		}
		i++;
	} while (count != wanted);
	Common::String name;
	while (i < size && text[i] != '\r')
		name += (char)text[i++];
	return name;
}

static bool readOriginalPlayersAtl(Common::SeekableReadStream &in, Common::String players[kSaveMaxPlayers],
                                   int chapters[kSaveMaxPlayers]) {
	byte b[kOrigPlayersSize];
	if (in.read(b, kOrigPlayersSize) != kOrigPlayersSize)
		return false;
	for (uint i = 0; i < kSaveMaxPlayers; i++) {
		Common::String name;
		for (uint j = 0; j < 17 && b[17 * i + j]; j++)
			name += (char)b[17 * i + j];
		if (name.empty() || name == "#New Player")
			break;
		players[i] = name;
		chapters[i] = b[85 + i];
	}
	return true;
}

bool readPlayersFile(Common::SeekableReadStream &in, Common::String players[kSaveMaxPlayers],
                     int chapters[kSaveMaxPlayers]) {
	for (uint i = 0; i < kSaveMaxPlayers; i++) {
		players[i].clear();
		chapters[i] = 0;
	}
	const int64 start = in.pos();
	uint32 magic   = in.readUint32BE();
	uint32 version = in.readUint32BE();
	if (magic == kPlayersMagic && version >= 1) {
		uint32 count = in.readUint32BE();
		for (uint32 i = 0; i < count && i < kSaveMaxPlayers && !in.eos(); i++) {
			uint16 len = in.readUint16BE();
			Common::String name;
			for (uint16 j = 0; j < len && !in.eos(); j++)
				name += (char)in.readByte();
			players[i] = name;
		}
		if (version >= 2) {
			for (uint32 i = 0; i < count && i < kSaveMaxPlayers && !in.eos(); i++)
				chapters[i] = in.readSint32BE();
		}
		return true;
	}
	if (in.size() - start != kOrigPlayersSize || !in.seek(start))
		return false;
	return readOriginalPlayersAtl(in, players, chapters);
}

void writePlayersFile(Common::WriteStream &out, const Common::String players[kSaveMaxPlayers],
                      const int chapters[kSaveMaxPlayers]) {
	out.writeUint32BE(kPlayersMagic);
	out.writeUint32BE(kPlayersVersion);
	out.writeUint32BE(kSaveMaxPlayers);
	for (uint i = 0; i < kSaveMaxPlayers; i++) {
		out.writeUint16BE((uint16)players[i].size());
		out.write(players[i].c_str(), players[i].size());
	}
	for (uint i = 0; i < kSaveMaxPlayers; i++)
		out.writeSint32BE(chapters[i]);
}

bool checkpointWritten(uint chapter) {
	return chapter >= 1 && chapter < kSavePlayerSlotStride;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
