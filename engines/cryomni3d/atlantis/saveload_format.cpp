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
static const uint32 kOrigPlayersSize = 97;     // English build: 6 chapter, 6 letter bytes
static const uint32 kOrigPlayersSizeFr = 95;   // French build: 5 and 5

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
                                   int chapters[kSaveMaxPlayers], char letters[kSaveMaxPlayers] = nullptr) {
	byte b[kOrigPlayersSize];
	const uint32 size = (uint32)(in.size() - in.pos());
	if (size != kOrigPlayersSize && size != kOrigPlayersSizeFr)
		return false;
	if (in.read(b, size) != size)
		return false;
	// names 5 x 17, then the chapters, then the save-file letters (one array
	// of 6 bytes each in the English build, of 5 in the French one)
	const uint letterBase = (size == kOrigPlayersSize) ? 0x5b : 0x5a;
	if (letters) {
		for (uint i = 0; i < kSaveMaxPlayers; i++)
			letters[i] = (char)b[letterBase + i];
	}
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
	if ((in.size() - start != kOrigPlayersSize && in.size() - start != kOrigPlayersSizeFr) || !in.seek(start))
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

bool readOriginalPlayersLetters(Common::SeekableReadStream &in, char letters[kSaveMaxPlayers]) {
	Common::String players[kSaveMaxPlayers];
	int chapters[kSaveMaxPlayers];
	return readOriginalPlayersAtl(in, players, chapters, letters);
}

bool parseOriginalCheckpoint(const byte *b, uint32 size, OrigCheckpoint &cp) {
	if (size != kOrigCheckpointSize)
		return false;
	Common::String path;
	for (uint i = 0; i < 0x104 && b[i]; i++)
		path += (char)b[i];
	// "wam\atlan4.wam" -> "ATLAN4.WAM"
	const char *slash = strrchr(path.c_str(), '\\');
	cp.wamName = slash ? Common::String(slash + 1) : path;
	cp.wamName.toUppercase();
	cp.chapter = b[0x104];
	cp.prevChapter = b[0x105];
	cp.placeId = (int32)READ_LE_UINT32(b + 0x112);
	cp.lastVue = (int32)READ_LE_UINT32(b + 0x116);
	cp.incPerso = b[0x11e];
	const int32 count = (int32)READ_LE_UINT32(b + 0x11f);
	cp.inventory.clear();
	for (int32 i = 0; i < count && i < 30; i++) {
		const byte *r = b + 0x123 + 9 * i;
		OrigCheckpoint::Record rec;
		rec.sprite = r[0];
		rec.id = (int32)READ_LE_UINT32(r + 1);
		cp.inventory.push_back(rec);
	}
	cp.conCounters.resize(0x5dc);
	memcpy(cp.conCounters.data(), b + 0x231, 0x5dc);
	for (uint i = 0; i < 7; i++)
		cp.vars[i] = (int32)READ_LE_UINT32(b + 0x80d + 4 * i);
	cp.guestCount = MIN<uint>(b[0x829], 4);
	for (uint i = 0; i < 4; i++) {
		cp.guestId[i] = (int32)READ_LE_UINT32(b + 0x82a + 5 * i);
		cp.guestSprite[i] = b[0x82a + 5 * i + 4];
	}
	const int32 hidden = (int32)READ_LE_UINT32(b + 0x83e);
	cp.hiddenPersos.clear();
	for (int32 i = 0; i < hidden && i < 256; i++)
		cp.hiddenPersos.push_back((int16)READ_LE_UINT16(b + 0x842 + 2 * i));
	return true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
