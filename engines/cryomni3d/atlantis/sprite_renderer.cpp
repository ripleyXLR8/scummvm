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

// 2D sprite (SPR) and TGA decoding.  This grows over several commits: it
// starts with the shared SPR RLE decoder, the subject-menu icon loader and the
// full-screen TGA loader; the NPC/prop scene sprites, cursors, inventory icons,
// menu sprites and bitmap fonts layer on as their consumers land.
//
// SPR file layout (SPRITE\2D\*.SPR, verified from atlantis.exe):
//   [0x00] uint16 flags        (bit 2 = 256-entry RGB555 colour table follows;
//                               bit 3 = a second 256-entry table follows)
//   [....] uint16 colorTable[256]   (RGB555, present when bit 2 set)
//   [....] uint16 count        (number of sprites)
//   [....] uint32 offsets[count]    (relative to this position)
//   per sprite: uint16 w, h; int16 xoff, yoff; then the RLE rows.
//
// Per-row RLE (FUN_0041047f): each control byte's top 2 bits select the run
// type, low 6 bits the run length: 0 = transparent skip, 1 = `cnt` colour
// indices, 2 = `cnt` (factor, index) edge-blend pairs (the colour is already
// premultiplied by factor/32 in the data; the blitter adds the framebuffer's
// (32-factor)/32 share — see blendSprPixel565 in sprite_blend.h).

#include "common/debug.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

void CryOmni3DEngine_Atlantis::decodeSprite(Common::SeekableReadStream &stream,
        SubjSprite &spr, const uint16 *colorTable) {
	// Per-row offset table: h x uint32, relative to the pixel-data base.
	Common::Array<uint32> rowOff(spr.h);
	for (uint16 r = 0; r < spr.h; r++)
		rowOff[r] = stream.readUint32LE();
	const int64 pixBase = stream.pos();

	spr.pixels.resize((uint)spr.w * spr.h);
	spr.blend.resize((uint)spr.w * spr.h);
	for (uint i = 0; i < spr.pixels.size(); i++) {
		spr.pixels[i] = kSprTransp;
		spr.blend[i]  = kSprNoBlend;
	}

	for (uint16 ry = 0; ry < spr.h; ry++) {
		stream.seek(pixBase + rowOff[ry]);
		int rx = 0;
		while (rx < spr.w && !stream.eos()) {
			byte b = stream.readByte();
			int btype = (b >> 6) & 0x3;
			int cnt   = b & 0x3F;
			if (btype == 0) {
				rx += cnt;  // transparent run
			} else if (btype == 1) {
				// Opaque run: cnt colour-index bytes.
				for (int i = 0; i < cnt && rx < spr.w; i++, rx++) {
					uint16 col = colorTable[stream.readByte()];
					if (col == kSprTransp) col = 0x0000;
					spr.pixels[ry * spr.w + rx] = col;
				}
			} else {
				// Blend run: cnt (factor, index) pairs.  The colour-table
				// entry is already premultiplied by factor/32 in the data;
				// keep the factor so drawing adds the framebuffer share.
				for (int i = 0; i < cnt && rx < spr.w; i++, rx++) {
					byte factor = stream.readByte() & 0x1f;
					uint16 col  = colorTable[stream.readByte()];
					if (col == kSprTransp) col = 0x0000;
					spr.pixels[ry * spr.w + rx] = col;
					spr.blend[ry * spr.w + rx]  = factor;
				}
			}
		}
	}
}

void CryOmni3DEngine_Atlantis::loadSubjectSprites() {
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    openBigFileStream(kFileTypeSprite2D, "SUJETS.SPR"));
	if (!stream) {
		warning("Cannot open SPRITE\\2D\\SUJETS.SPR");
		return;
	}

	// flags word: bit 2 = has 256-entry colour table stored as RGB555.
	uint16 flags = stream->readUint16LE();

	uint16 colorTable[256];
	if (flags & 0x4) {
		// Convert the RGB555 table to RGB565.  (Can't use the screen format
		// here — green is 5 bits in RGB555, 6 in RGB565, so shift g left 6.)
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5)  & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	} else {
		memset(colorTable, 0, sizeof(colorTable));
	}
	if (flags & 0x8)
		stream->skip(256 * 2);   // 512-entry table: skip the extra 256 entries

	uint16 count = stream->readUint16LE();
	if (count == 0 || count > 256) {
		warning("loadSubjectSprites: unexpected count %u", count);
		return;
	}

	// Offsets in the sprite table are relative to this position.
	const int64 sprDataBase = stream->pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream->readUint32LE();

	_subjSprites.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream->seek(sprDataBase + offsets[si])) {
			warning("loadSubjectSprites: seek failed for sprite %u", si);
			continue;
		}
		SubjSprite &spr = _subjSprites[si];
		spr.w    = stream->readUint16LE();
		spr.h    = stream->readUint16LE();
		spr.xoff = stream->readSint16LE();
		spr.yoff = stream->readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 640 || spr.h > 480)
			continue;
		decodeSprite(*stream, spr, colorTable);
	}

	debugC(1, kDebugScript, "loadSubjectSprites: loaded %u sprites from SUJETS.SPR", count);
}

Graphics::Surface *CryOmni3DEngine_Atlantis::loadTGA(FileType ft, const char *name) {
	Common::ScopedPtr<Common::SeekableReadStream> stream(openBigFileStream(ft, name));
	if (!stream) {
		warning("loadTGA: cannot open %s", name);
		return nullptr;
	}

	// TGA header (18 bytes): all game TGAs are type 2 (uncompressed true-colour),
	// 640x480, 15-bit depth.
	byte  idLen   = stream->readByte();
	/* cmType  = */ stream->readByte();
	byte  imgType = stream->readByte();
	stream->skip(9);                    // colour-map spec (5) + x/y origin (4)
	uint16 w      = stream->readUint16LE();
	uint16 h      = stream->readUint16LE();
	byte   depth  = stream->readByte();
	stream->readByte();                 // image descriptor
	stream->skip(idLen);                // optional ID field

	if (imgType != 2 || depth != 15 || w == 0 || h == 0) {
		warning("loadTGA: unexpected format in %s (type=%u depth=%u %ux%u)",
		        name, imgType, depth, w, h);
		return nullptr;
	}

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::Surface *surf = new Graphics::Surface();
	surf->create(w, h, fmt);

	for (uint16 y = 0; y < h; y++) {
		// TGA origin is bottom-left by default (image_descriptor bit 5 = 0).
		uint16 *row = (uint16 *)surf->getBasePtr(0, h - 1 - y);
		for (uint16 x = 0; x < w; x++) {
			uint16 px = stream->readUint16LE();
			uint8 r = ((px >> 10) & 0x1f) << 3;
			uint8 g = ((px >>  5) & 0x1f) << 3;
			uint8 b = ( px        & 0x1f) << 3;
			row[x] = (uint16)fmt.RGBToColor(r, g, b);
		}
	}

	return surf;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
