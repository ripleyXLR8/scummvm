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

#include "audio/audiostream.h"
#include "audio/decoders/apc.h"
#include "audio/timestamp.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/apc_stream.h"

namespace CryOmni3D {
namespace Atlantis {

uint32 apcHeaderFrames(Common::SeekableReadStream &file) {
	const int64 start = file.pos();
	byte header[0x20];
	const bool ok = file.read(header, sizeof(header)) == sizeof(header);
	file.seek(start);
	return ok ? READ_LE_UINT32(header + 0xc) : 0;
}

Audio::AudioStream *makeAtlantisAPCStream(Common::SeekableReadStream &file) {
	const uint32 frames = apcHeaderFrames(file);
	Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(file);
	if (!apc)
		return nullptr;
	const int64 remaining = file.size() - file.pos();
	if (remaining > 0) {
		byte *buf = (byte *)malloc((size_t)remaining);
		file.read(buf, (uint32)remaining);
		apc->queuePacket(new Common::MemoryReadStream(buf, (uint32)remaining, DisposeAfterUse::YES));
	}
	apc->finish();
	// FUN_004101ce decodes while st->remaining (the header's frame count)
	// is not 0.
	return Audio::makeLimitingAudioStream(apc, Audio::Timestamp(0, frames, apc->getRate()));
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
