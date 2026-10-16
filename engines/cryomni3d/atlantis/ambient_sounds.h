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

#ifndef CRYOMNI3D_ATLANTIS_AMBIENT_SOUNDS_H
#define CRYOMNI3D_ATLANTIS_AMBIENT_SOUNDS_H

#include "common/array.h"
#include "common/scummsys.h"

#include "cryomni3d/atlantis/wam_parser.h"

namespace CryOmni3D {
namespace Atlantis {

// The places' ambient sounds (the sea at the fisherman's, the palace
// fountain, the forest waterfall, Muria): the WAM's 3D ambient objects,
// loaded on place entry by FUN_00431a1c and updated every frame by
// FUN_00432178.  atlantis-re: functions/audio-FUN_00431a1c.md,
// functions/audio-FUN_00432178.md, experiments/2026-10-14-06.

// FUN_00450ed8 and FUN_00432178's pan / volume for one object.
struct AmbientGains {
	int32 right;    // object +0x1e
	int32 left;     // object +0x22, after FUN_00432178's "a + b == 0" fix
	int32 out26;    // object +0x26 = trunc(ref c)
	uint8 pan;      // AIL_set_sample_pan
	uint8 volume;   // AIL_set_sample_volume
};

// pos: the object's position after the load rotation (x' = -y, y' = x,
// z' = -z); cam: the place's centre (FUN_004315b4); alpha, beta: the view
// angles (0x00bcb59c, 0x00bcb598); ear: float 0x00497c90 (0.65 once a WAM
// is loaded).
AmbientGains ambientGains(const float pos[3], float loudness, const AtlantisCentrePos &cam,
                          const AtlantisAmbientRef &ref, float alpha, float beta, float ear = 0.65f);

// The six Miles sample handles of the original (0x006cc560..0x006cc574).
class AmbientSampleOutput {
public:
	enum Status { kPlaying = 4, kDone = 2, kStopped = 8 };
	virtual ~AmbientSampleOutput() {}
	virtual int status(uint slot) = 0;
	// AIL_set_sample_address + loop count + AIL_start_sample: variant k of
	// object `object` (an index in the WAM's object table).
	virtual void start(uint slot, uint object, uint variant, bool loopForever) = 0;
	virtual void stop(uint slot) = 0;
	virtual void setPan(uint slot, uint8 pan) = 0;
	virtual void setVolume(uint slot, uint8 volume) = 0;
	// The record's buffers are freed (FUN_00431a1c remove pass, FUN_00431e98).
	virtual void release(uint object) = 0;
};

class AmbientSounds {
public:
	static const uint kSlots = 6;

	AmbientSounds();

	// The milliseconds clock FUN_004320bc draws from (timeGetTime - base).
	typedef uint32 (*ClockFn)(void *ctx);
	void setClock(ClockFn fn, void *ctx) { _clock = fn; _clockCtx = ctx; }

	// FUN_00431a1c on entering `place` of `objects` (the WAM's table).
	// False when a variant cannot be loaded (`canLoad` says so).
	typedef bool (*CanLoadFn)(void *ctx, uint object, uint variant);
	bool enterPlace(AmbientSampleOutput &out, const AtlantisPlace &place,
	                const Common::Array<AtlantisAmbientObject> &objects,
	                CanLoadFn canLoad = nullptr, void *ctx = nullptr);
	// FUN_00432178's per-sample loop for the current place.
	void frame(AmbientSampleOutput &out, const AtlantisPlace &place,
	           const Common::Array<AtlantisAmbientObject> &objects,
	           const AtlantisCentrePos &cam, float alpha, float beta, int32 chapterTime);
	// FUN_00431e98.
	void stopAll(AmbientSampleOutput &out);
	// The effects volume 0x00495e73 (0..127) that FUN_00431e98 gives every
	// sample before stopping it.
	void setEffectsVolume(uint8 v) { _effectsVolume = v; }

	// FUN_0043259c, before a move video: for each ambient sound of the place
	// being left, the per-frame pan / volume steps from the last values
	// toward FUN_00450ed8's gains at the destination's camera `cam` (with
	// the current angles), divided by the video's frame count.
	void prepareGlide(const AtlantisPlace &place, const Common::Array<AtlantisAmbientObject> &objects,
	                  const AtlantisCentrePos &cam, float alpha, float beta, int32 frames);
	// FUN_00432758, once per frame of any video (FUN_0041bc74): restarts a
	// finished multi-variant sample, then adds the steps and sets pan and
	// volume.
	void videoFrame(AmbientSampleOutput &out, const AtlantisPlace &place,
	                const Common::Array<AtlantisAmbientObject> &objects, int32 chapterTime);

	bool active() const { return _active; }
	uint count() const { return _count; }

	// The 0x2e-byte records of 0x006120f0, public for the tests.
	struct Record {
		uint8 count;       // +0
		uint8 current;     // +1
		uint32 delay;      // +2: CHAPTERTIME seconds
		uint32 start;      // +6: pause start, 0 = none
		int object;        // +0xa: the object (an index in the WAM table), -1 = none
	};
	Record _records[kSlots + 1];
	bool _loopFlag;        // 0x00495e8c
	uint8 _effectsVolume;  // 0x00495e73
	// The last pan / volume set, as floats (0x006cc5d0 / 0x006cc548), and
	// the video steps (0x006cc530 / 0x006cc5b8); nothing clears them.
	float _curPan[kSlots], _curVol[kSlots];
	float _stepPan[kSlots], _stepVol[kSlots];

	// FUN_004320bc.
	int pickVariant(const AtlantisAmbientObject &obj, uint count);

private:
	int objectOf(const AtlantisAmbientRef &ref, const Common::Array<AtlantisAmbientObject> &objects) const;
	void startVariant(AmbientSampleOutput &out, uint i, int v, bool useLoopFlag);

	uint _count;           // 0x00772911
	bool _active;          // 0x00496980
	ClockFn _clock;
	void *_clockCtx;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
