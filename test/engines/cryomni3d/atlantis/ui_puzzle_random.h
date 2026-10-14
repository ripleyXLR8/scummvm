// Verifier "ui": the original's random generators (task 23).
//
// Original: atlantis-cd-en-714240's C rand @ 0x00453cd1 (seed 1: srand has
// no caller) and the ogre queen placement FUN_004422b8 on the puzzle LCG
// FUN_00447524, emulated unmodified by atlantis-re/tools/emu/puzzle_rng.py.
// Port: crtRandNext, puzzleLcgNext, ogreQueenCell (random.h).

#ifndef TEST_ATLANTIS_UI_PUZZLE_RANDOM_H
#define TEST_ATLANTIS_UI_PUZZLE_RANDOM_H

#include "cryomni3d/atlantis/random.h"

class UiPuzzleRandomTestSuite : public CxxTest::TestSuite {
public:
	void test_crt_rand_from_seed_1() {
		static const int kWant[] = { 16838, 5758, 10113, 17515, 31051, 5627, 23010, 7419, 16212, 4086, 2749, 12767 };
		uint32 seed = 1;
		for (uint i = 0; i < ARRAYSIZE(kWant); i++)
			TS_ASSERT_EQUALS(CryOmni3D::Atlantis::crtRandNext(seed), kWant[i]);
	}

	void test_ogre_queen_placement() {
		static const int kCases[][4] = {
			{ 0, 6, 4, 43930 }, { 1, 4, 6, 14011 }, { 1234, 6, 4, 15577 },
			{ 32767, 6, 4, 15477 }, { 32768, 5, 5, 13800 }, { 48879, 5, 5, 35852 }
		};
		for (uint i = 0; i < ARRAYSIZE(kCases); i++) {
			uint16 seed = (uint16)kCases[i][0];
			int x, y;
			CryOmni3D::Atlantis::ogreQueenCell(seed, x, y);
			TS_ASSERT_EQUALS(x, kCases[i][1]);
			TS_ASSERT_EQUALS(y, kCases[i][2]);
			TS_ASSERT_EQUALS((int)seed, kCases[i][3]);
		}
	}
};

#endif
