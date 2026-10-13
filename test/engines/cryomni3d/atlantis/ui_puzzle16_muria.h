#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/str.h"

#include "cryomni3d/atlantis/muria_game.h"

// ui audit — puzzle 16 (Muria), whole runs.
//
// Expected values come from the ORIGINAL FUN_00448ad0 + FUN_0043ee54 of
// atlantis-cd-en-714240 run under Unicorn with scripted players:
// atlantis-re/tools/emu/ui_puzzle16_muria.py -> evidence/golden/ui_puzzle16_muria.txt.
// Per pass: the input, the sounds and script hooks, the md5 of the draws
// and of the state (piece and slot records, hand, cursor animation,
// result, click flag, FLAGTEMP3).
//
// Port code exercised directly: MuriaGame (muria_game.cpp).

namespace UiMuria {

using CryOmni3D::Atlantis::MuriaGame;

class Replay : public MuriaGame {
public:
	const Common::Array<Common::String> *lines;
	uint next, end;
	int mismatches;
	Common::String firstMismatch, draws, sounds;
	uint frames;

	Replay() : lines(nullptr), next(0), end(0), mismatches(0), frames(0) {}

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
		for (int i = 0; i < kNumPieces; i++) {
			put16(pieces[i].kind); put16(pieces[i].x); put16(pieces[i].y); put16(pieces[i].slot);
		}
		for (int i = 0; i < kNumSlots; i++) {
			put16(slots[i].kind); put16(slots[i].x); put16(slots[i].y);
			put16(slots[i].cx); put16(slots[i].cy); put16(slots[i].piece);
		}
		put32(hand);
		put16(cursorMode); put16(cursorFrame); put16(cursorStep);
		put32(result);
		put32(click ? 1 : 0);
		put32(flagTemp3);
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
	void addSound(const Common::String &s) {
		sounds += (sounds.empty() ? "" : " ") + s;
	}
	void loadPicture(const char *name) override { add(Common::String("picture ") + name); }
	void drawBackground() override { add("bg"); }
	void drawSprite(char bank, int x, int y, int frame) override {
		add(Common::String::format("%c %d %d %d", bank, x, y, frame));
	}
	void present() override { add("present"); }
	void playSound(int slot) override { addSound(Common::String::format("%d", slot)); }
	void scriptHook() override { addSound("script"); }
	bool aborted() const override { return mismatches != 0; }
};

} // End of namespace UiMuria

class UiPuzzle16MuriaTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_slot_at() {
		// FUN_0043f750: the top row is aimed 50 pixels above its click
		// point, the others at it; the nearest within 40 wins.
		UiMuria::Replay g;
		TS_ASSERT_EQUALS(g.slotAt(186, 134), 0);
		TS_ASSERT_EQUALS(g.slotAt(186, 184), 0);   // 25 away: still slot 0
		TS_ASSERT_EQUALS(g.slotAt(186, 300), -1);
		TS_ASSERT_EQUALS(g.slotAt(175, 227), 4);
		TS_ASSERT_EQUALS(g.slotAt(478, 269), 11);
		TS_ASSERT_EQUALS(g.slotAt(320, 420), -1);
	}

	void test_runs_vs_original() {
		using namespace UiMuria;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle16_muria: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle16_muria.txt", golden));
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
			Replay g;
			g.lines = &golden;
			g.next = i + 1;
			g.end = i + 1 + (uint)atoi(tok[3].c_str());
			const int got = g.run();
			g.closeLine();
			TS_ASSERT_EQUALS(g.firstMismatch, Common::String());
			TS_ASSERT_EQUALS(g.mismatches, 0);
			TS_ASSERT_EQUALS(g.frames, (uint)atoi(tok[3].c_str()));
			TS_ASSERT_EQUALS(got, atoi(tok[5].c_str()));
			scenarios++;
		}
		TS_ASSERT_EQUALS(scenarios, 4);
	}
};
