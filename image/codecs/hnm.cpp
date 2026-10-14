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

#include "image/codecs/hnm.h"

#include "common/stream.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "graphics/surface.h"

#include "common/debug.h"

// HNM6Decoder::setBitExact() enables code which generates bit accurate
// images in RGB565 mode

namespace Image {

namespace HNM6 {

struct BitBuffer {
	BitBuffer() :
		_buffer(nullptr), _size(0), _pos(0), _reg(0), _bits(0) { }

	inline void reset(byte *buffer, uint32 size);
	FORCEINLINE byte next();

private:
	inline void loadReg();

	byte *_buffer;
	uint32 _size;
	uint32 _pos;

	uint32 _reg;
	uint32 _bits;
};

struct MotionBuffer {
	MotionBuffer() : _buffer(nullptr), _size(0), _pos(0) { }

	inline void reset(byte *buffer, uint32 size);
	FORCEINLINE uint16 next();

private:
	byte *_buffer;
	uint32 _size;
	uint32 _pos;
};

struct ShortMotionBuffer {
	ShortMotionBuffer() :
		_buffer(nullptr), _size(0), _pos(0), _other(0) { }

	inline void reset(byte *buffer, uint32 size);
	FORCEINLINE uint16 next();

private:
	inline uint16 loadReg();

	byte *_buffer;
	uint32 _size;
	uint32 _pos;

	uint16 _other;
};

struct JPEGBuffer {
	JPEGBuffer() :
		_buffer(nullptr), _size(0), _pos(0), _other(0) { }

	inline void reset(byte *buffer, uint32 size);
	FORCEINLINE byte next();

private:
	inline byte loadReg();

	byte *_buffer;
	uint32 _size;
	uint32 _pos;

	byte _other;
};

void BitBuffer::reset(byte *buffer, uint32 size) {
	_buffer = buffer;
	_size = size;
	_pos = 0;
	//_reg = 0; // Useless
	_bits = 0;
}

byte BitBuffer::next() {
	if (!_bits) {
		loadReg();
	}
	byte ret = _reg >> 31;
	_reg <<= 1;
	_bits--;
	return ret;
}

void BitBuffer::loadReg() {
	if (_bits) {
		error("BUG: Loading register while still loaded");
	}
	if (_pos + 4 > _size) {
		error("Can't feed bitbuffer register: not enough data");
	}
	_reg = READ_LE_UINT32(_buffer + _pos);
	_pos += sizeof(uint32);
	_bits = sizeof(uint32) * 8;
}

void MotionBuffer::reset(byte *buffer, uint32 size) {
	_buffer = buffer;
	_size = size;
	_pos = 0;
}

uint16 MotionBuffer::next() {
	if (_pos + 2 > _size) {
		error("Can't get motion word: not enough data");
	}
	uint16 ret = READ_LE_UINT16(_buffer + _pos);
	_pos += sizeof(uint16);
	return ret;
}

void ShortMotionBuffer::reset(byte *buffer, uint32 size) {
	_buffer = buffer;
	_size = size;
	_pos = 0;
	_other = 0;
}

uint16 ShortMotionBuffer::next() {
	if (_other) {
		uint16 ret = _other & 0xfff;
		_other = 0;
		return ret;
	}

	return loadReg();
}

uint16 ShortMotionBuffer::loadReg() {
	if (_other) {
		error("BUG: Loading register while still loaded");
	}
	if (_pos + 2 > _size) {
		error("Can't feed shortmo register: not enough data");
	}
	if (_pos + 3 > _size) {
		// We are at the end: load the last 2 bytes for a last word
		uint16 ret = READ_LE_UINT16(_buffer + _pos);
		_pos += sizeof(uint16);
		return ret & 0xfff;
	}
	uint32 fullword = READ_LE_UINT16(_buffer + _pos);
	fullword |= *(_buffer + _pos + 2) << 16;
	_pos += 3 * sizeof(byte);

	// 0x8000 is a marker of validity
	_other = ((fullword >> 12) & 0xfff) | 0x8000;
	return fullword & 0xfff;
}

void JPEGBuffer::reset(byte *buffer, uint32 size) {
	_buffer = buffer;
	_size = size;
	_pos = 0;
	_other = 0;
}

byte JPEGBuffer::next() {
	if (_other) {
		byte ret = _other & 0xf;
		_other = 0;
		return ret;
	}

	return loadReg();
}

byte JPEGBuffer::loadReg() {
	if (_other) {
		error("BUG: Loading register while still loaded");
	}
	if (_pos > _size) {
		error("Can't feed JPEG register: not enough data");
	}

	byte fullword = *(_buffer + _pos);
	_pos++;

	// 0x80 is a marker of validity
	_other = (fullword >> 4) | 0x80;
	return fullword & 0xf;
}

static void YUVtoRGB(Graphics::Surface &current, uint x, uint y, int32 *coeffs, bool bitExact) {
#define CR(x) ((16 * (x - 256) + 8) / 10)
#define CB(x) ((x - 256) / 3)
	Graphics::PixelFormat &format = current.format;
	for (uint ry = 0; ry < 8; ry++) {
		void *linePtr = current.getBasePtr(x, y + ry);
		for (uint rx = 0; rx < 8; rx++, coeffs++) {
			int32 cy = coeffs[0];
			int32 cu = coeffs[64];
			int32 cv = coeffs[128];

			cy = cy >> 4;

			int32 cr = CR((cv >> 4) + 256);
			int32 cb = CB((cu >> 4) + 256);

			byte r = CLIP<int32>(cy + 128 + cr, 0, 255);
			// In original code G is clipped a little more
			byte g = CLIP<int32>(cy + 128 - (cr >> 1) - cb, 0, bitExact ? 251 : 255);
			byte b = CLIP<int32>(cy + 128 + (cu >> 3), 0, 255);

			if (bitExact) {
				// Original shifts R and B by 3 and G by 2
				r &= 0xf8;
				g &= 0xfc;
				b &= 0xf8;
			}

			if (format.bytesPerPixel == 2) {
				((uint16 *)linePtr)[rx] = format.RGBToColor(r, g, b);
			} else if (format.bytesPerPixel == 4) {
				((uint32 *)linePtr)[rx] = format.RGBToColor(r, g, b);
			}
		}
	}
#undef CR
#undef CB
}

/**
 * This is an optimized Arai, Agui & Nakajima implementation
 * Only the rows and columns set with values are taken into account
 * The order of the IDCT being equivalent, the mask is used to
 * determine where to start
 */
#define AAN_STRIDE(inout, stride, inc, mask) do {       \
    int32 *x = inout;                                   \
    byte mask_ = mask;                                  \
    for(uint i = 0; i < 8 && mask_;                     \
            i++, x += inc, mask_ >>= 1) {               \
        int32 x2n6 = x[2*stride] - x[6*stride];         \
        int32 x2p6 = x[2*stride] + x[6*stride];         \
        int32 x0n4 = x[0*stride] - x[4*stride];         \
        int32 x0p4 = x[0*stride] + x[4*stride];         \
                                                        \
        int32 x5n3 = x[5*stride] - x[3*stride];         \
        int32 x3p5 = x[3*stride] + x[5*stride];         \
        int32 x1n7 = x[1*stride] - x[7*stride];         \
        int32 x1p7 = x[1*stride] + x[7*stride];         \
                                                        \
        int32 tmp0 = ((3* x2n6) >> 1) - x2p6;           \
        int32 tmp1 = (3 * (x1p7 - x3p5)) >> 1;          \
        int32 tmp2 = 30 * (x5n3 + x1n7);                \
        int32 tmp3 = (17 * x1n7 - tmp2) >> 4;           \
        int32 tmp4 = (tmp2 - 40 * x5n3) >> 4;           \
        int32 tmp5 = tmp4 - x3p5 - x1p7;                \
                                                        \
        x[0*stride] = x1p7 + x3p5 + x0p4 + x2p6;        \
        x[7*stride] = x0p4 + x2p6 - x1p7 - x3p5;        \
        x[1*stride] = tmp0 + x0n4 + tmp4 - x3p5 - x1p7; \
        x[6*stride] = tmp0 + x0n4 - tmp4 + x3p5 + x1p7; \
        x[2*stride] = x0n4 - tmp0 + tmp1 - tmp5;        \
        x[5*stride] = x0n4 - tmp0 - tmp1 + tmp5;        \
        x[3*stride] = x0p4 - x2p6 - tmp1 + tmp5 - tmp3; \
        x[4*stride] = x0p4 - x2p6 + tmp1 - tmp5 + tmp3; \
    }                                                   \
} while(0)

static void aanidct(int32 *inout, uint16 mask, bool bitExact) {
	if (mask == 0x0101) {
		// Simple case: 1 coefficient in the top left corner: it means constant
		for (uint i = 1; i < 64; i++) {
			inout[i] = inout[0];
		}
		return;
	}

	uint8 rowmask, colmask;
	rowmask = mask >> 8;
	colmask = mask & 0xff;

	// Original decoder has a bug where the masks are compared
	// with a sign compare. This means that 0x80 < 0x1.
	// The AAN order is then not optimal.
	const bool rowFirst = bitExact ? (int8)rowmask < (int8)colmask : rowmask < colmask;
	if (rowFirst) {
		// Inside row
		AAN_STRIDE(inout, 1, 8, rowmask);
		if (rowmask == 0x01) {
			// Simple case: Only one line filled, copy to others
			for (uint i = 1; i < 8; i++) {
				memcpy(inout + 8 * i, inout, sizeof(*inout) * 8);
			}
		} else {
			AAN_STRIDE(inout, 8, 1, 0xff);
		}
	} else {
		// Inside column
		AAN_STRIDE(inout, 8, 1, colmask);
		if (colmask == 0x01) {
			// Simple case: Only one column filled, copy to others
			for (uint i = 0; i < 8; i++) {
				// NlogN memset
				inout[8 * i + 1] = inout[8 * i + 0];
				memcpy(inout + 8 * i + 2, inout + 8 * i, sizeof(*inout) * 2);
				memcpy(inout + 8 * i + 4, inout + 8 * i, sizeof(*inout) * 4);
			}
		} else {
			AAN_STRIDE(inout, 1, 8, 0xff);
		}
	}
}
#undef AAN_STRIDE

template<int sx, int sy, typename PixelType>
static void doMotion(byte xform, Graphics::Surface &dst, const Graphics::Surface &src,
                     int srx, int sry, int dx, int dy) {
	int bpp = sizeof(PixelType);
	int stride = dst.pitch;
	const byte *srcP;
	byte *dstP;
#define COPY_PIXEL() *((PixelType *)dstP) = *((const PixelType *)srcP)

	switch (xform) {
	case 0: // Copy
		srcP = (const byte *)src.getBasePtr(srx, sry);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			memmove(dstP, srcP, bpp * sx);
			dstP += stride;
			srcP += stride;
		}
		break;
	case 1: // Horizontal Flip
		srcP = (const byte *)src.getBasePtr(srx + sx - 1, sry);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP -= bpp;
			}
			dstP += stride - sx * bpp;
			srcP += stride + sx * bpp;
		}
		break;
	case 2: // Vertical Flip
		srcP = (const byte *)src.getBasePtr(srx, sry + sy - 1);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			memmove(dstP, srcP, bpp * sx);
			dstP += stride;
			srcP -= stride;
		}
		break;
	case 3: // Cross Flip
		srcP = (const byte *)src.getBasePtr(srx + sx - 1, sry + sy - 1);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP -= bpp;
			}
			dstP += stride - sx * bpp;
			srcP -= stride - sx * bpp;
		}
		break;
	case 4: // Forward Flip
		srcP = (const byte *)src.getBasePtr(srx + sy - 1, sry + sx - 1);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP -= stride;
			}
			dstP += stride - sx * bpp;
			srcP += sx * stride - bpp;
		}
		break;
	case 5: // Forward Rotate
		srcP = (const byte *)src.getBasePtr(srx, sry + sx - 1);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP -= stride;
			}
			dstP += stride - sx * bpp;
			srcP += sx * stride + bpp;
		}
		break;
	case 6: // Backward Rotate
		srcP = (const byte *)src.getBasePtr(srx + sy - 1, sry);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP += stride;
			}
			dstP += stride - sx * bpp;
			srcP -= sx * stride + bpp;
		}
		break;
	case 7: // Backward Flip
		srcP = (const byte *)src.getBasePtr(srx, sry);
		dstP = (byte *)dst.getBasePtr(dx, dy);
		for (int y = 0; y < sy; y++) {
			for (int x = 0; x < sx; x++) {
				COPY_PIXEL();
				dstP += bpp;
				srcP += stride;
			}
			dstP += stride - sx * bpp;
			srcP -= sx * stride - bpp;
		}
		break;
	default:
		error("BUG: Invalid motion transform");
	}
#undef COPY_PIXEL
}

template<int sx, int sy>
FORCEINLINE static void doMotion(byte xform, Graphics::Surface &dst, const Graphics::Surface &src,
                                 int srx, int sry, int dx, int dy) {
	if (dst.format.bytesPerPixel == 2) {
		doMotion<sx, sy, uint16>(xform, dst, src, srx, sry, dx, dy);
	} else if (dst.format.bytesPerPixel == 4) {
		doMotion<sx, sy, uint32>(xform, dst, src, srx, sry, dx, dy);
	}
}

// Generated by atlantis-re/tools/emu/video_ix_motion_order.py: the read /
// write order of the 32 IX motion-copy routines of atlantis.exe (tables
// 0x004b88c8 8x8, 0x004b88e8 4x8, 0x004b8908 8x4, 0x004b8928 4x4,
// 0x004b8948, 0x004b8968, 0x004b8988 for the small blocks; eight
// transforms each).  One byte per event: bit 7 set = write, low 6 bits =
// y * 8 + x of the destination pixel; clear = read, low 6 bits = y * 8 + x
// of the source pixel.  A write takes the value of the source pixel its
// transform maps to it, as last read.
static const byte kIXMotionOrder8x8[8][128] = {
	{ 0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x04, 0x05, 0x06, 0x07, 0x84, 0x85, 0x86, 0x87, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b, 0x0c, 0x0d, 0x0e, 0x0f, 0x8c, 0x8d, 0x8e, 0x8f, 0x10, 0x11, 0x12, 0x13, 0x90, 0x91, 0x92, 0x93, 0x14, 0x15, 0x16, 0x17, 0x94, 0x95, 0x96, 0x97, 0x18, 0x19, 0x1a, 0x1b, 0x98, 0x99, 0x9a, 0x9b, 0x1c, 0x1d, 0x1e, 0x1f, 0x9c, 0x9d, 0x9e, 0x9f, 0x20, 0x21, 0x22, 0x23, 0xa0, 0xa1, 0xa2, 0xa3, 0x24, 0x25, 0x26, 0x27, 0xa4, 0xa5, 0xa6, 0xa7, 0x28, 0x29, 0x2a, 0x2b, 0xa8, 0xa9, 0xaa, 0xab, 0x2c, 0x2d, 0x2e, 0x2f, 0xac, 0xad, 0xae, 0xaf, 0x30, 0x31, 0x32, 0x33, 0xb0, 0xb1, 0xb2, 0xb3, 0x34, 0x35, 0x36, 0x37, 0xb4, 0xb5, 0xb6, 0xb7, 0x38, 0x39, 0x3a, 0x3b, 0xb8, 0xb9, 0xba, 0xbb, 0x3c, 0x3d, 0x3e, 0x3f, 0xbc, 0xbd, 0xbe, 0xbf },   // 0x46da93
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x84, 0x85, 0x86, 0x87, 0x0a, 0x0b, 0x82, 0x83, 0x08, 0x09, 0x80, 0x81, 0x0c, 0x0d, 0x0e, 0x0f, 0x8c, 0x8d, 0x8e, 0x8f, 0x12, 0x13, 0x88, 0x89, 0x10, 0x11, 0x8a, 0x8b, 0x14, 0x15, 0x16, 0x17, 0x94, 0x95, 0x96, 0x97, 0x1a, 0x1b, 0x90, 0x91, 0x18, 0x19, 0x92, 0x93, 0x1c, 0x1d, 0x1e, 0x1f, 0x9c, 0x9d, 0x9e, 0x9f, 0x22, 0x23, 0x98, 0x99, 0x20, 0x21, 0x9a, 0x9b, 0x24, 0x25, 0x26, 0x27, 0xa4, 0xa5, 0xa6, 0xa7, 0x2a, 0x2b, 0xa0, 0xa1, 0x28, 0x29, 0xa2, 0xa3, 0x2c, 0x2d, 0x2e, 0x2f, 0xac, 0xad, 0xae, 0xaf, 0x32, 0x33, 0xa8, 0xa9, 0x30, 0x31, 0xaa, 0xab, 0x34, 0x35, 0x36, 0x37, 0xb4, 0xb5, 0xb6, 0xb7, 0x3a, 0x3b, 0xb0, 0xb1, 0x38, 0x39, 0xb2, 0xb3, 0x3c, 0x3d, 0x3e, 0x3f, 0xbc, 0xbd, 0xbe, 0xbf, 0xb8, 0xb9, 0xba, 0xbb },   // 0x46dbfa
	{ 0x00, 0x01, 0x02, 0x03, 0xb8, 0xb9, 0xba, 0xbb, 0x04, 0x05, 0x06, 0x07, 0xbc, 0xbd, 0xbe, 0xbf, 0x08, 0x09, 0x0a, 0x0b, 0xb0, 0xb1, 0xb2, 0xb3, 0x0c, 0x0d, 0x0e, 0x0f, 0xb4, 0xb5, 0xb6, 0xb7, 0x10, 0x11, 0x12, 0x13, 0xa8, 0xa9, 0xaa, 0xab, 0x14, 0x15, 0x16, 0x17, 0xac, 0xad, 0xae, 0xaf, 0x18, 0x19, 0x1a, 0x1b, 0xa0, 0xa1, 0xa2, 0xa3, 0x1c, 0x1d, 0x1e, 0x1f, 0xa4, 0xa5, 0xa6, 0xa7, 0x20, 0x21, 0x22, 0x23, 0x98, 0x99, 0x9a, 0x9b, 0x24, 0x25, 0x26, 0x27, 0x9c, 0x9d, 0x9e, 0x9f, 0x28, 0x29, 0x2a, 0x2b, 0x90, 0x91, 0x92, 0x93, 0x2c, 0x2d, 0x2e, 0x2f, 0x94, 0x95, 0x96, 0x97, 0x30, 0x31, 0x32, 0x33, 0x88, 0x89, 0x8a, 0x8b, 0x34, 0x35, 0x36, 0x37, 0x8c, 0x8d, 0x8e, 0x8f, 0x38, 0x39, 0x3a, 0x3b, 0x80, 0x81, 0x82, 0x83, 0x3c, 0x3d, 0x3e, 0x3f, 0x84, 0x85, 0x86, 0x87 },   // 0x46ddc1
	{ 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x84, 0x85, 0x86, 0x87, 0x32, 0x33, 0x82, 0x83, 0x30, 0x31, 0x80, 0x81, 0x34, 0x35, 0x36, 0x37, 0x8c, 0x8d, 0x8e, 0x8f, 0x2a, 0x2b, 0x88, 0x89, 0x28, 0x29, 0x8a, 0x8b, 0x2c, 0x2d, 0x2e, 0x2f, 0x94, 0x95, 0x96, 0x97, 0x22, 0x23, 0x90, 0x91, 0x20, 0x21, 0x92, 0x93, 0x24, 0x25, 0x26, 0x27, 0x9c, 0x9d, 0x9e, 0x9f, 0x1a, 0x1b, 0x98, 0x99, 0x18, 0x19, 0x9a, 0x9b, 0x1c, 0x1d, 0x1e, 0x1f, 0xa4, 0xa5, 0xa6, 0xa7, 0x12, 0x13, 0xa0, 0xa1, 0x10, 0x11, 0xa2, 0xa3, 0x14, 0x15, 0x16, 0x17, 0xac, 0xad, 0xae, 0xaf, 0x0a, 0x0b, 0xa8, 0xa9, 0x08, 0x09, 0xaa, 0xab, 0x0c, 0x0d, 0x0e, 0x0f, 0xb4, 0xb5, 0xb6, 0xb7, 0x02, 0x03, 0xb0, 0xb1, 0x00, 0x01, 0xb2, 0xb3, 0x04, 0x05, 0x06, 0x07, 0xbc, 0xbd, 0xbe, 0xbf, 0xb8, 0xb9, 0xba, 0xbb },   // 0x46df28
	{ 0x38, 0x39, 0x30, 0x31, 0xb8, 0xb9, 0xb0, 0xb1, 0x3a, 0x3b, 0x32, 0x33, 0xa8, 0xa9, 0xa0, 0xa1, 0x3c, 0x3d, 0x34, 0x35, 0x98, 0x99, 0x90, 0x91, 0x3e, 0x3f, 0x36, 0x37, 0x88, 0x89, 0x80, 0x81, 0x28, 0x29, 0x20, 0x21, 0xba, 0xbb, 0xb2, 0xb3, 0x2a, 0x2b, 0x22, 0x23, 0xaa, 0xab, 0xa2, 0xa3, 0x2c, 0x2d, 0x24, 0x25, 0x9a, 0x9b, 0x92, 0x93, 0x2e, 0x2f, 0x26, 0x27, 0x8a, 0x8b, 0x82, 0x83, 0x18, 0x19, 0x10, 0x11, 0xbc, 0xbd, 0xb4, 0xb5, 0x1a, 0x1b, 0x12, 0x13, 0xac, 0xad, 0xa4, 0xa5, 0x1c, 0x1d, 0x14, 0x15, 0x9c, 0x9d, 0x94, 0x95, 0x1e, 0x1f, 0x16, 0x17, 0x8c, 0x8d, 0x84, 0x85, 0x08, 0x09, 0x00, 0x01, 0xbe, 0xbf, 0xb6, 0xb7, 0x0a, 0x0b, 0x02, 0x03, 0xae, 0xaf, 0xa6, 0xa7, 0x0c, 0x0d, 0x04, 0x05, 0x9e, 0x9f, 0x96, 0x97, 0x0e, 0x0f, 0x06, 0x07, 0x8e, 0x8f, 0x86, 0x87 },   // 0x46e0ef
	{ 0x38, 0x39, 0x30, 0x31, 0x80, 0x81, 0x88, 0x89, 0x3a, 0x3b, 0x32, 0x33, 0x90, 0x91, 0x98, 0x99, 0x3c, 0x3d, 0x34, 0x35, 0xa0, 0xa1, 0xa8, 0xa9, 0x3e, 0x3f, 0x36, 0x37, 0xb0, 0xb1, 0xb8, 0xb9, 0x28, 0x29, 0x20, 0x21, 0x82, 0x83, 0x8a, 0x8b, 0x2a, 0x2b, 0x22, 0x23, 0x92, 0x93, 0x9a, 0x9b, 0x2c, 0x2d, 0x24, 0x25, 0xa2, 0xa3, 0xaa, 0xab, 0x2e, 0x2f, 0x26, 0x27, 0xb2, 0xb3, 0xba, 0xbb, 0x18, 0x19, 0x10, 0x11, 0x84, 0x85, 0x8c, 0x8d, 0x1a, 0x1b, 0x12, 0x13, 0x94, 0x95, 0x9c, 0x9d, 0x1c, 0x1d, 0x14, 0x15, 0xa4, 0xa5, 0xac, 0xad, 0x1e, 0x1f, 0x16, 0x17, 0xb4, 0xb5, 0xbc, 0xbd, 0x08, 0x09, 0x00, 0x01, 0x86, 0x87, 0x8e, 0x8f, 0x0a, 0x0b, 0x02, 0x03, 0x96, 0x97, 0x9e, 0x9f, 0x0c, 0x0d, 0x04, 0x05, 0xa6, 0xa7, 0xae, 0xaf, 0x0e, 0x0f, 0x06, 0x07, 0xb6, 0xb7, 0xbe, 0xbf },   // 0x46e3e6
	{ 0x00, 0x01, 0x08, 0x09, 0xb8, 0xb9, 0xb0, 0xb1, 0x02, 0x03, 0x0a, 0x0b, 0xa8, 0xa9, 0xa0, 0xa1, 0x04, 0x05, 0x0c, 0x0d, 0x98, 0x99, 0x90, 0x91, 0x06, 0x07, 0x0e, 0x0f, 0x88, 0x89, 0x80, 0x81, 0x10, 0x11, 0x18, 0x19, 0xba, 0xbb, 0xb2, 0xb3, 0x12, 0x13, 0x1a, 0x1b, 0xaa, 0xab, 0xa2, 0xa3, 0x14, 0x15, 0x1c, 0x1d, 0x9a, 0x9b, 0x92, 0x93, 0x16, 0x17, 0x1e, 0x1f, 0x8a, 0x8b, 0x82, 0x83, 0x20, 0x21, 0x28, 0x29, 0xbc, 0xbd, 0xb4, 0xb5, 0x22, 0x23, 0x2a, 0x2b, 0xac, 0xad, 0xa4, 0xa5, 0x24, 0x25, 0x2c, 0x2d, 0x9c, 0x9d, 0x94, 0x95, 0x26, 0x27, 0x2e, 0x2f, 0x8c, 0x8d, 0x84, 0x85, 0x30, 0x31, 0x38, 0x39, 0xbe, 0xbf, 0xb6, 0xb7, 0x32, 0x33, 0x3a, 0x3b, 0xae, 0xaf, 0xa6, 0xa7, 0x34, 0x35, 0x3c, 0x3d, 0x9e, 0x9f, 0x96, 0x97, 0x36, 0x37, 0x3e, 0x3f, 0x8e, 0x8f, 0x86, 0x87 },   // 0x46e6dd
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x02, 0x03, 0x0a, 0x0b, 0x90, 0x91, 0x98, 0x99, 0x04, 0x05, 0x0c, 0x0d, 0xa0, 0xa1, 0xa8, 0xa9, 0x06, 0x07, 0x0e, 0x0f, 0xb0, 0xb1, 0xb8, 0xb9, 0x10, 0x11, 0x18, 0x19, 0x82, 0x83, 0x8a, 0x8b, 0x12, 0x13, 0x1a, 0x1b, 0x92, 0x93, 0x9a, 0x9b, 0x14, 0x15, 0x1c, 0x1d, 0xa2, 0xa3, 0xaa, 0xab, 0x16, 0x17, 0x1e, 0x1f, 0xb2, 0xb3, 0xba, 0xbb, 0x20, 0x21, 0x28, 0x29, 0x84, 0x85, 0x8c, 0x8d, 0x22, 0x23, 0x2a, 0x2b, 0x94, 0x95, 0x9c, 0x9d, 0x24, 0x25, 0x2c, 0x2d, 0xa4, 0xa5, 0xac, 0xad, 0x26, 0x27, 0x2e, 0x2f, 0xb4, 0xb5, 0xbc, 0xbd, 0x30, 0x31, 0x38, 0x39, 0x86, 0x87, 0x8e, 0x8f, 0x32, 0x33, 0x3a, 0x3b, 0x96, 0x97, 0x9e, 0x9f, 0x34, 0x35, 0x3c, 0x3d, 0xa6, 0xa7, 0xae, 0xaf, 0x36, 0x37, 0x3e, 0x3f, 0xb6, 0xb7, 0xbe, 0xbf },   // 0x46e9d4
};
static const byte kIXMotionOrder4x8[8][64] = {
	{ 0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b, 0x10, 0x11, 0x12, 0x13, 0x90, 0x91, 0x92, 0x93, 0x18, 0x19, 0x1a, 0x1b, 0x98, 0x99, 0x9a, 0x9b, 0x20, 0x21, 0x22, 0x23, 0xa0, 0xa1, 0xa2, 0xa3, 0x28, 0x29, 0x2a, 0x2b, 0xa8, 0xa9, 0xaa, 0xab, 0x30, 0x31, 0x32, 0x33, 0xb0, 0xb1, 0xb2, 0xb3, 0x38, 0x39, 0x3a, 0x3b, 0xb8, 0xb9, 0xba, 0xbb },   // 0x46eccb
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x80, 0x81, 0x82, 0x83, 0x12, 0x13, 0x88, 0x89, 0x10, 0x11, 0x8a, 0x8b, 0x18, 0x19, 0x1a, 0x1b, 0x90, 0x91, 0x92, 0x93, 0x22, 0x23, 0x98, 0x99, 0x20, 0x21, 0x9a, 0x9b, 0x28, 0x29, 0x2a, 0x2b, 0xa0, 0xa1, 0xa2, 0xa3, 0x32, 0x33, 0xa8, 0xa9, 0x30, 0x31, 0xaa, 0xab, 0x38, 0x39, 0x3a, 0x3b, 0xb0, 0xb1, 0xb2, 0xb3, 0xb8, 0xb9, 0xba, 0xbb },   // 0x46ed7e
	{ 0x00, 0x01, 0x02, 0x03, 0xb8, 0xb9, 0xba, 0xbb, 0x08, 0x09, 0x0a, 0x0b, 0xb0, 0xb1, 0xb2, 0xb3, 0x10, 0x11, 0x12, 0x13, 0xa8, 0xa9, 0xaa, 0xab, 0x18, 0x19, 0x1a, 0x1b, 0xa0, 0xa1, 0xa2, 0xa3, 0x20, 0x21, 0x22, 0x23, 0x98, 0x99, 0x9a, 0x9b, 0x28, 0x29, 0x2a, 0x2b, 0x90, 0x91, 0x92, 0x93, 0x30, 0x31, 0x32, 0x33, 0x88, 0x89, 0x8a, 0x8b, 0x38, 0x39, 0x3a, 0x3b, 0x80, 0x81, 0x82, 0x83 },   // 0x46ee61
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0xb8, 0xb9, 0xba, 0xbb, 0x12, 0x13, 0xb0, 0xb1, 0x10, 0x11, 0xb2, 0xb3, 0x18, 0x19, 0x1a, 0x1b, 0xa8, 0xa9, 0xaa, 0xab, 0x22, 0x23, 0xa0, 0xa1, 0x20, 0x21, 0xa2, 0xa3, 0x28, 0x29, 0x2a, 0x2b, 0x98, 0x99, 0x9a, 0x9b, 0x32, 0x33, 0x90, 0x91, 0x30, 0x31, 0x92, 0x93, 0x38, 0x39, 0x3a, 0x3b, 0x88, 0x89, 0x8a, 0x8b, 0x80, 0x81, 0x82, 0x83 },   // 0x46ef14
	{ 0x18, 0x19, 0x10, 0x11, 0xb8, 0xb9, 0xb0, 0xb1, 0x1a, 0x1b, 0x12, 0x13, 0xa8, 0xa9, 0xa0, 0xa1, 0x1c, 0x1d, 0x14, 0x15, 0x98, 0x99, 0x90, 0x91, 0x1e, 0x1f, 0x16, 0x17, 0x88, 0x89, 0x80, 0x81, 0x08, 0x09, 0x00, 0x01, 0xba, 0xbb, 0xb2, 0xb3, 0x0a, 0x0b, 0x02, 0x03, 0xaa, 0xab, 0xa2, 0xa3, 0x0c, 0x0d, 0x04, 0x05, 0x9a, 0x9b, 0x92, 0x93, 0x0e, 0x0f, 0x06, 0x07, 0x8a, 0x8b, 0x82, 0x83 },   // 0x46eff7
	{ 0x18, 0x19, 0x10, 0x11, 0x80, 0x81, 0x88, 0x89, 0x1a, 0x1b, 0x12, 0x13, 0x90, 0x91, 0x98, 0x99, 0x1c, 0x1d, 0x14, 0x15, 0xa0, 0xa1, 0xa8, 0xa9, 0x1e, 0x1f, 0x16, 0x17, 0xb0, 0xb1, 0xb8, 0xb9, 0x08, 0x09, 0x00, 0x01, 0x82, 0x83, 0x8a, 0x8b, 0x0a, 0x0b, 0x02, 0x03, 0x92, 0x93, 0x9a, 0x9b, 0x0c, 0x0d, 0x04, 0x05, 0xa2, 0xa3, 0xaa, 0xab, 0x0e, 0x0f, 0x06, 0x07, 0xb2, 0xb3, 0xba, 0xbb },   // 0x46f16c
	{ 0x00, 0x01, 0x08, 0x09, 0xb8, 0xb9, 0xb0, 0xb1, 0x02, 0x03, 0x0a, 0x0b, 0xa8, 0xa9, 0xa0, 0xa1, 0x04, 0x05, 0x0c, 0x0d, 0x98, 0x99, 0x90, 0x91, 0x06, 0x07, 0x0e, 0x0f, 0x88, 0x89, 0x80, 0x81, 0x10, 0x11, 0x18, 0x19, 0xba, 0xbb, 0xb2, 0xb3, 0x12, 0x13, 0x1a, 0x1b, 0xaa, 0xab, 0xa2, 0xa3, 0x14, 0x15, 0x1c, 0x1d, 0x9a, 0x9b, 0x92, 0x93, 0x16, 0x17, 0x1e, 0x1f, 0x8a, 0x8b, 0x82, 0x83 },   // 0x46f2e1
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x02, 0x03, 0x0a, 0x0b, 0x90, 0x91, 0x98, 0x99, 0x04, 0x05, 0x0c, 0x0d, 0xa0, 0xa1, 0xa8, 0xa9, 0x06, 0x07, 0x0e, 0x0f, 0xb0, 0xb1, 0xb8, 0xb9, 0x10, 0x11, 0x18, 0x19, 0x82, 0x83, 0x8a, 0x8b, 0x12, 0x13, 0x1a, 0x1b, 0x92, 0x93, 0x9a, 0x9b, 0x14, 0x15, 0x1c, 0x1d, 0xa2, 0xa3, 0xaa, 0xab, 0x16, 0x17, 0x1e, 0x1f, 0xb2, 0xb3, 0xba, 0xbb },   // 0x46f456
};
static const byte kIXMotionOrder8x4[8][64] = {
	{ 0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x04, 0x05, 0x06, 0x07, 0x84, 0x85, 0x86, 0x87, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b, 0x0c, 0x0d, 0x0e, 0x0f, 0x8c, 0x8d, 0x8e, 0x8f, 0x10, 0x11, 0x12, 0x13, 0x90, 0x91, 0x92, 0x93, 0x14, 0x15, 0x16, 0x17, 0x94, 0x95, 0x96, 0x97, 0x18, 0x19, 0x1a, 0x1b, 0x98, 0x99, 0x9a, 0x9b, 0x1c, 0x1d, 0x1e, 0x1f, 0x9c, 0x9d, 0x9e, 0x9f },   // 0x46f5cb
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x84, 0x85, 0x86, 0x87, 0x0a, 0x0b, 0x82, 0x83, 0x08, 0x09, 0x80, 0x81, 0x0c, 0x0d, 0x0e, 0x0f, 0x8c, 0x8d, 0x8e, 0x8f, 0x12, 0x13, 0x88, 0x89, 0x10, 0x11, 0x8a, 0x8b, 0x14, 0x15, 0x16, 0x17, 0x94, 0x95, 0x96, 0x97, 0x1a, 0x1b, 0x90, 0x91, 0x18, 0x19, 0x92, 0x93, 0x1c, 0x1d, 0x1e, 0x1f, 0x9c, 0x9d, 0x9e, 0x9f, 0x98, 0x99, 0x9a, 0x9b },   // 0x46f672
	{ 0x00, 0x01, 0x02, 0x03, 0x98, 0x99, 0x9a, 0x9b, 0x04, 0x05, 0x06, 0x07, 0x9c, 0x9d, 0x9e, 0x9f, 0x08, 0x09, 0x0a, 0x0b, 0x90, 0x91, 0x92, 0x93, 0x0c, 0x0d, 0x0e, 0x0f, 0x94, 0x95, 0x96, 0x97, 0x10, 0x11, 0x12, 0x13, 0x88, 0x89, 0x8a, 0x8b, 0x14, 0x15, 0x16, 0x17, 0x8c, 0x8d, 0x8e, 0x8f, 0x18, 0x19, 0x1a, 0x1b, 0x80, 0x81, 0x82, 0x83, 0x1c, 0x1d, 0x1e, 0x1f, 0x84, 0x85, 0x86, 0x87 },   // 0x46f749
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x9c, 0x9d, 0x9e, 0x9f, 0x0a, 0x0b, 0x9a, 0x9b, 0x08, 0x09, 0x98, 0x99, 0x0c, 0x0d, 0x0e, 0x0f, 0x94, 0x95, 0x96, 0x97, 0x12, 0x13, 0x90, 0x91, 0x10, 0x11, 0x92, 0x93, 0x14, 0x15, 0x16, 0x17, 0x8c, 0x8d, 0x8e, 0x8f, 0x1a, 0x1b, 0x88, 0x89, 0x18, 0x19, 0x8a, 0x8b, 0x1c, 0x1d, 0x1e, 0x1f, 0x84, 0x85, 0x86, 0x87, 0x80, 0x81, 0x82, 0x83 },   // 0x46f7f0
	{ 0x38, 0x39, 0x30, 0x31, 0x98, 0x99, 0x90, 0x91, 0x3a, 0x3b, 0x32, 0x33, 0x88, 0x89, 0x80, 0x81, 0x28, 0x29, 0x20, 0x21, 0x9a, 0x9b, 0x92, 0x93, 0x2a, 0x2b, 0x22, 0x23, 0x8a, 0x8b, 0x82, 0x83, 0x18, 0x19, 0x10, 0x11, 0x9c, 0x9d, 0x94, 0x95, 0x1a, 0x1b, 0x12, 0x13, 0x8c, 0x8d, 0x84, 0x85, 0x08, 0x09, 0x00, 0x01, 0x9e, 0x9f, 0x96, 0x97, 0x0a, 0x0b, 0x02, 0x03, 0x8e, 0x8f, 0x86, 0x87 },   // 0x46f8c7
	{ 0x38, 0x39, 0x30, 0x31, 0x80, 0x81, 0x88, 0x89, 0x3a, 0x3b, 0x32, 0x33, 0x90, 0x91, 0x98, 0x99, 0x28, 0x29, 0x20, 0x21, 0x82, 0x83, 0x8a, 0x8b, 0x2a, 0x2b, 0x22, 0x23, 0x92, 0x93, 0x9a, 0x9b, 0x18, 0x19, 0x10, 0x11, 0x84, 0x85, 0x8c, 0x8d, 0x1a, 0x1b, 0x12, 0x13, 0x94, 0x95, 0x9c, 0x9d, 0x08, 0x09, 0x00, 0x01, 0x86, 0x87, 0x8e, 0x8f, 0x0a, 0x0b, 0x02, 0x03, 0x96, 0x97, 0x9e, 0x9f },   // 0x46fa3c
	{ 0x00, 0x01, 0x08, 0x09, 0x98, 0x99, 0x90, 0x91, 0x02, 0x03, 0x0a, 0x0b, 0x88, 0x89, 0x80, 0x81, 0x10, 0x11, 0x18, 0x19, 0x9a, 0x9b, 0x92, 0x93, 0x12, 0x13, 0x1a, 0x1b, 0x8a, 0x8b, 0x82, 0x83, 0x20, 0x21, 0x28, 0x29, 0x9c, 0x9d, 0x94, 0x95, 0x22, 0x23, 0x2a, 0x2b, 0x8c, 0x8d, 0x84, 0x85, 0x30, 0x31, 0x38, 0x39, 0x9e, 0x9f, 0x96, 0x97, 0x32, 0x33, 0x3a, 0x3b, 0x8e, 0x8f, 0x86, 0x87 },   // 0x46fbb1
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x02, 0x03, 0x0a, 0x0b, 0x90, 0x91, 0x98, 0x99, 0x10, 0x11, 0x18, 0x19, 0x82, 0x83, 0x8a, 0x8b, 0x12, 0x13, 0x1a, 0x1b, 0x92, 0x93, 0x9a, 0x9b, 0x20, 0x21, 0x28, 0x29, 0x84, 0x85, 0x8c, 0x8d, 0x22, 0x23, 0x2a, 0x2b, 0x94, 0x95, 0x9c, 0x9d, 0x30, 0x31, 0x38, 0x39, 0x86, 0x87, 0x8e, 0x8f, 0x32, 0x33, 0x3a, 0x3b, 0x96, 0x97, 0x9e, 0x9f },   // 0x46fd26
};
static const byte kIXMotionOrder4x4[8][32] = {
	{ 0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b, 0x10, 0x11, 0x12, 0x13, 0x90, 0x91, 0x92, 0x93, 0x18, 0x19, 0x1a, 0x1b, 0x98, 0x99, 0x9a, 0x9b },   // 0x46fe9b
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x80, 0x81, 0x82, 0x83, 0x12, 0x13, 0x88, 0x89, 0x10, 0x11, 0x8a, 0x8b, 0x18, 0x19, 0x1a, 0x1b, 0x90, 0x91, 0x92, 0x93, 0x98, 0x99, 0x9a, 0x9b },   // 0x46feee
	{ 0x00, 0x01, 0x02, 0x03, 0x98, 0x99, 0x9a, 0x9b, 0x08, 0x09, 0x0a, 0x0b, 0x90, 0x91, 0x92, 0x93, 0x10, 0x11, 0x12, 0x13, 0x88, 0x89, 0x8a, 0x8b, 0x18, 0x19, 0x1a, 0x1b, 0x80, 0x81, 0x82, 0x83 },   // 0x46ff59
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x98, 0x99, 0x9a, 0x9b, 0x12, 0x13, 0x90, 0x91, 0x10, 0x11, 0x92, 0x93, 0x18, 0x19, 0x1a, 0x1b, 0x88, 0x89, 0x8a, 0x8b, 0x80, 0x81, 0x82, 0x83 },   // 0x46ffac
	{ 0x18, 0x19, 0x10, 0x11, 0x98, 0x99, 0x90, 0x91, 0x1a, 0x1b, 0x12, 0x13, 0x88, 0x89, 0x80, 0x81, 0x08, 0x09, 0x00, 0x01, 0x9a, 0x9b, 0x92, 0x93, 0x0a, 0x0b, 0x02, 0x03, 0x8a, 0x8b, 0x82, 0x83 },   // 0x470017
	{ 0x18, 0x19, 0x10, 0x11, 0x80, 0x81, 0x88, 0x89, 0x1a, 0x1b, 0x12, 0x13, 0x90, 0x91, 0x98, 0x99, 0x08, 0x09, 0x00, 0x01, 0x82, 0x83, 0x8a, 0x8b, 0x0a, 0x0b, 0x02, 0x03, 0x92, 0x93, 0x9a, 0x9b },   // 0x4700ce
	{ 0x00, 0x01, 0x08, 0x09, 0x98, 0x99, 0x90, 0x91, 0x02, 0x03, 0x0a, 0x0b, 0x88, 0x89, 0x80, 0x81, 0x10, 0x11, 0x18, 0x19, 0x9a, 0x9b, 0x92, 0x93, 0x12, 0x13, 0x1a, 0x1b, 0x8a, 0x8b, 0x82, 0x83 },   // 0x470185
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x02, 0x03, 0x0a, 0x0b, 0x90, 0x91, 0x98, 0x99, 0x10, 0x11, 0x18, 0x19, 0x82, 0x83, 0x8a, 0x8b, 0x12, 0x13, 0x1a, 0x1b, 0x92, 0x93, 0x9a, 0x9b },   // 0x47023c
};
static const byte kIXMotionOrder2x4[8][16] = {
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x10, 0x11, 0x18, 0x19, 0x90, 0x91, 0x98, 0x99 },   // 0x4702f3
	{ 0x00, 0x01, 0x08, 0x09, 0x10, 0x11, 0x18, 0x19, 0x80, 0x81, 0x88, 0x89, 0x90, 0x91, 0x98, 0x99 },   // 0x47031c
	{ 0x00, 0x01, 0x08, 0x09, 0x10, 0x11, 0x98, 0x99, 0x18, 0x19, 0x90, 0x91, 0x88, 0x89, 0x80, 0x81 },   // 0x470351
	{ 0x00, 0x01, 0x08, 0x09, 0x10, 0x11, 0x18, 0x19, 0x98, 0x99, 0x90, 0x91, 0x88, 0x89, 0x80, 0x81 },   // 0x47037a
	{ 0x08, 0x09, 0x00, 0x01, 0x98, 0x99, 0x90, 0x91, 0x0a, 0x0b, 0x02, 0x03, 0x88, 0x89, 0x80, 0x81 },   // 0x4703af
	{ 0x08, 0x09, 0x00, 0x01, 0x80, 0x81, 0x88, 0x89, 0x0a, 0x0b, 0x02, 0x03, 0x90, 0x91, 0x98, 0x99 },   // 0x470407
	{ 0x00, 0x01, 0x08, 0x09, 0x98, 0x99, 0x90, 0x91, 0x02, 0x03, 0x0a, 0x0b, 0x88, 0x89, 0x80, 0x81 },   // 0x47045f
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x02, 0x03, 0x0a, 0x0b, 0x90, 0x91, 0x98, 0x99 },   // 0x4704b7
};
static const byte kIXMotionOrder4x2[8][16] = {
	{ 0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x82, 0x83, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b },   // 0x47050f
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x80, 0x81, 0x82, 0x83, 0x88, 0x89, 0x8a, 0x8b },   // 0x470532
	{ 0x00, 0x01, 0x02, 0x03, 0x88, 0x89, 0x8a, 0x8b, 0x08, 0x09, 0x0a, 0x0b, 0x80, 0x81, 0x82, 0x83 },   // 0x470561
	{ 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0a, 0x0b, 0x88, 0x89, 0x8a, 0x8b, 0x80, 0x81, 0x82, 0x83 },   // 0x470584
	{ 0x18, 0x19, 0x10, 0x11, 0x88, 0x89, 0x80, 0x81, 0x08, 0x09, 0x00, 0x01, 0x8a, 0x8b, 0x82, 0x83 },   // 0x4705b3
	{ 0x18, 0x19, 0x10, 0x11, 0x80, 0x81, 0x88, 0x89, 0x08, 0x09, 0x00, 0x01, 0x82, 0x83, 0x8a, 0x8b },   // 0x47060b
	{ 0x00, 0x01, 0x08, 0x09, 0x88, 0x89, 0x80, 0x81, 0x10, 0x11, 0x18, 0x19, 0x8a, 0x8b, 0x82, 0x83 },   // 0x470663
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89, 0x10, 0x11, 0x18, 0x19, 0x82, 0x83, 0x8a, 0x8b },   // 0x4706bb
};
static const byte kIXMotionOrder2x2[8][8] = {
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89 },   // 0x470713
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89 },   // 0x470724
	{ 0x00, 0x01, 0x08, 0x09, 0x88, 0x89, 0x80, 0x81 },   // 0x47073b
	{ 0x00, 0x01, 0x08, 0x09, 0x88, 0x89, 0x80, 0x81 },   // 0x47074c
	{ 0x08, 0x09, 0x00, 0x01, 0x80, 0x81, 0x88, 0x89 },   // 0x470763
	{ 0x08, 0x09, 0x00, 0x01, 0x80, 0x81, 0x88, 0x89 },   // 0x47078d
	{ 0x00, 0x01, 0x08, 0x09, 0x88, 0x89, 0x80, 0x81 },   // 0x4707b7
	{ 0x00, 0x01, 0x08, 0x09, 0x80, 0x81, 0x88, 0x89 },   // 0x4707e1
};

// The source pixel of destination (x, y) for transform xform of an sx x sy
// block (the same mapping as doMotion).
template<int sx, int sy>
FORCEINLINE static int motionSource(byte xform, int x, int y) {
	switch (xform) {
	case 0: return y * 8 + x;
	case 1: return y * 8 + (sx - 1 - x);
	case 2: return (sy - 1 - y) * 8 + x;
	case 3: return (sy - 1 - y) * 8 + (sx - 1 - x);
	case 4: return (sx - 1 - x) * 8 + (sy - 1 - y);
	case 5: return (sx - 1 - x) * 8 + y;
	case 6: return x * 8 + (sy - 1 - y);
	default: return x * 8 + y;
	}
}

// The IX motion copy in the order of atlantis.exe's routines: when the source
// lies in the image being decoded and overlaps the destination, a pixel takes
// the value it had when the original read it.
template<int sx, int sy, typename PixelType>
static void doMotionOrdered(const byte *order, byte xform, Graphics::Surface &dst,
                            const Graphics::Surface &src, int srx, int sry, int dx, int dy) {
	const byte *srcP = (const byte *)src.getBasePtr(srx, sry);
	byte *dstP = (byte *)dst.getBasePtr(dx, dy);
	const int sPitch = src.pitch, dPitch = dst.pitch;
	PixelType tmp[64];
	for (int i = 0; i < 2 * sx * sy; i++) {
		const byte ev = order[i];
		const int x = ev & 7, y = (ev >> 3) & 7;
		if (ev & 0x80)
			*(PixelType *)(dstP + y * dPitch + x * sizeof(PixelType)) = tmp[motionSource<sx, sy>(xform, x, y)];
		else
			tmp[y * 8 + x] = *(const PixelType *)(srcP + y * sPitch + x * sizeof(PixelType));
	}
}

template<int sx, int sy>
FORCEINLINE static void doMotionIX(byte xform, Graphics::Surface &dst, const Graphics::Surface &src,
                                   int srx, int sry, int dx, int dy) {
	const byte *order;
	if (sx == 8 && sy == 8)
		order = kIXMotionOrder8x8[xform];
	else if (sx == 4 && sy == 8)
		order = kIXMotionOrder4x8[xform];
	else if (sx == 8 && sy == 4)
		order = kIXMotionOrder8x4[xform];
	else if (sx == 4 && sy == 4)
		order = kIXMotionOrder4x4[xform];
	else if (sx == 2 && sy == 4)
		order = kIXMotionOrder2x4[xform];
	else if (sx == 4 && sy == 2)
		order = kIXMotionOrder4x2[xform];
	else
		order = kIXMotionOrder2x2[xform];
	if (dst.format.bytesPerPixel == 2)
		doMotionOrdered<sx, sy, uint16>(order, xform, dst, src, srx, sry, dx, dy);
	else
		doMotionOrdered<sx, sy, uint32>(order, xform, dst, src, srx, sry, dx, dy);
}

/* Classic JPEG quantization tables */
static const int32 LUMA_QUANT_TABLE[] = {
	16,  11,  10,  16,  24,  40,  51,  61,
	12,  12,  14,  19,  26,  58,  60,  55,
	14,  13,  16,  24,  40,  57,  69,  56,
	14,  17,  22,  29,  51,  87,  80,  62,
	18,  22,  37,  56,  68, 109, 103,  77,
	24,  35,  55,  64,  81, 104, 113,  92,
	49,  64,  78,  87, 103, 121, 120, 101,
	72,  92,  95,  98, 112, 100, 103,  99
};

static const int32 CHROMA_QUANT_TABLE[] = {
	17,  18,  24,  47,  99,  99,  99,  99,
	18,  21,  26,  66,  99,  99,  99,  99,
	24,  26,  56,  99,  99,  99,  99,  99,
	47,  66,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99
};

/* Factors for the Arai, Agui & Nakajima IDCT */
static const int32 AAN_FACTORS[] = {
	16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
	22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
	21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
	19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
	16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
	12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
	 8867, 12299, 11585, 10426,  8867,  6967,  4799,  2446,
	 4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

/* Classic JPEG zig-zag encoding table */
static const byte ZIGZAG[] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

/* These masks are used to tell which rows (high order) and columns (low order) have values.
 * This allow to skip some operations in IDCT. */
static const uint16 MASKS[] = {
	0x0101, 0x0102, 0x0201, 0x0401, 0x0202, 0x0104, 0x0108, 0x0204,
	0x0402, 0x0801, 0x1001, 0x0802, 0x0404, 0x0208, 0x0110, 0x0120,
	0x0210, 0x0408, 0x0804, 0x1002, 0x2001, 0x4001, 0x2002, 0x1004,
	0x0808, 0x0410, 0x0220, 0x0140, 0x0180, 0x0240, 0x0420, 0x0810,
	0x1008, 0x2004, 0x4002, 0x8001, 0x8002, 0x4004, 0x2008, 0x1010,
	0x0820, 0x0440, 0x0280, 0x0480, 0x0840, 0x1020, 0x2010, 0x4008,
	0x8004, 0x8008, 0x4010, 0x2020, 0x1040, 0x0880, 0x1080, 0x2040,
	0x4020, 0x8010, 0x8020, 0x4040, 0x2080, 0x4080, 0x8040, 0x8080
};

static const int8 S2[] = {
	 1,  1,  1,  2,  1, -2,  1, -1,
	 2,  1,  2,  2,  2, -2,  2, -1,
	-2,  1, -2,  2, -2, -2, -2, -1,
	-1,  1, -1,  2, -1, -2, -1, -1
};

static const int8 S4[] = {
	 1,  2,  3,  4,  5,  6,  7,  8,
	-8, -7, -6, -5, -4, -3, -2, -1
};

static_assert(ARRAYSIZE(LUMA_QUANT_TABLE) == 64, "invalid luma table size");
static_assert(ARRAYSIZE(CHROMA_QUANT_TABLE) == 64, "invalid chromaa table size");
static_assert(ARRAYSIZE(AAN_FACTORS) == 64, "invalid AAN factors table size");
static_assert(ARRAYSIZE(ZIGZAG) == 64, "invalid zigzag table size");
static_assert(ARRAYSIZE(S2) == 16 * 2, "invalid S2 table size");
static_assert(ARRAYSIZE(S4) == 16, "invalid S4 table size");

static const int HEADER_SIZE = 6 * sizeof(uint32);

/**
 * HNM6 image decoder interface.
 *
 * Used by HNM6 image and video format.
 */
class DecoderImpl : public HNM6Decoder {
public:
	DecoderImpl(uint16 width, uint16 height, const Graphics::PixelFormat &format,
	            uint32 bufferSize, bool videoMode = false);
	~DecoderImpl() override;

	const Graphics::Surface *decodeFrame(Common::SeekableReadStream &stream) override;

private:
	uint32 _bufferSize;
	byte *_buffer;
	Graphics::Surface _surface;
	Graphics::Surface _surfaceOld;
	// Video mode: both surfaces in one zeroed block, with kBlockPadRows rows
	// before and after it (the farthest motion source is 127 rows away).
	static const uint kBlockPadRows = 136;
	byte *_block = nullptr;

	bool _keyframe;
	HNM6::BitBuffer _bitbuf;
	HNM6::MotionBuffer _motion;
	HNM6::ShortMotionBuffer _shortmo;
	HNM6::JPEGBuffer _jpeg;

	int32 coeffs[3 * 64];
	int32 luma_quant_table[64];
	int32 chroma_quant_table[64];

	// Simple functions and called at one place only
	FORCEINLINE void reset(byte *buffer, uint32 bit_start, uint32 motion_start,
	                       uint32 shortmotion_start, uint32 jpeg_start, uint32 end,
	                       int32 quality);
	// Just a wrapper
	FORCEINLINE void decode(Graphics::Surface &current, Graphics::Surface &old);
	// Just a loop wrapper
	FORCEINLINE void decodeIWkf(Graphics::Surface &current);
	void decodeIWkf88(Graphics::Surface &current, uint x, uint y);
	void decodeIWkf44(Graphics::Surface &current, uint x, uint y);
	// Just a loop wrapper
	FORCEINLINE void decodeIXkf(Graphics::Surface &current);
	inline void decodeIXkf88(Graphics::Surface &current, uint x, uint y);
	// Simple functions and called at one place only
	FORCEINLINE void decodeIXkf48(Graphics::Surface &current, uint x, uint y);
	FORCEINLINE void decodeIXkf84(Graphics::Surface &current, uint x, uint y);
	void decodeIXkf44(Graphics::Surface &current, uint x, uint y);
	// Simple functions and called at one place only
	FORCEINLINE void decodeIXkf24(Graphics::Surface &current, uint x, uint y);
	FORCEINLINE void decodeIXkf42(Graphics::Surface &current, uint x, uint y);
	// Just a loop wrapper
	FORCEINLINE void decodeIXif(Graphics::Surface &current, Graphics::Surface &previous);
	void decodeIXif88(Graphics::Surface &current, Graphics::Surface &previous, uint x, uint y);
	// Simple functions and called at one place only
	FORCEINLINE void decodeIXif48(Graphics::Surface &current, Graphics::Surface &previous,
	                              uint x, uint y);
	FORCEINLINE void decodeIXif84(Graphics::Surface &current, Graphics::Surface &previous,
	                              uint x, uint y);
	void decodeIXif44(Graphics::Surface &current, Graphics::Surface &previous, uint x, uint y);
	// Simple functions and called at one place only
	FORCEINLINE void decodeIXif24(Graphics::Surface &current, Graphics::Surface &previous,
	                              uint x, uint y);
	FORCEINLINE void decodeIXif42(Graphics::Surface &current, Graphics::Surface &previous,
	                              uint x, uint y);
	void decodeIXif22(Graphics::Surface &current, Graphics::Surface &previous, uint x, uint y);
	// Simple function and called at 3 places only
	FORCEINLINE void renderKeyblock(Graphics::Surface &current, uint x, uint y);
	void renderPlane(int32 *dst, int32 *quant_table);
	// Time-critical parsing functions
	template<int sx, int sy>
	FORCEINLINE void renderIWmotion(Graphics::Surface &current, uint x, uint y);
	FORCEINLINE void renderIWshortmo(Graphics::Surface &current, uint x, uint y);
	template<int sx, int sy, bool small = false>
	FORCEINLINE void renderIXkfMotion(Graphics::Surface &current, uint x, uint y);
	template<int sx, int sy>
	FORCEINLINE void renderSkip(Graphics::Surface &current, Graphics::Surface &previous,
	                            uint x, uint y);
	template<int sx, int sy, bool small = false>
	FORCEINLINE void renderIXifMotion(Graphics::Surface &current, Graphics::Surface &previous,
	                                  uint x, uint y);
	template<int sx, int sy>
	FORCEINLINE void renderIXifShortmo(Graphics::Surface &current, Graphics::Surface &previous,
	                                   uint x, uint y);
};

void DecoderImpl::reset(byte *buffer, uint32 bit_start, uint32 motion_start,
                        uint32 shortmotion_start, uint32 jpeg_start, uint32 end,
                        int32 quality) {
	_bitbuf.reset(buffer + bit_start, motion_start - bit_start);
	_motion.reset(buffer + motion_start, shortmotion_start - motion_start);
	_shortmo.reset(buffer + shortmotion_start, jpeg_start - shortmotion_start);
	_jpeg.reset(buffer + jpeg_start, end - jpeg_start);

	assert(!_warpMode || quality > 0);
	_keyframe = quality < 0;

	quality = ABS(quality);
	quality = CLIP<int32>(quality, 1, 100);

	uint32 qf;
	if (quality >= 50) {
		qf = 200 - 2 * quality;
	} else {
		qf = 5000 / quality;
	}

	for (uint i = 0; i < 64; i++) {
		luma_quant_table[i] = (CLIP<int32>(
		                           (LUMA_QUANT_TABLE[ZIGZAG[i]] * qf + 50) / 100,
		                           8, 255) *
		                       AAN_FACTORS[ZIGZAG[i]]) >> 13;
	}
	for (uint i = 0; i < 64; i++) {
		chroma_quant_table[i] = (CLIP<int32>(
		                             (CHROMA_QUANT_TABLE[ZIGZAG[i]] * qf + 50) / 100,
		                             8, 255) *
		                         AAN_FACTORS[ZIGZAG[i]]) >> 13;
	}
}

void DecoderImpl::decode(Graphics::Surface &current, Graphics::Surface &old) {
	assert((current.w & 0x7) == 0 && (current.h & 0x7) == 0);
	if (_warpMode) {
		decodeIWkf(current);
	} else if (_keyframe) {
		decodeIXkf(current);
	} else {
		assert((old.w & 0x7) == 0 && (old.h & 0x7) == 0);
		assert(old.getPixels() != nullptr);
		decodeIXif(current, old);
	}
}

void DecoderImpl::decodeIWkf(Graphics::Surface &current) {
	// WARP decoder
	for (int16 y = 0; y < current.h; y += 8) {
		for (int16 x = 0; x < current.w; x += 8) {
			decodeIWkf88(current, x, y);
		}
	}
}

void DecoderImpl::decodeIWkf88(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11 : Keyblock
			renderKeyblock(current, x, y);
		} else {   // 10 : Motion
			renderIWmotion<8, 8>(current, x, y);
		}
	} else {   // 0 : Cross-cut
		decodeIWkf44(current, x + 0, y + 0);
		decodeIWkf44(current, x + 4, y + 0);
		decodeIWkf44(current, x + 0, y + 4);
		decodeIWkf44(current, x + 4, y + 4);
	}
}

void DecoderImpl::decodeIWkf44(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1 : Double cross-cut
		renderIWshortmo(current, x + 0, y + 0);
		renderIWshortmo(current, x + 2, y + 0);
		renderIWshortmo(current, x + 0, y + 2);
		renderIWshortmo(current, x + 2, y + 2);
	} else {   // 0 : Motion
		renderIWmotion<4, 4>(current, x, y);
	}
}

void DecoderImpl::decodeIXkf(Graphics::Surface &current) {
	// Standard decoder for keyframes
	for (int16 y = 0; y < current.h; y += 8) {
		for (int16 x = 0; x < current.w; x += 8) {
			decodeIXkf88(current, x, y);
		}
	}
}

void DecoderImpl::decodeIXkf88(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11
			bit = _bitbuf.next();
			if (bit) { // 111: Motion
				renderIXkfMotion<8, 8>(current, x, y);
			} else {   // 110: Keyblock
				renderKeyblock(current, x, y);
			}
		} else {   // 10: Cross-cut
			decodeIXkf44(current, x + 0, y + 0);
			decodeIXkf44(current, x + 4, y + 0);
			decodeIXkf44(current, x + 0, y + 4);
			decodeIXkf44(current, x + 4, y + 4);
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01: V-cut
			decodeIXkf48(current, x + 0, y + 0);
			decodeIXkf48(current, x + 4, y + 0);
		} else {   // 00: H-cut
			decodeIXkf84(current, x + 0, y + 0);
			decodeIXkf84(current, x + 0, y + 4);
		}
	}
}

void DecoderImpl::decodeIXkf48(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1: Motion
		renderIXkfMotion<4, 8>(current, x, y);
	} else {   // 0: Cross-cut
		decodeIXkf44(current, x + 0, y + 0);
		decodeIXkf44(current, x + 0, y + 4);
	}
}

void DecoderImpl::decodeIXkf84(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1: Motion
		renderIXkfMotion<8, 4>(current, x, y);
	} else {   // 0: Cross-cut
		decodeIXkf44(current, x + 0, y + 0);
		decodeIXkf44(current, x + 4, y + 0);
	}
}

void DecoderImpl::decodeIXkf44(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11: Double cross-cut
			// 2x2 blocks are always motion small
			renderIXkfMotion<2, 2, true>(current, x + 0, y + 0);
			renderIXkfMotion<2, 2, true>(current, x + 2, y + 0);
			renderIXkfMotion<2, 2, true>(current, x + 0, y + 2);
			renderIXkfMotion<2, 2, true>(current, x + 2, y + 2);
		} else {   // 10: DV-cut
			decodeIXkf24(current, x + 0, y + 0);
			decodeIXkf24(current, x + 2, y + 0);
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01: DH-cut
			decodeIXkf42(current, x + 0, y + 0);
			decodeIXkf42(current, x + 0, y + 2);
		} else {   // 00: Motion
			renderIXkfMotion<4, 4>(current, x, y);
		}
	}
}

void DecoderImpl::decodeIXkf24(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1: Double cross-cut
		// 2x2 blocks are always motion small
		renderIXkfMotion<2, 2, true>(current, x + 0, y + 0);
		renderIXkfMotion<2, 2, true>(current, x + 0, y + 2);
	} else {   // 0: Motion small
		renderIXkfMotion<2, 4, true>(current, x, y);
	}
}

void DecoderImpl::decodeIXkf42(Graphics::Surface &current, uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1: Double cross-cut
		// 2x2 blocks are always motion small
		renderIXkfMotion<2, 2, true>(current, x + 0, y + 0);
		renderIXkfMotion<2, 2, true>(current, x + 2, y + 0);
	} else {   // 0: Motion small
		renderIXkfMotion<4, 2, true>(current, x, y);
	}
}

void DecoderImpl::decodeIXif(Graphics::Surface &current, Graphics::Surface &previous) {
	// Standard decoder for keyframes
	for (int16 y = 0; y < current.h; y += 8) {
		for (int16 x = 0; x < current.w; x += 8) {
			decodeIXif88(current, previous, x, y);
		}
	}
}

void DecoderImpl::decodeIXif88(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11
			bit = _bitbuf.next();
			if (bit) { // 111
				bit = _bitbuf.next();
				if (bit) { // 1111: Unknown
					assert(false);
				} else {   // 1110: Cross-cut
					decodeIXif44(current, previous, x + 0, y + 0);
					decodeIXif44(current, previous, x + 4, y + 0);
					decodeIXif44(current, previous, x + 0, y + 4);
					decodeIXif44(current, previous, x + 4, y + 4);
				}
			} else {   // 110: Skip
				renderSkip<8, 8>(current, previous, x, y);
			}
		} else {   // 10
			bit = _bitbuf.next();
			if (bit) { // 101: V-cut
				decodeIXif48(current, previous, x + 0, y + 0);
				decodeIXif48(current, previous, x + 4, y + 0);
			} else {   // 100: H-cut
				decodeIXif84(current, previous, x + 0, y + 0);
				decodeIXif84(current, previous, x + 0, y + 4);
			}
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01
			bit = _bitbuf.next();
			if (bit) { // 011: Keyblock
				renderKeyblock(current, x, y);
			} else {   // 010: Motion
				renderIXifMotion<8, 8>(current, previous, x, y);
			}
		} else {   // 00: Short motion
			renderIXifShortmo<8, 8>(current, previous, x, y);
		}
	}
}

void DecoderImpl::decodeIXif48(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11: Skip
			renderSkip<4, 8>(current, previous, x, y);
		} else {   // 10: Cross-cut
			decodeIXif44(current, previous, x + 0, y + 0);
			decodeIXif44(current, previous, x + 0, y + 4);
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01: Motion
			renderIXifMotion<4, 8>(current, previous, x, y);
		} else {   // 00: Short motion
			renderIXifShortmo<4, 8>(current, previous, x, y);
		}
	}
}

void DecoderImpl::decodeIXif84(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11: Skip
			renderSkip<8, 4>(current, previous, x, y);
		} else {   // 10: Cross-cut
			decodeIXif44(current, previous, x + 0, y + 0);
			decodeIXif44(current, previous, x + 4, y + 0);
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01: Motion
			renderIXifMotion<8, 4>(current, previous, x, y);
		} else {   // 00: Short motion
			renderIXifShortmo<8, 4>(current, previous, x, y);
		}
	}
}

void DecoderImpl::decodeIXif44(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11
			bit = _bitbuf.next();
			if (bit) { // 111: Double cross-cut
				decodeIXif22(current, previous, x + 0, y + 0);
				decodeIXif22(current, previous, x + 2, y + 0);
				decodeIXif22(current, previous, x + 0, y + 2);
				decodeIXif22(current, previous, x + 2, y + 2);
			} else {   // 110: DV-cut
				decodeIXif24(current, previous, x + 0, y + 0);
				decodeIXif24(current, previous, x + 2, y + 0);
			}
		} else {   // 10
			bit = _bitbuf.next();
			if (bit) { // 101: DH-cut
				decodeIXif42(current, previous, x + 0, y + 0);
				decodeIXif42(current, previous, x + 0, y + 2);
			} else {   // 100: Skip
				renderSkip<4, 4>(current, previous, x, y);
			}
		}
	} else {   // 0
		bit = _bitbuf.next();
		if (bit) { // 01: Motion
			renderIXifMotion<4, 4>(current, previous, x, y);
		} else {   // 00: Short motion
			renderIXifShortmo<4, 4>(current, previous, x, y);
		}
	}
}

void DecoderImpl::decodeIXif24(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11
			bit = _bitbuf.next();
			if (bit) { // 111: Double cross-cut
				decodeIXif22(current, previous, x + 0, y + 0);
				decodeIXif22(current, previous, x + 0, y + 2);
			} else {   // 110: Skip
				renderSkip<2, 4>(current, previous, x, y);
			}
		} else {   // 10: Motion small
			renderIXifMotion<2, 4, true>(current, previous, x, y);
		}
	} else {   // 0: Short motion
		renderIXifShortmo<2, 4>(current, previous, x, y);
	}
}

void DecoderImpl::decodeIXif42(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11
			bit = _bitbuf.next();
			if (bit) { // 111: Double cross-cut
				decodeIXif22(current, previous, x + 0, y + 0);
				decodeIXif22(current, previous, x + 2, y + 0);
			} else {   // 110: Skip
				renderSkip<4, 2>(current, previous, x, y);
			}
		} else {   // 10: Motion small
			renderIXifMotion<4, 2, true>(current, previous, x, y);
		}
	} else {   // 0: Short motion
		renderIXifShortmo<4, 2>(current, previous, x, y);
	}
}

void DecoderImpl::decodeIXif22(Graphics::Surface &current, Graphics::Surface &previous,
                               uint x, uint y) {
	byte bit = _bitbuf.next();
	if (bit) { // 1
		bit = _bitbuf.next();
		if (bit) { // 11: Skip
			renderSkip<2, 2>(current, previous, x, y);
		} else {   // 10: Motion small
			renderIXifMotion<2, 2, true>(current, previous, x, y);
		}
	} else {   // 0: Short motion
		renderIXifShortmo<2, 2>(current, previous, x, y);
	}
}

void DecoderImpl::renderKeyblock(Graphics::Surface &current, uint x, uint y) {
	renderPlane(coeffs, luma_quant_table);
	renderPlane(coeffs + 64, chroma_quant_table);
	renderPlane(coeffs + 128, chroma_quant_table);
	HNM6::YUVtoRGB(current, x, y, coeffs, _bitExact);
}

void DecoderImpl::renderPlane(int32 *dst, int32 *quant_table) {
	uint i = 0;
	// This mask is used to optimize the IDCT
	uint16 colrow_mask = 0;
	int8 b;
	memset(dst, 0, 64 * sizeof(*dst));

#define WRITE_COEFF_0(y) do { i += y; } while(0)
#define WRITE_COEFF(x) do { \
    dst[ZIGZAG[i]] = (x) * quant_table[i]; \
    colrow_mask |= MASKS[i]; \
    i++; \
} while(0)

	b = _jpeg.next() << 4;
	b |= _jpeg.next();
	WRITE_COEFF(b);
	while (i < 64) {
		b = _jpeg.next();
		switch (b) {
		case 0: // Finish with 0
			i = 64;
			break;
		case 1:
			WRITE_COEFF_0(5);
			break;
		case 2:
			WRITE_COEFF_0(1);
			WRITE_COEFF(1);
			break;
		case 3:
			WRITE_COEFF_0(1);
			WRITE_COEFF(-1);
			break;
		case 4:
			WRITE_COEFF_0(2);
			WRITE_COEFF(1);
			break;
		case 5:
			WRITE_COEFF_0(2);
			WRITE_COEFF(-1);
			break;
		case 6:
			WRITE_COEFF_0(3);
			WRITE_COEFF(1);
			break;
		case 7:
			WRITE_COEFF_0(3);
			WRITE_COEFF(-1);
			break;
		case 8: {
			b = _jpeg.next();
			byte z = (b >> 2) & 0x3;
			byte t = b & 0x3;
			z++;
			t += 2;
			WRITE_COEFF_0(z);
			for (; t > 1; t -= 2) {
				b = _jpeg.next();
				const int8 *p = S2 + 2 * b;
				WRITE_COEFF(p[0]);
				WRITE_COEFF(p[1]);
			}
			if (t) {
				b = _jpeg.next();
				const int8 *p = S2 + 2 * b;
				WRITE_COEFF(p[0]);
				t--;
			}
			break;
		}
		case 9: {
			b = _jpeg.next();
			byte z = (b >> 2) & 0x3;
			byte t = b & 0x3;
			z++;
			t++;
			WRITE_COEFF_0(z);
			for (; t > 0; t--) {
				b = _jpeg.next();
				WRITE_COEFF(S4[b]);
			}
			break;
		}
		case 10:
			b = _jpeg.next();
			WRITE_COEFF(S2[2 * b]);
			WRITE_COEFF(S2[2 * b + 1]);
			break;
		case 13:
			b = _jpeg.next();
			WRITE_COEFF(S4[b]);
			// fall through
		case 12:
			b = _jpeg.next();
			WRITE_COEFF(S4[b]);
			// fall through
		case 11:
			b = _jpeg.next();
			WRITE_COEFF(S4[b]);
			break;
		case 14:
			WRITE_COEFF_0(1);
			break;
		case 15:
			b = _jpeg.next() << 4;
			b |= _jpeg.next();
			WRITE_COEFF(b);
			break;
		default:
			error("BUG: JPEG control word out of range");
			break;
		}
	}
#undef WRITE_COEFF_0
#undef WRITE_COEFF

	HNM6::aanidct(dst, colrow_mask, _bitExact);
}

// These macros are used in motion rendering
// Wrap the x coordinate around the line
#define WRAP_LINE(x, w) do { \
    if (x < 0) { \
        x += w; \
    } else if (x >= w) { \
        x -= w; \
    } \
} while(0)
// Convert a coordinate to its block (8x8) one
#define BALIGN(v) ((v) & ~0x7)

template<int sx, int sy>
void DecoderImpl::renderIWmotion(Graphics::Surface &current, uint x, uint y) {
	uint16 moword = _motion.next();
	byte xform = _bitbuf.next() << 2;
	xform |= _bitbuf.next() << 1;
	xform |= moword >> 15;

	int offy = (sx == 8) ? 0 : 4;
	int srx = BALIGN(x) + 128 - ((moword >> 7) & 0xFF);
	int sry = BALIGN(y) + offy - (moword & 0x7F);
	WRAP_LINE(srx, current.w);
	HNM6::doMotion<sx, sy>(xform, current, current, srx, sry, x, y);
}

void DecoderImpl::renderIWshortmo(Graphics::Surface &current, uint x, uint y) {
	uint16 moword = _shortmo.next();
	byte xform = (moword >> 1) & 7;

	uint16 index = (moword & 0x1) << 8 | (moword & 0xF0) | (moword >> 8);

	int xmotion, ymotion;
	if (index < 12 * 8) {
		xmotion = -2 - index % 12;
		ymotion = 6 - index / 12;
	} else {
		index -= 12 * 8;
		xmotion = 19 - index % 32;
		ymotion = -2 - index / 32;
		if (ymotion <= -8) {
			ymotion--;
		}
	}

	// If srx goes beyond the line it will end up on previous or next line
	// This is expected.
	// For this to work, lines must be contiguous.
	int srx = BALIGN(x) + xmotion;
	int sry = BALIGN(y) + ymotion;

	HNM6::doMotion<2, 2>(xform, current, current, srx, sry, x, y);
}

template<int sx, int sy, bool small>
void DecoderImpl::renderIXkfMotion(Graphics::Surface &current, uint x, uint y) {
	uint16 moword = _motion.next();

	byte xform;
	int srx, sry;

	if (small) {
		xform = (moword >> 12) & 0x7;
		srx = BALIGN(x) + 63 - (moword & 0x7F);
		sry = BALIGN(y) + 6 - ((moword >> 7) & 0x1F);
		// If srx goes beyond the line it will end up on previous or next line
		// This is expected.
		// For this to work, lines must be contiguous.
	} else {
		xform = _bitbuf.next() << 2;
		xform |= _bitbuf.next() << 1;
		xform |= moword >> 15;

		int offy = (sx == 8 && sy == 8) ? 0 : 4;
		srx = BALIGN(x) + 128 - ((moword >> 7) & 0xFF);
		sry = BALIGN(y) + offy - (moword & 0x7F);
		WRAP_LINE(srx, current.w);
	}

	HNM6::doMotionIX<sx, sy>(xform, current, current, srx, sry, x, y);
}

template<int sx, int sy>
void DecoderImpl::renderSkip(Graphics::Surface &current, Graphics::Surface &previous,
                             uint x, uint y) {
	// Use already coded copy transform
	doMotion<sx, sy>(0, current, previous, x, y, x, y);
}

template<int sx, int sy, bool small>
void DecoderImpl::renderIXifMotion(Graphics::Surface &current, Graphics::Surface &previous,
                                   uint x, uint y) {
	uint16 moword = _motion.next();

	byte xform;
	int srx, sry;

	bool use_current = (moword & 0x8000) != 0;

	if (small) {
		xform = (moword >> 12) & 0x7;
		if (use_current) {
			srx = BALIGN(x) + 63 - (moword & 0x7F);
			sry = BALIGN(y) + 6 - ((moword >> 7) & 0x1F);
		} else {
			srx = BALIGN(x) + 31 - (moword & 0x3F);
			sry = BALIGN(y) + 31 - ((moword >> 6) & 0x3F);
		}
		// If srx goes beyond the line it will end up on previous or next line
		// This is expected.
		// For this to work, lines must be contiguous.
	} else {
		xform = _bitbuf.next() << 2;
		xform |= _bitbuf.next() << 1;
		xform |= _bitbuf.next();

		int offy = use_current ? ((sx == 8 && sy == 8) ? 0 : 4) : 64;
		srx = BALIGN(x) + 128 - ((moword >> 7) & 0xFF);
		sry = BALIGN(y) + offy - (moword & 0x7F);
		WRAP_LINE(srx, current.w);
	}

	HNM6::doMotionIX<sx, sy>(xform, current, use_current ? current : previous, srx, sry, x, y);
}

template<int sx, int sy>
void DecoderImpl::renderIXifShortmo(Graphics::Surface &current, Graphics::Surface &previous,
                                    uint x, uint y) {
	uint16 somoword = _shortmo.next();

	int ox, oy;
	if (somoword == 0) {
		ox = 1;
		oy = 0;
	} else {
		somoword--;
		int span = 1;
		while (somoword >= 8 * span) {
			somoword -= 8 * span;
			span++;
		}
		int baseX = -span + 1;
		int baseY = -span + 1;

		int side_sz = (span * 8) / 4;
		byte side = somoword / side_sz;
		int side_pos = somoword % side_sz;

		int offX, offY;
		switch (side) {
		case 0:
			offX = 0;
			offY = side_pos;
			break;
		case 1:
			offX = side_pos + 1;
			offY = side_sz - 1;
			break;
		case 2:
			offX = side_sz;
			offY = side_sz - 1 - side_pos - 1;
			break;
		case 3:
			offX = side_sz - 1 - side_pos;
			offY = -1;
			break;
		default:
			error("BUG: Invalid side in short motion");
		}
		ox = baseX + offX;
		oy = baseY + offY;
	}

	// This is NOT aligned on block coordinates
	// If srx goes beyond the line it will end up on previous or next line
	// This is expected.
	// For this to work, lines must be contiguous.
	int srx = x + ox;
	int sry = y + oy;

	// Use already coded copy transform
	HNM6::doMotion<sx, sy>(0, current, previous, srx, sry, x, y);
}

#undef WRAP_LINE
#undef BALIGN

DecoderImpl::DecoderImpl(uint16 width, uint16 height, const Graphics::PixelFormat &format,
                         uint32 bufferSize, bool videoMode) :
	HNM6Decoder(width, height, format, videoMode), _bufferSize(bufferSize), _keyframe(false) {
	if (format.bytesPerPixel != 2 && format.bytesPerPixel != 4) {
		error("Unsupported bpp");
	}

	if (bufferSize < HEADER_SIZE) {
		error("Invalid buffer size");
	}
	_buffer = new byte[bufferSize];
	_surface.create(_width, _height, _format);
	if (videoMode) {
		_surfaceOld.create(_width, _height, _format);
	}
}

DecoderImpl::~DecoderImpl() {
	delete [] _buffer;
	if (_block) {
		delete [] _block;
	} else {
		_surface.free();
		_surfaceOld.free();
	}
}

const Graphics::Surface *DecoderImpl::decodeFrame(Common::SeekableReadStream &stream) {
	// Switch both surfaces
	void *oldPixels = _surfaceOld.getPixels();
	if (oldPixels) {
		_surfaceOld.setPixels(_surface.getPixels());
		_surface.setPixels(oldPixels);
	}

	if (stream.read(_buffer, HEADER_SIZE) != HEADER_SIZE) {
		error("Invalid header");
	}

	int32 quality = READ_LE_UINT32(_buffer + 0 * 4);
	uint32 bit_start = READ_LE_UINT32(_buffer + 1 * 4);
	uint32 motion_start = READ_LE_UINT32(_buffer + 2 * 4);
	uint32 shortmotion_start = READ_LE_UINT32(_buffer + 3 * 4);
	uint32 jpeg_start = READ_LE_UINT32(_buffer + 4 * 4);
	uint32 end = READ_LE_UINT32(_buffer + 5 * 4);

	if (bit_start > end ||
	    motion_start > end ||
	    shortmotion_start > end ||
	    jpeg_start > end ||
	    end > _bufferSize) {
		error("Invalid header offsets");
	}

	if (stream.read(_buffer + HEADER_SIZE, end - HEADER_SIZE) != end - HEADER_SIZE) {
		error("Invalid chunk length");
	}

	reset(_buffer, bit_start, motion_start,
	      shortmotion_start, jpeg_start, end, quality);
	decode(_surface, _surfaceOld);

	return &_surface;
}

} // End of namespace HNM6

HNM6Decoder *createHNM6Decoder(uint16 width, uint16 height, const Graphics::PixelFormat &format,
                               uint32 bufferSize, bool videoMode) {
	return new HNM6::DecoderImpl(width, height, format, bufferSize, videoMode);
}

} // End of namespace Image
