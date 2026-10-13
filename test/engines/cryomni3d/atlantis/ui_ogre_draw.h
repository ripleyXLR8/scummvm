#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"

#include "cryomni3d/atlantis/ogre_draw.h"

// ui audit, task 18 — the ogre game's drawing.
//
// Expected values come from the ORIGINAL FUN_00442b88, FUN_004439c4,
// FUN_004436f4, FUN_00443378 and FUN_00442d78 of atlantis-cd-en-714240 run
// unmodified under Unicorn (sprite calls, presents, sounds, SetCursorPos
// recorded): atlantis-re/tools/emu/ui_ogre_draw.py ->
// evidence/golden/ui_ogre_draw.txt.
//
// Port code exercised directly: OgreDraw (ogre_draw.h), which
// OgreGamePuzzle draws with.

namespace OgreDrawTest {

typedef CryOmni3D::Atlantis::OgreEvent Ev;

inline Common::String line(const char *name, const Common::Array<Ev> &ev) {
	static const char *const kSounds[] = { "", "cyclo1", "cyclo2", "cyclo3", "cyclo4", "cyclo5" };
	Common::String s = Common::String(name) + ":";
	bool first = true;
	for (uint i = 0; i < ev.size(); i++) {
		Common::String t;
		switch (ev[i].kind) {
		case Ev::kSprite: t = Common::String::format("%c %d %d %d", ev[i].set, ev[i].x, ev[i].y, ev[i].frame); break;
		case Ev::kBackground: t = "bg"; break;
		case Ev::kPresent: t = "present"; break;
		case Ev::kSound: t = Common::String("sound ") + kSounds[ev[i].sound]; break;
		case Ev::kStdCursor: t = "stdcursor"; break;
		case Ev::kSetCursorPos: t = Common::String::format("setcursorpos %d %d", ev[i].x, ev[i].y); break;
		case Ev::kWait: continue;
		}
		s += (first ? " " : " | ") + t;
		first = false;
	}
	return s;
}

inline void records(CryOmni3D::Atlantis::OgreRules &r, int hx, int hy, int qx, int qy, int ax, int ay,
                    int bx, int by, int hflag = 1) {
	r.p[0].flag = hflag; r.p[0].x = hx; r.p[0].y = hy;
	r.p[1].flag = 0;     r.p[1].x = qx; r.p[1].y = qy;
	r.p[2].flag = 1;     r.p[2].x = ax; r.p[2].y = ay;
	r.p[3].flag = 1;     r.p[3].x = bx; r.p[3].y = by;
}

} // End of namespace OgreDrawTest

class UiOgreDrawTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using namespace OgreDrawTest;
		using CryOmni3D::Atlantis::OgreDraw;
		using CryOmni3D::Atlantis::OgreRules;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_ogre_draw: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_ogre_draw.txt", golden));
		Common::Array<Common::String> out;
		OgreRules r;
		memset(r.occ, 0, sizeof(r.occ));
		memset(r.mask, 0, sizeof(r.mask));
		for (int k = 0; k < 3; k++)
			r.pathX[k] = r.pathY[k] = 0;
		Common::Array<Ev> ev;

		records(r, 0, 0, 4, 6, 6, 6, 0, 6);
		ev.clear(); OgreDraw::board(ev, r, false, 0, 0); out.push_back(line("board idle", ev));
		records(r, 4, 6, 4, 6, 6, 6, 0, 6);
		ev.clear(); OgreDraw::board(ev, r, false, 0, 0); out.push_back(line("board hero on queen", ev));
		records(r, 3, 3, 4, 6, 6, 6, 0, 6, 0);
		const int held[8][2] = { { 262, 210 }, { 322, 210 }, { 382, 210 }, { 322, 270 }, { 382, 270 },
		                         { 442, 330 }, { 50, 50 }, { 600, 450 } };
		for (int i = 0; i < 8; i++) {
			ev.clear();
			OgreDraw::board(ev, r, true, held[i][0], held[i][1]);
			out.push_back(line(Common::String::format("board held mouse %d,%d", held[i][0], held[i][1]).c_str(), ev));
		}
		const int curs[6][2] = { { 300, 200 }, { 100, 200 }, { 600, 200 }, { 300, 30 }, { 300, 470 }, { 50, 10 } };
		for (int i = 0; i < 6; i++) {
			int mx = curs[i][0], my = curs[i][1];
			ev.clear();
			OgreDraw::cursor(ev, true, 20, 30, mx, my);
			out.push_back(line(Common::String::format("cursor held mouse %d,%d", curs[i][0], curs[i][1]).c_str(), ev));
			out.push_back(Common::String::format("mouse after %d,%d", mx, my));
		}
		const int free[5] = { 49, 50, 300, 590, 591 };
		for (int i = 0; i < 5; i++) {
			int mx = free[i], my = 200;
			ev.clear();
			OgreDraw::cursor(ev, false, 20, 30, mx, my);
			out.push_back(line(Common::String::format("cursor free mouse %d,200", free[i]).c_str(), ev));
		}
		records(r, 0, 0, 4, 6, 6, 6, 0, 6);
		ev.clear(); OgreDraw::reveal(ev, r); out.push_back(line("reveal", ev));
		records(r, 0, 0, 6, 6, 6, 6, 0, 6);
		ev.clear(); OgreDraw::reveal(ev, r); out.push_back(line("reveal on ogre", ev));
		records(r, 4, 6, 4, 6, 6, 6, 0, 6);
		r.pathX[0] = 3; r.pathY[0] = 6; r.pathX[1] = 4; r.pathY[1] = 6; r.pathX[2] = 4; r.pathY[2] = 6;
		ev.clear(); OgreDraw::win(ev, r); out.push_back(line("win", ev));
		records(r, 2, 2, 4, 6, 2, 3, 0, 6);
		ev.clear(); OgreDraw::lose(ev, r); out.push_back(line("lose", ev));

		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}
};
