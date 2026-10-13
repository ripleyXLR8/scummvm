#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/snakes_rules.h"

// ui audit, task 18 — puzzle 8 (StartPuzzle=8, CHAPI034): four snakes in a maze.
//
// Expected values come from the ORIGINAL code of atlantis-cd-en-714240 run
// unmodified under Unicorn: the PCX decoder FUN_0043a53c on
// PUZZLES\CHEMIN00.PCX, the placement click FUN_0043c35c, the steering
// click FUN_0043c6bc and the frame FUN_0043bebc (win test and sprite draws):
// atlantis-re/tools/emu/ui_puzzle8_snakes.py -> evidence/golden/ui_puzzle8_snakes.txt.
//
// Port code exercised directly: SnakesRules (snakes_rules.h), the rules
// SnakesPuzzle runs.

namespace UiSnakes {

typedef CryOmni3D::Atlantis::SnakesRules Rules;

inline bool readPcx(Common::Array<byte> &out) {
	SearchMan.addDirectory("ui_snakes_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
	CryOmni3D::Atlantis::BigFileArchive big;
	for (int cd = 1; cd <= 4; cd++)
		big.open(Common::Path(Common::String::format("BIGCD%d.BIG", cd)));
	SearchMan.remove("ui_snakes_data");
	Common::ScopedPtr<Common::SeekableReadStream> s(big.createReadStreamForMember(Common::String("PUZZLES\\CHEMIN00.PCX")));
	if (!s)
		return false;
	out.resize((uint)s->size());
	s->read(out.data(), out.size());
	return true;
}

inline Common::String snakeRepr(const Rules::Snake &s) {
	Common::String d;
	for (int i = 0; i < s.len; i++)
		d += Common::String::format("%02x", s.dirs[i]);
	return Common::String::format("%d,%d,%d,", s.x, s.y, s.len) + d;
}

inline Common::String stateRepr(const Rules &r) {
	Common::String out;
	for (int k = 0; k < Rules::kNumSnakes; k++) {
		if (k)
			out += " | ";
		out += snakeRepr(r.snakes[k]);
	}
	return out;
}

inline Common::String tupleRepr(const int *v, int n) {
	Common::String s = "(";
	for (int i = 0; i < n; i++)
		s += Common::String::format(i ? ", %d" : "%d", v[i]);
	return s + ")";
}

inline Common::String drawRepr(const Rules &r) {
	Common::Array<Rules::Draw> d;
	r.draws(d);
	Common::String s;
	for (uint i = 0; i < d.size(); i++)
		s += Common::String::format(i ? " %d,%d,%d" : "%d,%d,%d", d[i].x, d[i].y, d[i].frame);
	return s;
}

inline int won(Rules &r, int mouseX, int mouseY) {
	r.result = 0;
	r.frame(mouseX, mouseY);
	return r.result;
}

static const int kOffsets[4][2] = { { 8, 0 }, { -8, 0 }, { 0, -8 }, { 0, 8 } };

struct Placement {
	int holes[4];
	Rules state;
};

inline void placeAll(int k, const Rules &cur, int *chosen, Common::Array<Placement> &out) {
	if (k == 4) {
		Placement p;
		for (int i = 0; i < 4; i++)
			p.holes[i] = chosen[i];
		p.state = cur;
		out.push_back(p);
		return;
	}
	for (int h = 0; h < Rules::kNumHoles; h++) {
		if (cur.holes[h].used)
			continue;
		Rules r = cur;
		r.place(r.pieces[k].x, r.pieces[k].y);
		r.place(r.holes[h].x, r.holes[h].y);
		if (!r.pieces[k].placed)
			continue;
		chosen[k] = h;
		placeAll(k + 1, r, chosen, out);
	}
}

} // End of namespace UiSnakes

class UiPuzzleSnakesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using namespace UiSnakes;
		if (!AtlantisTest::haveData()) {
			TS_WARN("ui_puzzle_snakes: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle8_snakes.txt", golden));
		Common::Array<byte> pcx;
		TS_ASSERT(readPcx(pcx));
		if (pcx.empty())
			return;
		Common::Array<Common::String> out;

		Rules start;
		TS_ASSERT(start.decodeMap(pcx.data(), pcx.size()));
		out.push_back("map md5 " + AtlantisTest::md5Hex(start.map.data(), start.map.size()));
		uint hist[256] = {};
		for (uint i = 0; i < start.map.size(); i++)
			hist[start.map[i]]++;
		Common::String hl = "map values";
		for (int v = 0; v < 256; v++)
			if (hist[v])
				hl += Common::String::format(" %d=%u", v, hist[v]);
		out.push_back(hl);
		Common::String holes = "holes (used, x, y, partner, dir)";
		for (int h = 0; h < Rules::kNumHoles; h++) {
			const Rules::Hole &o = start.holes[h];
			const int v[5] = { o.used, o.x, o.y, o.partner, o.dir };
			holes += " " + tupleRepr(v, 5);
		}
		out.push_back(holes);

		const int corners[3][2] = { { 0, 0 }, { 5, 0 }, { 0, 5 } };
		for (int i = 0; i < 3; i++) {
			Rules r = start;
			r.place(40, 448);
			out.push_back(Common::String::format("carrying piece 0 at (%d, %d): result %d", corners[i][0],
			                                     corners[i][1], won(r, corners[i][0], corners[i][1])));
		}

		const int edges[4] = { 29, 30, 620, 621 };
		for (int i = 0; i < 4; i++) {
			Rules r = start;
			r.place(edges[i], 448);
			out.push_back(Common::String::format("placement click at (%d, 448): result %d", edges[i], r.result));
		}

		Common::Array<Placement> placements;
		int chosen[4];
		placeAll(0, start, chosen, placements);
		out.push_back(Common::String::format("placements %u", placements.size()));

		uint totalMoves = 0;
		for (uint pi = 0; pi < placements.size(); pi++) {
			Rules r = placements[pi].state;
			const Common::String st0 = stateRepr(r);
			uint32 x = pi + 1;
			Common::String lines = st0;
			uint moved = 0;
			int mx = 0, my = 0;
			for (int n = 0; n < 400; n++) {
				x = (x * 1103515245u + 12345u) & 0x7fffffff;
				const int k = (x >> 16) % 4, di = (x >> 8) % 4;
				const Common::String before = stateRepr(r);
				mx = r.snakes[k].x + kOffsets[di][0];
				my = r.snakes[k].y + kOffsets[di][1];
				const Rules::Sound snd = r.steer(mx, my);
				const Common::String after = stateRepr(r);
				moved += (after != before);
				lines += Common::String::format("\ns%dd%d -> ", k, di) + after +
				         Common::String::format(" %d", snd != Rules::kSoundNone ? 1 : 0);
			}
			Rules w = r;
			const int win = won(w, mx, my);
			totalMoves += moved;
			const Common::String dl = drawRepr(r);
			out.push_back("placement " + tupleRepr(placements[pi].holes, 4) + " start " + st0 + " end " +
			              stateRepr(r) + Common::String::format(" moves %u won %d md5 ", moved, win) +
			              AtlantisTest::md5Hex((const byte *)lines.c_str(), lines.size()) + " draws " +
			              AtlantisTest::md5Hex((const byte *)dl.c_str(), dl.size()));
		}
		out.push_back(Common::String::format("total moves %u", totalMoves));

		// The solution the harness found, replayed.
		for (uint i = 0; i < golden.size(); i++) {
			if (!golden[i].hasPrefix("solved placement "))
				continue;
			const Common::String &g = golden[i];
			int h[4];
			TS_ASSERT_EQUALS(sscanf(g.c_str(), "solved placement (%d, %d, %d, %d)", &h[0], &h[1], &h[2], &h[3]), 4);
			uint pi = 0;
			while (pi < placements.size() && memcmp(placements[pi].holes, h, sizeof(h)))
				pi++;
			TS_ASSERT(pi < placements.size());
			if (pi >= placements.size())
				break;
			Rules r = placements[pi].state;
			const char *c = strstr(g.c_str(), "clicks ");
			Common::String line = "solved placement " + tupleRepr(h, 4) + " clicks";
			int k, di, mx = 0, my = 0;
			for (c = c ? c + 7 : ""; sscanf(c, "s%dd%d", &k, &di) == 2;) {
				mx = r.snakes[k].x + kOffsets[di][0];
				my = r.snakes[k].y + kOffsets[di][1];
				r.steer(mx, my);
				line += Common::String::format(" s%dd%d", k, di);
				c = strchr(c, ' ');
				if (!c)
					break;
				c++;
			}
			out.push_back(line);
			Rules w = r;
			out.push_back("solved end " + stateRepr(r) + Common::String::format(" won %d", won(w, mx, my)));
			out.push_back("solved draws " + drawRepr(r));
			break;
		}

		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}
};
