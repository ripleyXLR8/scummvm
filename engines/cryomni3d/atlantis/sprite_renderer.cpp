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

#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// --- SPW (panoramic NPC/prop sprite) decode --------------------------------
//
// SPW file layout (verified from atlantis.exe):
//   [0x00] uint32 file_size
//   [0x04] uint32 num_frames
//   [0x08] uint32 format            (9 = 16-bit palette, 8 = 15-bit)
//   [0x14] uint32 frame_offsets[]   (low 2 bits hold flags; mask with ~3)
//   per frame: x1/y_bot/x2/y_top bbox, format, pixel_stream_off, RGB565
//   palette, then the flat-buffer RLE pixel stream.

static uint32 readSPWFrameOffset(Common::SeekableReadStream *s, uint frameIdx = 0) {
	if (!s->seek(0x14 + frameIdx * 4))
		return 0;
	return s->readUint32LE() & 0xFFFFFFFCU;
}

static bool readSPWBounds(Common::SeekableReadStream *s,
                          int16 &x1, int16 &y1, int16 &x2, int16 &y2,
                          uint frameIdx = 0) {
	uint32 frameOff = readSPWFrameOffset(s, frameIdx);
	if (!frameOff || s->err())
		return false;
	if (!s->seek(frameOff))
		return false;
	x1 = (int16)s->readUint16LE();  // frame[0x00]: x_left
	s->readUint16LE();              // frame[0x02]: skip
	y2 = (int16)s->readUint32LE();  // frame[0x04]: y_bottom
	x2 = (int16)s->readUint32LE();  // frame[0x08]: x_right
	y1 = (int16)s->readUint16LE();  // frame[0x0C]: y_top
	return !s->err();
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
static void decodeSPWPixels(Common::SeekableReadStream *spw,
                            Graphics::Surface &surf, uint frameIdx = 0) {
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

void CryOmni3DEngine_Atlantis::loadFontSpriteSet(const char *sprName,
        Common::Array<SubjSprite> &out) {
	out.clear();
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    openBigFileStream(kFileTypeSprite2D, sprName));
	if (!stream) {
		warning("loadFontSpriteSet: cannot open SPRITE\\2D\\%s", sprName);
		return;
	}

	uint16 flags = stream->readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5)  & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	}
	if (flags & 0x8)
		stream->skip(256 * 2);

	uint16 count = stream->readUint16LE();
	if (count == 0 || count > 256) {
		warning("loadFontSpriteSet(%s): unexpected count %u", sprName, count);
		return;
	}

	const int64 sprDataBase = stream->pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream->readUint32LE();

	out.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream->seek(sprDataBase + offsets[si])) {
			warning("loadFontSpriteSet(%s): seek failed for sprite %u", sprName, si);
			continue;
		}
		SubjSprite &spr = out[si];
		spr.w    = stream->readUint16LE();
		spr.h    = stream->readUint16LE();
		spr.xoff = stream->readSint16LE();
		spr.yoff = stream->readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 640 || spr.h > 480)
			continue;
		decodeSprite(*stream, spr, colorTable);
	}
}

void CryOmni3DEngine_Atlantis::loadFontMaxSprites() {
	// FONTMAX = the gold menu font (FONTMAX2 = hover); FONTMIN = the smaller
	// red player-name font (FONTMIN2 = hover).
	loadFontSpriteSet("FONTMAX.SPR",  _fontMaxSprites);
	loadFontSpriteSet("FONTMAX2.SPR", _fontMaxHoverSprites);
	loadFontSpriteSet("FONTMIN.SPR",  _fontMinSprites);
	loadFontSpriteSet("FONTMIN2.SPR", _fontMin2Sprites);
	debugC(1, kDebugScript, "loadFontMaxSprites: %u FONTMAX + %u FONTMIN glyphs",
	      _fontMaxSprites.size(), _fontMinSprites.size());
}

void CryOmni3DEngine_Atlantis::loadCreditFontSprites() {
	// Two glyph banks for the credits roll (same SPR format as FONTMAX).
	loadFontSpriteSet("CREDBLAN.SPR", _creditBlanSprites);
	loadFontSpriteSet("CREDBLEU.SPR", _creditBleuSprites);
	debugC(1, kDebugScript, "loadCreditFontSprites: %u blanc + %u bleu glyphs",
	      _creditBlanSprites.size(), _creditBleuSprites.size());
}

void CryOmni3DEngine_Atlantis::loadMenuSprites() {
	// SPRMENU.SPR: sparse golden-filigree overlays for the toolbar and menus.
	loadFontSpriteSet("SPRMENU.SPR", _menuSprites);
	debugC(1, kDebugScript, "loadMenuSprites: %u sprites from SPRMENU.SPR", _menuSprites.size());
}

void CryOmni3DEngine_Atlantis::blitMenuSprite(Graphics::ManagedSurface &dst, uint idx,
        int hx, int hy) const {
	if (idx >= _menuSprites.size() || _menuSprites[idx].pixels.empty())
		return;
	const SubjSprite &s = _menuSprites[idx];
	int baseX = hx - s.xoff;
	int baseY = hy - s.yoff;
	for (int sy = 0; sy < (int)s.h; sy++) {
		int dy = baseY + sy;
		if (dy < 0 || dy >= dst.h)
			continue;
		for (int sx = 0; sx < (int)s.w; sx++) {
			int dx = baseX + sx;
			if (dx < 0 || dx >= dst.w)
				continue;
			uint sidx = sy * s.w + sx;
			uint16 pix = s.pixels[sidx];
			if (pix == kSprTransp)
				continue;
			uint16 *dp = (uint16 *)dst.getBasePtr(dx, dy);
			if (s.blend[sidx] != kSprNoBlend)
				pix = blendSprPixel565(pix, *dp, s.blend[sidx]);
			*dp = pix;
		}
	}
}

void CryOmni3DEngine_Atlantis::loadInventoryBarSprite() {
	// OBJETS0.SPR holds the single ~599x82 inventory-bar background (golden
	// line + 9 slot ornaments).  Same SPR format as the other 2D sprites.
	_inventoryBarSprite = SubjSprite();
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    openBigFileStream(kFileTypeSprite2D, "OBJETS0.SPR"));
	if (!stream) {
		warning("loadInventoryBarSprite: cannot open SPRITE\\2D\\OBJETS0.SPR");
		return;
	}

	uint16 flags = stream->readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5)  & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	}
	if (flags & 0x8)
		stream->skip(256 * 2);

	uint16 count = stream->readUint16LE();
	if (count != 1) {
		warning("loadInventoryBarSprite: expected 1 sprite, got %u", count);
		return;
	}

	const int64 sprDataBase = stream->pos();
	uint32 spr0Off = stream->readUint32LE();
	if (!stream->seek(sprDataBase + spr0Off)) {
		warning("loadInventoryBarSprite: seek failed");
		return;
	}

	SubjSprite &spr = _inventoryBarSprite;
	spr.w    = stream->readUint16LE();
	spr.h    = stream->readUint16LE();
	spr.xoff = stream->readSint16LE();
	spr.yoff = stream->readSint16LE();
	if (spr.w == 0 || spr.h == 0 || spr.w > 1024 || spr.h > 256) {
		warning("loadInventoryBarSprite: bad dims %ux%u", spr.w, spr.h);
		spr = SubjSprite();
		return;
	}
	decodeSprite(*stream, spr, colorTable);

	debugC(1, kDebugScript, "loadInventoryBarSprite: loaded %ux%u from OBJETS0.SPR (hotspot %d,%d)",
	      spr.w, spr.h, spr.xoff, spr.yoff);
}

void CryOmni3DEngine_Atlantis::drawInventoryIcon(Graphics::ManagedSurface &dst,
        const Object *obj, const Common::Rect &r) {
	if (_objSprites.empty() || obj == nullptr || _objects.empty())
		return;
	// Find this object's icon via its slot in _objects (parallel to _objSprites).
	ptrdiff_t idx = obj - &_objects.front();
	if (idx < 0 || (uint)idx >= _objSprites.size())
		return;
	const SubjSprite &spr = _objSprites[(uint)idx];
	if (spr.pixels.empty())
		return;

	// Centre the icon on the slot; clip ONLY to the surface (OBJETS1 icons run
	// up to 60x60, larger than the 50x40 slot, so per-slot clipping would trim
	// them — the 70px column spacing leaves room).
	int cx = (r.left + r.right)  / 2;
	int cy = (r.top  + r.bottom) / 2;
	int dx = cx - spr.xoff;
	int dy = cy - spr.yoff;
	for (int y = 0; y < (int)spr.h; y++) {
		int sy = dy + y;
		if (sy < 0 || sy >= (int)dst.h) continue;
		for (int x = 0; x < (int)spr.w; x++) {
			int sx = dx + x;
			if (sx < 0 || sx >= (int)dst.w) continue;
			uint sidx = y * spr.w + x;
			uint16 pix = spr.pixels[sidx];
			if (pix == kSprTransp) continue;
			uint16 *dp = (uint16 *)dst.getBasePtr(sx, sy);
			if (spr.blend[sidx] != kSprNoBlend)
				pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
			*dp = pix;
		}
	}
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

void CryOmni3DEngine_Atlantis::loadObjectSprites() {
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    openBigFileStream(kFileTypeSprite2D, "OBJETS1.SPR"));
	if (!stream) {
		warning("loadObjectSprites: cannot open SPRITE\\2D\\OBJETS1.SPR");
		return;
	}

	uint16 flags = stream->readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5)  & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	}
	if (flags & 0x8)
		stream->skip(256 * 2);

	uint16 count = stream->readUint16LE();
	if (count == 0 || count > 256) {
		warning("loadObjectSprites: unexpected count %u", count);
		return;
	}

	const int64 sprDataBase = stream->pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream->readUint32LE();

	_objSprites.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream->seek(sprDataBase + offsets[si])) {
			warning("loadObjectSprites: seek failed for sprite %u", si);
			continue;
		}
		SubjSprite &spr = _objSprites[si];
		spr.w    = stream->readUint16LE();
		spr.h    = stream->readUint16LE();
		spr.xoff = stream->readSint16LE();
		spr.yoff = stream->readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 640 || spr.h > 480)
			continue;
		decodeSprite(*stream, spr, colorTable);
	}

	debugC(1, kDebugScript, "loadObjectSprites: loaded %u sprites from OBJETS1.SPR", count);
}

void CryOmni3DEngine_Atlantis::loadCursorSprites() {
	Common::ScopedPtr<Common::SeekableReadStream> stream(
	    openBigFileStream(kFileTypeSprite2D, "CURSEURS.SPR"));
	if (!stream) {
		warning("loadCursorSprites: cannot open SPRITE\\2D\\CURSEURS.SPR");
		return;
	}

	uint16 flags = stream->readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = stream->readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5)  & 0x1f;
			uint16 b =  rgb555        & 0x1f;
			uint16 rgb565 = (uint16)((r << 11) | (g << 6) | b);
			// Remap the transparency key to near-black to avoid false keying.
			colorTable[i] = (rgb565 == kWarpCursorKey) ? 0x0000u : rgb565;
		}
	}
	if (flags & 0x8)
		stream->skip(512);

	uint16 count = stream->readUint16LE();
	if (count == 0 || count > 256) {
		warning("loadCursorSprites: unexpected sprite count %u", count);
		return;
	}

	const int64 sprDataBase = stream->pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = stream->readUint32LE();

	_cursorSprites.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!stream->seek(sprDataBase + offsets[si])) {
			warning("loadCursorSprites: seek failed for sprite %u", si);
			continue;
		}
		CursorSpr &spr = _cursorSprites[si];
		spr.w    = stream->readUint16LE();
		spr.h    = stream->readUint16LE();
		spr.xoff = stream->readSint16LE();
		spr.yoff = stream->readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 64 || spr.h > 64)
			continue;

		Common::Array<uint32> rowOff(spr.h);
		for (uint16 r = 0; r < spr.h; r++)
			rowOff[r] = stream->readUint32LE();
		const int64 pixBase = stream->pos();

		spr.pixels.resize(spr.w * spr.h, kWarpCursorKey);
		spr.blend.resize(spr.w * spr.h, kSprNoBlend);

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
					for (int i = 0; i < cnt && rx < (int)spr.w; i++, rx++)
						spr.pixels[ry * spr.w + rx] = colorTable[stream->readByte()];
				} else {
					for (int i = 0; i < cnt && rx < (int)spr.w; i++, rx++) {
						byte factor = stream->readByte() & 0x1f;
						spr.pixels[ry * spr.w + rx] = colorTable[stream->readByte()];
						spr.blend[ry * spr.w + rx]  = factor;
					}
				}
			}
		}
	}

	debugC(1, kDebugScript, "loadCursorSprites: loaded %u sprites from CURSEURS.SPR", count);
}

void CryOmni3DEngine_Atlantis::loadSpritePlaceList() {
	_spritePlaceList.clear();

	const Common::String &listFile = _wam.spwListFile;
	if (listFile.empty()) {
		debugC(1, kDebugVideo, "loadSpritePlaceList: WAM has no SPW list");
		return;
	}

	Common::SeekableReadStream *s = openBigFileStream(kFileTypeSprite, listFile);
	if (!s) {
		warning("loadSpritePlaceList: cannot open '%s'", listFile.c_str());
		return;
	}

	uint32 fileSize = (uint32)s->size();
	byte *buf = new byte[fileSize + 1];
	s->read(buf, fileSize);
	buf[fileSize] = '\0';
	delete s;

	const char *p = (const char *)buf;
	while (*p) {
		while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		const char *eol = p;
		while (*eol && *eol != '\r' && *eol != '\n')
			eol++;
		Common::String line(p, (uint32)(eol - p));
		p = eol;
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;

		Common::Array<Common::String> tok;
		const char *tp = line.c_str();
		while (*tp) {
			while (*tp == ' ' || *tp == '\t') tp++;
			if (!*tp) break;
			const char *te = tp;
			while (*te && *te != ' ' && *te != '\t') te++;
			tok.push_back(Common::String(tp, (uint32)(te - tp)));
			tp = te;
		}
		if (tok.size() < 4)
			continue;

		SpritePlaceEntry entry;
		entry.placeId = (uint16)atoi(tok[0].c_str());
		entry.type    = atoi(tok[1].c_str());
		entry.frames  = atoi(tok[2].c_str());
		entry.angle   = 0.f;
		entry.persoId = 0;
		entry.interactive = false;
		// Trailing '-' on the placeId token marks the sprite as start-inactive.
		entry.startActive = !tok[0].empty() && tok[0][tok[0].size() - 1] != '-';
		entry.active = entry.startActive;
		entry.frame  = 0;

		// Two row formats, distinguished by token count (never 5 or 7):
		//   4 tokens: placeId type frames spwFile
		//   6 tokens: placeId type frames -persoId angle spwFile
		if (tok.size() >= 6) {
			int signedId  = atoi(tok[3].c_str());
			entry.persoId = (signedId < 0) ? -signedId : signedId;
			entry.angle   = (float)atof(tok[4].c_str());
			// A leading '-' on the angle token marks the NPC's home (clickable)
			// place — atof drops the sign on a zero magnitude, so check the raw
			// token.
			entry.interactive = (!tok[4].empty() && tok[4][0] == '-');
			entry.spwFile = tok[5];
		} else {
			entry.spwFile = tok[3];
		}

		_spritePlaceList.push_back(entry);
	}

	delete[] buf;
	debugC(1, kDebugVideo, "loadSpritePlaceList: %u entries from '%s'",
	      _spritePlaceList.size(), listFile.c_str());
}

// SPF list file (e.g. SPFATL1.TXT) lists, for each place-to-place
// transition, which NPCs to show animated during the camera move:
//
//   fromPlace toPlace persoId spfFile
//   001 002 600 a1001002.spf
//   ...
//   #014 016 612 a8014016.spf   <- '#' starts a comment / disabled entry
//   FIN
//
// Stored verbatim into _spriteTransList; playTransitionVideo filters by
// the active (fromPlace, toPlace) pair when a transition starts.
void CryOmni3DEngine_Atlantis::loadSpfList() {
	_spriteTransList.clear();

	const Common::String &listFile = _wam.spfListFile;
	if (listFile.empty()) {
		debugC(1, kDebugVideo, "loadSpfList: WAM has no SPF list");
		return;
	}

	Common::SeekableReadStream *s = openBigFileStream(kFileTypeSprite, listFile);
	if (!s) {
		warning("loadSpfList: cannot open '%s'", listFile.c_str());
		return;
	}

	uint32 fileSize = (uint32)s->size();
	byte *buf = new byte[fileSize + 1];
	s->read(buf, fileSize);
	buf[fileSize] = '\0';
	delete s;

	const char *p = (const char *)buf;
	while (*p) {
		while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
		if (!*p) break;

		// Skip comment / disabled lines (leading '#').
		if (*p == '#') {
			while (*p && *p != '\r' && *p != '\n') p++;
			continue;
		}

		const char *eol = p;
		while (*eol && *eol != '\r' && *eol != '\n') eol++;
		Common::String line(p, (uint32)(eol - p));
		p = eol;

		if (line.equalsIgnoreCase("FIN")) break;

		Common::Array<Common::String> tok;
		const char *tp = line.c_str();
		while (*tp) {
			while (*tp == ' ' || *tp == '\t') tp++;
			if (!*tp) break;
			const char *te = tp;
			while (*te && *te != ' ' && *te != '\t') te++;
			tok.push_back(Common::String(tp, (uint32)(te - tp)));
			tp = te;
		}

		if (tok.size() < 4) continue;
		SpriteTransEntry entry;
		entry.fromPlace = (uint16)atoi(tok[0].c_str());
		entry.toPlace   = (uint16)atoi(tok[1].c_str());
		entry.persoId   = atoi(tok[2].c_str());
		entry.spfFile   = tok[3];
		_spriteTransList.push_back(entry);
		debugC(3, kDebugVideo, "  spf: %u->%u perso=%d file=%s",
		      entry.fromPlace, entry.toPlace, entry.persoId, entry.spfFile.c_str());
	}

	delete[] buf;
	debugC(1, kDebugVideo, "loadSpfList: %u entries from '%s'",
	      _spriteTransList.size(), listFile.c_str());
}

// SPF files share the SPW on-disk format (file_size / num_frames / format
// header followed by per-frame palette + RLE pixel stream), so we drive
// the same decoder against the transition video's 640×480 frame surface.
// Out-of-range frameIdx (transition video longer than the SPF animation)
// clamps: the SPF's last frame stays on screen instead of glitching.
void CryOmni3DEngine_Atlantis::compositeSpfFrame(Common::SeekableReadStream *spf,
                                                  uint frameIdx,
                                                  Graphics::Surface &dst) {
	if (!spf) return;

	// Bounds check against header[0x04] = num_frames.
	if (!spf->seek(4)) return;
	uint32 numFrames = spf->readUint32LE();
	if (!numFrames) return;
	if (frameIdx >= numFrames) frameIdx = numFrames - 1;

	decodeSPWPixels(spf, dst, frameIdx);
}

void CryOmni3DEngine_Atlantis::rebuildNpcPlaceBounds() {
	// Always clear, even if !_warpLoaded, so stale bounds from the previous
	// place can't false-positive a hit-test before the new place is composited.
	_npcPlaceBounds.clear();
	if (!_warpLoaded)
		return;
	for (const SpritePlaceEntry &entry : _spritePlaceList) {
		if (entry.placeId != (uint16)_currentPlaceId)
			continue;
		// Click target: a real perso, marked interactive, currently active and
		// not hidden.  type 0 = normal NPC; type 3 = script-managed clickable
		// props (ropes/traps flagged interactive via the '-00' angle prefix).
		if (entry.persoId <= 0 || !entry.interactive)
			continue;
		if (entry.type != 0 && entry.type != 3)
			continue;
		if (!entry.active)
			continue;
		bool hidden = false;
		for (int h : _hiddenPersos) if (h == entry.persoId) { hidden = true; break; }
		if (hidden)
			continue;

		Common::SeekableReadStream *spw =
		    openBigFileStream(kFileTypeSpriteCyclo, entry.spwFile);
		if (!spw)
			continue;
		int16 x1, y1, x2, y2;
		if (readSPWBounds(spw, x1, y1, x2, y2) && y2 >= y1) {
			if (x2 >= x1) {
				NPCBound nb;
				nb.persoId    = entry.persoId;
				nb.panoBounds = Common::Rect(x1, y1, x2 + 1, y2 + 1);
				_npcPlaceBounds.push_back(nb);
			} else {
				// Sprite straddles the panorama's 0/2048 seam — split in two.
				const int16 panoW = 2048;
				NPCBound nbR, nbL;
				nbR.persoId    = entry.persoId;
				nbR.panoBounds = Common::Rect(x1, y1, panoW, y2 + 1);
				_npcPlaceBounds.push_back(nbR);
				nbL.persoId    = entry.persoId;
				nbL.panoBounds = Common::Rect(0, y1, x2 + 1, y2 + 1);
				_npcPlaceBounds.push_back(nbL);
			}
		}
		delete spw;
	}
}

void CryOmni3DEngine_Atlantis::compositeNPCSprites() {
	if (!_warpLoaded)
		return;

	// Animated props and wspranim poses (the SPW-header frame count + the
	// per-frame tick) land with the music/timer layer; for now sprites render
	// their first frame statically.
	_propAnimFrames = 0;
	_propAnimFrame  = 0;

	// Rebaseline each current-place sprite to its parsed startActive flag so
	// state from a previous place doesn't leak; a persoId in _hiddenPersos
	// stays deactivated (a /set(hideperso=N) persists across places).
	for (SpritePlaceEntry &e : _spritePlaceList) {
		if (e.placeId != (uint16)_currentPlaceId)
			continue;
		e.active = e.startActive;
		e.frame  = 0;
		if (e.persoId > 0) {
			for (int h : _hiddenPersos)
				if (h == e.persoId) { e.active = false; break; }
		}
	}

	rebuildNpcPlaceBounds();
	recompositeSpriteLayer();
}

void CryOmni3DEngine_Atlantis::recompositeSpriteLayer() {
	if (!_warpLoaded)
		return;

	// Start from the clean cyclo so sprites don't stack across recomposites.
	_warpSurface.copyFrom(_cycloSurface);

	// Render every active sprite at the current place (frame 0 for now).
	for (const SpritePlaceEntry &entry : _spritePlaceList) {
		if (entry.placeId != (uint16)_currentPlaceId)
			continue;
		if (!entry.active)
			continue;
		Common::SeekableReadStream *spw =
		    openBigFileStream(kFileTypeSpriteCyclo, entry.spwFile);
		if (!spw) {
			warning("recompositeSpriteLayer: cannot open '%s'", entry.spwFile.c_str());
			continue;
		}
		int16 x1, y1, x2, y2;
		if (!readSPWBounds(spw, x1, y1, x2, y2, 0)) {
			warning("recompositeSpriteLayer: bad header in '%s'", entry.spwFile.c_str());
			delete spw;
			continue;
		}
		debugC(5, kDebugVideo, "recompositeSpriteLayer: place=%u %s bounds [%d,%d]-[%d,%d]",
		      entry.placeId, entry.spwFile.c_str(), x1, y1, x2, y2);
		decodeSPWPixels(spw, _warpSurface, 0);
		delete spw;
	}
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
