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

#ifndef CRYOMNI3D_ATLANTIS_HEAD3D_H
#define CRYOMNI3D_ATLANTIS_HEAD3D_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Atlantis {

// The 3D talking head of the dialogs (the animated mouth): the F3DC files
// (.3DC mesh, *A.3DA head animation, *M.3DA mouth shapes), the .S3D tables
// and cameras, the .SYC lip-sync track, drawn over the dialog frame by the
// original's FUN_0044c9c0.  Transcribed from atlantis.exe through the
// bit-exact model atlantis-re/tools/emu/mouth_model.py; notes in
// atlantis-re/functions/video-FUN_0044c9c0.md and
// atlantis-re/formats/video-dialog-f3dc.md.

// Where the head reads its files (the BIG archives in the engine, a
// test reader in the tests).  Names are archive paths, upper case.
class HeadFileSource {
public:
	virtual ~HeadFileSource() {}
	virtual bool readFile(const Common::String &name, Common::Array<byte> &out) = 0;
};

class DialogHead {
public:
	DialogHead();

	// FUN_0042ee88 step 4: s3d, 3dc, A.3da, M.3da are the INITDIAL
	// sub-record's base names, cam the INITDIAL camera 2..5, syc the
	// voice's .SYC (WAV\<name>).  False when a file is missing.
	bool load(HeadFileSource &src, const Common::String &s3d, const Common::String &c3dc,
	          const Common::String &a3da, const Common::String &m3da, int cam,
	          const Common::String &syc);

	// FUN_0044dd10: the UBB frame index drives the head animation.
	void setFrame(int frame) { _frame = frame; }
	// FUN_0044e860: the mouth at t ms; 0, or -1 once the track has ended.
	int lipsync(int32 t);
	// FUN_0044e2e0: the neutral mouth.
	void neutral();
	// FUN_00463530 up to the projection (FUN_00463060), for every shown node.
	void transform();

	// The node drawn (the largest shown mesh) and its projection, for tests:
	// per vertex, visible (0x40 set) and the projected x, y.
	struct ProjectedVertex {
		uint32 flags;
		int32 x, y;
		float iz;   // 2^30 / z, as the rasteriser reads it
	};
	const Common::Array<ProjectedVertex> &projected() const;

	// FUN_0044c9c0 after transform(): the head drawn over a 640x480 RGB565
	// buffer (pitch in pixels); pixels outside the faces are left alone.
	void render(uint16 *dst, int pitch);

	DialogHead(const DialogHead &) = delete;
	DialogHead &operator=(const DialogHead &) = delete;
	~DialogHead();

private:
	struct Entry;
	// An edge record of the original (0x58 / 0x64 bytes, kept in the loaded
	// block between frames): one per face corner, shared by the two faces
	// of an edge.
	struct Edge {
		uint32 id;
		int va, vb, node;
		uint32 flags;
		int32 x, dx;
		float iz, diz, dizdx;
		Edge *next, *pred;
		int shade, tex;
		int32 u, v, du, dv, dudx, dvdx;            // group type 2 (16.16)
		float U, V, dU, dV, dUdx, dVdx, A58, A5c, A60;   // group type 9 (u / z, v / z)
		uint32 gtype;
		uint16 colour;                             // group type 1
		Entry *entry;
	};
	struct Entry {
		uint32 type;
		int32 xl, xr;
		Edge *first, *cur;
		Entry *behind, *next, *prev;
	};
	struct Texture {
		Common::Array<uint32> table;   // 32 shades x 256 colours, RGB565 << 16
		Common::Array<byte> image;     // 256 x 256
	};

	struct Face {
		uint32 flags;
		int vi[3];
		uint32 recs[3];   // edge-record ids (file offsets), shared by neighbours
		int32 uv[3][2];
		int plane;
		int32 d;
		int group;
	};
	struct Group {
		uint32 type;
		Common::String material;
	};
	struct Node {
		uint32 flags;
		int32 pos[3];
		int32 M[9];
		Common::Array<int32> verts;   // x, y, z per vertex
		Common::Array<int32> planes;  // nx, ny, nz per plane
		int32 radius;
		int32 center[3];
		Common::Array<Face> faces;
		Common::Array<Group> groups;
		int parent, child, sibling;  // node indices, -1 for none
		int32 W[9];
		int32 wp[3];
		Common::Array<ProjectedVertex> proj;
		Common::Array<Common::Array<uint> > faceEdges;   // per face, the three _edges indices
		bool drawn;
	};
	struct Key {
		int32 t;
		int32 v[4];
	};
	struct Track {
		int32 last;
		Common::Array<Key> rot, pos;
	};
	struct SycRecord {
		int32 t;
		int32 phoneme;
	};

	bool load3dc(const Common::Array<byte> &f);
	bool loadTracks(const Common::Array<byte> &f);
	bool loadShapes(const Common::Array<byte> &f, uint nv);
	bool loadS3d(const Common::Array<byte> &f);
	bool loadSyc(const Common::Array<byte> &f);
	void camera(int c);
	void morph(int mesh, int shape, int w);
	void headAnim();
	void reset();
	void visit(int n, const int32 *W, const int32 *wp);
	bool nodeCulled(const Node &n) const;
	void draw(Node &n);
	void setupGroup(Node &n, int group);
	void buildLists();
	void rasterise(uint16 *dst, int pitch);
	Entry *depthInsert(Entry *top, Entry *nw, int32 x);
	void buildSpans(Entry *head, Entry *tail);
	void fillSpan(uint16 *dst, int pitch, int row, int32 xs, int32 xe, const Edge &e, uint32 type);
	Entry *newEntry(uint32 type);
	bool loadTexture(HeadFileSource &src, const Common::String &name);

	Common::Array<Node> _nodes;
	Common::Array<Track> _tracks;
	Common::Array<Common::Array<int32> > _shapes;   // per shape: pivot then vertices, x, y, z
	Common::Array<int> _meshes;                      // s3d: the meshes the mouth deforms
	Common::Array<Common::Array<int> > _rows;        // s3d: per mesh, the shape of each entry
	int32 _phon[27][6];
	Common::Array<double> _cams;                     // s3d: 8 doubles per camera
	Common::Array<SycRecord> _syc;
	Common::HashMap<Common::String, uint32> _colours;   // material -> record +0x20
	Common::HashMap<Common::String, Common::String> _textures;   // material -> .3DM name

	int _frames, _frame;
	int _idx, _next, _prev, _blend;
	int32 _rootW[9], _rootP[3];
	int32 _fx, _fy, _cx, _cy, _near, _far, _kx, _ky;
	int32 _planes[4][3];
	int _drawnNode;

	Common::Array<Edge> _edges;                        // sorted by record id
	Common::HashMap<uint32, uint> _edgeIndex;
	Common::Array<Texture> _textureData;
	Common::HashMap<Common::String, int> _textureIndex;   // material -> _textureData
	// FUN_004567b4's row lists (0x00be473c start, 0x00be373c end, 0x00be573c replacements)
	Common::Array<Common::Array<Edge *> > _startRows, _endRows, _replRows;
	Common::Array<Entry *> _entryPool;
	uint _entryUsed;
	struct Span {
		int32 xs, xe;
		Edge *e;
		uint32 type;
	};
	Common::Array<Span> _spans;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
