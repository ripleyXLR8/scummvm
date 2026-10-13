#include "atlantis_test_helper.h"

#include "cryomni3d/atlantis/puzzle_eclipse.h"

// ui audit — eclipse puzzle (StartPuzzle=0) rules vs the original.
//
// Expected values were produced by running the ORIGINAL click handler of
// atlantis-cd-en-714240 (FUN_00441cd8, with FUN_00441f50 / FUN_00441c68)
// unmodified under Unicorn: atlantis-re/tools/emu/ui_puzzles_eclipse.py,
// output in atlantis-re/evidence/golden/ui_puzzle_eclipse.txt.
//
// FAITHFUL COPY WARNING: the port's rules live in private members and in
// EclipsePuzzle::run() (engines/cryomni3d/atlantis/puzzle_eclipse.cpp), which
// need a running engine (sprite/TGA loading, event loop) and cannot be called
// from a test.  The namespace PortCopy below is a line-for-line copy of the
// port's hit-zone constants, EclipsePuzzle::tick(),
// EclipsePuzzle::isWin() and the click branch of EclipsePuzzle::run(), taken
// from scummvm branch atlantis-v2 as audited on 2026-10-12.  Extraction that
// would make the real code testable: move tick/isWin/the click classifier
// into a free function or an engine-independent EclipseRules class.

namespace UiEclipse {
namespace PortCopy {

// The record table is the port's own (puzzle_eclipse.h, header-only).
static const CryOmni3D::Atlantis::EclipseRecordInit *const kRecordInit = CryOmni3D::Atlantis::kEclipseRecordInit;
static const int kLever1X0 = 0x78 + 1, kLever1X1 = 0xf0;
static const int kLever2X0 = 0x1ae + 1, kLever2X1 = 0x226;
static const int kLeverY0 = 0x172 + 1, kLeverY1 = 0x1e0, kLeverYUp = 0x1a9 + 1;
static const int kExitLeftX0 = 0, kExitLeftX1 = 0xa0;
static const int kExitRightX0 = 0x21d, kExitRightX1 = 0x280, kExitY1 = 0xdc;
static const int kLever1Records[] = { 2, 5, 6 };
static const int kLever2Records[] = { 3, 4 };

struct Record { int dir, x, y, min, max, curr; };

struct Puzzle {
	Record r[7];
	Puzzle() {
		for (int i = 0; i < 7; i++) {
			r[i].dir = kRecordInit[i].dir; r[i].x = kRecordInit[i].x; r[i].y = kRecordInit[i].y;
			r[i].min = kRecordInit[i].min; r[i].max = kRecordInit[i].max;
			r[i].curr = (i >= 2) ? (r[i].min + 0x25) : r[i].min;
		}
	}
	void tick(int i) {
		Record &x = r[i];
		if (x.dir == 0) return;
		if (x.dir == 1) { x.curr++; if (x.curr > x.max) x.curr = x.min; }
		else if (x.dir == 2) { x.curr--; if (x.curr < x.min) x.curr = x.max; }
	}
	bool isWin() const { return r[4].curr == r[4].min + 1 && r[5].curr == r[5].min; }
	// Click branch of run(); returns 'E' (exit) or 0.
	char click(int mx, int my) {
		bool inExitL = (mx >= kExitLeftX0 && mx < kExitLeftX1 && my >= 0 && my < kExitY1);
		bool inExitR = (mx >= kExitRightX0 && mx < kExitRightX1 && my >= 0 && my < kExitY1);
		if (inExitL || inExitR)
			return 'E';
		if (my >= kLeverY0 && my < kLeverY1) {
			int dir = (my < kLeverYUp) ? 1 : 2;
			if (mx >= kLever1X0 && mx < kLever1X1) {
				for (uint k = 0; k < ARRAYSIZE(kLever1Records); k++) {
					int idx = kLever1Records[k];
					r[idx].dir = dir; tick(idx); r[idx].dir = 0;
				}
			} else if (mx >= kLever2X0 && mx < kLever2X1) {
				for (uint k = 0; k < ARRAYSIZE(kLever2Records); k++) {
					int idx = kLever2Records[k];
					r[idx].dir = dir; tick(idx); r[idx].dir = 0;
				}
			}
		}
		return 0;
	}
};

} // namespace PortCopy

// Original .data record table @ 0x49745e (dir, x, y, min, max, curr).
static const int kOrigTable[7][6] = {
	{ 1, 215, 308,   0,   7,   0 },
	{ 1, 298, 308,   8,  15,   8 },
	{ 0, 227, 384,  16,  86,  16 },
	{ 0, 443, 373,  87, 157,  87 },
	{ 0, 319, 204,   0,  70,   0 },
	{ 0, 236, 222,  71, 141,  71 },
	{ 0, 271, 198, 142, 212, 142 },
};

} // namespace UiEclipse

class UiPuzzleEclipseTestSuite : public CxxTest::TestSuite {
public:
	// Geometry of the record table (x, y, min, max) and the initial frame.
	void test_record_table_geometry() {
		UiEclipse::PortCopy::Puzzle p;
		for (int i = 0; i < 7; i++) {
			TS_ASSERT_EQUALS(p.r[i].x, UiEclipse::kOrigTable[i][1]);
			TS_ASSERT_EQUALS(p.r[i].y, UiEclipse::kOrigTable[i][2]);
			TS_ASSERT_EQUALS(p.r[i].min, UiEclipse::kOrigTable[i][3]);
			TS_ASSERT_EQUALS(p.r[i].max, UiEclipse::kOrigTable[i][4]);
			// FUN_0044154c @ 0x4415df: curr = min + 0x25 for records 2..6;
			// records 0/1 keep their .data curr.
			int exp = (i >= 2) ? UiEclipse::kOrigTable[i][3] + 0x25 : UiEclipse::kOrigTable[i][5];
			TS_ASSERT_EQUALS(p.r[i].curr, exp);
		}
	}

	// Record 0 (PZ2L frames 0..7) has dir = 1 in the original table, so it
	// cycles on its own like record 1.
	void test_record_dirs() {
		UiEclipse::PortCopy::Puzzle p;
		for (int i = 0; i < 7; i++)
			TS_ASSERT_EQUALS(p.r[i].dir, UiEclipse::kOrigTable[i][0]);
	}

	// Every pixel of the 640x480 screen: action class of a click.
	void test_hitmap_every_pixel() {
		Common::String all;
		for (int y = 0; y < 480; y++) {
			if (y)
				all += '\n';
			for (int x = 0; x < 640; x++) {
				UiEclipse::PortCopy::Puzzle p;
				int before[7];
				for (int i = 0; i < 7; i++) before[i] = p.r[i].curr;
				char c = p.click(x, y);
				if (!c) {
					c = '.';
					if (p.r[2].curr != before[2]) c = (p.r[2].curr == before[2] + 1) ? 'a' : 'b';
					else if (p.r[3].curr != before[3]) c = (p.r[3].curr == before[3] + 1) ? 'c' : 'd';
				}
				all += c;
			}
		}
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex((const byte *)all.c_str(), all.size()),
		                 Common::String("9b84e4e2bbed965a729f29faa97b3208"));
	}

	// All 71 x 71 reachable lever states x 4 lever actions: next state and
	// win flag (original: DAT_007bebf4 set by FUN_00441cd8 @ 0x441f32).
	void test_all_transitions() {
		static const int clicks[4][2] = { {180, 400}, {180, 450}, {490, 400}, {490, 450} };
		Common::String all;
		int wins = 0;
		for (int o1 = 0; o1 < 71; o1++)
			for (int o2 = 0; o2 < 71; o2++)
				for (int k = 0; k < 4; k++) {
					UiEclipse::PortCopy::Puzzle p;
					for (int i = 2; i < 7; i++) {
						int n = p.r[i].max - p.r[i].min + 1;
						int off = (i == 2 || i == 5 || i == 6) ? o1 : o2;
						p.r[i].curr = p.r[i].min + (0x25 + off) % n;
					}
					p.click(clicks[k][0], clicks[k][1]);
					int n1 = ((p.r[2].curr - p.r[2].min - 0x25) % 71 + 71) % 71;
					int n2 = ((p.r[3].curr - p.r[3].min - 0x25) % 71 + 71) % 71;
					// Lever-1 records 5/6 and lever-2 record 4 must stay in step.
					TS_ASSERT_EQUALS(((p.r[5].curr - p.r[5].min - 0x25) % 71 + 71) % 71, n1);
					TS_ASSERT_EQUALS(((p.r[6].curr - p.r[6].min - 0x25) % 71 + 71) % 71, n1);
					TS_ASSERT_EQUALS(((p.r[4].curr - p.r[4].min - 0x25) % 71 + 71) % 71, n2);
					int w = p.isWin() ? 1 : 0;
					wins += w;
					if (!all.empty())
						all += '\n';
					all += Common::String::format("%d %d %c %d %d %d", o1, o2, "abcd"[k], n1, n2, w);
				}
		TS_ASSERT_EQUALS(wins, 4);
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex((const byte *)all.c_str(), all.size()),
		                 Common::String("5a0ebcac450a944f548262fbc74b97a1"));
	}

	// The single winning configuration: lever-1 offset 34, lever-2 offset 35
	// from the starting position (sun record 4 at frame 1, moon record 5 at 71).
	void test_win_state() {
		UiEclipse::PortCopy::Puzzle p;
		for (int i = 0; i < 34; i++) p.click(180, 400);
		TS_ASSERT(!p.isWin());
		for (int i = 0; i < 35; i++) p.click(490, 400);
		TS_ASSERT(p.isWin());
		TS_ASSERT_EQUALS(p.r[4].curr, 1);
		TS_ASSERT_EQUALS(p.r[5].curr, 71);
		TS_ASSERT_EQUALS(p.r[6].curr, 142);
	}
};
