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

#include "cryomni3d/atlantis/ambient_output.h"

#include "audio/audiostream.h"
#include "audio/decoders/apc.h"
#include "audio/decoders/raw.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/debug.h"
#include "common/stream.h"

#include "cryomni3d/cryomni3d.h"

namespace CryOmni3D {
namespace Atlantis {

AtlantisAmbientOutput::AtlantisAmbientOutput(Audio::Mixer *mixer) : _mixer(mixer) {
	for (uint i = 0; i < AmbientSounds::kSlots; i++) {
		_started[i] = false;
		_pan[i] = 0x40;   // FUN_00430d20: pan 0x40, volume 0
		_volume[i] = 0;
	}
}

AtlantisAmbientOutput::~AtlantisAmbientOutput() {
	stopAll();
}

bool AtlantisAmbientOutput::load(uint object, uint variant, Common::SeekableReadStream &apc) {
	// FUN_00417cb4: the buffer holds the header's sample count (+0xc) times 2
	// (mono) or 4 (stereo, flags +0x1c & 1) bytes, and nothing more.
	const int64 start = apc.pos();
	apc.seek(start + 0xc);
	const uint32 samples = apc.readUint32LE();
	apc.seek(start + 0x1c);
	const uint32 bytes = samples * ((apc.readUint32LE() & 1) ? 4 : 2);
	apc.seek(start);
	Audio::PacketizedAudioStream *dec = Audio::makeAPCStream(apc);
	if (!dec)
		return false;
	const int64 remaining = apc.size() - apc.pos();
	if (remaining > 0) {
		byte *buf = (byte *)malloc((size_t)remaining);
		apc.read(buf, (uint32)remaining);
		dec->queuePacket(new Common::MemoryReadStream(buf, (uint32)remaining, DisposeAfterUse::YES));
	}
	dec->finish();
	Common::Array<byte> &out = _pcm[object * 4 + variant];
	out.clear();
	int16 tmp[2048];
	for (;;) {
		const int n = dec->readBuffer(tmp, ARRAYSIZE(tmp));
		if (n <= 0)
			break;
		const uint at = out.size();
		out.resize(at + 2 * n);
		for (int k = 0; k < n; k++)
			WRITE_LE_INT16(&out[at + 2 * k], tmp[k]);
		if (dec->endOfData())
			break;
	}
	delete dec;
	if (out.size() > bytes)
		out.resize(bytes);
	debugC(1, kDebugMusic, "ambient: object %u variant %u loaded, %u bytes", object, variant, out.size());
	return true;
}

int AtlantisAmbientOutput::status(uint slot) {
	if (_started[slot] && _mixer->isSoundHandleActive(_handles[slot]))
		return kPlaying;
	return _started[slot] ? kDone : kStopped;
}

void AtlantisAmbientOutput::start(uint slot, uint object, uint variant, bool loopForever) {
	_mixer->stopHandle(_handles[slot]);
	_started[slot] = false;
	if (!_pcm.contains(object * 4 + variant))
		return;
	const Common::Array<byte> &pcm = _pcm[object * 4 + variant];
	if (pcm.empty())
		return;
	// A copy: the original frees a record's buffers while a shifted handle may
	// still play them; here the mixer keeps its own.
	byte *copy = (byte *)malloc(pcm.size());
	memcpy(copy, pcm.data(), pcm.size());
	Audio::SeekableAudioStream *raw = Audio::makeRawStream(copy, pcm.size(), 22050,
	                                  Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN, DisposeAfterUse::YES);
	Audio::AudioStream *s = loopForever ? (Audio::AudioStream *)Audio::makeLoopingAudioStream(raw, 0) : raw;
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &_handles[slot], s, -1,
	                   (byte)(_volume[slot] * Audio::Mixer::kMaxChannelVolume / 127),
	                   milesPanBalance(_pan[slot]));
	_started[slot] = true;
	debugC(1, kDebugMusic, "ambient: slot %u plays object %u variant %u%s", slot, object, variant,
	       loopForever ? " (loop)" : "");
}

void AtlantisAmbientOutput::stop(uint slot) {
	_mixer->stopHandle(_handles[slot]);
}

// Miles pan 0 (left) .. 127 (right) onto the mixer's balance with the law of
// the MSS32.DLL DirectSound back-end (milesPanBalance); Miles volume 0..127
// onto 0..255, linear in amplitude as there (0x2000dc60).
void AtlantisAmbientOutput::setPan(uint slot, uint8 pan) {
	_pan[slot] = pan & 0x7f;
	_mixer->setChannelBalance(_handles[slot], milesPanBalance(_pan[slot]));
}

void AtlantisAmbientOutput::setVolume(uint slot, uint8 volume) {
	_volume[slot] = volume & 0x7f;
	_mixer->setChannelVolume(_handles[slot], (byte)(_volume[slot] * Audio::Mixer::kMaxChannelVolume / 127));
}

void AtlantisAmbientOutput::release(uint object) {
	for (uint k = 0; k < 4; k++)
		_pcm.erase(object * 4 + k);
}

void AtlantisAmbientOutput::stopAll() {
	for (uint i = 0; i < AmbientSounds::kSlots; i++) {
		_mixer->stopHandle(_handles[i]);
		_started[i] = false;
	}
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
