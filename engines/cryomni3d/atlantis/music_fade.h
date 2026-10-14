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

#ifndef CRYOMNI3D_ATLANTIS_MUSIC_FADE_H
#define CRYOMNI3D_ATLANTIS_MUSIC_FADE_H

#include "common/mutex.h"
#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The music volume of atlantis.exe and its fades, moved by the 200 Hz timer
// callback FUN_00431150: the tick 0x006ce118 + 1, then FUN_0042f7a8 (down)
// and FUN_0042f974 (up): one AIL volume step (0..127) every 5 ticks (25 ms).
// Field names give the original's variables.  See atlantis-re
// functions/audio-FUN_00432ea8.md; emulated by tools/emu/audio_music_fade.py.
struct AtlantisMusicFade {
	Common::Mutex mutex;
	uint8 cur;          // 0x00495e71: the music sample's volume
	uint8 mode;         // 0x00495eac: 2 fade down, 1 fade up, 0 idle
	int32 downTarget;   // 0x00495e6c: -1 until the fade down starts
	int32 upTarget;     // 0x006ce124
	uint8 percent;      // 0x00495ea4: fade-down percentage (80 by default)
	uint8 vol;          // 0x00495e72: the music volume (sound menu)
	uint32 ticks;       // 0x006ce118
	uint32 lastStep;    // 0x006ce0f4
	// FUN_0042f7a8's second part: the NewSound sample 0x006ce110 (cinema
	// sound) fading out after a cut-short video.
	uint8 sfxMode;      // 0x00495eb0: 2 fade out
	int32 sfxVol;       // 0x006ce128
	int32 sfxVol0;      // its start, 0x00495e73 (the effects volume)
	uint32 sfxLast;     // 0x006ce104
	bool sfxChanged;    // set by tick: apply sfxVol
	bool sfxStop;       // set by tick: stop the sample

	AtlantisMusicFade() : cur(0), mode(0), downTarget(-1), upTarget(0), percent(80), vol(127),
		ticks(0), lastStep(0), sfxMode(0), sfxVol(0), sfxVol0(127), sfxLast(0), sfxChanged(false),
		sfxStop(false) {}

	// One tick of FUN_00431150.
	void tick() {
		Common::StackLock lock(mutex);
		ticks++;
		// FUN_0042f7a8: the target is computed on the first tick; already
		// reached, the fade ends without a step.
		if (mode == 2) {
			bool done = false;
			if (downTarget == -1) {
				upTarget = vol;
				downTarget = (int32)vol - (int32)vol * percent / 100;
				if (downTarget < 0)
					downTarget = 0;
				if ((int32)cur <= downTarget) {
					downTarget = -1;
					mode = 0;
					done = true;
				}
			}
			if (!done) {
				if (ticks - lastStep > 4) {
					lastStep = ticks;
					cur = (uint8)(cur - 1);
				}
				if ((int32)cur <= downTarget) {
					mode = 0;
					downTarget = -1;
				}
			}
		}
		if (sfxMode == 2) {
			if (ticks - sfxLast > 4) {
				sfxLast = ticks;
				sfxVol -= 2;
				if (sfxVol < 0)
					sfxVol = 0;
				sfxChanged = true;
			}
			if (sfxVol < 1) {
				sfxStop = true;
				sfxMode = 0;
			}
		}
		// FUN_0042f974: the step comes before the test, so a fade that
		// starts at its target still goes one step above it.
		if (mode == 1) {
			if (ticks - lastStep > 4) {
				lastStep = ticks;
				cur = (uint8)(cur + 1);
			}
			if (upTarget <= (int32)cur)
				mode = 0;
		}
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
