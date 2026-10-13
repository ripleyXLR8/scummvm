#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/str.h"

#include "cryomni3d/atlantis/taquin_game.h"

// ui audit — puzzles 6, 7 and 14 (the sliding puzzles), whole runs.
//
// Expected values come from the ORIGINAL FUN_00446300, FUN_0044659c and
// FUN_00446830 of atlantis-cd-en-714240 run unmodified under Unicorn with
// scripted players (leave without the piece, solve, random):
// atlantis-re/tools/emu/ui_puzzle6_taquin.py -> evidence/golden/ui_puzzle6_taquin.txt.
// Per segment between two FUN_0043318c calls: the input, the sounds, the
// md5 of the draws and of the state.
//
// Port code exercised directly: TaquinGame (taquin_game.cpp).

namespace UiTaquin {

using CryOmni3D::Atlantis::TaquinGame;

class Replay : public TaquinGame {
public:
	const Common::Array<Common::String> *lines;
	uint next, end;
	int mismatches;
	Common::String firstMismatch, draws, sounds;
	uint frames;

	explicit Replay(int v) : TaquinGame(v), lines(nullptr), next(0), end(0), mismatches(0), frames(0) {}

	void parse(uint idx, int &mx, int &my, bool &click) {
		mx = my = 0;
		click = false;
		if (idx >= end)
			return;
		const Common::String &l = (*lines)[idx];
		Common::Array<Common::String> tok;
		Common::String cur;
		for (uint k = 0; k < l.find('|'); k++) {
			if (l[k] == ' ') {
				if (!cur.empty())
					tok.push_back(cur);
				cur.clear();
			} else {
				cur += l[k];
			}
		}
		mx = atoi(tok[1].c_str());
		my = atoi(tok[2].c_str());
		click = tok[3] == "1";
	}

	void closeLine() {
		if (next >= end) {
			mismatches++;
			return;
		}
		const Common::String &l = (*lines)[next++];
		frames++;
		const uint b1 = l.find('|'), b2 = l.find('|', b1 + 1), b3 = l.find('|', b2 + 1);
		const Common::String wantSnd = l.substr(b1 + 2, b2 - b1 - 3);
		const Common::String wantDraws = l.substr(b2 + 2, b3 - b2 - 3);
		const Common::String wantState = l.substr(b3 + 2);
		byte st[18 + 6 + 6 + 8];
		uint o = 0;
		auto put16 = [&](int16 v) { st[o++] = (byte)v; st[o++] = (byte)((uint16)v >> 8); };
		for (int i = 0; i < 9; i++)
			put16(board[i]);
		put16(movingCell);
		put16(moveDx);
		put16(moveDy);
		put16(cursorMode);
		put16(cursorFrame);
		put16(cursorStep);
		const int32 w[2] = { objectInHand, result };
		for (int j = 0; j < 2; j++)
			for (int b = 0; b < 4; b++)
				st[o++] = (byte)((uint32)w[j] >> (8 * b));
		const Common::String gotDraws = AtlantisTest::md5Hex((const byte *)draws.c_str(), draws.size());
		const Common::String gotState = AtlantisTest::md5Hex(st, sizeof(st));
		if (gotDraws != wantDraws || gotState != wantState || sounds != wantSnd) {
			if (mismatches == 0)
				firstMismatch = Common::String::format("line '%s': sounds '%s' draws %s state %s", l.c_str(),
				                                       sounds.c_str(), gotDraws == wantDraws ? "ok" : "differ",
				                                       gotState == wantState ? "ok" : "differ");
			mismatches++;
		}
		draws.clear();
		sounds.clear();
	}

protected:
	void startInput(int &mx, int &my, bool &click) override { parse(next, mx, my, click); }
	void boundary(int &mx, int &my, bool &click) override {
		closeLine();
		if (frames >= 1500)
			result = 2;   // the harness stops runaway scenarios
		parse(next, mx, my, click);
	}
	void add(const Common::String &s) {
		if (!draws.empty())
			draws += "\n";
		draws += s;
	}
	void drawBackground() override { add("bg"); }
	void drawSprite(char bank, int x, int y, int frame) override {
		add(Common::String::format("%c %d %d %d", bank, x, y, frame));
	}
	void drawStandardCursor(int, int) override { add("stdcursor"); }
	void present() override { add("present"); }
	void playSound(int slot) override {
		sounds += (sounds.empty() ? "" : " ") + Common::String::format("%d", slot);
	}
	bool aborted() const override { return mismatches != 0; }
};

} // End of namespace UiTaquin

class UiPuzzle6TaquinTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_runs_vs_original() {
		using namespace UiTaquin;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle6_taquin: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle6_taquin.txt", golden));
		int scenarios = 0;
		for (uint i = 0; i < golden.size(); i++) {
			if (!golden[i].hasPrefix("scenario "))
				continue;
			// "scenario NAME variant V held H frames N result R"
			Common::Array<Common::String> tok;
			Common::String cur;
			for (uint k = 0; k <= golden[i].size(); k++) {
				if (k == golden[i].size() || golden[i][k] == ' ') {
					tok.push_back(cur);
					cur.clear();
				} else {
					cur += golden[i][k];
				}
			}
			Replay g(atoi(tok[3].c_str()));
			g.objectInHand = atoi(tok[5].c_str());
			g.lines = &golden;
			g.next = i + 1;
			g.end = i + 1 + (uint)atoi(tok[7].c_str());
			const int got = g.run();
			g.closeLine();
			TS_ASSERT_EQUALS(g.firstMismatch, Common::String());
			TS_ASSERT_EQUALS(g.mismatches, 0);
			TS_ASSERT_EQUALS(g.frames, (uint)atoi(tok[7].c_str()));
			TS_ASSERT_EQUALS(got, atoi(tok[9].c_str()));
			scenarios++;
		}
		TS_ASSERT_EQUALS(scenarios, 9);
	}
};
