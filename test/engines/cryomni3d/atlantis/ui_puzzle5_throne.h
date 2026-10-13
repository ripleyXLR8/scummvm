#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/str.h"

#include "cryomni3d/atlantis/throne_game.h"

// ui audit — puzzle 5 (the throne lock), whole runs.
//
// Expected values come from the ORIGINAL FUN_0043fcb4 of
// atlantis-cd-en-714240 run unmodified under Unicorn with three scripted
// players (win, random_play, leave):
// atlantis-re/tools/emu/ui_puzzle5_throne.py -> evidence/golden/ui_puzzle5_throne.txt.
// Per pass: the input, the sounds, the md5 of the draws and of the state.
//
// Port code exercised directly: ThroneGame (throne_game.cpp).

namespace UiThrone {

using CryOmni3D::Atlantis::ThroneGame;

class Replay : public ThroneGame {
public:
	const Common::Array<Common::String> *lines;
	uint next, end;
	int mismatches;
	Common::String firstMismatch, draws, sounds;
	uint frames;

	Replay() : lines(nullptr), next(0), end(0), mismatches(0), frames(0) {}

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
		byte st[108 + 6 + 12];
		uint o = 0;
		for (int k = 0; k < kNumParts; k++) {
			const int16 v[6] = { parts[k].base, parts[k].count, parts[k].init, parts[k].cur, parts[k].x, parts[k].y };
			for (int j = 0; j < 6; j++) {
				st[o++] = (byte)v[j];
				st[o++] = (byte)((uint16)v[j] >> 8);
			}
		}
		const int16 c[3] = { cursorMode, cursorFrame, cursorStep };
		for (int j = 0; j < 3; j++) {
			st[o++] = (byte)c[j];
			st[o++] = (byte)((uint16)c[j] >> 8);
		}
		const int32 w[3] = { counter, active, result };
		for (int j = 0; j < 3; j++)
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
	void readInput(int &mx, int &my, bool &click) override {
		mx = my = 0;
		click = false;
		if (next >= end)
			return;
		const Common::String &l = (*lines)[next];
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
	void add(const Common::String &s) {
		if (!draws.empty())
			draws += "\n";
		draws += s;
	}
	void drawBackground() override { add("bg"); }
	void drawSprite(char bank, int x, int y, int frame) override {
		add(Common::String::format("%c %d %d %d", bank, x, y, frame));
	}
	void present() override {
		add("present");
		closeLine();
		if (frames >= 3000)
			result = 2;   // the harness stops runaway scenarios
	}
	void playSound(int slot) override {
		sounds += (sounds.empty() ? "" : " ") + Common::String::format("%d", slot);
	}
	void waitTicks(uint32) override {}
	bool aborted() const override { return mismatches != 0; }
};

} // End of namespace UiThrone

class UiPuzzle5ThroneTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_runs_vs_original() {
		using namespace UiThrone;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle5_throne: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle5_throne.txt", golden));
		int scenarios = 0;
		for (uint i = 0; i < golden.size(); i++) {
			if (!golden[i].hasPrefix("scenario "))
				continue;
			// "scenario NAME frames N result R"
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
			const uint lines = (uint)atoi(tok[3].c_str());   // the passes and the closing line
			Replay g;
			g.lines = &golden;
			g.next = i + 1;
			g.end = i + 1 + lines;
			const int got = g.run();
			g.closeLine();   // the sounds after the last pass
			TS_ASSERT_EQUALS(g.firstMismatch, Common::String());
			TS_ASSERT_EQUALS(g.mismatches, 0);
			TS_ASSERT_EQUALS(g.frames, lines);
			TS_ASSERT_EQUALS(got, atoi(tok[5].c_str()));
			scenarios++;
		}
		TS_ASSERT_EQUALS(scenarios, 3);
	}
};
