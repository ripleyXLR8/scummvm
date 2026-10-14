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

#include "audio/mixer.h"
#include "common/array.h"
#include "common/hashmap.h"

#include "cryomni3d/atlantis/ambient_sounds.h"

namespace Common {
class SeekableReadStream;
}

namespace CryOmni3D {
namespace Atlantis {

// The six Miles sample handles of the ambient sounds on the ScummVM mixer.
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

private:
	Audio::Mixer *_mixer;
	Audio::SoundHandle _handles[AmbientSounds::kSlots];
	bool _started[AmbientSounds::kSlots];
	uint8 _pan[AmbientSounds::kSlots], _volume[AmbientSounds::kSlots];
	Common::HashMap<uint, Common::Array<byte> > _pcm;   // object * 4 + variant -> LE s16
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
