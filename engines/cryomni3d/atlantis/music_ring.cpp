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

#include "common/endian.h"
#include "common/util.h"
#include "common/textconsole.h"

#include "cryomni3d/atlantis/music_ring.h"

namespace CryOmni3D {
namespace Atlantis {

// The standard IMA tables (atlantis.exe 0x00490050, 0x004901c0).
static const int32 kStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};
static const int32 kIndexAdjust[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

MusicRing::MusicRing(const byte *file, uint32 size, uint32 ahead, uint32 granularity) :
		_file(file), _size(size), _filePos(0), _ahead(ahead), _gran(granularity ? granularity : 1),
		_src(0), _total(0), _remaining(0), _rate(22050), _stereo(true),
		_write(0), _refillBlock(0), _releaseBlock(0), _mark(0), _on(false), _play(0) {
	_mem = new byte[kRingSize + kFlagsSize]();
	memset(_buf, 0, sizeof(_buf));
	_pred[0] = _pred[1] = 0;
	_index[0] = _index[1] = 0;
}

MusicRing::~MusicRing() {
	delete[] _mem;
}

uint32 MusicRing::flag(uint i) const {
	return READ_LE_UINT32(_mem + kRingSize + 4 * i);
}

void MusicRing::setFlag(uint i, uint32 v) {
	WRITE_LE_UINT32(_mem + kRingSize + 4 * i, v);
}

uint32 MusicRing::readFile(uint32 n) {
	// ReadFile: what is left of the file; the rest of the buffer keeps its
	// previous bytes.
	const uint32 got = (_filePos < _size) ? MIN<uint32>(n, _size - _filePos) : 0;
	memcpy(_buf, _file + _filePos, got);
	_filePos += got;
	return got;
}

bool MusicRing::header() {
	// FUN_00417dd0: "CRYO_APC", data at +0x20, rate +0x10, frames +0x0c,
	// predictors +0x14 / +0x18, step indexes 0, stereo flag +0x1c bit 0.
	if (memcmp(_buf, "CRYO_APC", 8))
		return false;
	_src = 0x20;
	_total = _remaining = READ_LE_UINT32(_buf + 0x0c);
	_rate = READ_LE_UINT32(_buf + 0x10);
	_pred[0] = (int32)READ_LE_UINT32(_buf + 0x14);
	_pred[1] = (int32)READ_LE_UINT32(_buf + 0x18);
	_index[0] = _index[1] = 0;
	_stereo = (READ_LE_UINT32(_buf + 0x1c) & 1) != 0;
	return true;
}

void MusicRing::decode(uint32 count) {
	// FUN_004101ce: count frames, at most the frames left; high nibble
	// first (left, then right in stereo); 32-bit predictor, never clamped,
	// stored as int16.
	const uint32 n = MIN(count, _remaining);
	_remaining -= n;
	const uint channels = _stereo ? 2 : 1;
	for (uint32 f = 0; f < n; f++) {
		const byte b = (_src < sizeof(_buf)) ? _buf[_src] : 0;
		_src++;
		const byte codes[2] = { (byte)(b >> 4), (byte)(b & 0xf) };
		for (uint k = 0; k < 2; k++) {
			const uint ch = (channels == 2) ? k : 0;
			const byte c = codes[k];
			const int32 step = kStepTable[_index[ch]];
			int32 d = step >> 3;
			if (c & 4)
				d += step;
			if (c & 2)
				d += step >> 1;
			if (c & 1)
				d += step >> 2;
			if (c & 8)
				d = -d;
			_pred[ch] += d;
			_index[ch] = CLIP<int32>(_index[ch] + kIndexAdjust[c & 7], 0, 88);
			if (_write + 2 <= kRingSize + kFlagsSize)
				WRITE_LE_UINT16(_mem + _write, (uint16)(int16)_pred[ch]);
			_write += 2;
		}
	}
}

bool MusicRing::start() {
	// FUN_00432ea8 @ 0x00432f44..0x00433109.
	_write = 0;
	_releaseBlock = _refillBlock = 0;
	_mark = 0;
	_filePos = 0;
	if (readFile(kBlockBytes) == 0)
		return false;
	memset(_mem, 0, kRingSize + kFlagsSize);
	if (!header() || !_stereo)
		return false;
	decode(0x3fe0);
	setFlag(0, 1);
	for (uint i = 1; i < 8; i++) {
		readFile(kBlockBytes);
		_src = 0;
		decode(kBlockBytes);
		setFlag(i, 1);
	}
	_on = true;
	_write = 0;
	_play = 0;
	return true;
}

void MusicRing::release() {
	// FUN_00433310: the position minus 26000, unsigned; one ring added when
	// it is below the mark.
	if (!_on)
		return;
	const uint32 pos = (uint32)(((_play + _ahead) / _gran * _gran) % kRingSize);
	const uint32 p = pos - 0x6590;
	const uint32 q = (p < _mark) ? p + kRingSize : p;
	if (q - _mark > 0x10000) {
		setFlag(_releaseBlock, 0);
		_releaseBlock = (_releaseBlock + 1) & 7;
		_mark = p;
	}
}

void MusicRing::refill() {
	// FUN_0043318c.
	if (flag(_refillBlock) != 0 || !_on)
		return;
	_src = 0;
	const uint32 got = readFile(kBlockBytes);
	decode(kBlockBytes);
	if (got < kBlockBytes) {
		// The loop: back to the start, the header again, the next block
		// decoded right after the tail.
		_filePos = 0;
		readFile(0x20);
		header();
		readFile(kBlockBytes);
		_src = 0;
		decode(kBlockBytes);
	}
	setFlag(_refillBlock, 1);
	if (++_refillBlock == 8) {
		_write = 0;
		_refillBlock = 0;
	}
}

uint MusicRing::framesOfTick(uint32 t) const {
	return (uint)((uint64)(t + 1) * _rate * 5 / 1000 - (uint64)t * _rate * 5 / 1000);
}

void MusicRing::play(uint frames, int16 *out) {
	for (uint f = 0; f < frames; f++) {
		const uint32 off = (uint32)(_play % kRingSize);
		out[2 * f] = (int16)READ_LE_UINT16(_mem + off);
		out[2 * f + 1] = (int16)READ_LE_UINT16(_mem + off + 2);
		_play += 4;
	}
}

void MusicRing::timer() {
	release();
	refill();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
