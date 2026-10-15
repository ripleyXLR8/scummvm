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

#ifndef CRYOMNI3D_ATLANTIS_AMBIENT_OUTPUT_H
#define CRYOMNI3D_ATLANTIS_AMBIENT_OUTPUT_H

#include <math.h>

#include "audio/mixer.h"
#include "common/array.h"
#include "common/hashmap.h"

#include "cryomni3d/atlantis/ambient_sounds.h"
#include "cryomni3d/atlantis/miles_samples.h"

namespace Common {
class SeekableReadStream;
}

namespace CryOmni3D {
namespace Atlantis {

// Miles pan 0 (left) .. 127 (right) onto the mixer's balance, as the
// DirectSound back-end of the shipped MSS32.DLL plays it (preference 15 = 0
// selects it; atlantis.exe sets no preference): 0x2000dd50 calls
// IDirectSoundBuffer::SetPan with trunc(2000 log10(p / 63)) for p < 63,
// -trunc(2000 log10((127 - p) / 63)) for p > 64, 0 for 63 and 64, -10000
// for p = 0 (helper 0x2000dcd0, -2000.0 at 0x2001e008).  The near channel
// stays full and the far one gets 10^(cB / 2000), i.e. p / 63 (or
// (127 - p) / 63) up to the truncation; the mixer's far channel is
// (127 - |balance|) / 127, so the balance is the nearest step to that.
// See atlantis-re: functions/audio-FUN_00432178.md (Port) and
// experiments/2026-10-15-61-mss32-status-pan.md.
inline int8 milesPanBalance(uint8 pan) {
	const int p = pan & 0x7f;
	if (p == 63 || p == 64)
		return 0;
	const int q = (p < 63) ? p : 127 - p;     // the far channel's numerator
	const double cB = (q == 0) ? -10000.0 : trunc(2000.0 * log10(q / 63.0));
	const int far = (int)floor(127.0 * pow(10.0, cB / 2000.0) + 0.5);
	return (int8)((p < 63) ? far - 127 : 127 - far);
}

// The six Miles sample handles of the ambient sounds on the ScummVM mixer,
// which the puzzles' sound effects share (FUN_0043ca28, FUN_0043cd60,
// FUN_0043ce80, FUN_0043cf70) with their statuses (miles_samples.h).
// FUN_00430d20 sets them up as 16-bit mono at 22050 Hz and nothing changes
// that, so every variant plays at 22050 Hz whatever its header says (two
// 16000 Hz waves are heard 1.38 times fast in the original too).
class AtlantisAmbientOutput : public AmbientSampleOutput {
public:
	explicit AtlantisAmbientOutput(Audio::Mixer *mixer);
	~AtlantisAmbientOutput() override;

	// FUN_00431a1c: decode variant `variant` of object `object` (an APC file)
	// into 16-bit PCM, kept until release(object).
	bool load(uint object, uint variant, Common::SeekableReadStream &apc);

	int status(uint slot) override;
	void start(uint slot, uint object, uint variant, bool loopForever) override;
	void stop(uint slot) override;
	void setPan(uint slot, uint8 pan) override;
	void setVolume(uint slot, uint8 volume) override;
	void release(uint object) override;
	void stopAll();

	// AIL_set_sample_address + AIL_start_sample of another sound on the slot
	// (a puzzle effect, the options' channel test): `s` is played with the
	// mixer volume and balance given (the slot's pan / volume are set by the
	// caller through setPan / setVolume when they are the original's).
	void playOn(uint slot, Audio::AudioStream *s, byte mixerVolume, int8 balance);
	// The statuses as AIL_sample_status returns them.
	void statuses(int st[AmbientSounds::kSlots]);

private:
	Audio::Mixer *_mixer;
	Audio::SoundHandle _handles[AmbientSounds::kSlots];
	MilesSampleStatus _miles;
	uint8 _pan[AmbientSounds::kSlots], _volume[AmbientSounds::kSlots];
	Common::HashMap<uint, Common::Array<byte> > _pcm;   // object * 4 + variant -> LE s16
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
