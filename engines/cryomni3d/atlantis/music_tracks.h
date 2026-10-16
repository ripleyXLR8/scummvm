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

#ifndef CRYOMNI3D_ATLANTIS_MUSIC_TRACKS_H
#define CRYOMNI3D_ATLANTIS_MUSIC_TRACKS_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// Music track table, 1-based as the startmusik / stopmusik / loadmusik CON
// command ids (entry 0 unused, nullptr).  atlantis.exe's table: 13-byte
// records at VA 0x00495eec, index = StartMusik - 1 (FUN_0042a100 @
// 0x0042ae0f calls FUN_00432ea8(N - 1)).  Filenames live in the BigFile's
// WAV\ directory.  In its own unit so the tests can link it
// (test/engines/cryomni3d/atlantis/scripts_music.h).
enum {
	kMusicTrackCount = 32   // entries, including the unused entry 0
};

extern const char *const kMusicTrackNames[kMusicTrackCount];

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
