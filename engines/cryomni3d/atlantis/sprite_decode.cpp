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

#include "common/stream.h"

#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/atlantis/sprite_blend.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

void decodeSprRows(Common::SeekableReadStream &stream, SprSprite &spr, const uint16 *colorTable) {
	// Per-row offset table: h x uint32, relative to the pixel-data base.
	Common::Array<uint32> rowOff(spr.h);
	for (uint16 r = 0; r < spr.h; r++)
		rowOff[r] = stream.readUint32LE();
	const int64 pixBase = stream.pos();

	spr.pixels.resize((uint)spr.w * spr.h);
	spr.blend.resize((uint)spr.w * spr.h);
	for (uint i = 0; i < spr.pixels.size(); i++) {
		spr.pixels[i] = 0;
		spr.blend[i]  = kSprTranspBlend;
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
					spr.pixels[ry * spr.w + rx] = colorTable[stream.readByte()];
					spr.blend[ry * spr.w + rx]  = kSprNoBlend;
				}
			} else {
				// Blend run: cnt (factor, index) pairs.  The colour-table
				// entry is already premultiplied by factor/32 in the data;
				// keep the factor so drawing adds the framebuffer share.
				for (int i = 0; i < cnt && rx < spr.w; i++, rx++) {
					byte factor = stream.readByte() & 0x1f;
					spr.pixels[ry * spr.w + rx] = colorTable[stream.readByte()];
					spr.blend[ry * spr.w + rx]  = factor;
				}
			}
		}
	}
}

// SPW file layout (verified from atlantis.exe):
//   [0x00] uint32 file_size
//   [0x04] uint32 num_frames
//   [0x08] uint32 format            (9 = 16-bit palette, 8 = 15-bit)
//   [0x14] uint32 frame_offsets[]   (low 2 bits hold flags; mask with ~3)
//   per frame: x1/y_bot/x2/y_top bbox, format, pixel_stream_off, RGB565
//   palette, then the flat-buffer RLE pixel stream.

uint32 readSPWFrameOffset(Common::SeekableReadStream *s, uint frameIdx) {
	if (!s->seek(0x14 + frameIdx * 4))
		return 0;
	return s->readUint32LE() & 0xFFFFFFFCU;
}


static inline uint32 rgb565ToSurfaceColor(uint16 v, const Graphics::PixelFormat &fmt) {
	uint8 r = ((v >> 11) & 0x1F) * 255 / 31;
	uint8 g = ((v >>  5) & 0x3F) * 255 / 63;
	uint8 b = (v & 0x1F) * 255 / 31;
	return fmt.RGBToColor(r, g, b);
}

static inline void putSurfacePixel(Graphics::Surface &surf, int x, int y, uint32 color) {
	byte *dst = (byte *)surf.getBasePtr(x, y);
	switch (surf.format.bytesPerPixel) {
	case 2: *(uint16 *)dst = (uint16)color; break;
	case 4: *(uint32 *)dst = color;         break;
	default: break;
	}
}

// Decode an SPW frame and composite it onto surf.  The pixel stream uses a
// flat-buffer model matching the original engine: the output pointer starts at
// flat offset 0 of the framebuffer and ROW_SEP opcodes advance it by an
// arbitrary pixel count that may cross row boundaries; pixel coordinates derive
// from the flat offset divided by the surface width.
void decodeSPWPixels(Common::SeekableReadStream *spw,
                     Graphics::Surface &surf, uint frameIdx) {
	uint32 frameOff = readSPWFrameOffset(spw, frameIdx);
	if (!frameOff)
		return;

	if (!spw->seek(frameOff + 0x14))
		return;
	uint32 pixOff = spw->readUint32LE();
	if (spw->err() || pixOff < 0x18)
		return;

	// Palette: frame[0x18 .. frame+pixOff-1], 2 bytes per RGB565 entry.
	uint32 palCount = (pixOff - 0x18) / 2;
	if (palCount > 256)
		palCount = 256;
	uint16 palette[256] = {};
	if (!spw->seek(frameOff + 0x18))
		return;
	for (uint32 i = 0; i < palCount; ++i)
		palette[i] = spw->readUint16LE();
	if (spw->err())
		return;

	if (!spw->seek(frameOff + pixOff))
		return;

	const Graphics::PixelFormat &fmt = surf.format;
	const int32 surfW = surf.w;
	const int64 surfArea = (int64)surfW * surf.h;
	int64 curFlat = 0;

	while (!spw->eos()) {
		uint16 ctrl = spw->readUint16LE();
		if (ctrl == 0x8000)
			break;  // END
		if (ctrl >= 0xC000) {
			// ROW_SEP long form: advance = (ctrl & 0x3FFF)*65536 + extra word.
			uint16 extra = spw->readUint16LE();
			curFlat += (int64)(ctrl & 0x3FFF) * 65536 + extra;
			if (curFlat >= surfArea)
				break;
		} else if (ctrl >= 0x8000) {
			// ROW_SEP short form: advance = ctrl & 0x7FFF pixels.
			curFlat += (ctrl & 0x7FFF);
			if (curFlat >= surfArea)
				break;
		} else if (ctrl >= 0x4000) {
			// BLEND: (ctrl & 0x3FFF) pixels; 2 bytes each (index, factor).
			int count = ctrl & 0x3FFF;
			for (int i = 0; i < count && !spw->eos(); ++i) {
				uint8 idx    = spw->readByte();
				uint8 factor = spw->readByte();
				if (curFlat >= 0 && curFlat < surfArea) {
					int cx = (int)(curFlat % surfW);
					int cy = (int)(curFlat / surfW);
					uint16 src = (idx < palCount) ? palette[idx] : 0;
					uint16 dst = ((const uint16 *)surf.getBasePtr(cx, cy))[0];
					uint16 out = blendSpwPixel565(src, dst, factor);
					putSurfacePixel(surf, cx, cy, rgb565ToSurfaceColor(out, fmt));
				}
				++curFlat;
			}
		} else if (ctrl > 0) {
			// DRAW: ctrl pixels; 1 byte each (palette index).
			int count = ctrl;
			for (int i = 0; i < count && !spw->eos(); ++i) {
				uint8 idx = spw->readByte();
				if (curFlat >= 0 && curFlat < surfArea) {
					int cx = (int)(curFlat % surfW);
					int cy = (int)(curFlat / surfW);
					uint16 c = (idx < palCount) ? palette[idx] : 0;
					putSurfacePixel(surf, cx, cy, rgb565ToSurfaceColor(c, fmt));
				}
				++curFlat;
			}
		}
		// ctrl == 0: no-op
	}
}

bool readSprFile(Common::SeekableReadStream &stream, Common::Array<SprSprite> &out,
                 uint16 maxW, uint16 maxH) {
	out.clear();
	const uint16 flags = stream.readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		// RGB555 to RGB565 as FUN_00410782 does: r << 11 | g << 6 | b.
		for (int i = 0; i < 256; i++) {
			const uint16 rgb555 = stream.readUint16LE();
			const uint16 r = (rgb555 >> 10) & 0x1f;
			const uint16 g = (rgb555 >> 5) & 0x1f;
			const uint16 b = rgb555 & 0x1f;
			colorTable[i] = (uint16)((r << 11) | (g << 6) | b);
		}
	}
	if (flags & 0x8)
		stream.skip(256 * 2);

	const uint16 count = stream.readUint16LE();
	if (count == 0 || count > 256)
		return false;

	// Offsets in the sprite table are relative to this position.
	const int64 base = stream.pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream.readUint32LE();

	out.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream.seek(base + offsets[si]))
			continue;
		SprSprite &spr = out[si];
		spr.w    = stream.readUint16LE();
		spr.h    = stream.readUint16LE();
		spr.xoff = stream.readSint16LE();
		spr.yoff = stream.readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > maxW || spr.h > maxH)
			continue;
		decodeSprRows(stream, spr, colorTable);
	}
	return true;
}

Graphics::Surface *readTga15(Common::SeekableReadStream &stream, const Graphics::PixelFormat &fmt) {
	// TGA header (18 bytes): all game TGAs are type 2 (uncompressed
	// true-colour), 640x480, 15-bit depth.
	const byte idLen = stream.readByte();
	/* cmType = */ stream.readByte();
	const byte imgType = stream.readByte();
	stream.skip(9);                     // colour-map spec (5) + x/y origin (4)
	const uint16 w = stream.readUint16LE();
	const uint16 h = stream.readUint16LE();
	const byte depth = stream.readByte();
	stream.readByte();                  // image descriptor
	stream.skip(idLen);                 // optional ID field
	if (imgType != 2 || depth != 15 || w == 0 || h == 0)
		return nullptr;

	Graphics::Surface *surf = new Graphics::Surface();
	surf->create(w, h, fmt);
	for (uint16 y = 0; y < h; y++) {
		// TGA origin is bottom-left (image descriptor bit 5 = 0).
		byte *row = (byte *)surf->getBasePtr(0, h - 1 - y);
		for (uint16 x = 0; x < w; x++) {
			const uint16 px = stream.readUint16LE();
			const uint8 r = ((px >> 10) & 0x1f) << 3;
			const uint8 g = ((px >> 5) & 0x1f) << 3;
			const uint8 b = (px & 0x1f) << 3;
			const uint32 c = fmt.RGBToColor(r, g, b);
			if (fmt.bytesPerPixel == 2)
				((uint16 *)row)[x] = (uint16)c;
			else if (fmt.bytesPerPixel == 4)
				((uint32 *)row)[x] = c;
		}
	}
	return surf;
}

void drawSprSprite(Graphics::Surface &dst, const SprSprite &spr, int x, int y) {
	if (spr.pixels.empty())
		return;
	const int baseX = x - spr.xoff;
	const int baseY = y - spr.yoff;
	for (int row = 0; row < (int)spr.h; row++) {
		const int dy = baseY + row;
		if (dy < 0 || dy >= (int)dst.h)
			continue;
		for (int col = 0; col < (int)spr.w; col++) {
			const int dx = baseX + col;
			if (dx < 0 || dx >= (int)dst.w)
				continue;
			const uint sidx = row * spr.w + col;
			if (spr.blend[sidx] == kSprTranspBlend)
				continue;
			uint16 pix = spr.pixels[sidx];
			uint16 *dp = (uint16 *)dst.getBasePtr(dx, dy);
			if (spr.blend[sidx] != kSprNoBlend)
				pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
			*dp = pix;
		}
	}
}

void compositeSpfFrame(Common::SeekableReadStream *spf, uint frameIdx, Graphics::Surface &dst) {
	if (!spf) return;

	// Header[0x04] = num_frames.
	if (!spf->seek(4)) return;
	uint32 numFrames = spf->readUint32LE();
	if (frameIdx >= numFrames) return;

	decodeSPWPixels(spf, dst, frameIdx);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
