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
// Port code exercised directly (linked): CryOmni3D::Atlantis::Omni3DManagerAtlantis (atlantis/omni3d.cpp),
// CryOmni3D::Atlantis::AtlantisPlace::hitTest (wam_parser.cpp) and
// CryOmni3D::Atlantis::hitTestPoint (wam_parser.h), the flip handleWarp
// (navigation.cpp) applies to mapMouseCoords().

#include "atlantis_test_helper.h"

#include "common/array.h"
#include "common/rect.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/warp_cursor.h"
#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/npc_click.h"
#include "cryomni3d/atlantis/sprite_anim.h"
#include "cryomni3d/atlantis/wam_parser.h"
#include "cryomni3d/atlantis/warp_momentum.h"
#include "cryomni3d/atlantis/warp_pointer.h"
#include "common/archive.h"
#include "common/fs.h"
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

	// Rendered 640x480 view, original FUN_0045179a / FUN_00451915 / FUN_00451d8b
	// vs Omni3DManagerAtlantis, same FOV and angles.  The original digests come
	// from tools/emu/graphics_omni3d.py with the game's x87 control word 0x127f.
	void test_projection_same_fov() {
		using namespace GraphicsNavTest;
		static const struct { double fov, a, b; const char *orig; } cases[] = {
			{75.0,   0.0,  0.0,   "bcbc7625823ab0642a1634bf64e36a8a"},
			{75.0,   1.0,  0.25,  "793681ba0dc96cab61ab119d17d2aaa4"},
			{75.0,   3.0, -0.5,   "df5598759f445e4f7b9eb5d852c03b12"},
			{75.0,   6.0,  0.75,  "f83b989cb75a4db55f610f1b2c795007"},
			{75.137, 1.0,  0.25,  "80a70639be883a61395deee473d1a0d8"},
			{20.68,  2.0,  0.375, "0e1a5c2d1147f50a61880fe5a663f905"},
			{48.26,  4.5, -0.25,  "a010e7d4993c132c5261c37207d7ebec"},
		};
		Pano pano;
		for (uint i = 0; i < ARRAYSIZE(cases); i++) {
			CryOmni3D::Atlantis::Omni3DManagerAtlantis m;
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
	// 0x00428cbd); the port initialises Omni3DManagerAtlantis with kWarpFovDeg.
	void test_projection_warp_fov() {
		using namespace GraphicsNavTest;
		Pano pano;
		CryOmni3D::Atlantis::Omni3DManagerAtlantis m;
		setupPort(m, pano.s, CryOmni3D::Atlantis::kWarpFovDeg, 0.0, 0.0);   // what the port renders
		TS_ASSERT_EQUALS(md5Surface(m.getSurface()), Common::String("f7b654b61a06df529a00de37343db613"));
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
			CryOmni3D::Atlantis::Omni3DManagerAtlantis m;
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
			CryOmni3D::Atlantis::Omni3DManagerAtlantis m;
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

	// The rows of a WAM's sprite list (SPRLIST\\<spwListFile>), as the
	// engine's loadSpritePlaceList parses them.  Member names are looked up
	// upper-cased, as the engine does (openBigFileStream).
	static void loadSpriteList(CryOmni3D::Atlantis::BigFileArchive &big, const CryOmni3D::Atlantis::AtlantisWAMParser &w,
	                           Common::Array<CryOmni3D::Atlantis::SpritePlaceEntry> &entries) {
		using namespace CryOmni3D::Atlantis;
		entries.clear();
		Common::String listName = "SPRLIST\\" + w.spwListFile;
		listName.toUppercase();
		Common::ScopedPtr<Common::SeekableReadStream> ls(big.createReadStreamForMember(listName));
		if (!ls)
			return;
		Common::String text;
		while (!ls->eos()) {
			char c = (char)ls->readByte();
			if (!ls->eos())
				text += c;
		}
		const char *p = text.c_str();
		while (*p) {
			while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
			const char *eol = p;
			while (*eol && *eol != '\r' && *eol != '\n') eol++;
			SpritePlaceEntry e;
			if (eol > p && parseSpritePlaceLine(Common::String(p, eol - p), e))
				entries.push_back(e);
			p = eol;
		}
	}

	// NPC / object click areas over every WAM, against the ORIGINAL
	// FUN_00421ebc + FUN_00424a20 + FUN_00422d5c run end to end under
	// emulation on the game data (atlantis-re tools/emu/scripts_npc_click.py,
	// evidence/golden/graphics_npc_click.txt): the NPC rectangles after the
	// sprite list is loaded, and the hit-test result at a 48-px grid plus
	// every rectangle edge +-1, in each view that has sprites.  Port:
	// AtlantisWAMParser, parseSpritePlaceLine, readSpwFrame0,
	// initNpcClickRects / applyNpcRowRect and npcHitTest (npc_click.cpp).
	void test_npc_click_areas_data() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no game data configured, skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("graphics_npc_click.txt", golden));
		if (!SearchMan.hasArchive("graphics_npc_data"))
			SearchMan.addDirectory("graphics_npc_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		uint wams = 0;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			if (!it->_key.hasSuffix("|rects"))
				continue;
			Common::String name = it->_key.substr(0, it->_key.size() - 6);
			wams++;
			Common::ScopedPtr<Common::SeekableReadStream> ws(big.createReadStreamForMember("WAM\\" + name));
			TS_ASSERT(ws);
			if (!ws)
				continue;
			AtlantisWAMParser w;
			w.loadStream(*ws);
			const Common::Array<AtlantisNPCEntry> &npcs = w.getNPCEntries();

			Common::Array<SpritePlaceEntry> entries;
			loadSpriteList(big, w, entries);
			Common::Array<NpcClickRect> rects;
			initNpcClickRects(npcs, rects);
			for (const SpritePlaceEntry &e : entries) {
				if (!npcRowMatches(npcs, e))
					continue;
				Common::String spwName = "SPRITE\\CYCLO\\" + e.spwFile;
				spwName.toUppercase();
				Common::ScopedPtr<Common::SeekableReadStream> spw(big.createReadStreamForMember(spwName));
				SpwFrame0 f;
				if (spw && readSpwFrame0(*spw, f))
					applyNpcRowRect(npcs, e, f, rects);
			}
			Common::String rtext;
			for (uint i = 0; i < npcs.size(); i++)
				rtext += Common::String::format("%u %u %s %d %d %d %d\n", i, npcs[i].persoId, npcs[i].spriteFile.c_str(),
				                                rects[i].x, rects[i].y, rects[i].w, rects[i].h);
			TSM_ASSERT_EQUALS((name + ": NPC rectangles").c_str(),
			                  AtlantisTest::md5Hex((const byte *)rtext.c_str(), rtext.size()), it->_value);

			Common::Array<Common::Point> pts;
			for (int y = 0; y < 768; y += 48)
				for (int x = 0; x < 2048; x += 48)
					pts.push_back(Common::Point(x, y));
			for (uint i = 0; i < rects.size(); i++) {
				const int cxs[2] = { rects[i].x, rects[i].x + rects[i].w };
				const int cys[2] = { rects[i].y, rects[i].y + rects[i].h };
				for (int dx = -1; dx <= 1; dx++)
					for (int dy = -1; dy <= 1; dy++)
						for (int a = 0; a < 2; a++)
							for (int b = 0; b < 2; b++)
								pts.push_back(Common::Point(cxs[a] + dx, cys[b] + dy));
			}
			Common::Array<byte> res;
			const char *vp = golden[name + "|vues"].c_str();
			while (*vp && *vp != '-') {
				uint16 vue = (uint16)atoi(vp);
				while (*vp && *vp != ',') vp++;
				if (*vp == ',') vp++;
				for (const Common::Point &pt : pts) {
					uint32 id = npcHitTest(npcs, rects, entries, vue, pt.x, pt.y);
					for (int k = 0; k < 4; k++)
						res.push_back((byte)(id >> (8 * k)));
				}
			}
			TSM_ASSERT_EQUALS((name + ": hit-test results").c_str(),
			                  AtlantisTest::md5Hex(res.data(), res.size()), golden[name + "|hits"]);
		}
		TS_ASSERT_EQUALS(wams, 38u);
		SearchMan.remove("graphics_npc_data");
	}

	// Object pick-up rows over every WAM, against the place tables the
	// ORIGINAL loaders build under emulation (atlantis-re
	// tools/emu/scripts_npc_click.py pickup,
	// evidence/golden/scripts_object_pickup.txt): for each view, the entries
	// FUN_004278a0's pick-up loop takes (object id, type != 3) and their icon
	// byte.  Port: parseSpritePlaceLine (icon) and objectPickupRows.
	void test_object_pickup_rows_data() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no game data configured, skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("scripts_object_pickup.txt", golden));
		if (!SearchMan.hasArchive("graphics_pickup_data"))
			SearchMan.addDirectory("graphics_pickup_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		uint wams = 0;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			if (!it->_key.hasSuffix("|rows"))
				continue;
			Common::String name = it->_key.substr(0, it->_key.size() - 5);
			wams++;
			Common::ScopedPtr<Common::SeekableReadStream> ws(big.createReadStreamForMember("WAM\\" + name));
			TS_ASSERT(ws);
			if (!ws)
				continue;
			AtlantisWAMParser w;
			w.loadStream(*ws);
			Common::Array<SpritePlaceEntry> entries;
			loadSpriteList(big, w, entries);
			Common::String text;
			uint n = 0;
			Common::Array<uint> rows;
			for (uint vue = 0; vue < 512; vue++) {
				for (uint i = 0; i < entries.size(); i++) {
					const SpritePlaceEntry &e = entries[i];
					if (e.placeId != vue || e.persoId < 0x100 || e.persoId >= 0x200)
						continue;
					objectPickupRows(entries, (uint16)vue, e.persoId, rows);
					bool taken = false;
					for (uint r : rows)
						if (r == i) taken = true;
					if (!taken)
						continue;
					text += Common::String::format("%u %d %s %u\n", vue, e.persoId, e.rawFile.c_str(), e.icon);
					n++;
				}
			}
			TSM_ASSERT_EQUALS((name + ": pick-up row count").c_str(), Common::String::format("%u", n), golden[name + "|count"]);
			TSM_ASSERT_EQUALS((name + ": pick-up rows").c_str(),
			                  AtlantisTest::md5Hex((const byte *)text.c_str(), text.size()), it->_value);
		}
		TS_ASSERT_EQUALS(wams, 38u);
		SearchMan.remove("graphics_pickup_data");
	}

	// Place-sprite animation against the ORIGINAL FUN_00425304 run under
	// emulation on synthetic place tables (atlantis-re
	// tools/emu/scripts_sprite_anim.py, evidence/golden/graphics_sprite_anim.txt):
	// 24 scenarios of up to 5 entries (active, type 0..3, frame count 1..8),
	// a current animation entry, and 60 calls at increasing tick values each;
	// after every call the entries' active flags and frames and WSPRFRAME.
	// Port: animatePlaceSprites (sprite_anim.cpp).
	static long nextLong(const char *&p) {
		char *end = nullptr;
		long v = strtol(p, &end, 10);
		p = end;
		return v;
	}

	void test_sprite_anim_vs_original() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> lines;
		TS_ASSERT(AtlantisTest::loadGoldenLines("graphics_sprite_anim.txt", lines));
		Common::Array<SpritePlaceEntry> entries;
		uint current = 0;
		uint32 last[kSpriteAnimSlots];
		int wspr = 0;
		uint scenarios = 0, steps = 0, bad = 0;
		for (const Common::String &l : lines) {
			if (l.empty() || l[0] == '#')
				continue;
			const char *p = l.c_str() + 2;
			if (l[0] == 'S') {
				scenarios++;
				entries.clear();
				memset(last, 0, sizeof(last));
				wspr = 0;
				current = (uint)nextLong(p);
				while (*p == ' ') {
					p++;
					SpritePlaceEntry e;
					e.placeId = 5;
					e.active = nextLong(p) != 0;
					p++;
					e.type = (int)nextLong(p);
					p++;
					e.frameCount = (int)nextLong(p);
					e.frame = 0;
					e.persoId = 0;
					entries.push_back(e);
				}
				continue;
			}
			// T <ticks> <a:f>... W <w>
			steps++;
			uint32 ticks = (uint32)nextLong(p);
			animatePlaceSprites(entries, 5, current, ticks, last, wspr);
			Common::String got = Common::String::format("T %u", ticks);
			for (const SpritePlaceEntry &e : entries)
				got += Common::String::format(" %d:%d", e.active ? 1 : 0, e.frame);
			got += Common::String::format(" W %d", wspr);
			if (got != l) {
				if (bad++ < 5)
					TS_FAIL(Common::String::format("sprite anim: got '%s', original '%s'", got.c_str(), l.c_str()).c_str());
			}
		}
		TS_ASSERT_EQUALS(scenarios, 24u);
		TS_ASSERT_EQUALS(steps, 24u * 60u);
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// The place-sprite active flag over ShowPerso / HidePerso / NEWWSPRANIM /
	// STOPWSPRANIM / type-2 cycles and place re-entries, in every WAM, against
	// the ORIGINAL table run under emulation (FUN_00421ebc with FUN_00424a20
	// and FUN_00427d30, FUN_0042431c, FUN_00427eb0, FUN_00427bd0,
	// FUN_00428da8, FUN_00425304; atlantis-re tools/emu/scripts_sprite_active.py,
	// golden scripts_sprite_active.txt, experiment 2026-10-15-95).  Port:
	// parseSpritePlaceLine, spriteListApplyHidden, spriteListSetPerso,
	// start/stopPlaceSpriteAnim, animatePlaceSprites, spriteListEnterPlace.
	void test_sprite_active_vs_original() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> lines;
		TS_ASSERT(AtlantisTest::loadGoldenLines("scripts_sprite_active.txt", lines));
		if (!SearchMan.hasArchive("graphics_npc_data"))
			SearchMan.addDirectory("graphics_npc_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		Common::String wam;
		Common::Array<SpritePlaceEntry> parsed;
		uint rows = 0, scen = 0, bad = 0;
		auto viewRow = [](Common::Array<SpritePlaceEntry> &es, int v, int k) -> SpritePlaceEntry * {
			int n = 0;
			for (SpritePlaceEntry &e : es)
				if (e.placeId == (uint16)v && n++ == k)
					return &e;
			return nullptr;
		};
		for (const Common::String &l : lines) {
			if (l.size() < 2 || l[1] != ' ')
				continue;
			char w[32], a[32];
			if (l[0] == 'L') {
				// L <WAM> <list>: the port's parse of the list.
				if (sscanf(l.c_str() + 2, "%31s %31s", w, a) != 2)
					continue;
				wam = w;
				parsed.clear();
				Common::String listName = Common::String("SPRLIST\\") + a;
				listName.toUppercase();
				Common::ScopedPtr<Common::SeekableReadStream> ls(big.createReadStreamForMember(listName));
				if (!ls)
					continue;
				Common::String text;
				while (true) {
					char c = (char)ls->readByte();
					if (ls->eos())
						break;
					text += c;
				}
				const char *p = text.c_str();
				while (*p) {
					while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
					const char *eol = p;
					while (*eol && *eol != '\r' && *eol != '\n') eol++;
					SpritePlaceEntry e;
					if (eol > p && parseSpritePlaceLine(Common::String(p, eol - p), e))
						parsed.push_back(e);
					p = eol;
				}
				continue;
			}
			if (l[0] == 'R') {
				// R <WAM> <vue> <k> <id> <active> <type>
				int v, k, id, act, typ;
				if (sscanf(l.c_str() + 2, "%31s %d %d %d %d %d", w, &v, &k, &id, &act, &typ) != 6)
					continue;
				rows++;
				SpritePlaceEntry *e = viewRow(parsed, v, k);
				if (!e || (e->active ? 1 : 0) != (act ? 1 : 0) || e->persoId != (id > 0 ? id : 0) || e->type != typ) {
					if (bad++ < 5)
						TS_FAIL(Common::String::format("sprite list %s view %d row %d differs from the original", w, v, k).c_str());
				}
				continue;
			}
			if (l[0] != 'S')
				continue;
			// S <WAM> <kind> <arg...>: <v>.<k>=<active>[(before x)] ...
			const char *colon = strchr(l.c_str(), ':');
			if (!colon || sscanf(l.c_str() + 2, "%31s %31s", w, a) != 2)
				continue;
			scen++;
			const Common::String kind(a);
			const char *argp = l.c_str() + 2 + strlen(w) + 1 + strlen(a) + 1;
			int arg1 = atoi(argp), arg2 = 0;
			const char *sp = strchr(argp, ' ');
			if (sp && sp < colon)
				arg2 = atoi(sp + 1);
			// The expected rows.
			Common::Array<int> tv, tk, ta;
			for (const char *q = colon + 1; *q;) {
				while (*q == ' ') q++;
				int v, k, act;
				if (sscanf(q, "%d.%d=%d", &v, &k, &act) == 3) {
					tv.push_back(v); tk.push_back(k); ta.push_back(act);
				}
				while (*q && *q != ' ') q++;
			}
			Common::Array<SpritePlaceEntry> es = parsed;
			Common::Array<int> hidden;
			if (kind == "HLOAD" || kind == "HSHOW") {
				hidden.push_back(arg1);
				spriteListApplyHidden(es, hidden);   // the list load
			}
			for (uint i = 0; i < tv.size(); i++)
				spriteListEnterPlace(es, (uint16)tv[i]);
			uint current = 0;
			if (kind == "SHOW" || kind == "HSHOW") {
				spriteListSetPerso(es, arg1, true);
			} else if (kind == "HIDE") {
				spriteListSetPerso(es, arg1, false);
			} else if (kind == "NEW") {
				startPlaceSpriteAnim(es, (uint16)arg1, arg2 + 1, current);
			} else if (kind == "STOP") {
				stopPlaceSpriteAnim(es, (uint16)arg1, arg2 + 1);
			} else if (kind == "T2") {
				SpritePlaceEntry *e = viewRow(es, arg1, arg2);
				if (e && !e->active)
					startPlaceSpriteAnim(es, (uint16)arg1, arg2 + 1, current);
				if (e)
					e->frameCount = 3;    // any count: the cycle ends at it
				uint32 last[kSpriteAnimSlots];
				memset(last, 0, sizeof(last));
				int wspr = 0;
				for (uint32 t = 1; t < 200 && e && e->active; t++)
					animatePlaceSprites(es, (uint16)arg1, current, 1000 + 14 * t, last, wspr);
			}
			for (uint i = 0; i < tv.size(); i++)
				spriteListEnterPlace(es, (uint16)tv[i]);
			for (uint i = 0; i < tv.size(); i++) {
				SpritePlaceEntry *e = viewRow(es, tv[i], tk[i]);
				if (!e || (e->active ? 1 : 0) != ta[i]) {
					if (bad++ < 5)
						TS_FAIL(Common::String::format("sprite active %s %s %d: row %d.%d port %d original %d",
						        w, kind.c_str(), arg1, tv[i], tk[i], e ? (int)e->active : -1, ta[i]).c_str());
				}
			}
		}
		TS_ASSERT_EQUALS(rows, 1393u);
		TS_ASSERT_EQUALS(scen, 3463u);
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Arrow shot in bow mode, from the static reading of atlantis.exe
	// FUN_0041bfa0 @ 0x0041c61d..0x0041c6a0 (atlantis-re
	// experiments/2026-10-13-20-archery-and-sprite-animation.md): the click is
	// consumed only in bow mode and shoots only when AngleWarpY < 20; the
	// arrow frame steps while TirFleche is set and both reset at 15.
	void test_bow_frame_step() {
		using namespace CryOmni3D::Atlantis;
		int tir = 0;
		uint frame = 0;
		// Not in bow mode: the click is not consumed and does not shoot.
		TS_ASSERT(!bowFrameStep(false, true, 0, tir, frame));
		TS_ASSERT_EQUALS(tir, 0);
		TS_ASSERT_EQUALS(frame, 0u);
		// Aiming too high: consumed, no shot.
		TS_ASSERT(bowFrameStep(true, true, 20, tir, frame));
		TS_ASSERT_EQUALS(tir, 0);
		// A shot: the frame steps in the same call.
		TS_ASSERT(bowFrameStep(true, true, 19, tir, frame));
		TS_ASSERT_EQUALS(tir, 1);
		TS_ASSERT_EQUALS(frame, 1u);
		for (uint i = 2; i < 15; i++) {
			TS_ASSERT(!bowFrameStep(true, false, 0, tir, frame));
			TS_ASSERT_EQUALS(frame, i);
			TS_ASSERT_EQUALS(tir, 1);
		}
		bowFrameStep(true, false, 0, tir, frame);
		TS_ASSERT_EQUALS(frame, 0u);
		TS_ASSERT_EQUALS(tir, 0);
		// A script-set TirFleche runs the arrow outside bow mode too.
		tir = 1;
		bowFrameStep(false, false, 0, tir, frame);
		TS_ASSERT_EQUALS(frame, 1u);
	}

	// Dialog camera FOV table (atlantis-cd-en-714240 @ 0x00496080, 4 floats,
	// read as [cam*4 + 0x496078] at 0x00428a0e / 0x00428ad4) versus the port's
	// kDialogCamFovDeg (atlantis/omni3d.h).
	void test_camera_fov_table() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_navigation: no atlantis_test.cfg, data test skipped");
			return;
		}
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
			TS_ASSERT_EQUALS(v, CryOmni3D::Atlantis::kDialogCamFovDeg[i]);
		}
	}

	// Warp cursor: CURSEURS.SPR sprite drawn on 16 consecutive frames, -1 =
	// none.  Original: FUN_00422fcc emulated (tools/emu/graphics_cursor.py)
	// for both look modes (DAT_004960c8 = 0 mouse-look, 1 edge-scroll);
	// "click" presets 0x004968b8 as FUN_004278a0 does.  Port: warpCursorStep.
	void test_cursor_sequences() {
		static const struct { const char *name; uint zone, id, held; bool click; int orig[2][16]; } cases[] = {
			{"nothing", 0, 0, 0, false,
			 {{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
			  {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}}},
			{"navigation zone 12", 12, 0, 0, false,
			 {{9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14},
			  {9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14}}},
			{"npc 600", 0, 600, 0, false,
			 {{26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41},
			  {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41}}},
			{"scene object 300", 0, 300, 0, false,
			 {{18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19},
			  {18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19}}},
			{"zone 12 while holding 300", 12, 0, 300, false,
			 {{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
			  {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}}},
			{"id 0x219", 0, 0x219, 0, false,
			 {{9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14},
			  {9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14}}},
			{"zone 2300 holding 300", 2300, 0, 300, false,
			 {{18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19},
			  {18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19}}},
			{"zone 2300 holding 301", 2300, 0, 301, false,
			 {{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
			  {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}}},
			{"zone 3300 holding 300", 3300, 0, 300, false,
			 {{18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19},
			  {18, 19, 20, 21, 22, 21, 20, 19, 18, 19, 20, 21, 22, 21, 20, 19}}},
			{"zone 600", 600, 0, 0, false,
			 {{26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41},
			  {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41}}},
			{"click, nothing", 0, 0, 0, true,
			 {{0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
			  {0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}}},
			{"click, zone 12", 12, 0, 0, true,
			 {{3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 9, 10, 11, 12, 13},
			  {3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 9, 10, 11, 12, 13}}},
			{"click, npc 600", 0, 600, 0, true,
			 {{0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
			  {0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}}},
		};
		for (uint c = 0; c < ARRAYSIZE(cases); c++) {
			for (int mode = 0; mode < 2; mode++) {
				CryOmni3D::Atlantis::WarpCursorState st;
				st.clickAnim = cases[c].click;
				CryOmni3D::Atlantis::WarpCursorInput in;
				in.zone = cases[c].zone;
				in.id = cases[c].id;
				in.held = cases[c].held;
				in.hidden = false;
				in.edgeScroll = (mode == 1);
				uint diffs = 0;
				Common::String seq;
				for (int f = 0; f < 16; f++) {
					int spr = CryOmni3D::Atlantis::warpCursorStep(st, in).sprite;
					seq += Common::String::format("%d ", spr);
					if (spr != cases[c].orig[mode][f])
						diffs++;
				}
				if (diffs)
					TS_WARN(Common::String::format("cursor %s (mode %d): port %s", cases[c].name, mode, seq.c_str()).c_str());
				TS_ASSERT_EQUALS(diffs, 0u);
			}
		}
	}

	// FUN_0041bfa0 step 4 (atlantis-re functions/graphics-FUN_0041bfa0.md,
	// STATIC @ 0x0041c213..0x0041c2b5): one delta of 30 px, then none.
	// Expected values: the same float / double sequence computed outside the
	// port (Python, struct-packed floats): alpha 1.01, 1.018, 1.0244,
	// 1.02952, 1.033616 and 1.05 (30 / 600) in the limit.
	void test_warp_momentum() {
		using CryOmni3D::Atlantis::WarpMomentum;
		WarpMomentum m;
		float alpha = 1.0f, beta = 0.2f;
		static const float kWant[5] = { 1.00999999f, 1.01800001f, 1.0244f, 1.02952003f, 1.03361607f };
		for (int i = 0; i < 400; i++) {
			m.step(i == 0 ? 30.0f : 0.0f, i == 0 ? -40.0f : 0.0f, alpha, beta);
			if (i < 5)
				TS_ASSERT_DELTA(alpha, kWant[i], 1e-6f);
		}
		TS_ASSERT_DELTA(alpha, 1.04999983f, 1e-6f);   // d / 600 in total
		TS_ASSERT_DELTA(beta, 0.249999985f, 1e-6f);   // beta - (-40) / 800
	}

	// ANGLEWARPX / ANGLEWARPY and the look step of FUN_0041bfa0 against the
	// original run unmodified (atlantis-re tools/emu/graphics_anglewarp.py,
	// golden graphics_anglewarp.txt, experiment 2026-10-16-11): the smallest
	// float of every degree on both axes (B rows), and 6720 frames of
	// scripted mouse motion in OMNI 3D MODE 1 and 2 (F rows: ANGLEWARPX /
	// ANGLEWARPY stored at the frame start, then the angles and momentum).
	void test_anglewarp_vs_original() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("anglewarp: no golden configured, skipped");
			return;
		}
		Common::Array<Common::String> lines;
		TS_ASSERT(AtlantisTest::loadGoldenLines("graphics_anglewarp.txt", lines));
		Omni3DManagerAtlantis omni;
		omni.setHFov((double)kWarpFovDeg * M_PI / 180.0);
		const float pi = omni.piF();
		auto u2f = [](uint32 u) { float f; memcpy(&f, &u, 4); return f; };
		auto f2u = [](float f) { uint32 u; memcpy(&u, &f, 4); return u; };
		uint bounds = 0, frames = 0, bad = 0;
		Common::String trace;
		int traceMode = -1;
		WarpMomentum m;
		float alpha = 0.f, beta = 0.f, prevA = -1.f, prevB = -1.f;
		for (uint li = 0; li < lines.size(); li++) {
			const Common::String &l = lines[li];
			if (l.hasPrefix("B ")) {
				char axis;
				int k;
				uint32 bits;
				if (sscanf(l.c_str(), "B %c %d %x", &axis, &k, &bits) != 3) {
					TS_FAIL(l.c_str());
					continue;
				}
				bounds++;
				const int at = angleWarpDegrees(u2f(bits), pi);
				const int below = angleWarpDegrees(u2f(bits - 1), pi);   // next float toward 0
				if (at != k || below != (k > 0 ? k - 1 : k + 1)) {
					bad++;
					TS_WARN(Common::String::format("%s: port %d / %d", l.c_str(), at, below).c_str());
				}
			} else if (l.hasPrefix("F ")) {
				char name[32];
				int mode, idx, mx, my, dx, dy, awx, awy;
				uint32 a, b, vx, vy;
				if (sscanf(l.c_str(), "F %31s %d %d: %d %d d %d %d -> %d %d %x %x %x %x", name, &mode, &idx, &mx, &my,
				           &dx, &dy, &awx, &awy, &a, &b, &vx, &vy) != 13) {
					TS_FAIL(l.c_str());
					continue;
				}
				if (trace != name || traceMode != mode) {
					trace = name;
					traceMode = mode;
					m = WarpMomentum();
					alpha = beta = 0.f;
					prevA = prevB = -1.f;
				}
				frames++;
				const WarpLookFrame f = warpLookFrame(m, alpha, beta, dx, dy, mode != 0, prevA, prevB, pi,
				                                      omni.twoPiF(), omni.betaLimit());
				prevA = alpha;
				prevB = beta;
				if (f.angleWarpX != awx || f.angleWarpY != awy || f2u(alpha) != a || f2u(beta) != b ||
				        f2u(m.vx) != vx || f2u(m.vy) != vy) {
					bad++;
					if (bad < 10)
						TS_WARN(Common::String::format("%s: port %d %d %08x %08x %08x %08x", l.c_str(), f.angleWarpX,
						                               f.angleWarpY, f2u(alpha), f2u(beta), f2u(m.vx), f2u(m.vy)).c_str());
				}
			}
		}
		TS_ASSERT_EQUALS(bounds, 450u);
		TS_ASSERT_EQUALS(frames, 6720u);
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// The warp pointer across loops (atlantis-re functions/graphics-FUN_0041bfa0.md,
	// experiments/2026-10-15-70): the image values (0, 0) and -1.0
	// (0x004960ec, 0x0049698c), (0x140, 0x140) after a panorama load
	// (FUN_0044a424 @ 0x0044a70b / 0x0044a715), the step-5 recentre only when
	// the angles change (0x0041c2bb..0x0041c363).
	void test_warp_pointer() {
		using CryOmni3D::Atlantis::WarpPointer;
		WarpPointer p;
		TS_ASSERT_EQUALS(p.pos, Common::Point(0, 0));
		TS_ASSERT_EQUALS(p.prevAlpha, -1.0f);
		TS_ASSERT_EQUALS(p.prevBeta, -1.0f);
		// The first loop of a session: any view differs from -1.0.
		p.panoramaLoaded();
		TS_ASSERT_EQUALS(p.pos, Common::Point(320, 320));
		TS_ASSERT(p.frame(0, 0, 0.0f, 0.0f, 0.0f, 0.0f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(320, 240));
		// A motion that does not turn the view moves the pointer.
		TS_ASSERT(!p.frame(15, -7, 0.0f, 0.0f, 0.0f, 0.0f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(335, 233));
		// A checkpoint reload at the same angles: the loop keeps (320, 320).
		p.panoramaLoaded();
		TS_ASSERT(!p.frame(0, 0, 0.0f, 0.0f, 0.0f, 0.0f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(320, 320));
		// After a dialog the last mouse position, kept while the view rests.
		p.mouseMoved(Common::Point(500, 400));
		TS_ASSERT(!p.frame(0, 0, 0.0f, 0.0f, 0.0f, 0.0f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(500, 400));
		// The pointer is clamped to the screen.
		TS_ASSERT(!p.frame(400, 400, 0.0f, 0.0f, 0.0f, 0.0f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(639, 479));
		// Momentum moves the view: recentred; the stored angles are the
		// frame's end ones (after the pitch clamp), the test uses the new.
		TS_ASSERT(p.frame(0, 0, 0.01f, 0.9f, 0.01f, 0.785398f));
		TS_ASSERT_EQUALS(p.pos, Common::Point(320, 240));
		TS_ASSERT_EQUALS(p.prevBeta, 0.785398f);
		p.mouseMoved(Common::Point(10, 20));
		TS_ASSERT(p.frame(0, 0, 0.01f, 0.9f, 0.01f, 0.785398f));   // 0.9 != 0.785398
		// The bar close recentres.
		p.mouseMoved(Common::Point(10, 20));
		p.recentre();
		TS_ASSERT_EQUALS(p.pos, Common::Point(320, 240));
	}
};
