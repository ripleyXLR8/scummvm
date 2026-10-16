#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/algorithm.h"
#include "common/archive.h"
#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/pieces_rules.h"

// ui audit, task 18 — puzzle 3 (StartPuzzle=3, CHAPI029): six rotating pieces.
//
// Expected values come from the ORIGINAL click handler FUN_004460c8 and
// set-up FUN_00445ba0 of atlantis-cd-en-714240, run unmodified under Unicorn
// over the real hit map PUZZLES\PUZSRP2.BMP:
// atlantis-re/tools/emu/ui_puzzle3_serpent.py -> evidence/golden/ui_puzzle3_serpent.txt.
//
// Port code exercised directly: RotatingPiecesRules (pieces_rules.h), the
// rules RotatingPiecesPuzzle runs.

namespace UiPieces {

typedef CryOmni3D::Atlantis::RotatingPiecesRules Rules;

inline Common::String tuple6(const int16 *v) {
	return Common::String::format("(%d, %d, %d, %d, %d, %d)", v[0], v[1], v[2], v[3], v[4], v[5]);
}

inline Common::String stateRepr(const Rules &r) {
	return "(" + tuple6(r.pieces) + ", " + tuple6(r.counters) + ")";
}

inline bool readHitMap(Common::Array<byte> &out) {
	SearchMan.addDirectory("ui_pieces_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
	CryOmni3D::Atlantis::BigFileArchive big;
	for (int cd = 1; cd <= 4; cd++)
		big.open(Common::Path(Common::String::format("BIGCD%d.BIG", cd)));
	SearchMan.remove("ui_pieces_data");
	Common::ScopedPtr<Common::SeekableReadStream> s(big.createReadStreamForMember(Common::String("PUZZLES\\PUZSRP2.BMP")));
	if (!s)
		return false;
	out.resize((uint)s->size());
	s->read(out.data(), out.size());
	return true;
}

} // End of namespace UiPieces

class UiPuzzlePiecesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Start state, click map and every reachable state, line by line against
	// the emulated original.
	void test_vs_original() {
		using namespace UiPieces;
		if (!AtlantisTest::haveData()) {
			TS_WARN("ui_puzzle_pieces: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle3_serpent.txt", golden));
		Common::Array<byte> map;
		TS_ASSERT(readHitMap(map));
		if (map.empty())
			return;
		Common::Array<Common::String> out;

		const Rules start;
		out.push_back("start pieces " + tuple6(start.pieces) + " counters " + tuple6(start.counters));

		// Click map from the start state.
		Common::String all;
		uint counts[128] = {};
		int repX[13], repY[13];
		for (int v = 0; v < 13; v++)
			repX[v] = -1;
		for (int y = 0; y < 480; y++) {
			if (y)
				all += '\n';
			for (int x = 0; x < 640; x++) {
				Rules r;
				char c = '.';
				switch (r.click(map, x, y)) {
				case Rules::kClickLeave: c = 'L'; break;
				case Rules::kClickReset: c = 'R'; break;
				case Rules::kClickBoard: c = 'k'; break;
				case Rules::kClickMove: {
					const int v = Rules::mapValue(map, x, y, 0x4b438);
					c = (char)('a' + v - 1);
					if (repX[v] < 0) {
						repX[v] = x;
						repY[v] = y;
					}
					break;
				}
				default: break;
				}
				counts[(int)c]++;
				all += c;
			}
		}
		out.push_back("clickmap md5 " + AtlantisTest::md5Hex((const byte *)all.c_str(), all.size()));
		Common::String cl = "clickmap counts";
		for (int c = 0; c < 128; c++)
			if (counts[c])
				cl += Common::String::format(" %c=%u", (char)c, counts[c]);
		out.push_back(cl);
		for (int v = 1; v <= 12; v++)
			if (repX[v] >= 0)
				out.push_back(Common::String::format("move v=%d at (%d,%d)", v, repX[v], repY[v]));

		// Every state reachable by the moves and the reset.
		int resetX = -1, resetY = -1;
		for (int y = 0; y < 480 && resetX < 0; y++)
			for (int x = 0; x < 640; x++)
				if (Rules::mapValue(map, x, y, 0x4b438) == 13) {
					resetX = x;
					resetY = y;
					break;
				}
		Common::Array<Rules> queue;
		Common::HashMap<Common::String, bool> seen;
		queue.push_back(start);
		seen[stateRepr(start)] = true;
		Common::Array<Common::String> lines;
		int wins = 0;
		for (uint qi = 0; qi < queue.size(); qi++) {
			const Rules cur = queue[qi];
			for (int a = 1; a <= 13; a++) {
				int x, y;
				Common::String name;
				if (a <= 12) {
					if (repX[a] < 0)
						continue;
					x = repX[a];
					y = repY[a];
					name = Common::String::format("m%d", a);
				} else {
					x = resetX;
					y = resetY;
					name = "reset";
				}
				// The harness clears the cursor animation before each click.
				Rules r = cur;
				r.cursorMode = r.cursorFrame = r.cursorStep = 0;
				const Rules::Click c = r.click(map, x, y);
				const int snd = (c == Rules::kClickMove || c == Rules::kClickReset || c == Rules::kClickBoard) ? 1 : 0;
				const int win = r.solved() ? 1 : 0;
				// The harness's r is the original's puzzle RESULT 0x004974c0
				// after the click: 2 for a leave click (as RotatingPiecesPuzzle
				// sets it on kClickLeave), else 0.
				const int res = c == Rules::kClickLeave ? 2 : 0;
				lines.push_back(stateRepr(cur) + " " + name + " -> " + stateRepr(r) +
				                Common::String::format(" r=%d win=%d snd=%d", res, win, snd));
				const Common::String key = stateRepr(r);
				if (!seen.contains(key)) {
					seen[key] = true;
					queue.push_back(r);
					wins += win;
				}
			}
		}
		Common::sort(lines.begin(), lines.end());
		out.push_back(Common::String::format("states %u transitions %u winning_states %d",
		                                     queue.size(), lines.size(), wins));
		Common::String joined;
		for (uint i = 0; i < lines.size(); i++) {
			if (i)
				joined += '\n';
			joined += lines[i];
		}
		out.push_back("transitions md5 " + AtlantisTest::md5Hex((const byte *)joined.c_str(), joined.size()));

		// Compare with the golden, skipping its comment line.
		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}
};
