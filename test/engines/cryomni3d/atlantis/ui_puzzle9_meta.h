#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/md5.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/meta_game.h"

// ui audit — puzzles 9..12 (the metal pieces), whole runs.
//
// Expected values come from the ORIGINAL FUN_00448ad0 + FUN_0044763c of
// atlantis-cd-en-714240 run under Unicorn (the sprite blitter running for
// real) with scripted players: stages 1..3 won in one memory, stage 4 and
// stage 1 played at random, a leave:
// atlantis-re/tools/emu/ui_puzzle9_meta.py -> evidence/golden/ui_puzzle9_meta.txt.
// Per pass: the input, the sounds, the md5 of the draws and of the state
// (with the md5 of the class map).
//
// Port code exercised directly: MetaGame (meta_game.cpp).

namespace UiMeta {

using CryOmni3D::Atlantis::MetaGame;

struct Data {
	Common::Array<byte> map, spr;
};

inline bool readData(Data &d) {
	SearchMan.addDirectory("ui_meta_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
	CryOmni3D::Atlantis::BigFileArchive big;
	for (int cd = 1; cd <= 4; cd++)
		big.open(Common::Path(Common::String::format("BIGCD%d.BIG", cd)));
	SearchMan.remove("ui_meta_data");
	Common::ScopedPtr<Common::SeekableReadStream> b(big.createReadStreamForMember(Common::String("PUZZLES\\PZMETAM.BMP")));
	Common::ScopedPtr<Common::SeekableReadStream> s(big.createReadStreamForMember(Common::String("PUZZLES\\PZMETAP1.SPR")));
	if (!b || !s || b->size() < 0x436 + (int64)MetaGame::kMapSize)
		return false;
	b->seek(0x436);
	d.map.resize(MetaGame::kMapSize);
	b->read(d.map.data(), d.map.size());
	d.spr.resize((uint)s->size());
	s->read(d.spr.data(), d.spr.size());
	return true;
}

class Replay : public MetaGame {
public:
	const Data *data;
	const Common::Array<Common::String> *lines;
	uint next, end;
	int mismatches;
	Common::String firstMismatch, draws, sounds;
	uint frames;

	Replay() : data(nullptr), lines(nullptr), next(0), end(0), mismatches(0), frames(0) {}

	void parse(uint idx, int &mx, int &my, bool &click, bool &right) {
		mx = 320;
		my = 470;
		click = right = false;
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
		right = tok[4] == "1";
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
			const Piece &p = pieces[i];
			put16(p.base); put16(p.unk2); put16(p.cls); put16(p.orient); put16(p.x); put16(p.y); put16(p.unkC);
		}
		put16(count);
		put16(cursorMode); put16(cursorFrame); put16(cursorStep);
		put32(grabX); put32(grabY); put32(repeat); put32(unkF4); put32(held);
		put16(picture);
		put32(objectInHand); put32(broughtFlag); put32(result);
		Common::MemoryReadStream ms(map.data(), map.size());
		byte digest[16];
		Common::computeStreamMD5(ms, digest);
		for (int k = 0; k < 16; k++)
			st.push_back(digest[k]);
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
	void startInput(int &mx, int &my, bool &click, bool &right) override { parse(next, mx, my, click, right); }
	void boundary(int &mx, int &my, bool &click, bool &right) override {
		closeLine();
		parse(next, mx, my, click, right);
	}
	void loadPicture(int) override {}
	void reloadMap() override { map = data->map; }
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
	int currentPlace() override { return 0; }
	bool aborted() const override { return mismatches != 0; }
};

} // End of namespace UiMeta

class UiPuzzle9MetaTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_runs_vs_original() {
		using namespace UiMeta;
		if (AtlantisTest::goldenDir().empty() || AtlantisTest::dataDir().empty()) {
			TS_WARN("ui_puzzle9_meta: no golden or data directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle9_meta.txt", golden));
		Data data;
		TS_ASSERT(readData(data));
		// win_stage1..3 and wander_stage4 share one memory; the others start
		// fresh.
		Common::ScopedPtr<Replay> g;
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
			if (!g || !(tok[1].hasPrefix("win_stage") || tok[1] == "wander_stage4")) {
				g.reset(new Replay());
				TS_ASSERT(g->loadCoverage(data.spr.data(), data.spr.size()));
				g->data = &data;
			}
			g->lines = &golden;
			g->next = i + 1;
			g->end = i + 1 + (uint)atoi(tok[7].c_str());
			g->frames = 0;
			g->objectInHand = atoi(tok[5].c_str());
			const int got = g->run(atoi(tok[3].c_str()));
			g->closeLine();
			TS_ASSERT_EQUALS(g->firstMismatch, Common::String());
			TS_ASSERT_EQUALS(g->mismatches, 0);
			TS_ASSERT_EQUALS(g->frames, (uint)atoi(tok[7].c_str()));
			TS_ASSERT_EQUALS(got, atoi(tok[9].c_str()));
			scenarios++;
		}
		TS_ASSERT_EQUALS(scenarios, 6);
	}
};
