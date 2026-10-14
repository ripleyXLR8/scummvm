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

#ifndef CRYOMNI3D_ATLANTIS_SPRITE_BLEND_H
#define CRYOMNI3D_ATLANTIS_SPRITE_BLEND_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The original game (atlantis.exe) anti-aliases sprite edges by blending each
// edge pixel against the framebuffer.  Every blend pixel carries a 5-bit
// coverage `factor` (0..31) and a colour that is *already premultiplied* by
// factor/32 in the sprite data; the destination then contributes the
// remaining (32 - factor)/32 of itself, per RGB565 channel.
//
// This reproduces the original's three precomputed per-channel tables (built
// by FUN_0042b930, consumed by the sprite blitter FUN_0041047f).  The integer
// math here is bit-exact against the runtime table dumps: each table entry is
// (channel * (32 - factor)) / 32, so a `>> 5` reproduces every table lookup
// (the 6-bit green channel's *2/64 reduces to the same *1/32).
//
// A value of kSprNoBlend in a sprite's parallel blend array marks an opaque
// pixel that needs no framebuffer blending; kSprTranspBlend marks a
// transparent pixel.  SPR factors are 0..31, so every RGB565 colour stays
// usable (the original draws by run type, not by colour).
enum { kSprNoBlend = 0xFF, kSprTranspBlend = 0xFE };

inline uint16 blendSprPixel565(uint16 src, uint16 dst, byte factor) {
	uint inv = 32 - (factor & 0x1f);
	uint b = (( dst        & 0x1f) * inv) >> 5;
	uint g = (((dst >>  5) & 0x3f) * inv) >> 5;
	uint r = (((dst >> 11) & 0x1f) * inv) >> 5;
	return (uint16)(src + (uint16)((r << 11) | (g << 5) | b));
}

// UBB dialog-compositor blend — atlantis.exe FUN_00450b08's shade path (the
// indirect blend through the pointer at 0x00497c8c, FUN_004508e0).  The
// foreground counts fully (32/32) and the background with the weight
// 31 - max(shade, fg blue, fg green >> 1, fg red), as below.  shade == 0x1f
// is a plain copy at the call site; this is the partial-coverage path that
// anti-aliases the UBB head edge against the cyclo panorama.
inline uint16 blendUbbPixel565(uint16 fg, uint16 dst, uint shade) {
	// atlantis.exe FUN_004508e0(shade, fg, dst) with the table FUN_00450d90
	// builds (T[a][c] = (31 - a) * c): a is the largest of shade and fg's
	// 5-bit blue, green and red; each channel is (fg * 32 + T[a][dst]) >> 5,
	// green from its top 5 bits and >> 4 into the 6-bit field.  The three
	// parts are added, so a channel can carry into the next one, as in the
	// original.
	const uint fb = fg & 0x1f, fgn = ((fg >> 5) & 0x3f) >> 1, fr = (fg >> 11) & 0x1f;
	const uint db = dst & 0x1f, dgn = ((dst >> 5) & 0x3f) >> 1, dr = (dst >> 11) & 0x1f;
	uint a = shade;
	if (a < fb) a = fb;
	if (a < fgn) a = fgn;
	if (a < fr) a = fr;
	const uint k = 31 - a;
	const uint g = ((((uint16)(k * dgn)) + (fgn << 5)) >> 4) << 5;
	const uint b = ((fb << 5) + (uint16)(k * db)) >> 5;
	const uint r = ((((uint16)(k * dr)) + (fr << 5)) >> 5) << 11;
	return (uint16)(r + g + b);
}

typedef uint16 (*UbbBlendFn)(uint16 fg, uint16 dst, uint shade);

// atlantis.exe FUN_00450b08: the dialog frame's opcode stream over dst16
// (frameN pixels).  0: skip a 24-bit run (0 ends the stream); 1: a run of
// palette colours with one shade byte (>> 3), copied as is when it is 0x1f
// (FUN_00450ad3), blended otherwise; 2: a run with one shade byte per pixel,
// ALWAYS blended (the original calls the blend even at 0x1f).
inline void composeUbbIa(uint16 *dst16, const byte *srcBase, const uint16 *palLUT,
                         const byte *opcodes, uint32 opSize, uint32 frameN,
                         UbbBlendFn blend = blendUbbPixel565) {
	const byte *op = opcodes, *opEnd = opcodes + opSize;
	uint32 pos = 0;
	while (op + 2 <= opEnd && pos < frameN) {
		const byte type = op[0];
		const byte runHi = op[1];
		if (type == 0) {
			if (op + 4 > opEnd) break;
			const uint32 runlen = ((uint32)op[1]) | ((uint32)op[2] << 8) | ((uint32)op[3] << 16);
			if (runlen == 0) break;
			pos += runlen;
			op += 4;
		} else if (type == 1) {
			if (op + 3 > opEnd) break;
			const uint32 runlen = runHi;
			const byte *src = srcBase + pos;
			const uint shade = (uint)(op[2] >> 3);
			for (uint32 i = 0; i < runlen && pos + i < frameN; i++)
				dst16[pos + i] = (shade == 0x1f) ? palLUT[src[i]]
				                                 : blend(palLUT[src[i]], dst16[pos + i], shade);
			pos += runlen;
			op += 3;
		} else if (type == 2) {
			if (op + 2 + runHi > opEnd) break;
			const uint32 runlen = runHi;
			const byte *src = srcBase + pos;
			const byte *shadeBytes = op + 2;
			for (uint32 i = 0; i < runlen && pos + i < frameN; i++)
				dst16[pos + i] = blend(palLUT[src[i]], dst16[pos + i], (uint)(shadeBytes[i] >> 3));
			pos += runlen;
			op += 2 + runlen;
		} else {
			break;   // unknown opcode: the original stops too (0x00450c82)
		}
	}
}

// SPW (panorama animated sprite) blend — atlantis.exe FUN_00452bde /
// FUN_00452d39, with tables built by FUN_00452ac8.  Same premultiplied-alpha
// model as blendSprPixel565, but the SPW tables scale the green channel one
// bit coarser (>>6, placed via <<6), so green is effectively 5-bit here.
inline uint16 blendSpwPixel565(uint16 src, uint16 dst, byte factor) {
	uint inv = 32 - (factor & 0x1f);
	uint b = (( dst        & 0x1f) * inv) >> 5;
	uint g = (((dst >>  5) & 0x3f) * inv) >> 6;
	uint r = (((dst >> 11) & 0x1f) * inv) >> 5;
	return (uint16)(src + (uint16)((r << 11) | (g << 6) | b));
}

// Menu backdrop dim — atlantis.exe FUN_004108e0 (called by FUN_0041cc28
// before the in-game menu and the disc prompt): p - ((p & mask) >> 1) on
// each pixel, mask 0xf79e in 5-6-5 (key 0x235), 0xfbde in 5-5-5 (key 0x613).
// Red and blue become c - c / 2; the 5-6-5 mask also clears green's bit 1,
// so green becomes g - ((g & 0x3c) >> 1).
inline uint16 dimMenuPixel(uint16 p, bool rgb565) {
	const uint16 mask = rgb565 ? 0xf79e : 0xfbde;
	return (uint16)(p - ((p & mask) >> 1));
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
