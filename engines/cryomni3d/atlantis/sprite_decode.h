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

#ifndef CRYOMNI3D_ATLANTIS_SPRITE_DECODE_H
#define CRYOMNI3D_ATLANTIS_SPRITE_DECODE_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {
struct PixelFormat;
struct Surface;
}

namespace CryOmni3D {
namespace Atlantis {

// A 2D sprite (SPR) decoded into RGB565 pixels plus a parallel per-pixel blend
// array: kSprNoBlend = opaque, kSprTranspBlend = transparent, otherwise a
// 5-bit edge coverage factor (sprite_blend.h).
struct SprSprite {
	uint16 w, h;
	int16  xoff, yoff;
	Common::Array<uint16> pixels;
	Common::Array<uint8>  blend;

	SprSprite() : w(0), h(0), xoff(0), yoff(0) {}
};

// Decode one sprite's RLE rows (atlantis.exe FUN_0041047f) into spr.pixels
// and spr.blend.  spr.w/h must already be set and the stream positioned at
// the per-row offset table.  colorTable maps the 8-bit indices to RGB565.
void decodeSprRows(Common::SeekableReadStream &stream, SprSprite &spr, const uint16 *colorTable);

// Read a whole SPR file: flags (bit 2: a 256-entry RGB555 colour table,
// bit 3: 256 more entries, skipped), sprite count, offsets, then per sprite
// w, h, xoff, yoff and its rows.  A sprite of 0 or more than maxW x maxH
// keeps its header and no pixels.  Returns false for a count of 0 or more
// than 256.
bool readSprFile(Common::SeekableReadStream &stream, Common::Array<SprSprite> &out,
                 uint16 maxW = 640, uint16 maxH = 480);

// Read a game TGA: type 2, 15-bit, rows bottom-up, converted to fmt (as
// FUN_0041b654 / FUN_00449600 / FUN_004174a0 load it).  Returns nullptr for
// another format.  The caller frees the surface.
Graphics::Surface *readTga15(Common::SeekableReadStream &stream, const Graphics::PixelFormat &fmt);

// Draw spr with its hotspot at (x, y) onto an RGB565 surface, clipped to the
// surface, as FUN_00410430 / FUN_0041047f draw it.
void drawSprSprite(Graphics::Surface &dst, const SprSprite &spr, int x, int y);

// SPW / SPF frames (FUN_00452bde).
uint32 readSPWFrameOffset(Common::SeekableReadStream *s, uint frameIdx = 0);
void decodeSPWPixels(Common::SeekableReadStream *spw, Graphics::Surface &surf, uint frameIdx = 0);

// Composite frame frameIdx of an SPF onto dst.  Like FUN_004256e8 (test at
// 0x004257b6), nothing is drawn once frameIdx reaches the frame count.
void compositeSpfFrame(Common::SeekableReadStream *spf, uint frameIdx, Graphics::Surface &dst);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
