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

#include "cryomni3d/atlantis/head3d.h"

#include "common/endian.h"
#include "common/textconsole.h"

#include <math.h>

// The arithmetic follows atlantis.exe exactly: 32-bit wrapping products,
// arithmetic right shifts, truncating divisions and conversions, and the
// x87 at 53-bit precision (control word 0x127f), which is IEEE double
// arithmetic; a float32 store of the original is a (float) cast here.

namespace CryOmni3D {
namespace Atlantis {

namespace {

inline int32 wrap32(int64 v) {
	return (int32)(uint32)(uint64)v;
}

// C integer division (toward zero).
inline int32 tdiv(int64 a, int64 b) {
	return (int32)(a / b);
}

// (v + (v < 0 ? 0x7fff : 0)) >> 15 of a wrapped product: division by 32768 toward zero.
inline int32 tsar15(int64 v) {
	return wrap32(v) / 32768;
}

inline int32 truncd(double x) {
	return (int32)x;
}

inline int32 rd32(const Common::Array<byte> &f, uint32 off) {
	return (int32)READ_LE_UINT32(&f[off]);
}

inline double rdDouble(const Common::Array<byte> &f, uint32 off) {
	uint64 bits = READ_LE_UINT64(&f[off]);
	double d;
	memcpy(&d, &bits, sizeof(d));
	return d;
}

Common::String cstr(const Common::Array<byte> &f, uint32 off, uint32 maxLen) {
	Common::String s;
	for (uint32 i = 0; i < maxLen && off + i < f.size() && f[off + i]; i++)
		s += (char)f[off + i];
	return s;
}

// FUN_0045c8a0: the trig tables (cos 0x00bea780, sin 0x00bee780, acos 0x00be6780).
struct TrigTables {
	int32 cosT[4096], sinT[4096], acosT[4096];
	TrigTables() {
		const double step = (double)0.0015339808305725455f;   // float 0x004bf064
		double a = 0.0;
		for (int i = 0; i < 4096; i++) {
			cosT[i] = truncd(cos(a) * 32768.0);
			sinT[i] = truncd(sin(a) * 32768.0);
			a = a + step;
		}
		for (int i = -2048; i < 2048; i++)
			acosT[i + 2048] = truncd(acos(i * 0.00048828125) * 2048.0 * 0.31830988618380013);
	}
};

const TrigTables &trig() {
	static TrigTables *t = nullptr;
	if (!t)
		t = new TrigTables();
	return *t;
}

// FUN_0045d460: slerp of two 1.15 quaternions, weight 0..256.
void slerp(const int32 *q0, const int32 *q1, int w, int32 *out) {
	const TrigTables &T = trig();
	const int64 dot = ((int64)q0[0] * q1[0] + (int64)q0[1] * q1[1] + (int64)q0[2] * q1[2] + (int64)q0[3] * q1[3]) >> 15;
	if (dot + 0x8000 <= 20)
		warning("Atlantis head: FUN_0045d460 opposite-quaternion branch (0x0045d5ab) not ported");
	const int e = 256 - w;
	int32 a, b;
	if (0x8000 - dot <= 20) {
		a = tdiv((int64)e << 15, 256);
		b = tdiv((int64)w << 15, 256);
	} else {
		const int idx = truncd((double)dot * 3.0517578125e-05 * 2048.0 + 2048.0);
		const int32 th = T.acosT[idx];
		const int32 s = T.sinT[th];
		a = tdiv((int64)T.sinT[tdiv((int64)e * th, 256)] << 15, s);
		b = tdiv((int64)T.sinT[tdiv((int64)w * th, 256)] << 15, s);
	}
	for (int k = 0; k < 4; k++)
		out[k] = wrap32((int64)q0[k] * a + (int64)b * q1[k]) >> 15;
}

// FUN_0045d128: quaternion to 3x3 matrix (1.15).
void quatMatrix(const int32 *q, int32 *m) {
	const int64 a = q[0], b = q[1], c = q[2], d = q[3];
	const int32 xx = wrap32(a * a), yy = wrap32(b * b), zz = wrap32(c * c);
	m[0] = 0x8000 - (wrap32((int64)yy + zz) >> 14);
	m[4] = 0x8000 - (wrap32((int64)xx + zz) >> 14);
	m[8] = 0x8000 - (wrap32((int64)xx + yy) >> 14);
	const int32 ab = wrap32(a * b), cd = wrap32(c * d);
	m[1] = wrap32((int64)ab - cd) >> 14;
	m[3] = wrap32((int64)ab + cd) >> 14;
	const int32 ac = wrap32(a * c), bd = wrap32(b * d);
	m[2] = wrap32((int64)ac + bd) >> 14;
	m[6] = wrap32((int64)ac - bd) >> 14;
	const int32 bc = wrap32(b * c), da = wrap32(d * a);
	m[5] = wrap32((int64)bc - da) >> 14;
	m[7] = wrap32((int64)bc + da) >> 14;
}

// FUN_0045d004: (lo * (256 - w) + hi * w) >> 8 per coordinate.
void lerp3(const int32 *lo, const int32 *hi, int w, int32 *out) {
	int32 r[3];
	for (int k = 0; k < 3; k++)
		r[k] = wrap32((int64)lo[k] * (256 - w) + (int64)hi[k] * w) >> 8;
	memcpy(out, r, sizeof(r));
}

// FUN_0045cda8: (A x B) >> 15.
void matmul(const int32 *A, const int32 *B, int32 *out) {
	int32 r[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[3 * i + j] = wrap32((int64)A[3 * i] * B[j] + (int64)A[3 * i + 1] * B[3 + j] + (int64)A[3 * i + 2] * B[6 + j]) >> 15;
	memcpy(out, r, sizeof(r));
}

// FUN_0045cf7c: v * 32768 / |v|, truncated; |v|^2 summed y, x, z.
void normalise(const int32 *v, int32 *out) {
	const double s = sqrt((double)((int64)v[1] * v[1]) + (double)((int64)v[0] * v[0]) + (double)((int64)v[2] * v[2]));
	const double inv = 1.0 / s;
	for (int k = 0; k < 3; k++)
		out[k] = truncd((double)wrap32((int64)v[k] << 15) * inv);
}

// FUN_0045caec(n, roll 0): the camera rotation from the normalised direction n.
void basis(const int32 *n, int32 *m) {
	const double c = (double)(1.0f / 32768.0f), K = 32768.0;
	const double x = n[0] * c, y = n[1] * c, z = n[2] * c;
	const double s = sqrt(1.0 - y * y);
	const double inv = 1.0 / s;
	m[0] = truncd(z * inv * K);
	m[1] = truncd((-x) * y * inv * K);
	m[2] = n[0];
	m[3] = 0;
	m[4] = truncd(s * K);
	m[5] = n[1];
	m[6] = truncd((-x) * inv * K);
	m[7] = truncd(K * (z * (-y) * inv));
	m[8] = n[2];
}

// An F3DC block (FUN_004187e0 / FUN_0044b77c): section offsets, file-relative.
bool sections(const Common::Array<byte> &f, Common::Array<uint32> &out) {
	if (f.size() < 0x20)
		return false;
	const uint32 n = (uint32)rd32(f, 0x1c);
	if (0x20 + 4 * (uint64)n > f.size())
		return false;
	out.resize(n);
	for (uint32 i = 0; i < n; i++)
		out[i] = 0x1c + (uint32)rd32(f, 0x20 + 4 * i);
	return true;
}

} // End of anonymous namespace

DialogHead::DialogHead() : _frames(0), _frame(0), _idx(1), _next(0), _prev(0), _blend(0),
	_fx(0), _fy(0), _cx(320), _cy(240), _near(0x80), _far(65000), _kx(0), _ky(0), _drawnNode(-1) {
	memset(_phon, 0, sizeof(_phon));
	memset(_rootW, 0, sizeof(_rootW));
	memset(_rootP, 0, sizeof(_rootP));
	memset(_planes, 0, sizeof(_planes));
}

bool DialogHead::load3dc(const Common::Array<byte> &f) {
	Common::Array<uint32> secs;
	if (!sections(f, secs) || secs.empty())
		return false;
	const uint32 R = secs[0] - 1;   // FUN_0044b2e8: relocation base section 0 - 1
	Common::HashMap<uint32, int> byOff;
	_nodes.resize(secs.size());
	Common::Array<uint32> links(secs.size() * 3);
	for (uint i = 0; i < secs.size(); i++) {
		const uint32 s = secs[i];
		Node &n = _nodes[i];
		byOff[s] = i;
		n.flags = (uint32)rd32(f, s + 0xc);
		for (int k = 0; k < 3; k++) {
			const uint32 p = (uint32)rd32(f, s + 0x10 + 4 * k);
			links[3 * i + k] = p ? R + p : 0;
		}
		for (int k = 0; k < 3; k++)
			n.pos[k] = rd32(f, s + 0x1c + 4 * k);
		for (int k = 0; k < 9; k++)
			n.M[k] = rd32(f, s + 0x28 + 4 * k);
		const uint32 nv = (uint32)rd32(f, s + 0x7c);
		const uint32 va = R + (uint32)rd32(f, s + 0x80);
		n.verts.resize(nv * 3);
		for (uint32 v = 0; v < nv; v++)
			for (int k = 0; k < 3; k++)
				n.verts[3 * v + k] = rd32(f, va + 0x28 * v + 4 + 4 * k);
		const uint32 npl = (uint32)rd32(f, s + 0x94);
		const uint32 pa = R + (uint32)rd32(f, s + 0x98);
		n.planes.resize(npl * 3);
		for (uint32 p = 0; p < npl; p++)
			for (int k = 0; k < 3; k++)
				n.planes[3 * p + k] = rd32(f, pa + 16 * p + 4 * k);
		n.radius = rd32(f, s + 0xb0);
		for (int k = 0; k < 3; k++)
			n.center[k] = rd32(f, s + 0xb4 + 4 * k);
		n.drawn = false;
		uint32 g = (uint32)rd32(f, s + 0xa4);
		g = g ? R + g : 0;
		while (g) {
			Group G;
			G.type = (uint32)rd32(f, g + 4);
			G.material = cstr(f, g + 12, 16);
			n.groups.push_back(G);
			if (G.type == 1 || G.type == 2 || G.type == 9) {
				uint32 fc = (uint32)rd32(f, g + 0x20);
				fc = fc ? R + fc : 0;
				while (fc) {
					Face F;
					F.flags = (uint32)rd32(f, fc);
					for (int k = 0; k < 3; k++) {
						F.vi[k] = (int)((R + (uint32)rd32(f, fc + 8 + 12 * k) - va) / 0x28);
						F.recs[k] = R + (uint32)rd32(f, fc + 16 + 12 * k);
						if (G.type != 1) {
							// type 1 faces are 0x34 bytes: no texture-coordinate pointers
							const uint32 uvp = R + (uint32)rd32(f, fc + 0x34 + 4 * k);
							F.uv[k][0] = rd32(f, uvp);
							F.uv[k][1] = rd32(f, uvp + 4);
						} else {
							F.uv[k][0] = F.uv[k][1] = 0;
						}
					}
					F.plane = (int)((R + (uint32)rd32(f, fc + 0x2c) - pa) / 16);
					F.d = rd32(f, fc + 0x30);
					F.group = (int)n.groups.size() - 1;
					n.faces.push_back(F);
					const uint32 nx = (uint32)rd32(f, fc + 4);
					fc = nx ? R + nx : 0;
				}
			}
			const uint32 nx = (uint32)rd32(f, g);
			g = nx ? R + nx : 0;
		}
	}
	for (uint i = 0; i < _nodes.size(); i++) {
		int *dst[3] = { &_nodes[i].parent, &_nodes[i].child, &_nodes[i].sibling };
		for (int k = 0; k < 3; k++)
			*dst[k] = byOff.contains(links[3 * i + k]) ? byOff[links[3 * i + k]] : -1;
	}
	// The material table (FUN_0044b4a4): records of 0x2c bytes after the sections.
	const uint32 mbase = 0x20 + 4 * secs.size();
	const uint32 m = (uint32)rd32(f, mbase);
	for (uint32 i = 0; i < m; i++) {
		const uint32 r = mbase + 4 + 0x2c * i;
		const Common::String name = cstr(f, r, 16);
		_textures[name] = cstr(f, r + 16, 16);
		// FUN_0044ac84 @ 0x0044ace6: a colour group's +8 is the record's +0x20
		_colours[name] = (uint32)rd32(f, r + 0x20);
	}
	return true;
}

bool DialogHead::loadTracks(const Common::Array<byte> &f) {
	Common::Array<uint32> secs;
	if (!sections(f, secs))
		return false;
	_tracks.resize(secs.size());
	for (uint i = 0; i < secs.size(); i++) {
		const uint32 s = secs[i];
		Track &t = _tracks[i];
		t.last = rd32(f, s);
		const int32 nrot = rd32(f, s + 4), npos = rd32(f, s + 8);
		const uint32 rk = 0x1c + (uint32)rd32(f, s + 12), pk = 0x1c + (uint32)rd32(f, s + 16);
		t.rot.resize(nrot);
		for (int32 k = 0; k < nrot; k++) {
			t.rot[k].t = rd32(f, rk + 0x14 * k);
			for (int c = 0; c < 4; c++)
				t.rot[k].v[c] = rd32(f, rk + 0x14 * k + 4 + 4 * c);
		}
		t.pos.resize(npos);
		for (int32 k = 0; k < npos; k++) {
			t.pos[k].t = rd32(f, pk + 0x10 * k);
			for (int c = 0; c < 3; c++)
				t.pos[k].v[c] = rd32(f, pk + 0x10 * k + 4 + 4 * c);
			t.pos[k].v[3] = 0;
		}
	}
	return true;
}

bool DialogHead::loadShapes(const Common::Array<byte> &f, uint nv) {
	Common::Array<uint32> secs;
	if (!sections(f, secs))
		return false;
	_shapes.resize(secs.size());
	for (uint i = 0; i < secs.size(); i++) {
		_shapes[i].resize((nv + 1) * 3);
		for (uint v = 0; v < (nv + 1) * 3; v++)
			_shapes[i][v] = rd32(f, secs[i] + 4 * v);
	}
	return true;
}

bool DialogHead::loadS3d(const Common::Array<byte> &f) {
	// FUN_0044d5f0 (version >= 0x96; the row pointers rebuilt)
	const int32 n1 = rd32(f, 0x10), n2 = rd32(f, 0x14);
	const uint32 mt = (uint32)rd32(f, 0x24), rt = (uint32)rd32(f, 0x28);
	_meshes.resize(n1);
	_rows.resize(n1);
	for (int32 r = 0; r < n1; r++) {
		_meshes[r] = rd32(f, mt + 4 * r);
		_rows[r].resize(n2);
		for (int32 k = 0; k < n2; k++)
			_rows[r][k] = rd32(f, rt + 4 * n1 + 4 * (r * n2 + k));
	}
	for (int i = 0; i < 27; i++)
		for (int k = 0; k < 6; k++)
			_phon[i][k] = rd32(f, 0x164 + 24 * i + 4 * k);
	_cams.clear();
	const uint32 ct = (uint32)rd32(f, 0x30);
	if (ct) {
		const int32 nc = rd32(f, 0x1c);
		for (int32 c = 0; c < nc; c++)
			for (int k = 0; k < 8; k++)
				_cams.push_back(rdDouble(f, ct + 0x54 * c + 8 * k));
	}
	return true;
}

bool DialogHead::loadSyc(const Common::Array<byte> &f) {
	// FUN_0044d390: records of 12 bytes {time ms, phoneme, energy}
	if (f.size() < 0x90)
		return false;
	const uint32 count = (uint32)rd32(f, 0x20);
	const uint32 base = 0x8c + (uint32)rd32(f, 0x8c) + 4;
	if (base + 12 * (uint64)count > f.size() || count < 2)
		return false;
	_syc.resize(count);
	for (uint32 i = 0; i < count; i++) {
		_syc[i].t = rd32(f, base + 12 * i);
		_syc[i].phoneme = rd32(f, base + 12 * i + 4);
	}
	return true;
}

bool DialogHead::load(HeadFileSource &src, const Common::String &s3d, const Common::String &c3dc,
                      const Common::String &a3da, const Common::String &m3da, int cam,
                      const Common::String &syc) {
	const Common::String P = "DIALOG\\PERSO\\";
	Common::Array<byte> f3dc, fa, fm, fs, fy;
	Common::String n3dc = P + c3dc + ".3DC", na = P + a3da + ".3DA", nm = P + m3da + ".3DA",
	               ns = P + s3d + ".S3D", ny = "WAV\\" + syc;
	n3dc.toUppercase(); na.toUppercase(); nm.toUppercase(); ns.toUppercase(); ny.toUppercase();
	if (!src.readFile(n3dc, f3dc) || !src.readFile(na, fa) || !src.readFile(nm, fm) ||
	        !src.readFile(ns, fs) || !src.readFile(ny, fy))
		return false;
	if (!load3dc(f3dc) || !loadTracks(fa) || !loadS3d(fs) || !loadSyc(fy))
		return false;
	uint nv = 0;
	for (uint i = 0; i < _meshes.size(); i++)
		if (_meshes[i] >= 0 && (uint)_meshes[i] < _nodes.size())
			nv = MAX<uint>(nv, _nodes[_meshes[i]].verts.size() / 3);
	if (!loadShapes(fm, nv))
		return false;
	if (cam < 2 || (uint)(cam - 2) * 8 + 8 > _cams.size() || _tracks.empty())
		return false;
	// FUN_00418b68: every node hidden (FUN_0044c250); FUN_00418e0c: the s3d meshes shown (FUN_0044c284)
	for (uint i = 0; i < _nodes.size(); i++)
		_nodes[i].flags |= 4;
	for (uint i = 0; i < _meshes.size(); i++)
		_nodes[_meshes[i]].flags &= ~4u;
	_frames = _tracks[0].last + 1;   // FUN_0044da90: FUN_00468534(...) + 1
	_idx = 1; _next = 0; _prev = 0; _blend = 0; _frame = 0;
	camera(cam - 2);
	neutral();
	return true;
}

// FUN_0044d7b0(slot, c)
void DialogHead::camera(int c) {
	const double *r = &_cams[8 * c];
	int32 d[3], p[3], nd[3], m[9];
	for (int k = 0; k < 3; k++) {
		d[k] = truncd(r[k] * r[6] - r[3 + k] * r[6]);
		p[k] = truncd(r[3 + k] * r[6]);
	}
	normalise(d, nd);
	basis(nd, m);
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			_rootW[3 * i + j] = m[3 * j + i];   // FUN_0045ce1c
	for (int i = 0; i < 3; i++)                 // FUN_0045ce64, FUN_0045cef4
		_rootP[i] = -(wrap32((int64)_rootW[3 * i] * p[0] + (int64)_rootW[3 * i + 1] * p[1] + (int64)_rootW[3 * i + 2] * p[2]) >> 15);
	const int32 focal = truncd(640 * 0.5 / tan(r[7] * 3.1415926535897 * 0.0027777777777777775));
	_fx = tdiv((int64)focal * 640, 640);
	const double aspect = (double)(float)((480 * 4.0) / (640 * 3.0));   // doubles 0x004bee63 = 4.0, 0x004bee6b = 3.0
	_fy = truncd(_fx * aspect);
	_cx = 320; _cy = 240; _near = 0x80; _far = 65000;
	_kx = tdiv((int64)640 << 15, -2 * (int64)_fx);
	_ky = tdiv((int64)480 << 15, -2 * (int64)_fy);
	const int32 pl[4][3] = { { -_fx, 0, -320 }, { _fx, 0, -320 }, { 0, -_fy, -240 }, { 0, _fy, -240 } };
	for (int i = 0; i < 4; i++)
		normalise(pl[i], _planes[i]);
}

// FUN_00468458
void DialogHead::morph(int mesh, int shape, int w) {
	if (w == 0)
		return;
	Common::Array<int32> &v = _nodes[mesh].verts;
	const Common::Array<int32> &sh = _shapes[shape];
	const uint nv = v.size() / 3;
	for (uint i = 0; i < nv; i++) {
		if (w == 255)
			memcpy(&v[3 * i], &sh[3 * (i + 1)], 3 * sizeof(int32));
		else
			lerp3(&v[3 * i], &sh[3 * (i + 1)], w, &v[3 * i]);
	}
}

// FUN_00468990(EAX = 3dc, EDX = EBX = A.3da + frame, ECX = 255, flags 0) -> FUN_0046853c per node
void DialogHead::headAnim() {
	if (_frame >= _frames)
		return;
	const double f = (double)(float)((double)((256 - 255) * _frame + _frame * 255) * (double)(1.0f / 256.0f));
	for (uint ni = 0; ni < _nodes.size() && ni < _tracks.size(); ni++) {
		Node &n = _nodes[ni];
		for (int kind = 0; kind < 2; kind++) {
			const Common::Array<Key> &keys = kind == 0 ? _tracks[ni].rot : _tracks[ni].pos;
			if (keys.size() <= 1)
				continue;
			uint lo = 0, hi = keys.size() - 1;
			while (lo + 1 < hi) {
				const uint mid = (lo + hi) / 2;
				if ((double)keys[mid].t < f)
					lo = mid;
				else
					hi = mid;
			}
			const Key &k1 = keys[hi];
			if ((double)k1.t > f) {
				const Key &k0 = keys[hi - 1];
				const int w = truncd((f - k0.t) * 256.0 / (double)(k1.t - k0.t));
				if (kind == 0) {
					int32 q[4];
					slerp(k0.v, k1.v, w, q);
					quatMatrix(q, n.M);
				} else {
					lerp3(k0.v, k1.v, w, n.pos);
				}
			} else {
				if (kind == 0)
					quatMatrix(k1.v, n.M);
				else
					memcpy(n.pos, k1.v, 3 * sizeof(int32));
			}
		}
	}
}

// FUN_0044dba0
void DialogHead::reset() {
	_idx = 1; _next = -1; _prev = -1; _blend = 0x7f;
}

void DialogHead::neutral() {
	const int32 *e = _phon[26];
	for (uint r = 0; r < _meshes.size(); r++) {
		const Common::Array<int> &row = _rows[r];
		morph(_meshes[r], row[e[0]], 255);
		if (e[2] >= 0 && e[3] >= 0)
			morph(_meshes[r], row[e[2]], e[3]);
		if (e[4] >= 0 && e[5] >= 0)
			morph(_meshes[r], row[e[4]], e[5]);
	}
	headAnim();
	reset();
}

int DialogHead::lipsync(int32 t) {
	const int last = (int)_syc.size() - 1;
	if (t >= _syc[last].t) {
		reset();
		return -1;
	}
	if (t != 0) {
		for (;;) {
			if (_syc[_idx - 1].t < t && t < _syc[_idx].t)
				break;
			if (t > _syc[_idx].t)
				_idx++;
			else
				break;
		}
		if (_idx > last)
			_idx = last;
	}
	// every phoneme of the shipped .SYC files is 0..0x1a (1713 files checked)
	if ((uint32)_syc[_idx].phoneme <= 0x1a)
		_next = _syc[_idx].phoneme;
	if ((uint32)_syc[_idx - 1].phoneme <= 0x1a)
		_prev = _syc[_idx - 1].phoneme;
	const int32 t0 = _syc[_idx - 1].t, t1 = _syc[_idx].t;
	const double frac = (double)(t - t0) / (double)(t1 - t0);
	_blend = CLIP<int32>(truncd(frac * 255.0), 0, 255);
	// _prev and _next are always set above for the shipped tracks; the guard only
	// keeps a malformed track from reading outside the table
	const int32 *Pp = _phon[_prev >= 0 ? _prev : 26], *Nn = _phon[_next >= 0 ? _next : 26];
	const double C = 0.00392156862745098;
	for (uint r = 0; r < _meshes.size(); r++) {
		const Common::Array<int> &row = _rows[r];
		const double w[6] = { (double)(255 - _blend), (double)(Pp[3] * (255 - _blend)) * C, (double)(Pp[5] * (255 - _blend)) * C,
		                      (double)_blend, (double)(Nn[3] * _blend) * C, (double)(Nn[5] * _blend) * C };
		const int32 s[6] = { Pp[0], Pp[2], Pp[4], Nn[0], Nn[2], Nn[4] };
		for (int k = 0; k < 6; k++) {
			if (0.0 > w[k] || s[k] < 0)
				continue;
			morph(_meshes[r], row[s[k]], truncd(w[k]));
		}
	}
	headAnim();
	if (_idx >= last || t >= _syc[last].t) {
		reset();
		return -1;
	}
	return 0;
}

// FUN_00461660: the bounding sphere against the four side planes and near / far.
bool DialogHead::nodeCulled(const Node &n) const {
	int32 c[3];
	for (int i = 0; i < 3; i++)
		c[i] = n.wp[i] + tsar15((int64)n.W[3 * i] * n.center[0] + (int64)n.W[3 * i + 1] * n.center[1] + (int64)n.W[3 * i + 2] * n.center[2]);
	bool out = false;
	for (int p = 0; p < 4; p++) {
		const int32 d = wrap32((int64)c[0] * _planes[p][0] + (int64)c[1] * _planes[p][1] + (int64)c[2] * _planes[p][2]) >> 15;
		if (d >= 1 && n.radius < d)
			out = true;
	}
	const int32 z = c[2];
	if (z <= _near && !(_near < z + n.radius))
		out = true;
	if (z >= _far && !(z - n.radius < _far))
		out = true;
	return out;
}

void DialogHead::draw(Node &n) {
	if (nodeCulled(n))
		return;
	const int32 *W = n.W, *wp = n.wp;
	const uint nv = n.verts.size() / 3;
	// FUN_004618b0
	Common::Array<int32> cam(nv * 3);
	Common::Array<uint32> vf(nv);
	for (uint v = 0; v < nv; v++) {
		const int32 *p = &n.verts[3 * v];
		int32 *c = &cam[3 * v];
		for (int i = 0; i < 3; i++)
			c[i] = tsar15((int64)W[3 * i] * p[0] + (int64)W[3 * i + 1] * p[1] + (int64)W[3 * i + 2] * p[2]) + wp[i];
		uint32 fl = 0;
		if (c[2] < _near)
			fl |= 0x10;
		else if (c[2] >= _far)
			fl |= 0x20;
		const int32 tx = wrap32((int64)_kx * c[2]) >> 15, ty = wrap32((int64)_ky * c[2]) >> 15;
		if (c[0] < tx)
			fl |= 2;
		else if (-tx < c[0])
			fl |= 1;
		if (ty < c[1]) {
			if (-ty < c[1])
				fl |= 8;
		} else {
			fl |= 4;
		}
		vf[v] = fl;
	}
	// FUN_00461b98: the eye in object space against each face plane
	int64 e[3];
	for (int j = 0; j < 3; j++)
		e[j] = (-(int64)wrap32((int64)W[j] * wp[0] + (int64)W[3 + j] * wp[1] + (int64)W[6 + j] * wp[2])) >> 15;
	const uint npl = n.planes.size() / 3;
	Common::Array<int64> pd(npl);
	for (uint p = 0; p < npl; p++) {
		const int32 *pl = &n.planes[3 * p];
		pd[p] = (int64)tsar15(e[1] * pl[1]) + tsar15(e[0] * pl[0]) + tsar15(e[2] * pl[2]);
	}
	// FUN_00462ebc
	for (uint fi = 0; fi < n.faces.size(); fi++) {
		Face &fc = n.faces[fi];
		uint32 fl = fc.flags;
		bool hidden;
		if (!(fl & 8)) {
			hidden = !(pd[fc.plane] - fc.d >= 0);
		} else {
			const int32 *a = &cam[3 * fc.vi[0]], *b = &cam[3 * fc.vi[1]], *c3 = &cam[3 * fc.vi[2]];
			int32 d1[3], d2[3];
			for (int k = 0; k < 3; k++) {
				d1[k] = a[k] - b[k];
				d2[k] = b[k] - c3[k];
			}
			const int32 cr = wrap32((int64)wrap32((int64)d1[0] * d2[1] - (int64)d1[1] * d2[0]) * a[2] +
			                        (int64)wrap32((int64)d1[2] * d2[0] - (int64)d1[0] * d2[2]) * a[1] +
			                        (int64)wrap32((int64)d1[1] * d2[2] - (int64)d1[2] * d2[1]) * a[0]);
			hidden = !(cr < 0);
		}
		if (hidden) {
			fl |= 1;
		} else {
			fl &= 8;
			const uint32 f0 = vf[fc.vi[0]], f1 = vf[fc.vi[1]], f2 = vf[fc.vi[2]];
			if ((f0 & f1 & f2 & 0x3f) || ((f0 | f1 | f2) & 0x20)) {
				fl |= 1;
			} else if ((f0 | f1 | f2) & 0x10) {
				// FUN_00461ecc (near-plane clipping) is not ported: no shipped head
				// and camera reaches it (atlantis-re experiments/2026-10-14-04).
				warning("Atlantis head: near-plane clipping FUN_00461ecc not ported; face skipped");
				fl |= 1;
			} else {
				for (int k = 0; k < 3; k++)
					vf[fc.vi[k]] |= 0x40;
			}
		}
		fc.flags = fl;
	}
	// FUN_00463060
	n.proj.resize(nv);
	for (uint v = 0; v < nv; v++) {
		ProjectedVertex &pv = n.proj[v];
		const int32 *c = &cam[3 * v];
		pv.flags = vf[v];
		if (vf[v] & 0x40) {
			pv.x = tdiv((int64)c[0] * _fx, c[2]) + _cx;
			pv.y = tdiv((int64)c[1] * _fy, c[2]) + _cy;
			pv.iz = (float)((double)1073741824.0f / (double)(float)c[2]);
		} else {
			pv.x = pv.y = 0;
			pv.iz = 0.0f;
		}
	}
	n.drawn = true;
}

void DialogHead::visit(int ni, const int32 *W, const int32 *wp) {
	Node &n = _nodes[ni];
	int32 W2[9], wp2[3];
	matmul(W, n.M, W2);
	for (int i = 0; i < 3; i++)
		wp2[i] = wp[i] + tdiv(wrap32((int64)W[3 * i] * n.pos[0] + (int64)W[3 * i + 1] * n.pos[1] + (int64)W[3 * i + 2] * n.pos[2]), 32768);
	memcpy(n.W, W2, sizeof(W2));
	memcpy(n.wp, wp2, sizeof(wp2));
	if (!(n.flags & 4))
		draw(n);
	for (int c = n.child; c >= 0; c = _nodes[c].sibling)
		if (!(_nodes[c].flags & 1))
			visit(c, W2, wp2);
}

void DialogHead::transform() {
	for (uint i = 0; i < _nodes.size(); i++)
		_nodes[i].drawn = false;
	if (_nodes.empty())
		return;
	visit(0, _rootW, _rootP);
	_drawnNode = -1;
	for (uint i = 0; i < _nodes.size(); i++)
		if (_nodes[i].drawn && (_drawnNode < 0 || _nodes[i].verts.size() > _nodes[_drawnNode].verts.size()))
			_drawnNode = i;
}

const Common::Array<DialogHead::ProjectedVertex> &DialogHead::projected() const {
	static const Common::Array<ProjectedVertex> empty;
	return _drawnNode >= 0 ? _nodes[_drawnNode].proj : empty;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
