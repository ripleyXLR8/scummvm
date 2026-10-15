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

#ifndef CRYOMNI3D_ATLANTIS_MILES_SAMPLES_H
#define CRYOMNI3D_ATLANTIS_MILES_SAMPLES_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The statuses of the six Miles sample handles 0x006cc560..0x006cc574
// (atlantis.exe 714240), which the ambient sounds and the puzzles' sound
// effects share.  AIL_sample_status gives 2 (SMP_DONE) for a sample that
// finished or was never started (AIL_init_sample at start-up, FUN_00430d20),
// 4 (SMP_PLAYING) while it plays, and 8 (SMP_STOPPED) after AIL_stop_sample
// stopped it while playing; AIL_stop_sample leaves a done (2) or stopped (8)
// sample as it is.  Only a start changes 8.
// See atlantis-re: experiments/2026-10-15-71-puzzle-sample-pools-wine.md,
// functions/puzzles-FUN_0043d28c.md (Differences), functions/audio-FUN_00431e98.md.
struct MilesSampleStatus {
	enum { kSlots = 6, kDone = 2, kPlaying = 4, kStopped = 8 };

	// kDone, kPlaying (started; done once its sound has ended) or kStopped.
	uint8 state[kSlots];

	MilesSampleStatus() { reset(); }
	void reset() {
		for (uint i = 0; i < kSlots; i++)
			state[i] = kDone;
	}
	// AIL_sample_status; `active`: the slot's sound is still playing.
	int status(uint i, bool active) const {
		return (state[i] == kPlaying && !active) ? (int)kDone : (int)state[i];
	}
	// AIL_start_sample.
	void started(uint i) { state[i] = kPlaying; }
	// AIL_stop_sample.
	void stopped(uint i, bool active) {
		if (status(i, active) == kPlaying)
			state[i] = kStopped;
		else
			state[i] = (uint8)status(i, active);
	}

	// The slot a puzzle effect of `bytes` decoded bytes (the record's size,
	// samples (+0xc) x 2 or x 4 when stereo, FUN_00417cb4) starts on, or -1
	// when it is dropped; st: the statuses of samples 0..5.
	//
	// FUN_0043ca28 (0x0043ca7e..0x0043cb79): under 20000 (0x4e20) bytes, the
	// first of samples 0 and 1 in status 2; otherwise sample 2 in status 2.
	static int slotCa28(const int *st, int32 bytes) {
		if (bytes <= 0)
			return -1;
		if (bytes < 0x4e20) {
			for (int i = 0; i < 2; i++)
				if (st[i] == kDone)
					return i;
			return -1;
		}
		return st[2] == kDone ? 2 : -1;
	}
	// FUN_0043cd60 (0x0043cdb6..0x0043ce6a), the FUN_00438bf0 puzzle: over
	// 120000 (0x1d4c0) bytes nothing; else the first of samples 0..2 in
	// status 2.
	static int slotCd60(const int *st, int32 bytes) {
		if (bytes <= 0 || bytes > 0x1d4c0)
			return -1;
		for (int i = 0; i < 3; i++)
			if (st[i] == kDone)
				return i;
		return -1;
	}
	// FUN_0043ce80 (0x0043ced3..0x0043ceed), the crab click: sample 0 in
	// status 2 or 8.
	static int slotCe80(const int *st, int32 bytes) {
		if (bytes <= 0)
			return -1;
		return (st[0] == kDone || st[0] == kStopped) ? 0 : -1;
	}
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
