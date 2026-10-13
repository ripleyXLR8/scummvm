// Audit "graphics", element 1: sprite decoders and edge blending.
//
// Every expected value here was produced by the ORIGINAL code of
// atlantis-cd-en-714240 run unmodified under Unicorn:
//   atlantis-re/tools/emu/graphics_sprites.py  (tables / sweep / synthetic / spr / spw / spf)
// Original functions: FUN_0042b930 + FUN_00452ac8 (blend tables),
// FUN_00410782 (SPR colour table RGB555 -> RGB565), FUN_00410430/FUN_0041047f
// (SPR blit), FUN_00452bde (SPW/SPF frame draw).
//
// Port code exercised directly: blendSprPixel565, blendSpwPixel565
// (sprite_blend.h), decodeSprRows, drawSprSprite, compositeSpfFrame /
// decodeSPWPixels (sprite_decode.cpp) and BigFileArchive (bigfile.cpp).
//
// FAITHFUL COPY written by hand (stated explicitly, as the brief requires):
//  - portLoadSpr(): copy of the header parsing shared by loadFontSpriteSet /
//    loadObjectSprites / loadSubjectSprites (flags, RGB555->RGB565 table,
//    count, offsets, per-sprite header, w/h <= 640x480 guard), which are
//    engine methods bound to openBigFileStream().

#include "atlantis_test_helper.h"

#include "common/array.h"
#include "common/fs.h"
#include "common/archive.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/atlantis/sprite_blend.h"
#include "cryomni3d/atlantis/sprite_decode.h"

#include "../../../system/null_osystem.h"

namespace GraphicsSpritesTest {

typedef CryOmni3D::Atlantis::SprSprite SubjSprite;

static const int kFbW = 640, kFbH = 480;
static const int kCycW = 2048, kCycH = 768;

// Background used by graphics_sprites.py: pixel i = (i * 2654435761) >> 16.
inline void fillBackground(uint16 *p, uint32 n) {
	for (uint32 i = 0; i < n; i++)
		p[i] = (uint16)((i * 2654435761U) >> 16);
}

inline Common::String md5Of(const uint16 *p, uint32 n) {
	// Little-endian byte image, as dumped from the emulator.
	Common::Array<byte> b(n * 2);
	for (uint32 i = 0; i < n; i++) {
		b[2 * i] = p[i] & 0xff;
		b[2 * i + 1] = p[i] >> 8;
	}
	return AtlantisTest::md5Hex(b.begin(), n * 2);
}

// FAITHFUL COPY of the port's SPR loader header parsing (see file comment).
inline void portLoadSpr(Common::SeekableReadStream &s, Common::Array<SubjSprite> &out) {
	out.clear();
	uint16 flags = s.readUint16LE();
	uint16 colorTable[256];
	memset(colorTable, 0, sizeof(colorTable));
	if (flags & 0x4) {
		for (int i = 0; i < 256; i++) {
			uint16 rgb555 = s.readUint16LE();
			uint16 r = (rgb555 >> 10) & 0x1f;
			uint16 g = (rgb555 >> 5) & 0x1f;
			uint16 b = rgb555 & 0x1f;
			colorTable[i] = (r << 11) | (g << 6) | b;
		}
	}
	if (flags & 0x8)
		s.skip(256 * 2);
	uint16 count = s.readUint16LE();
	if (count == 0 || count > 256)
		return;
	const int64 base = s.pos();
	Common::Array<uint32> offsets(count);
	for (uint16 i = 0; i < count; i++)
		offsets[i] = s.readUint32LE();
	out.resize(count);
	for (uint16 si = 0; si < count; si++) {
		if (!s.seek(base + offsets[si]))
			continue;
		SubjSprite &spr = out[si];
		spr.w = s.readUint16LE();
		spr.h = s.readUint16LE();
		spr.xoff = s.readSint16LE();
		spr.yoff = s.readSint16LE();
		if (spr.w == 0 || spr.h == 0 || spr.w > 640 || spr.h > 480)
			continue;
		CryOmni3D::Atlantis::decodeSprRows(s, spr, colorTable);
	}
}

// The port's blit (drawSprSprite, used by blitMenuSprite / drawSprite) on a
// 640x480 RGB565 buffer.
inline void portBlit(uint16 *fb, const SubjSprite &s, int hx, int hy) {
	Graphics::Surface surf;
	surf.init(kFbW, kFbH, kFbW * 2, fb, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	CryOmni3D::Atlantis::drawSprSprite(surf, s, hx, hy);
}

// Synthetic SPR of graphics_sprites.py (SYN_*): 8x3, hotspot (2,1).
inline Common::Array<byte> syntheticSpr() {
	static const uint16 table[6] = {0x0000, 0x0001, 0x7fff, 0x03e0, 0x001f, 0x8001};
	static const byte row0[] = {0x42, 1, 2, 0x02, 0x84, 16, 3, 31, 4, 0, 2, 8, 1};
	static const byte row1[] = {0x48, 0, 1, 2, 3, 4, 5, 1, 2};
	static const byte row2[] = {0x08};
	Common::Array<byte> d;
	struct W {
		static void u16(Common::Array<byte> &a, uint16 v) { a.push_back(v & 0xff); a.push_back(v >> 8); }
		static void u32(Common::Array<byte> &a, uint32 v) { u16(a, v & 0xffff); u16(a, v >> 16); }
	};
	W::u16(d, 4);
	for (int i = 0; i < 256; i++)
		W::u16(d, i < 6 ? table[i] : 0);
	W::u16(d, 1);
	W::u32(d, 4);
	W::u16(d, 8); W::u16(d, 3); W::u16(d, 2); W::u16(d, 1);
	W::u32(d, 0); W::u32(d, sizeof(row0)); W::u32(d, sizeof(row0) + sizeof(row1));
	for (uint i = 0; i < sizeof(row0); i++) d.push_back(row0[i]);
	for (uint i = 0; i < sizeof(row1); i++) d.push_back(row1[i]);
	for (uint i = 0; i < sizeof(row2); i++) d.push_back(row2[i]);
	return d;
}

// Synthetic SPW file (header + one format-9 frame) of graphics_sprites.py.
inline Common::Array<byte> syntheticSpw() {
	Common::Array<byte> f(0x18, 0);
	static const uint16 pal[6] = {0x0000, 0x0001, 0xffff, 0x07e0, 0xf800, 0x1234};
	f[0x10] = 9;
	f[0x14] = 0x18 + 12;
	for (int i = 0; i < 6; i++) { f.push_back(pal[i] & 0xff); f.push_back(pal[i] >> 8); }
	static const byte stream[] = {
		0x85, 0x82,                   // 0x8000|645 skip
		0x03, 0x00, 2, 3, 5,          // draw 3
		0x04, 0x40, 4, 16, 2, 31, 1, 0, 5, 8,   // blend 4 (index, factor)
		0x01, 0xC0, 0x02, 0x00,       // long skip 65538
		0x02, 0x00, 4, 3,             // draw 2
		0x00, 0x80                    // end
	};
	for (uint i = 0; i < sizeof(stream); i++) f.push_back(stream[i]);
	// File header: size, 1 frame, format 9, frame offset at 0x14 -> 0x18.
	Common::Array<byte> d(0x18, 0);
	uint32 size = 0x18 + f.size();
	d[0] = size & 0xff; d[1] = (size >> 8) & 0xff;
	d[4] = 1; d[8] = 9; d[0x14] = 0x18;
	for (uint i = 0; i < f.size(); i++) d.push_back(f[i]);
	return d;
}

// --- game data access -------------------------------------------------------
// Port-independent BIGCD1..4.BIG reader (same layout as
// atlantis-re/tools/emu/graphics_big.py; first disc wins on duplicates).
class Big {
public:
	struct Entry { int cd; uint32 offset, size; };
	Big() {
		for (int cd = 1; cd <= 4; cd++) {
			Common::File f;
			if (!f.open(node(cd)))
				continue;
			f.seek(16);
			uint32 n = f.readUint32LE();
			f.seek(24);
			uint32 base = f.readUint32LE();
			f.seek(32);
			uint32 seq = 0;
			for (uint32 i = 0; i < n; i++) {
				uint32 rec[8];
				for (int k = 0; k < 8; k++)
					rec[k] = f.readUint32LE();
				Common::String name;
				for (uint32 k = 0; k < rec[0]; k++) {
					char c = (char)f.readByte();
					if (c)
						name += c;
				}
				name.toUppercase();
				if (!_entries.contains(name)) {
					Entry e = { cd, base + seq, rec[2] };
					_entries[name] = e;
				}
				seq += rec[2];
			}
		}
	}
	// Caller owns the stream.
	Common::SeekableReadStream *open(const Common::String &name) const {
		if (!_entries.contains(name))
			return nullptr;
		const Entry &e = _entries[name];
		Common::File f;
		if (!f.open(node(e.cd)))
			return nullptr;
		f.seek(e.offset);
		byte *buf = (byte *)malloc(e.size);
		f.read(buf, e.size);
		return new Common::MemoryReadStream(buf, e.size, DisposeAfterUse::YES);
	}
private:
	static Common::FSNode node(int cd) {
		return Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')
		                      .appendComponent(Common::String::format("BIGCD%d.BIG", cd)));
	}
	Common::HashMap<Common::String, Entry> _entries;
};

struct KeyGroup {
	Common::String file;
	Common::Array<Common::String> keys;
};

// Group "FILE#rest" golden keys by FILE.
inline void groupKeys(const AtlantisTest::Golden &g, Common::Array<KeyGroup> &out) {
	Common::HashMap<Common::String, uint> idx;
	for (AtlantisTest::Golden::const_iterator it = g.begin(); it != g.end(); ++it) {
		uint hash = it->_key.findLastOf('#');
		Common::String file = it->_key.substr(0, hash);
		if (!idx.contains(file)) {
			idx[file] = out.size();
			out.push_back(KeyGroup());
			out.back().file = file;
		}
		out[idx[file]].keys.push_back(it->_key);
	}
}

} // End of namespace GraphicsSpritesTest

class GraphicsSpritesTestSuite : public CxxTest::TestSuite {
public:
	// FSNode (used by the shared helper) needs g_system.
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// FUN_0042b930 / FUN_00452ac8 tables applied by the ORIGINAL blitters over
	// every (factor 0..31, destination 0..65535) pair, for two source colours.
	// Digests from graphics_sprites.py "sweep" (UB-free: the original table
	// builder output also equals the closed form, see REPORT.md).
	void test_spr_blend_exhaustive() {
		static const struct { uint16 src; const char *md5; } ref[] = {
			{0x0000, "45daab81bcefdc11f968460e2ee4f660"},
			{0x8410, "0fab5bc750835a035abaa94483f3b040"},
		};
		Common::Array<uint16> out(32 * 65536);
		for (uint r = 0; r < 2; r++) {
			for (uint g = 0; g < 32 * 65536; g++)
				out[g] = CryOmni3D::Atlantis::blendSprPixel565(ref[r].src, g & 0xffff, g >> 16);
			TS_ASSERT_EQUALS(GraphicsSpritesTest::md5Of(out.begin(), out.size()), Common::String(ref[r].md5));
		}
	}

	void test_spw_blend_exhaustive() {
		static const struct { uint16 src; const char *md5; } ref[] = {
			{0x0000, "7e6dda789be67392f8e535076afe972a"},
			{0x8410, "c5ca5e0245bc7cd9a00458b2a04041a0"},
		};
		Common::Array<uint16> out(32 * 65536);
		for (uint r = 0; r < 2; r++) {
			for (uint g = 0; g < 32 * 65536; g++)
				out[g] = CryOmni3D::Atlantis::blendSpwPixel565(ref[r].src, g & 0xffff, g >> 16);
			TS_ASSERT_EQUALS(GraphicsSpritesTest::md5Of(out.begin(), out.size()), Common::String(ref[r].md5));
		}
	}

	// Synthetic 8x3 SPR blitted by FUN_00410430 at hotspot (12,6) onto the
	// standard background.  Rows 0 and 1 use colour-table entry 1 (RGB555
	// 0x0001 -> RGB565 0x0001), a colour the port once used as its
	// transparency marker.
	void test_spr_synthetic_window() {
		using namespace GraphicsSpritesTest;
		static const uint16 expected[3][8] = {
			{0x0001, 0xffdf, 0x200a, 0xbe42, 0x31ec, 0x001f, 0x98c7, 0x2541},
			{0x0000, 0x0001, 0xffdf, 0x07c0, 0x001f, 0x0001, 0x0001, 0xffdf},
			{0xf8fc, 0x9733, 0x356b, 0xd3a2, 0x71da, 0x1011, 0xae49, 0x4c80},
		};
		Common::Array<byte> d = syntheticSpr();
		Common::MemoryReadStream s(d.begin(), d.size());
		Common::Array<SubjSprite> sprites;
		portLoadSpr(s, sprites);
		TS_ASSERT_EQUALS(sprites.size(), 1u);
		Common::Array<uint16> fb(kFbW * kFbH);
		fillBackground(fb.begin(), fb.size());
		portBlit(fb.begin(), sprites[0], 12, 6);
		uint diffs = 0;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 8; c++) {
				uint16 got = fb[(5 + r) * kFbW + 10 + c];
				if (got != expected[r][c]) {
					diffs++;
					TS_WARN(Common::String::format("SPR synthetic (%d,%d): port 0x%04x original 0x%04x",
					                               c, r, got, expected[r][c]).c_str());
				}
			}
		TS_ASSERT_EQUALS(diffs, 0u);
		TS_ASSERT_EQUALS(md5Of(fb.begin(), fb.size()), Common::String("7cd5c6039580578a54b1ec3d6a17e213"));
	}

	// Same sprite, clipped at the top-left and fully off-screen bottom-right.
	void test_spr_synthetic_clipping() {
		using namespace GraphicsSpritesTest;
		Common::Array<byte> d = syntheticSpr();
		Common::MemoryReadStream s(d.begin(), d.size());
		Common::Array<SubjSprite> sprites;
		portLoadSpr(s, sprites);
		Common::Array<uint16> fb(kFbW * kFbH);
		fillBackground(fb.begin(), fb.size());
		portBlit(fb.begin(), sprites[0], 641, 481);
		TS_ASSERT_EQUALS(md5Of(fb.begin(), fb.size()), Common::String("3b034feb5c09d0701829bb2efbef37d8"));
		fillBackground(fb.begin(), fb.size());
		portBlit(fb.begin(), sprites[0], 1, 0);
		TS_ASSERT_EQUALS(md5Of(fb.begin(), fb.size()), Common::String("2d3428ecd8ec318e1a6a5aa9f023e0b2"));
	}

	// Synthetic SPW frame (draw / blend / short skip / long skip / end) drawn
	// by FUN_00452bde into a 2048x768 buffer, versus compositeSpfFrame.
	void test_spw_synthetic() {
		using namespace GraphicsSpritesTest;
		Common::Array<byte> d = syntheticSpw();
		Common::MemoryReadStream s(d.begin(), d.size());
		Graphics::Surface surf;
		surf.create(kCycW, kCycH, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		uint16 *p = (uint16 *)surf.getPixels();
		fillBackground(p, kCycW * kCycH);
		CryOmni3D::Atlantis::compositeSpfFrame(&s, 0, surf);
		static const uint32 at[] = {645, 646, 647, 648, 649, 650, 651, 66190, 66191};
		static const uint16 val[] = {0xffff, 0x07e0, 0x1234, 0x3206, 0xffff, 0xb8db, 0x4f81, 0xf800, 0x07e0};
		for (uint i = 0; i < ARRAYSIZE(at); i++)
			TS_ASSERT_EQUALS(p[at[i]], val[i]);
		TS_ASSERT_EQUALS(md5Of(p, kCycW * kCycH), Common::String("8db0dea5bf37a5fbe798b92e8b5f9949"));
		surf.free();
	}

	// Every sprite of every SPR file, three blit positions each, versus the
	// original blit (golden graphics_spr.txt).
	void test_spr_all_files() {
		using namespace GraphicsSpritesTest;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_sprites: no atlantis_test.cfg, data test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("graphics_spr.txt", golden));
		Common::Array<KeyGroup> groups;
		groupKeys(golden, groups);
		Big big;
		uint total = 0, bad = 0, badFiles = 0;
		for (uint gi = 0; gi < groups.size(); gi++) {
			Common::ScopedPtr<Common::SeekableReadStream> s(big.open(groups[gi].file));
			TS_ASSERT(s);
			if (!s)
				continue;
			Common::Array<SubjSprite> sprites;
			portLoadSpr(*s, sprites);
			uint fileBad = 0;
			Common::Array<uint16> fb(kFbW * kFbH);
			for (uint k = 0; k < groups[gi].keys.size(); k++) {
				const Common::String &key = groups[gi].keys[k];
				uint hash = key.findLastOf('#'), at = key.findLastOf('@');
				uint idx = atoi(key.substr(hash + 1, at - hash - 1).c_str());
				Common::String tag = key.substr(at + 1);
				TS_ASSERT(idx < sprites.size());
				if (idx >= sprites.size())
					continue;
				const SubjSprite &spr = sprites[idx];
				int x = spr.xoff, y = spr.yoff;
				if (tag == "clipA") {
					x = spr.xoff - spr.w / 3;
					y = spr.yoff - spr.h / 3;
				} else if (tag == "clipB") {
					x = spr.xoff + kFbW - (2 * spr.w) / 3;
					y = spr.yoff + kFbH - (2 * spr.h) / 3;
				}
				fillBackground(fb.begin(), fb.size());
				portBlit(fb.begin(), spr, x, y);
				total++;
				if (md5Of(fb.begin(), fb.size()) != golden[key]) {
					bad++;
					fileBad++;
				}
			}
			if (fileBad) {
				badFiles++;
				TS_WARN(Common::String::format("SPR %s: %u/%u blits differ from the original",
				                               groups[gi].file.c_str(), fileBad, groups[gi].keys.size()).c_str());
			}
		}
		TS_WARN(Common::String::format("SPR: %u blits compared, %u differ (%u files)", total, bad, badFiles).c_str());
		TS_ASSERT_EQUALS(total, golden.size());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	void spwFiles(const char *goldenName, int w, int h) {
		using namespace GraphicsSpritesTest;
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden(goldenName, golden));
		Common::Array<KeyGroup> groups;
		groupKeys(golden, groups);
		Big big;
		Graphics::Surface surf;
		surf.create(w, h, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		uint16 *p = (uint16 *)surf.getPixels();
		uint total = 0, bad = 0;
		for (uint gi = 0; gi < groups.size(); gi++) {
			Common::ScopedPtr<Common::SeekableReadStream> s(big.open(groups[gi].file));
			TS_ASSERT(s);
			if (!s)
				continue;
			for (uint k = 0; k < groups[gi].keys.size(); k++) {
				const Common::String &key = groups[gi].keys[k];
				uint idx = atoi(key.substr(key.findLastOf('#') + 1).c_str());
				fillBackground(p, w * h);
				CryOmni3D::Atlantis::compositeSpfFrame(s.get(), idx, surf);
				total++;
				if (md5Of(p, w * h) != golden[key]) {
					if (bad < 10)
						TS_WARN(Common::String::format("%s differs from the original", key.c_str()).c_str());
					bad++;
				}
			}
		}
		surf.free();
		TS_WARN(Common::String::format("%s: %u frames compared, %u differ", goldenName, total, bad).c_str());
		TS_ASSERT_EQUALS(total, golden.size());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// SPF frame index past the end.  Original transition overlay
	// (atlantis-cd-en-714240 FUN_004256e8 @ 0x004257b6..0x004257be) draws an
	// SPF only while the frame counter DAT_00496104 < (byte)numFrames; past
	// the end nothing is drawn.  No shipped SPF has more than 139 frames, so
	// the byte compare equals the port's full count.
	void test_spf_index_past_end() {
		using namespace GraphicsSpritesTest;
		Common::Array<byte> d = syntheticSpw();   // 1 frame
		Common::MemoryReadStream s(d.begin(), d.size());
		Graphics::Surface surf;
		surf.create(kCycW, kCycH, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		uint16 *p = (uint16 *)surf.getPixels();
		fillBackground(p, kCycW * kCycH);
		Common::String before = md5Of(p, kCycW * kCycH);
		CryOmni3D::Atlantis::compositeSpfFrame(&s, 1, surf);
		TS_ASSERT_EQUALS(md5Of(p, kCycW * kCycH), before);
		surf.free();
	}

	// TGA: original FUN_00449600 (RGB555 -> RGB565: ((v & 0x7fe0) << 1) | (v & 0x1f))
	// then FUN_004174a0 bottom-up copy; golden graphics_tga.txt.  Port side is
	// a FAITHFUL COPY of the pixel loop of CryOmni3DEngine_Atlantis::loadTGA
	// (engine method using openBigFileStream and g_system->getScreenFormat();
	// screen format taken as RGB565, the format the port initialises).
	void test_tga_all_files() {
		using namespace GraphicsSpritesTest;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_sprites: no atlantis_test.cfg, data test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("graphics_tga.txt", golden));
		Big big;
		Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		uint total = 0, bad = 0;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			Common::ScopedPtr<Common::SeekableReadStream> stream(big.open(it->_key));
			TS_ASSERT(stream);
			if (!stream)
				continue;
			byte  idLen   = stream->readByte();
			/* cmType  = */ stream->readByte();
			byte  imgType = stream->readByte();
			stream->skip(9);
			uint16 w      = stream->readUint16LE();
			uint16 h      = stream->readUint16LE();
			byte   depth  = stream->readByte();
			stream->readByte();
			stream->skip(idLen);
			TS_ASSERT(imgType == 2 && depth == 15 && w == kFbW && h == kFbH);
			Common::Array<uint16> img(w * h);
			for (uint16 y = 0; y < h; y++) {
				uint16 *row = img.begin() + (h - 1 - y) * w;
				for (uint16 x = 0; x < w; x++) {
					uint16 px = stream->readUint16LE();
					uint8 r = ((px >> 10) & 0x1f) << 3;
					uint8 g = ((px >>  5) & 0x1f) << 3;
					uint8 b = ( px        & 0x1f) << 3;
					row[x] = (uint16)fmt.RGBToColor(r, g, b);
				}
			}
			total++;
			if (md5Of(img.begin(), img.size()) != it->_value) {
				bad++;
				TS_WARN(Common::String::format("TGA %s differs", it->_key.c_str()).c_str());
			}
		}
		TS_WARN(Common::String::format("TGA: %u images compared, %u differ", total, bad).c_str());
		TS_ASSERT_EQUALS(total, golden.size());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Every frame of every SPW (2048x768 panorama buffer) and SPF (640x480).
	void test_spw_all_files() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_sprites: no atlantis_test.cfg, data test skipped");
			return;
		}
		spwFiles("graphics_spw.txt", GraphicsSpritesTest::kCycW, GraphicsSpritesTest::kCycH);
	}

	void test_spf_all_files() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_sprites: no atlantis_test.cfg, data test skipped");
			return;
		}
		spwFiles("graphics_spf.txt", GraphicsSpritesTest::kFbW, GraphicsSpritesTest::kFbH);
	}
};
