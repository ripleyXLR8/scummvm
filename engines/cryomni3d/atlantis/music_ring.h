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

#ifndef CRYOMNI3D_ATLANTIS_MUSIC_RING_H
#define CRYOMNI3D_ATLANTIS_MUSIC_RING_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The original's music streaming ring (atlantis.exe FUN_00432ea8 start,
// FUN_0043318c refill, FUN_00433310 release, FUN_004101ce decode,
// FUN_00417dd0 header): an 8-block ring of 0x80000 bytes that Miles loops
// over, each block refilled from the file once the timer has seen the play
// position move 0x10000 bytes past the last mark.  What is heard depends on
// the position the sound driver reports; the law measured under Wine (the
// play position + 0x8000, rounded down to 0x4000) is the default here.
// Memory is the ring followed by the 0x20000-byte area of the block flags,
// as in the original (0x0062c530, 0x006ac530), so a refill that runs past
// the ring end writes into the flags as it does there.
// atlantis-re functions/audio-FUN_00432ea8.md, decisions/0016 row 1,
// experiments/2026-10-16-17, -25, -28.
class MusicRing {
public:
	static const uint32 kRingSize = 0x80000;
	static const uint32 kFlagsSize = 0x20000;
	static const uint32 kBlockBytes = 0x4000;   // file bytes per block

	// file must outlive the ring.  ahead / granularity: the reported
	// position law (AIL_sample_position).
	MusicRing(const byte *file, uint32 size, uint32 ahead = 0x8000, uint32 granularity = 0x4000);
	~MusicRing();

	// FUN_00432ea8 from its first read on; false when the file is empty or
	// its header is not an APC header (the original returns 0), or when the
	// track is mono (every music track is stereo; not modelled).
	bool start();

	// The sample plays frames frames of the ring into out (interleaved
	// stereo, 2 x frames samples).
	void play(uint frames, int16 *out);
	// The end of one timer period: FUN_00433310, then FUN_0043318c (under
	// Wine the refill follows each release within one frame, experiment
	// 2026-10-16-25).
	void timer();
	// play then timer: one 5 ms period.
	void tick(uint frames, int16 *out) { play(frames, out); timer(); }

	// Frames of one 5 ms period t of the 200 Hz timer.
	uint framesOfTick(uint32 t) const;

	uint32 rate() const { return _rate; }
	bool stereo() const { return _stereo; }
	uint64 playedBytes() const { return _play; }
	uint32 headerFrames() const { return _total; }

private:
	uint32 readFile(uint32 n);              // ReadFile into _buf, bytes read
	bool header();                          // FUN_00417dd0 on _buf
	void decode(uint32 count);              // FUN_004101ce at _write
	void release();                         // FUN_00433310
	void refill();                          // FUN_0043318c
	uint32 flag(uint i) const;
	void setFlag(uint i, uint32 v);

	const byte *_file;
	uint32 _size, _filePos;
	uint32 _ahead, _gran;
	byte *_mem;                             // ring + flag area
	byte _buf[kBlockBytes];                 // [0x006ce0d4]
	uint32 _src;                            // decoder data offset in _buf
	uint32 _total, _remaining, _rate;
	bool _stereo;
	int32 _pred[2];
	int32 _index[2];
	uint32 _write;                          // 0x006ce0dc - ring
	uint32 _refillBlock;                    // 0x006ce0c8
	uint32 _releaseBlock;                   // 0x006ce0d8
	uint32 _mark;                           // 0x006ce0b0
	bool _on;                               // 0x00495ea8
	uint64 _play;                           // bytes the sample has played
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
