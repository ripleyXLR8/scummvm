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

#include "cryomni3d/atlantis/ambient_sounds.h"

#include "common/textconsole.h"

#include <math.h>

// The x87 runs at 53 bits (control word 0x127f): double arithmetic with a
// (float) cast wherever the original stores a float.  No fused multiply-add.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace CryOmni3D {
namespace Atlantis {

namespace {

inline double f(double x) {
	return (double)(float)x;
}

// FUN_00451d6e then FISTP: truncation; out of the int32 range the x87 stores
// the indefinite value 0x80000000.
inline int32 truncInt(double v) {
	if (!(v > -2147483649.0 && v < 2147483648.0))
		return (int32)0x80000000;
	return (int32)v;
}

} // End of anonymous namespace

AmbientGains ambientGains(const float pos[3], float loudness, const AtlantisCentrePos &cam,
                          const AtlantisAmbientRef &ref, float alpha, float beta, float ear) {
	// FUN_00450ed8
	const double dx = f((double)pos[0] - (double)cam.x);
	const double dy = f((double)pos[1] - (double)cam.y);
	const double dz = f((double)pos[2] - (double)cam.z);
	const double dist = f(sqrt(dx * dx + dy * dy + dz * dz));
	const double A = alpha, B = beta;
	const double cB = f(cos(B));
	const double F0 = f(cos(A) * cB), F1 = f(sin(A) * cB);
	const double sBx = sin(-B);
	const double sB = f(sBx);
	const double k = f(1.0 / sqrt(1.0 - sBx * sB));
	const double G = f(-F1 * k), H = f(F0 * k);
	const double q = ear;
	const double S = f(sin(q)), C = f(cos(q));
	const double a18 = f(F0 * C), a1c = f(G * S), a28 = f(F1 * C), a2c = f(H * S);
	const double Lx = f(a18 - a1c), Ly = f(a28 - a2c), Z = f(sB * C);
	const double Rx = f(a18 + a1c), Ry = f(a28 + a2c);
	const double zz = f(Z * dz);
	const double gL = f((Lx * dx + Ly * dy + zz) / (dist * 2.0) + 0.5);   // 0x00bcab64
	const double gR = f((Rx * dx + Ry * dy + zz) / (dist * 2.0) + 0.5);   // 0x00bcab60
	const double m = f((double)ref.m * (double)loudness);
	const double t = ref.t;
	auto gain = [&](double x) {
		const int32 v = truncInt(((1.0 - t) * x + t) * m / dist);
		return v >= 256 ? 255 : v;   // a signed compare: a negative passes
	};
	AmbientGains g;
	g.right = gain(gL);
	g.left = gain(gR);
	g.out26 = truncInt(ref.c);
	// FUN_00432178 @ 0x0043241e..0x00432581
	if (g.right + g.left == 0)
		g.left = 1;
	const int64 sum = (int64)g.right + g.left;
	const int32 pan = (int32)(((int64)g.right * 127) / sum);
	const int32 vol = MAX(g.right, g.left) / 2;
	g.pan = (uint8)(pan & 0xff);
	g.volume = (uint8)(vol & 0xff);
	return g;
}

AmbientSounds::AmbientSounds() : _loopFlag(false), _count(0), _active(false), _clock(nullptr), _clockCtx(nullptr) {
	for (uint i = 0; i <= kSlots; i++) {
		_records[i].count = _records[i].current = 0;
		_records[i].delay = _records[i].start = 0;
		_records[i].object = -1;
	}
}

// FUN_0043187c: the object with the ref's id; the last match wins.
int AmbientSounds::objectOf(const AtlantisAmbientRef &ref, const Common::Array<AtlantisAmbientObject> &objects) const {
	int found = -1;
	if (ref.objectId == 0xff)
		return -1;
	for (uint k = 0; k < objects.size(); k++)
		if (objects[k].id == ref.objectId)
			found = k;
	return found;
}

// FUN_004320bc: a draw of the clock (timeGetTime - 0x00bcb5b8) & 0x7f,
// redrawn while above 100, then the first variant whose running weight sum
// exceeds it; -1 for a pause.  A weight above 100 counts as weight - 100 and
// sets 0x00495e8c (loop forever) as soon as it is scanned.
int AmbientSounds::pickVariant(const AtlantisAmbientObject &obj, uint count) {
	uint32 r;
	do {
		r = (_clock ? _clock(_clockCtx) : 0) & 0x7f;
	} while (r > 100);
	uint32 sum = 0;
	for (uint k = 0; k < count && k < 4; k++) {
		uint32 p = obj.weights[k];
		if (p > 100) {
			p -= 100;
			_loopFlag = true;
		}
		sum += p;
		if (sum > r)
			return (int)k;
	}
	return -1;
}

bool AmbientSounds::enterPlace(AmbientSampleOutput &out, const AtlantisPlace &place,
                               const Common::Array<AtlantisAmbientObject> &objects,
                               CanLoadFn canLoad, void *ctx) {
	const uint n = MIN<uint>(place.ambientRefs.size(), kSlots);
	if (n == 0) {
		if (_active)
			stopAll(out);
		return true;
	}
	// The add pass: "already loaded" is pointer identity, i.e. the same
	// slot of the static object table.
	for (uint i = 0; i < n; i++) {
		const int obj = objectOf(place.ambientRefs[i], objects);
		if (obj < 0) {
			warning("Atlantis ambient: reference %u of place %u names no object", i, place.placeId);
			continue;
		}
		bool loaded = false;
		for (uint r = 0; r < _count; r++)
			if (_records[r].object == obj)
				loaded = true;
		if (loaded)
			continue;
		// FUN_00431f9c(i), into record i (the ref index, not the count)
		const AtlantisAmbientObject &o = objects[obj];
		Record &rec = _records[i];
		uint count = 0;
		for (uint k = 0; k < 4; k++)
			if (!o.variants[k].empty())
				count++;
		int32 sum = 0;
		for (uint k = 0; k < count; k++)
			sum += o.weights[k] > 100 ? o.weights[k] - 100 : o.weights[k];
		rec.count = (uint8)count;
		rec.delay = (uint32)((100 - sum) / 4);
		pickVariant(o, count);   // result discarded; it may set 0x00495e8c
		rec.object = obj;
		for (uint k = 0; k < count; k++) {
			if (canLoad && !canLoad(ctx, obj, k))
				return false;
			rec.start = 0;
			out.stop(i);   // loop count 1, AIL_stop_sample
		}
		_active = true;
		_count++;
	}
	// The remove pass: records whose object is no longer referenced; the
	// records shift down, the handles do not.
	for (uint r = 0; r < _count; r++) {
		bool referenced = false;
		for (uint i = 0; i < n; i++)
			if (objectOf(place.ambientRefs[i], objects) == _records[r].object)
				referenced = true;
		if (referenced)
			continue;
		out.setPan(r, 0x40);
		out.setVolume(r, 0);
		out.stop(r);
		out.release(_records[r].object);
		for (uint j = r; j < _count && j < kSlots; j++)
			_records[j] = _records[j + 1];
		r--;
		_count--;
	}
	return true;
}

void AmbientSounds::startVariant(AmbientSampleOutput &out, uint i, int v, bool useLoopFlag) {
	Record &rec = _records[i];
	rec.current = (uint8)v;
	out.stop(i);
	bool loopForever = false;
	if (useLoopFlag && _loopFlag) {
		_loopFlag = false;
		loopForever = true;
	}
	out.start(i, rec.object, (uint)v, loopForever);
}

void AmbientSounds::frame(AmbientSampleOutput &out, const AtlantisPlace &place,
                          const Common::Array<AtlantisAmbientObject> &objects,
                          const AtlantisCentrePos &cam, float alpha, float beta, int32 chapterTime) {
	// FUN_00432178 @ 0x0043219e: only with references and 0x00496980 == 1
	if (place.ambientRefs.empty() || !_active)
		return;
	for (uint i = 0; i < _count; i++) {
		if (i >= place.ambientRefs.size())
			break;
		const AtlantisAmbientRef &ref = place.ambientRefs[i];
		const int refObj = objectOf(ref, objects);
		if (refObj < 0)
			continue;
		const AtlantisAmbientObject &o = objects[refObj];
		Record &rec = _records[i];
		const int status = out.status(i);
		if (rec.start != 0) {
			// a pause: over when CHAPTERTIME - start > delay (unsigned)
			if ((uint32)(chapterTime - (int32)rec.start) > rec.delay) {
				int v = -1;
				for (int tries = 0; v == -1 && tries < 100000; tries++)
					v = pickVariant(o, rec.count);   // the original loops until a variant
				if (v != -1) {
					rec.current = (uint8)v;
					out.stop(i);
					out.start(i, rec.object, (uint)v, false);   // loop count 1
					rec.start = 0;
				}
			}
			continue;
		}
		if ((status == AmbientSampleOutput::kDone || status == AmbientSampleOutput::kStopped) && rec.count > 0) {
			const int v = pickVariant(o, rec.count);
			if (v == -1) {
				if (rec.start == 0)
					rec.start = (uint32)chapterTime;
				out.stop(i);
				continue;
			}
			startVariant(out, i, v, true);
		}
		// the positional update (FUN_00450ed8 with the object's rotated position)
		const float pos[3] = { -o.pos[1], o.pos[0], -o.pos[2] };
		const AmbientGains g = ambientGains(pos, o.loudness, cam, ref, alpha, beta);
		out.setPan(i, g.pan);
		out.setVolume(i, g.volume);
	}
}

// FUN_00431e98
void AmbientSounds::stopAll(AmbientSampleOutput &out) {
	for (uint i = 0; i < _count; i++) {
		out.setPan(i, 0x40);
		out.setVolume(i, (uint8)i);   // the loop index reaches AIL_set_sample_volume (0x00431eed)
		out.stop(i);
		out.release(_records[i].object);
		_records[i].object = -1;
		_records[i].count = 0;
	}
	_count = 0;
	_active = false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
