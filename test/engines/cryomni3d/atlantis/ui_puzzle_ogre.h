#include "atlantis_test_helper.h"

#include "common/algorithm.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/util.h"

// ui audit — ogre & hero game (StartPuzzle=2) rules vs the original.
//
// Expected values come from the ORIGINAL rule functions of
// atlantis-cd-en-714240 run unmodified under Unicorn
// (atlantis-re/tools/emu/ui_puzzles_ogre.py and ui_puzzles_ogre2.py, output in
// atlantis-re/evidence/golden/ui_puzzle_ogre.txt):
//   FUN_00443f94 path/mask builder (+ FUN_00443ea0 / FUN_00443f18 cell lookup)
//   FUN_00443c3c click handler (pick-up, drop, catch test, abort)
//   FUN_00442360 ogre turn          FUN_00442a0c queen walk
//
// FAITHFUL COPY WARNING: the port's rules are private members of
// OgreGamePuzzle (engines/cryomni3d/atlantis/puzzle_ogregame.cpp) whose
// constructor loads sprites through the engine, and moveOgres() animates
// through the engine; they cannot be called from a test.  PortCopy below is a
// line-for-line copy of OgreGamePuzzle::buildMoveMask(), the rule part of
// moveOgres() (animation removed), walkQueen(), and the pick-up / drop /
// catch / abort branches of run(), from scummvm branch atlantis-v2 as audited
// on 2026-10-12.  Extraction that would make the real code testable: an
// engine-independent OgreRules class (pieces, occupancy, mask, path, RNG
// callback) used by OgreGamePuzzle.

namespace UiOgre {
namespace PortCopy {

static const int kCellSize = 60, kBoardX = 113, kBoardY = 29;
static const int kAbortX0 = 0x32, kAbortX1 = 0x24e;

struct Piece { int flag, x, y; };

struct Game {
	Piece p[4];
	byte occ[49];
	byte mask[49];
	int pathX[3], pathY[3];
	// scripted RNG for walkQueen (stands for _rng.getRandomNumber(32767))
	Common::Array<int> rnd;
	uint rndPos;
	int rndDefault;
	int rand32767() { return rndPos < rnd.size() ? rnd[rndPos++] : rndDefault; }

	Game() : rndPos(0), rndDefault(0) {
		memset(p, 0, sizeof(p)); memset(occ, 0, sizeof(occ)); memset(mask, 0, sizeof(mask));
		for (int i = 0; i < 3; i++) { pathX[i] = -1; pathY[i] = -1; }
	}

	// OgreGamePuzzle::buildMoveMask
	void buildMoveMask(int mouseX, int mouseY) {
		memset(mask, 0, sizeof(mask));
		for (int i = 0; i < 3; i++) { pathX[i] = -1; pathY[i] = -1; }
		const int cx = (mouseX - kBoardX) / kCellSize;
		const int cy = (mouseY - kBoardY) / kCellSize;
		if (mouseX < kBoardX || mouseY < kBoardY || cx > 6 || cy > 6)
			return;
		const int hx = p[0].x, hy = p[0].y;
		const int dx = cx - hx, dy = cy - hy;
		const int dist = ABS(dx) + ABS(dy);
		if (dist >= 1 && dist <= 2) {
			int midX, midY;
			if (dx == 0 || dy == 0) { midX = hx + dx / 2; midY = hy + dy / 2; }
			else if (ABS(dx) < ABS(dy)) { midX = cx; midY = hy; }
			else { midX = hx; midY = cy; }
			mask[midY * 7 + midX] = 1;
			mask[cy * 7 + cx] = 1;
			pathX[1] = midX; pathY[1] = midY;
			pathX[2] = cx; pathY[2] = cy;
		}
		mask[hy * 7 + hx] = 1;
		pathX[0] = hx; pathY[0] = hy;
	}

	// OgreGamePuzzle::moveOgres without the slide animation; returns 2 on loss.
	int moveOgres() {
		int result = 0;
		for (int o = 2; o < 4 && result == 0; o++) {
			Piece &ogre = p[o];
			if (ogre.flag != 1) continue;
			int sx = 0, sy = 0;
			if (ogre.x < p[0].x) sx = 1; else if (ogre.x > p[0].x) sx = -1;
			if (ogre.y < p[0].y) sy = 1; else if (ogre.y > p[0].y) sy = -1;
			if (ogre.x + sx < 0 || ogre.x + sx > 6) sx = 0;
			if (ogre.y + sy < 0 || ogre.y + sy > 6) sy = 0;
			if (occ[(ogre.y + sy) * 7 + ogre.x + sx]) {
				int nx = ogre.x, ny = ogre.y;
				const int x0 = MAX(ogre.x - 1, 0), x1 = MIN(ogre.x + 1, 6);
				const int y0 = MAX(ogre.y - 1, 0), y1 = MIN(ogre.y + 1, 6);
				for (int yy = y0; yy <= y1; yy++)
					for (int xx = x0; xx <= x1; xx++)
						if (!occ[yy * 7 + xx]) { nx = xx; ny = yy; }
				sx = nx - ogre.x; sy = ny - ogre.y;
			}
			if (sx != 0 || sy != 0) {
				occ[ogre.y * 7 + ogre.x] = 0;
				ogre.x += sx; ogre.y += sy;
				occ[ogre.y * 7 + ogre.x] = 1;
			}
			if (ogre.x == p[0].x && ogre.y == p[0].y)
				result = 2;
		}
		return result;
	}

	// OgreGamePuzzle::walkQueen
	void walkQueen() {
		int sx = 1, sy = 1;
		for (int tries = 0; ; tries++) {
			if (rand32767() > 16000) sx = -sx;
			if (rand32767() > 16000) sy = -sy;
			if (p[1].x + sx > 6 || p[1].x + sx < 0) sx = -sx;
			if (p[1].y + sy > 6 || p[1].y + sy < 0) sy = -sy;
			if (tries >= 200) { sx = 0; sy = 0; break; }
			const int qx = p[1].x + sx, qy = p[1].y + sy;
			bool onPath = false;
			for (int i = 0; i < 3; i++)
				if (qx == pathX[i] && qy == pathY[i]) onPath = true;
			if (!onPath) break;
		}
		p[1].x += sx; p[1].y += sy;
	}

	// run(): pick-up test (not holding)
	bool pickUp(int mx, int my) const {
		const int px = p[0].x * kCellSize, py = p[0].y * kCellSize;
		return mx > px + 0x78 && mx < px + 0x9f && my > py + 0x1d && my < py + 0x54;
	}
	static bool abortClick(int mx) { return mx < kAbortX0 || mx > kAbortX1; }

	// run(): drop branch.  Returns -1 if the drop is refused, else the result
	// (0 continue, 255 win, 2 lose).  Queen walk / blink not run here.
	int drop(int mx, int my, bool runOgres) {
		const int cx = (mx - kBoardX) / kCellSize;
		const int cy = (my - kBoardY) / kCellSize;
		if (!(mx >= kBoardX && my >= kBoardY && cx < 7 && cy < 7 &&
		      !occ[cy * 7 + cx] && mask[cy * 7 + cx] == 1))
			return -1;
		p[0].x = cx; p[0].y = cy; p[0].flag = 1;
		for (int i = 0; i < 3; i++)
			if (p[1].x == pathX[i] && p[1].y == pathY[i])
				return 255;
		return runOgres ? moveOgres() : 0;
	}
};

static const int kCellPX[7] = { 143, 203, 263, 323, 383, 443, 503 };
static const int kCellPY[7] = { 59, 119, 179, 239, 299, 359, 419 };

} // namespace PortCopy

static Common::String md5Of(const Common::String &s) {
	return AtlantisTest::md5Hex((const byte *)s.c_str(), s.size());
}

static Common::String join(const Common::Array<Common::String> &a) {
	Common::String r;
	for (uint i = 0; i < a.size(); i++) {
		if (i) r += '\n';
		r += a[i];
	}
	return r;
}

// Original mouse->cell lookups of the path builder (FUN_00443ea0 / FUN_00443f18,
// table @ 0x49743c), every pixel: 99 = off-board.
static int origPathCol(int x) {
	static const int e[8] = { 112, 173, 233, 294, 354, 415, 476, 536 };
	if (x < e[0] || x >= e[7]) return 99;
	int c = 0;
	while (c < 6 && x >= e[c + 1]) c++;
	return c;
}
static int origPathRow(int y) {
	static const int e[7] = { 27, 88, 148, 209, 269, 330, 390 };   // row 6 runs to 479
	if (y < e[0]) return 99;
	int r = 0;
	while (r < 6 && y >= e[r + 1]) r++;
	return r;
}

} // namespace UiOgre

class UiPuzzleOgreTestSuite : public CxxTest::TestSuite {
	typedef UiOgre::PortCopy::Game Game;

	static Common::String stateKey(const Game &g) {
		Common::String s = Common::String::format("%d,%d,%d,%d,%d,%d,", g.p[0].x, g.p[0].y,
		                                          g.p[2].x, g.p[2].y, g.p[3].x, g.p[3].y);
		bool first = true;
		for (int i = 0; i < 49; i++)
			if (g.occ[i]) {
				if (!first) s += '.';
				s += Common::String::format("%d", i);
				first = false;
			}
		return s;
	}

public:
	// Initial records (FUN_0044203c + FUN_004422b8): hero (0,0), ogres (6,6), (0,6).
	void test_initial_pieces() {
		// Original: 0x495e4c = {1,0,0}, ogre 2 {1,6,6}, ogre 3 {1,0,6} (set by FUN_004422b8).
		Game g;
		g.p[0].flag = 1; g.p[0].x = 0; g.p[0].y = 0;
		g.p[2].flag = 1; g.p[2].x = 6; g.p[2].y = 6;
		g.p[3].flag = 1; g.p[3].x = 0; g.p[3].y = 6;
		TS_ASSERT_EQUALS(stateKey(g), Common::String("0,0,6,6,0,6,"));
	}

	// Pick-up rectangle and abort strip: original sweep of every pixel gives
	// 2052 pick-up pixels x = hx*60+121..158, y = hy*60+30..83, and 47520
	// abort pixels (x < 50 or x > 590) for each tested hero cell.
	void test_pickup_and_abort_every_pixel() {
		static const int hs[3][2] = { {0, 0}, {6, 6}, {3, 4} };
		for (int k = 0; k < 3; k++) {
			Game g;
			g.p[0].x = hs[k][0]; g.p[0].y = hs[k][1];
			int count = 0, minX = 9999, maxX = -1, minY = 9999, maxY = -1, aborts = 0;
			for (int y = 0; y < 480; y++)
				for (int x = 0; x < 640; x++) {
					if (g.pickUp(x, y)) {
						count++;
						minX = MIN(minX, x); maxX = MAX(maxX, x);
						minY = MIN(minY, y); maxY = MAX(maxY, y);
					} else if (Game::abortClick(x)) {
						aborts++;
					}
				}
			TS_ASSERT_EQUALS(count, 2052);
			TS_ASSERT_EQUALS(minX, hs[k][0] * 60 + 121);
			TS_ASSERT_EQUALS(maxX, hs[k][0] * 60 + 158);
			TS_ASSERT_EQUALS(minY, hs[k][1] * 60 + 30);
			TS_ASSERT_EQUALS(maxY, hs[k][1] * 60 + 83);
			TS_ASSERT_EQUALS(aborts, 47520);
		}
	}

	// KNOWN DIFFERENCE (expected to fail): the legal-move highlight uses the
	// exe's column/row edge table (112,173,233,294,354,415,476 | 536 and
	// 27,88,148,209,269,330,390 | row 6 down to 479); the port uses a uniform
	// 60-px grid from (113,29).  Columns 0..6 / rows 0..6 differ at their
	// edges by up to 3 px and the port has no row 6 for y >= 449.
	void test_path_cell_lookup_every_pixel() {
		int badX = 0, badY = 0;
		for (int x = 0; x < 640; x++) {
			int c = (x >= 113 && (x - 113) / 60 <= 6) ? (x - 113) / 60 : 99;
			if (c != UiOgre::origPathCol(x)) badX++;
		}
		for (int y = 0; y < 480; y++) {
			int r = (y >= 29 && (y - 29) / 60 <= 6) ? (y - 29) / 60 : 99;
			if (r != UiOgre::origPathRow(y)) badY++;
		}
		TS_ASSERT_EQUALS(badX, 0);
		TS_ASSERT_EQUALS(badY, 0);
	}

	// KNOWN DIFFERENCE (expected to fail): legal-move mask and path for every
	// hero cell x every target cell (mouse at the cell centre).  Straight
	// moves agree (test_path_builder_nondiagonal); for diagonal moves the
	// original picks the elbow from the cursor offset to the hero cell centre
	// (FUN_00443f94 @ 0x44409f: (cx,hy) if |mx-(colEdge[hx]+30)| >
	// |my-(rowEdge[hy]+30)| else (hx,cy)); the port's |dx| < |dy| test is
	// never true for a diagonal step, so it always uses (hx,cy).
	void test_path_builder_all_cells() { checkPathBuilder(false, "e29742f19eeb72dfcb3f80f6a1a28fb6"); }
	void test_path_builder_nondiagonal() { checkPathBuilder(true, "1ef75c21692f3319d86d4988a04c4319"); }

	// Concrete diagonal case: hero (6,0), cursor at the centre of (5,1)
	// (143+60*5, 59+60) = (443,119): original elbow (5,0), port (6,1).
	void test_diagonal_elbow_example() {
		Game g;
		g.p[0].x = 6; g.p[0].y = 0;
		g.buildMoveMask(443, 119);
		TS_ASSERT_EQUALS(g.pathX[1], 5);
		TS_ASSERT_EQUALS(g.pathY[1], 0);
	}

	void checkPathBuilder(bool nondiagOnly, const char *expMd5) {
		Common::Array<Common::String> lines;
		for (int hy = 0; hy < 7; hy++)
			for (int hx = 0; hx < 7; hx++)
				for (int cy = 0; cy < 7; cy++)
					for (int cx = 0; cx < 7; cx++) {
						if (nondiagOnly && hx != cx && hy != cy) continue;
						Game g;
						g.p[0].x = hx; g.p[0].y = hy;
						g.buildMoveMask(UiOgre::PortCopy::kCellPX[cx], UiOgre::PortCopy::kCellPY[cy]);
						Common::String cells;
						for (int i = 0; i < 49; i++)
							if (g.mask[i] == 1) {
								if (!cells.empty()) cells += '.';
								cells += Common::String::format("%d", i);
							}
						Common::String path;
						for (int i = 0; i < 3; i++) {
							if (i) path += ',';
							path += (g.pathX[i] == -1) ? Common::String("-") : Common::String::format("%d", g.pathX[i]);
							path += ',';
							path += (g.pathY[i] == -1) ? Common::String("-") : Common::String::format("%d", g.pathY[i]);
						}
						lines.push_back(Common::String::format("%d %d %d %d ", hx, hy, cx, cy) + cells + " " + path);
					}
		TS_ASSERT_EQUALS(UiOgre::md5Of(UiOgre::join(lines)), Common::String(expMd5));
	}

	// Catch test: every hero cell x every target at distance 0..2 x every
	// queen cell.  KNOWN DIFFERENCE (expected to fail) for the full set,
	// because of the diagonal elbow; the straight-move subset must match.
	void test_catch_all() { checkCatch(false, 1237, "7c0ca28cd41282f4a84646b3e2225d18"); }
	void test_catch_nondiagonal() { checkCatch(true, 805, "b5e8bf4fd682b23fff8aa18a0f4cacd0"); }
	void checkCatch(bool nondiagOnly, uint expCount, const char *expMd5) {
		Common::Array<Common::String> lines;
		for (int hy = 0; hy < 7; hy++)
			for (int hx = 0; hx < 7; hx++)
				for (int cy = 0; cy < 7; cy++)
					for (int cx = 0; cx < 7; cx++) {
						if (ABS(cx - hx) + ABS(cy - hy) > 2) continue;
						if (nondiagOnly && hx != cx && hy != cy) continue;
						for (int qy = 0; qy < 7; qy++)
							for (int qx = 0; qx < 7; qx++) {
								Game g;
								g.p[0].x = hx; g.p[0].y = hy;
								g.p[1].x = qx; g.p[1].y = qy;
								int mx = UiOgre::PortCopy::kCellPX[cx], my = UiOgre::PortCopy::kCellPY[cy];
								g.buildMoveMask(mx, my);
								if (g.drop(mx, my, false) == 255)
									lines.push_back(Common::String::format("%d %d %d %d %d %d", hx, hy, cx, cy, qx, qy));
							}
					}
		TS_ASSERT_EQUALS(lines.size(), expCount);
		TS_ASSERT_EQUALS(UiOgre::md5Of(UiOgre::join(lines)), Common::String(expMd5));
	}

	// Queen walk with scripted rand(): every hero move x queen cell x the
	// four first-pair outcomes x two continuation policies.  KNOWN
	// DIFFERENCE (expected to fail) for the full set, only because the
	// path it avoids has a different diagonal elbow; the straight-move
	// subset (same path) must match exactly.
	void test_queen_walk_all() { checkWalk(false, 196392, 6197, "e1c4aa8cc5c4accc29b41dedb1b3ee5f"); }
	void test_queen_walk_nondiagonal() { checkWalk(true, 139944, -1, "7d26f13ed7d0d15bb7e35084f958e176"); }
	void checkWalk(bool nondiagOnly, uint expCount, int expStay, const char *expMd5) {
		Common::Array<Common::String> lines;
		int stay = 0;
		for (int hy = 0; hy < 7; hy++)
			for (int hx = 0; hx < 7; hx++)
				for (int cy = 0; cy < 7; cy++)
					for (int cx = 0; cx < 7; cx++) {
						if (ABS(cx - hx) + ABS(cy - hy) > 2) continue;
						if (nondiagOnly && hx != cx && hy != cy) continue;
						for (int qy = 0; qy < 7; qy++)
							for (int qx = 0; qx < 7; qx++)
								for (int first = 0; first < 4; first++)
									for (int cont = 0; cont < 2; cont++) {
										Game g;
										g.p[0].x = hx; g.p[0].y = hy;
										g.buildMoveMask(UiOgre::PortCopy::kCellPX[cx], UiOgre::PortCopy::kCellPY[cy]);
										g.p[1].x = qx; g.p[1].y = qy;
										g.rnd.push_back((first & 1) ? 20000 : 100);
										g.rnd.push_back((first & 2) ? 20000 : 100);
										g.rndDefault = cont ? 20000 : 100;
										g.walkQueen();
										if (g.p[1].x == qx && g.p[1].y == qy) stay++;
										lines.push_back(Common::String::format("%d %d %d %d %d %d %d %d %d %d",
										    hx, hy, cx, cy, qx, qy, first, cont, g.p[1].x, g.p[1].y));
									}
					}
		TS_ASSERT_EQUALS(lines.size(), expCount);
		if (expStay >= 0)
			TS_ASSERT_EQUALS(stay, expStay);
		TS_ASSERT_EQUALS(UiOgre::md5Of(UiOgre::join(lines)), Common::String(expMd5));
	}

	// KNOWN DIFFERENCE (expected to fail): the original ogre diversion
	// (FUN_00442360, scan loop @ 0x442586) tests occupancy at (x0 + xx, yy) and moves
	// to (x0, yy) -- x0 = max(ogre.x-1, 0) -- instead of (xx, yy).
	// Concrete case: hero drops on (3,0) from (2,0), ogres at (4,4) and
	// (2,4): ogre A goes to (3,3); ogre B is diverted to (1,5) in the
	// original, (3,5) in the port.
	void test_ogre_diversion_example() {
		Game g;
		g.p[0].flag = 0; g.p[0].x = 2; g.p[0].y = 0;
		g.p[1].x = 99; g.p[1].y = 99;
		g.p[2].flag = 1; g.p[2].x = 4; g.p[2].y = 4;
		g.p[3].flag = 1; g.p[3].x = 2; g.p[3].y = 4;
		g.occ[4 * 7 + 4] = 1; g.occ[4 * 7 + 2] = 1;
		int mx = UiOgre::PortCopy::kCellPX[3], my = UiOgre::PortCopy::kCellPY[0];
		g.buildMoveMask(mx, my);
		TS_ASSERT_EQUALS(g.drop(mx, my, true), 0);
		TS_ASSERT_EQUALS(g.p[2].x, 3); TS_ASSERT_EQUALS(g.p[2].y, 3);
		TS_ASSERT_EQUALS(g.p[3].x, 1); TS_ASSERT_EQUALS(g.p[3].y, 5);
	}

	// KNOWN DIFFERENCE (expected to fail): breadth-first search of every
	// state reachable from the start by legal hero drops (queen parked off
	// the board).  Original: 2552 states, 21734 transitions (11952 losing),
	// listing md5 d5407e20e0b9e1f39da6ac6470cef3c3; 77 reachable states have
	// both ogres on one cell and 34 have an occupancy table that no longer
	// matches the ogres (consequences of the diversion bug).
	void test_bfs_all_reachable_states() {
		Common::HashMap<Common::String, bool> seen;
		Common::Array<Game> queue;
		Game s;
		s.p[0].x = 0; s.p[0].y = 0;
		s.p[1].x = 99; s.p[1].y = 99;
		s.p[2].flag = 1; s.p[2].x = 6; s.p[2].y = 6;
		s.p[3].flag = 1; s.p[3].x = 0; s.p[3].y = 6;
		s.occ[48] = 1; s.occ[42] = 1;
		queue.push_back(s);
		seen[stateKey(s)] = true;
		Common::Array<Common::String> lines;
		int losses = 0;
		for (uint qi = 0; qi < queue.size(); qi++) {
			const Game cur = queue[qi];
			const Common::String from = stateKey(cur);
			for (int cy = 0; cy < 7; cy++)
				for (int cx = 0; cx < 7; cx++) {
					Game g = cur;
					int mx = UiOgre::PortCopy::kCellPX[cx], my = UiOgre::PortCopy::kCellPY[cy];
					g.buildMoveMask(mx, my);
					int r = g.drop(mx, my, true);
					if (r < 0) continue;
					const Common::String to = stateKey(g);
					lines.push_back(from + Common::String::format(" %d %d %d ", cx, cy, r) + to);
					if (r == 2) losses++;
					if (r == 0 && !seen.contains(to)) {
						seen[to] = true;
						queue.push_back(g);
					}
				}
		}
		Common::sort(lines.begin(), lines.end());
		TS_ASSERT_EQUALS(queue.size(), 2552U);
		TS_ASSERT_EQUALS(lines.size(), 21734U);
		TS_ASSERT_EQUALS(losses, 11952);
		TS_ASSERT_EQUALS(UiOgre::md5Of(UiOgre::join(lines)), Common::String("d5407e20e0b9e1f39da6ac6470cef3c3"));
	}
};
