#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/str.h"

#include "cryomni3d/atlantis/rat_rules.h"

// ui audit — puzzle 4 (the rat's run), frame rules.
//
// Expected values come from the ORIGINAL FUN_004387ec, FUN_004395f4,
// FUN_0043947c and FUN_00439d58 of atlantis-cd-en-714240 run unmodified
// under Unicorn on a fixed pseudo-random input sequence:
// atlantis-re/tools/emu/ui_puzzle4_rules.py ->
// evidence/golden/ui_puzzle4_rules.txt.
//
// Port code exercised directly: RatRules (rat_rules.cpp).

class UiPuzzle4RulesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_frames_vs_original() {
		using CryOmni3D::Atlantis::RatRules;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle4_rules: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle4_rules.txt", golden));
		RatRules r;
		bool initChecked = false;
		r.reset();
		// The rounds activate records 0..9 and 11..28.
		for (int k = 0; k < 29; k++)
			if (k != 10)
				r.setB(RatRules::rec(k, RatRules::kActive), 1);
		int steps = 0, bad = 0;
		for (uint li = 0; li < golden.size(); li++) {
			const Common::String &l = golden[li];
			if (l.hasPrefix("#") || l.hasPrefix("state"))
				continue;
			if (l.hasPrefix("init ")) {
				RatRules fresh;
				Common::String hex;
				for (uint k = 0; k < RatRules::kSize; k++)
					hex += Common::String::format("%02x", fresh.mem[k]);
				TS_ASSERT_EQUALS(hex, l.substr(5));
				initChecked = true;
				continue;
			}
			// "i mx my click held ticks byte [cc0=v] [clear] | sounds | md5"
			const uint bar1 = l.find('|');
			const uint bar2 = l.find('|', bar1 + 1);
			Common::Array<Common::String> tok;
			Common::String cur;
			for (uint k = 0; k < bar1; k++) {
				if (l[k] == ' ') {
					if (!cur.empty())
						tok.push_back(cur);
					cur.clear();
				} else {
					cur += l[k];
				}
			}
			const int mx = atoi(tok[1].c_str()), my = atoi(tok[2].c_str());
			const bool click = tok[3] == "1", held = tok[4] == "1";
			const uint32 ticks = (uint32)strtoul(tok[5].c_str(), nullptr, 10);
			if (tok[6] != "-")
				r.setB(0x00496ca5, (byte)atoi(tok[6].c_str()));
			for (uint k = 7; k < tok.size(); k++) {
				if (tok[k].hasPrefix("cc0="))
					r.setI(0x00496cc0, atoi(tok[k].c_str() + 4));
				else if (tok[k] == "clear") {
					r.setB(0x004969c2, 0);
					r.result = 0;
				}
			}
			Common::Array<int> sounds;
			r.clicks(mx, my, click, held, sounds);
			r.frameUpdates(ticks, sounds);
			Common::String snd;
			for (uint k = 0; k < sounds.size(); k++)
				snd += (k ? " " : "") + Common::String::format("%d", sounds[k]);
			byte st[RatRules::kSize + 8];
			memcpy(st, r.mem, RatRules::kSize);
			for (int k = 0; k < 4; k++) {
				st[RatRules::kSize + k] = (byte)(r.clockTicked >> (8 * k));
				st[RatRules::kSize + 4 + k] = (byte)((uint32)r.result >> (8 * k));
			}
			const Common::String want = l.substr(bar2 + 2);
			const Common::String wantSnd = l.substr(bar1 + 2, bar2 - bar1 - 3);
			const Common::String got = AtlantisTest::md5Hex(st, sizeof(st));
			if (got != want || snd != wantSnd) {
				if (bad < 3) {
					TS_ASSERT_EQUALS(snd, wantSnd);
					TS_ASSERT_EQUALS(got, want + " (step " + tok[0] + ")");
				}
				bad++;
				break;   // the states diverge from here on
			}
			steps++;
		}
		TS_ASSERT(initChecked);
		TS_ASSERT_EQUALS(bad, 0);
		TS_ASSERT_EQUALS(steps, 6000);
	}
};
