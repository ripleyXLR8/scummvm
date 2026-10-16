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

#ifndef CRYOMNI3D_ATLANTIS_APC_STREAM_H
#define CRYOMNI3D_ATLANTIS_APC_STREAM_H

#include "common/scummsys.h"

namespace Common {
class SeekableReadStream;
}

namespace Audio {
class AudioStream;
}

namespace CryOmni3D {
namespace Atlantis {

// The frame count of an APC header (u32 at +0xc), or 0 when the header is
// too short.  The stream position is kept.
uint32 apcHeaderFrames(Common::SeekableReadStream &file);

// An APC sound decoded as atlantis.exe FUN_004101ce does: the whole data
// after the header, cut at the header's frame count.  Every shipped file has
// one data byte past that count (one stereo or two mono frames), which the
// original never plays.  Reads the file from its current position (the
// header) to the end; the file may be deleted afterwards.  Null when the
// header is not an APC 1.20 header.  atlantis-re functions/FUN_004101ce.md.
Audio::AudioStream *makeAtlantisAPCStream(Common::SeekableReadStream &file);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
