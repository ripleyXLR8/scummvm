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

#include "cryomni3d/atlantis/puzzle_assets.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

#include "common/debug.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

namespace CryOmni3D {
namespace Atlantis {

// Sentinel pixel value standing in for "transparent" inside a decoded frame
// (a real colour-table entry of 0x0001 is remapped to 0x0000 on load so this
// stays unambiguous).
static const uint16 kPuzzleSprTransp = 0x0001;

bool loadPuzzleSpr(CryOmni3DEngine_Atlantis *engine, const char *fname,
                   Common::Array<PuzzleSpr> &out) {
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    engine->openBigFileStream(kFileTypePuzzles, fname));
	if (!stream) {
		warning("loadPuzzleSpr: cannot open PUZZLES\\%s", fname);
		return false;
	}

	uint16 flags = stream->readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >>  5) & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	}
	if (flags & 0x8)
		stream->skip(512);

	uint16 count = stream->readUint16LE();
	if (count == 0 || count > 1024) return false;

	const int64 sprDataBase = stream->pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream->readUint32LE();

	out.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream->seek(sprDataBase + offsets[si])) continue;
		PuzzleSpr &spr = out[si];
		spr.w    = stream->readUint16LE();
		spr.h    = stream->readUint16LE();
		spr.xoff = stream->readSint16LE();
		spr.yoff = stream->readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 640 || spr.h > 480) continue;

		Common::Array<uint32> rowOff(spr.h);
		for (uint16 r = 0; r < spr.h; r++)
			rowOff[r] = stream->readUint32LE();
		const int64 pixBase = stream->pos();

		spr.pixels.resize((uint)spr.w * spr.h, kPuzzleSprTransp);
		spr.blend.resize((uint)spr.w * spr.h, (uint8)kSprNoBlend);

		for (uint16 ry = 0; ry < spr.h; ry++) {
			stream->seek(pixBase + rowOff[ry]);
			int rx = 0;
			while (rx < (int)spr.w && !stream->eos()) {
				byte b = stream->readByte();
				int btype = (b >> 6) & 0x3;
				int cnt   = b & 0x3F;
				if (btype == 0) {
					rx += cnt;
				} else if (btype == 1) {
					for (int i = 0; i < cnt && rx < (int)spr.w; i++, rx++) {
						uint16 col = colorTable[stream->readByte()];
						if (col == kPuzzleSprTransp) col = 0x0000;
						spr.pixels[ry * spr.w + rx] = col;
					}
				} else {
					for (int i = 0; i < cnt && rx < (int)spr.w; i++, rx++) {
						byte factor = stream->readByte() & 0x1f;
						uint16 col  = colorTable[stream->readByte()];
						if (col == kPuzzleSprTransp) col = 0x0000;
						spr.pixels[ry * spr.w + rx] = col;
						spr.blend[ry * spr.w + rx]  = factor;
					}
				}
			}
		}
	}
	debugC(1, kDebugScript, "loadPuzzleSpr: loaded %u frames from PUZZLES\\%s", count, fname);
	return true;
}

void blitFrame(Graphics::ManagedSurface &out, const PuzzleSpr &spr, int cx, int cy) {
	if (spr.pixels.empty() || out.format.bytesPerPixel != 2) return;
	int dx0 = cx - spr.xoff;
	int dy0 = cy - spr.yoff;
	for (int y = 0; y < spr.h; y++) {
		int dy = dy0 + y;
		if (dy < 0 || dy >= out.h) continue;
		for (int x = 0; x < spr.w; x++) {
			int dx = dx0 + x;
			if (dx < 0 || dx >= out.w) continue;
			uint16 src = spr.pixels[y * spr.w + x];
			if (src == kPuzzleSprTransp) continue;
			uint8 factor = spr.blend[y * spr.w + x];
			uint16 *p = (uint16 *)out.getBasePtr(dx, dy);
			*p = (factor == (uint8)kSprNoBlend) ? src
			                                     : blendSprPixel565(src, *p, factor);
		}
	}
}

bool loadPuzzleTGA(CryOmni3DEngine_Atlantis *engine, const char *fname,
                   Common::Array<uint16> &outPixels) {
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    engine->openBigFileStream(kFileTypePuzzles, fname));
	if (!stream) return false;
	byte idLen = stream->readByte();
	/* cmType = */ stream->readByte();
	byte imgType = stream->readByte();
	stream->skip(9);
	uint16 w = stream->readUint16LE();
	uint16 h = stream->readUint16LE();
	byte depth = stream->readByte();
	stream->readByte();
	stream->skip(idLen);
	// Puzzle backgrounds are full-screen 15-bit uncompressed TGAs.
	if (imgType != 2 || depth != 15 || w != 640 || h != 480) return false;
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	outPixels.resize((uint)w * h);
	for (uint16 y = 0; y < h; y++) {
		uint16 *row = outPixels.begin() + (uint)(h - 1 - y) * w;
		for (uint16 x = 0; x < w; x++) {
			uint16 px = stream->readUint16LE();
			uint8 r = (uint8)(((px >> 10) & 0x1f) << 3);
			uint8 g = (uint8)(((px >>  5) & 0x1f) << 3);
			uint8 b = (uint8)(( px        & 0x1f) << 3);
			row[x] = (uint16)fmt.RGBToColor(r, g, b);
		}
	}
	return true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
