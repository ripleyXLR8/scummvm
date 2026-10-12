// Audit "graphics", element 3: panoramic projection, centre hit point and
// zone resolution.
//
// Expected values come from the ORIGINAL code of atlantis-cd-en-714240 run
// under Unicorn (atlantis-re/tools/emu/graphics_omni3d.py, graphics_zones.py):
//   FUN_0045179a(hfovDeg, 50.0) + FUN_00451915(alpha, beta)  projection grid
//   FUN_00451d8b(src, dst)                                   warp renderer
//   FUN_0045166f                                             screen -> panorama
//   FUN_00422bb0                                             zone under the mouse
// on a synthetic 2048x1024 RGB565 panorama (pixel i = (i*2654435761) >> 16).
//
// Port code exercised directly (linked): CryOmni3D::Omni3DManager (omni3d.cpp),
// CryOmni3D::Atlantis::AtlantisPlace::hitTest (wam_parser.cpp) and
// CryOmni3D::Atlantis::hitTestPoint (wam_parser.h), the flip handleWarp
// (navigation.cpp) applies to mapMouseCoords().

#include "atlantis_test_helper.h"

#include "common/array.h"
#include "common/rect.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/wam_parser.h"
#include "../../../system/null_osystem.h"

namespace GraphicsNavTest {

static const Graphics::PixelFormat kFmt565(2, 5, 6, 5, 0, 11, 5, 0, 0);

inline float f32(double v) { return (float)v; }

struct Pano {
	Graphics::Surface s;
	Pano() {
		s.create(2048, 1024, kFmt565);
		uint16 *p = (uint16 *)s.getPixels();
		for (uint32 i = 0; i < 2048u * 1024u; i++)
			p[i] = (uint16)((i * 2654435761U) >> 16);
	}
	~Pano() { s.free(); }
};

inline void setupPort(CryOmni3D::Omni3DManager &m, const Graphics::Surface &src,
                      double fovDeg, double alpha, double beta) {
	// The original receives float arguments; give the port the same values.
	m.init((double)f32(fovDeg) * M_PI / 180.0);
	m.setPixelFormat(kFmt565);
	m.setSourceSurface(&src);
	m.setAlpha((double)f32(alpha));
	m.setBeta((double)f32(beta));
}

inline Common::String md5Surface(const Graphics::Surface *s) {
	Common::Array<byte> b(640 * 480 * 2);
	for (int y = 0; y < 480; y++) {
		const uint16 *row = (const uint16 *)s->getBasePtr(0, y);
		for (int x = 0; x < 640; x++) {
			b[(y * 640 + x) * 2] = row[x] & 0xff;
			b[(y * 640 + x) * 2 + 1] = row[x] >> 8;
		}
	}
	return AtlantisTest::md5Hex(b.begin(), b.size());
}

} // End of namespace GraphicsNavTest

class GraphicsNavigationTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Rendered 640x480 view, original FUN_00451d8b vs Omni3DManager::getSurface,
	// same FOV and angles.  The projection grids agree to <= 15/65536 px; the
	// difference is the per-row step of the tile interpolation (see REPORT.md).
	void test_projection_same_fov() {
		using namespace GraphicsNavTest;
		static const struct { double fov, a, b; const char *orig; } cases[] = {
			{75.0,   0.0,  0.0,   "60b2357e1eb610337d4b287591c9e4cd"},
			{75.0,   1.0,  0.25,  "ebbf4f4b3a7c95a51486dd984528da48"},
			{75.0,   3.0, -0.5,   "7ce1561377468d15b5ede8203137de33"},
			{75.0,   6.0,  0.75,  "26f5ae0197a016a428e1a79ede138680"},
			{75.137, 1.0,  0.25,  "f5d6951600c603f690eb823bd7b7eaa9"},
			{20.68,  2.0,  0.375, "13be1f2d380736c8d8ad4de683ba3f82"},
			{48.26,  4.5, -0.25,  "e590211931869dad7352261f1711a752"},
		};
		Pano pano;
		for (uint i = 0; i < ARRAYSIZE(cases); i++) {
			CryOmni3D::Omni3DManager m;
			setupPort(m, pano.s, cases[i].fov, cases[i].a, cases[i].b);
			Common::String got = md5Surface(m.getSurface());
			if (got != cases[i].orig)
				TS_WARN(Common::String::format("projection fov %.3f a %.3f b %.3f: port %s original %s",
				        cases[i].fov, cases[i].a, cases[i].b, got.c_str(), cases[i].orig).c_str());
			TS_ASSERT_EQUALS(got, Common::String(cases[i].orig));
		}
	}

	// The warp FOV: the original passes 75.137 deg (0x42964625, pushed at
	// atlantis-cd-en-714240 @ 0x0044a701, 0x0043044c, 0x00428abb, 0x00428b48,
	// 0x00428cbd); the port initialises Omni3DManager with 75 deg (engine.cpp).
	void test_projection_warp_fov() {
		using namespace GraphicsNavTest;
		Pano pano;
		CryOmni3D::Omni3DManager m;
		setupPort(m, pano.s, 75.0, 0.0, 0.0);   // what the port renders
		TS_ASSERT_EQUALS(md5Surface(m.getSurface()), Common::String("7ba99e56c5c021625370e91515570015"));
	}

	// Centre hit point: original FUN_0045166f returns y = 0x2ff - v; the port
	// takes mapMouseCoords() through hitTestPoint (handleWarp).
	void test_centre_point() {
		using namespace GraphicsNavTest;
		static const struct { double a, b; int ox, oy; } cases[] = {
			{0.0, 0.0, 0, 383}, {1.0, 0.25, 1722, 481}, {3.0, -0.5, 1070, 195}, {6.0, 0.75, 92, 652},
		};
		Pano pano;
		for (uint i = 0; i < ARRAYSIZE(cases); i++) {
			CryOmni3D::Omni3DManager m;
			setupPort(m, pano.s, 75.137, cases[i].a, cases[i].b);
			Common::Point pan = CryOmni3D::Atlantis::hitTestPoint(m.mapMouseCoords(Common::Point(320, 240)));
			TS_ASSERT_EQUALS(pan.x, cases[i].ox);
			TS_ASSERT_EQUALS(pan.y, cases[i].oy);
		}
	}

	// Zone resolution: original FUN_00422bb0 (inclusive right/bottom edges,
	// largest id wins) on zones placed around the original centre point;
	// port = AtlantisPlace::hitTest on the port's centre point.
	void test_zone_resolution() {
		using namespace GraphicsNavTest;
		struct Z { int16 x, y, w, h, id; };
		struct C { const char *name; int nz; Z z[2]; int origId; };
		static const struct { double a, b; int X, Y; } angles[] = {
			{1.0, 0.25, 1722, 481}, {3.0, -0.5, 1070, 195},
		};
		Pano pano;
		for (uint ai = 0; ai < ARRAYSIZE(angles); ai++) {
			const int X = angles[ai].X, Y = angles[ai].Y;
			const C cases[] = {
				{"inside",             1, {{(int16)(X - 5), (int16)(Y - 5), 10, 10, 7}, {0, 0, 0, 0, 0}}, 7},
				{"right_bottom_edge",  1, {{(int16)(X - 10), (int16)(Y - 10), 10, 10, 7}, {0, 0, 0, 0, 0}}, 7},
				{"left_top_edge",      1, {{(int16)X, (int16)Y, 10, 10, 7}, {0, 0, 0, 0, 0}}, 7},
				{"overlap_low_first",  2, {{(int16)(X - 5), (int16)(Y - 5), 10, 10, 3}, {(int16)(X - 6), (int16)(Y - 6), 12, 12, 9}}, 9},
				{"overlap_high_first", 2, {{(int16)(X - 6), (int16)(Y - 6), 12, 12, 9}, {(int16)(X - 5), (int16)(Y - 5), 10, 10, 3}}, 9},
				{"one_below",          1, {{(int16)(X - 5), (int16)(Y + 1), 10, 10, 7}, {0, 0, 0, 0, 0}}, 0},
				{"one_above",          1, {{(int16)(X - 5), (int16)(Y - 11), 10, 10, 7}, {0, 0, 0, 0, 0}}, 0},
			};
			CryOmni3D::Omni3DManager m;
			setupPort(m, pano.s, 75.137, angles[ai].a, angles[ai].b);
			Common::Point pan = CryOmni3D::Atlantis::hitTestPoint(m.mapMouseCoords(Common::Point(320, 240)));
			for (uint ci = 0; ci < ARRAYSIZE(cases); ci++) {
				CryOmni3D::Atlantis::AtlantisPlace place;
				for (int k = 0; k < cases[ci].nz; k++) {
					CryOmni3D::Atlantis::AtlantisZone z;
					z.x = cases[ci].z[k].x; z.y = cases[ci].z[k].y;
					z.w = cases[ci].z[k].w; z.h = cases[ci].z[k].h;
					z.action = cases[ci].z[k].id;
					place.zones.push_back(z);
				}
				int got = place.hitTest(pan.x, pan.y);
				if (got != cases[ci].origId)
					TS_WARN(Common::String::format("zones %s (angles %d): port %d original %d",
					        cases[ci].name, ai, got, cases[ci].origId).c_str());
				TS_ASSERT_EQUALS(got, cases[ci].origId);
			}
		}
	}

	// Dialog camera FOV table (atlantis-cd-en-714240 @ 0x00496080, 4 floats,
	// read as [cam*4 + 0x496078] at 0x00428a0e / 0x00428ad4) versus the port's
	// literal kCamFovDeg in dialog.cpp (FAITHFUL COPY of the literal; the
	// array is function-local).
	void test_camera_fov_table() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no atlantis_test.cfg, data test skipped");
			return;
		}
		static const float kCamFovDeg[4] = {20.68f, 30.0f, 48.0f, 48.26f};
		Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("atlantis.exe"));
		Common::File f;
		TS_ASSERT(f.open(node));
		if (!f.isOpen())
			return;
		f.seek(0x496080 - 0x490000 + 0x71200);   // DGROUP: va 0x490000, raw 0x71200
		for (int i = 0; i < 4; i++) {
			uint32 bits = f.readUint32LE();
			float v;
			memcpy(&v, &bits, 4);
			TS_ASSERT_EQUALS(v, kCamFovDeg[i]);
		}
	}

	// Warp cursor: CURSEURS.SPR sprite drawn on 16 consecutive frames.
	// Original: FUN_00422fcc emulated (graphics_cursor.py), mouse-look mode
	// (DAT_004960c8 = 0, the default), -1 = no cursor drawn.  Port: FAITHFUL
	// COPY of handleWarp's group choice (talk if an NPC is hit, navigate if a
	// zone is hit, else default) and of tickWarpCursor's frame advance
	// (kGroupFirst/kGroupCount, default group static), one advance per frame.
	void test_cursor_sequences() {
		static const int kGroupFirst[] = {0, 9, 18, 26};
		static const int kGroupCount[] = {9, 9, 8, 20};
		static const struct { const char *name; int zone, npc; int orig[16]; } cases[] = {
			{"nothing",            0,   0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
			{"navigation zone 12", 12,  0, {9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14}},
			{"npc 600",            0, 600, {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41}},
			{"scene object 300",   0, 300, {18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19}},
		};
		for (uint c = 0; c < ARRAYSIZE(cases); c++) {
			int grp = (cases[c].npc > 0) ? 3 : (cases[c].zone > 0) ? 1 : 0;
			int frame = -1;
			uint diffs = 0;
			Common::String seq;
			for (int f = 0; f < 16; f++) {
				if (grp == 0)
					frame = 0;
				else
					frame = (frame + 1) % kGroupCount[grp];
				int spr = kGroupFirst[grp] + frame;
				seq += Common::String::format("%d ", spr);
				if (spr != cases[c].orig[f])
					diffs++;
			}
			if (diffs)
				TS_WARN(Common::String::format("cursor %s: port %s", cases[c].name, seq.c_str()).c_str());
			TS_ASSERT_EQUALS(diffs, 0u);
		}
	}
};
