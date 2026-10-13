#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/str.h"

#include "cryomni3d/atlantis/crab_game.h"

// ui audit — puzzle 15 (the crab race), whole runs.
//
// Expected values come from the ORIGINAL FUN_00448ad0 + FUN_0043d28c of
// atlantis-cd-en-714240 run under Unicorn with scripted players:
// atlantis-re/tools/emu/ui_puzzle15_crab.py -> evidence/golden/ui_puzzle15_crab.txt.
// Per pass: the input, the sounds and comments, the md5 of the draws and
// of the state (crab records, state, hand, last comment, cursor animation,
// result, LCG seed, click flag).  A comment keeps its sample busy for
// kCommentFrames passes; rand is the MSVC generator seeded per scenario.
//
// Port code exercised directly: CrabGame (crab_game.cpp).

namespace UiCrab {

using CryOmni3D::Atlantis::CrabGame;

static const int kCommentFrames = 10;

class Replay : public CrabGame {
public:
	const Common::Array<Common::String> *lines;
	uint next, end;
	int mismatches;
	Common::String firstMismatch, draws, sounds;
	uint frames;
	int commentUntil;
	uint32 randSeed;

	Replay() : lines(nullptr), next(0), end(0), mismatches(0), frames(0), commentUntil(-1), randSeed(1) {}

	void parse(uint idx, int &mx, int &my, bool &click) {
		mx = 320;
		my = 240;
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
		Common::Array<byte> st;
		auto put16 = [&](int16 v) { st.push_back((byte)v); st.push_back((byte)((uint16)v >> 8)); };
		auto put32 = [&](int32 v) { for (int b = 0; b < 4; b++) st.push_back((byte)((uint32)v >> (8 * b))); };
		for (int i = 0; i < kNumCrabs; i++) {
			const Crab &c = crabs[i];
			put16(c.kind); put16(c.x); put16(c.y); put16(c.dir);
			put16(c.phase); put16(c.timer); put16(c.interval); put16(c.reload);
		}
		put32(state); put32(hand); put32(lastComment);
		put16(cursorMode); put16(cursorFrame); put16(cursorStep);
		put32(result);
		put16((int16)lcgSeed);
		put32(click ? 1 : 0);
		const Common::String gotDraws = AtlantisTest::md5Hex((const byte *)draws.c_str(), draws.size());
		const Common::String gotState = AtlantisTest::md5Hex(st.data(), st.size());
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
		parse(next, mx, my, click);
	}
	void add(const Common::String &s) {
		if (!draws.empty())
			draws += "\n";
		draws += s;
	}
	void loadPicture(const char *name) override { add(Common::String("picture ") + name); }
	void drawBackground() override { add("bg"); }
	void drawSprite(char bank, int x, int y, int frame) override {
		add(Common::String::format("%c %d %d %d", bank, x, y, frame));
	}
	void present() override { add("present"); }
	void playSound(int slot) override {
		sounds += (sounds.empty() ? "" : " ") + Common::String::format("%d", slot);
	}
	void playComment(int n) override {
		sounds += (sounds.empty() ? "" : " ") + Common::String::format("c%d", n);
		commentUntil = (int)frames + kCommentFrames;
	}
	bool commentBusy() override { return (int)frames < commentUntil; }
	int rand32767() override {
		randSeed = randSeed * 214013u + 2531011u;
		return (int)((randSeed >> 16) & 0x7fff);
	}
	bool aborted() const override { return mismatches != 0; }
};

} // End of namespace UiCrab

class UiPuzzle15CrabTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_lcg() {
		// FUN_00447524: masks 255, 7 and 3; the seed arithmetic is 16-bit.
		UiCrab::Replay g;
		g.lcgSeed = 0x1234;
		const int a = g.lcg(255);
		TS_ASSERT(a >= 0 && a <= 255);
		TS_ASSERT_EQUALS((int)g.lcgSeed, (int)(uint16)(0x1234 * -0x342f + 1));
		for (int i = 0; i < 100; i++) {
			const int v = g.lcg(6);
			TS_ASSERT(v >= 0 && v <= 6);
			const int w = g.lcg(3);
			TS_ASSERT(w >= 0 && w <= 3);
		}
	}

	void test_runs_vs_original() {
		using namespace UiCrab;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle15_crab: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle15_crab.txt", golden));
		int scenarios = 0;
		for (uint i = 0; i < golden.size(); i++) {
			if (!golden[i].hasPrefix("scenario "))
				continue;
			// "scenario NAME seed S rand R frames N result X"
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
			Replay g;
			g.lines = &golden;
			g.next = i + 1;
			g.end = i + 1 + (uint)atoi(tok[7].c_str());
			g.lcgSeed = (uint16)atoi(tok[3].c_str());
			g.randSeed = (uint32)atoi(tok[5].c_str());
			const int got = g.run();
			g.closeLine();
			TS_ASSERT_EQUALS(g.firstMismatch, Common::String());
			TS_ASSERT_EQUALS(g.mismatches, 0);
			TS_ASSERT_EQUALS(g.frames, (uint)atoi(tok[7].c_str()));
			TS_ASSERT_EQUALS(got, atoi(tok[9].c_str()));
			scenarios++;
		}
		TS_ASSERT_EQUALS(scenarios, 5);
	}
};
