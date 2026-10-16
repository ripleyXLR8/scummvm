// Verifier "ui": importing the original's checkpoints (owner decision,
// 2026-10-14).
//
// Original layouts: atlantis-re formats/ui-gam.md (checked on 74 real French
// checkpoints, experiments 2026-10-14-12) and formats/ui-players-atl.md
// (English 97-byte layout emulated, two real English files decoded; French
// 95-byte layout [INFERRED] from the single third-party file of
// evidence/saves: names 0x00..0x54, byte 0x55 = 85 = the chapter of its last
// checkpoint pa85, letters "abcde" at 0x5a; the French executable has not
// been read). Port: parseOriginalCheckpoint, readOriginalPlayersLetters
// (saveload_format.h), called directly on synthetic files laid out as the
// original writes them.

#ifndef TEST_ATLANTIS_UI_SAVES_IMPORT_H
#define TEST_ATLANTIS_UI_SAVES_IMPORT_H

#include "common/memstream.h"
#include "common/endian.h"

#include "cryomni3d/atlantis/saveload_format.h"

class UiSavesImportTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_checkpoint() {
		using namespace CryOmni3D::Atlantis;
		Common::Array<byte> b(kOrigCheckpointSize, 0);
		const char *path = "WAM\\COMPLEXE.WAM";
		memcpy(b.data(), path, strlen(path));
		b[0x104] = 30;                                   // chapter
		b[0x105] = 29;                                   // previous chapter
		WRITE_LE_UINT32(b.data() + 0x112, 9);            // place
		WRITE_LE_UINT32(b.data() + 0x116, 351);          // LastVue
		b[0x11e] = 2;                                    // IncPerso
		WRITE_LE_UINT32(b.data() + 0x11f, 2);            // inventory count
		b[0x123] = 5; WRITE_LE_UINT32(b.data() + 0x124, 262);
		b[0x12c] = 7; WRITE_LE_UINT32(b.data() + 0x12d, 300);
		b[0x231] = 1; b[0x231 + 0x5db] = 3;               // /con counters
		WRITE_LE_UINT32(b.data() + 0x80d, 11);           // CON variable 0
		WRITE_LE_UINT32(b.data() + 0x80d + 24, 17);      // CON variable 6
		b[0x829] = 1;                                    // one guest
		WRITE_LE_UINT32(b.data() + 0x82a, 587); b[0x82e] = 2;
		WRITE_LE_UINT32(b.data() + 0x83e, 2);            // two hidden persos
		WRITE_LE_UINT16(b.data() + 0x842, 523); WRITE_LE_UINT16(b.data() + 0x844, 600);
		OrigCheckpoint cp;
		TS_ASSERT(parseOriginalCheckpoint(b.data(), b.size(), cp));
		TS_ASSERT_EQUALS(cp.wamName, Common::String("COMPLEXE.WAM"));
		TS_ASSERT_EQUALS(cp.chapter, 30u);
		TS_ASSERT_EQUALS(cp.prevChapter, 29u);
		TS_ASSERT_EQUALS(cp.placeId, 9);
		TS_ASSERT_EQUALS(cp.lastVue, 351);
		TS_ASSERT_EQUALS(cp.incPerso, 2);
		TS_ASSERT_EQUALS(cp.inventory.size(), 2u);
		TS_ASSERT_EQUALS(cp.inventory[1].sprite, 7);
		TS_ASSERT_EQUALS(cp.inventory[1].id, 300);
		TS_ASSERT_EQUALS(cp.conCounters.size(), 0x5dcu);
		TS_ASSERT_EQUALS(cp.conCounters[0x5db], 3);
		TS_ASSERT_EQUALS(cp.vars[0], 11);
		TS_ASSERT_EQUALS(cp.vars[6], 17);
		TS_ASSERT_EQUALS(cp.guestCount, 1u);
		TS_ASSERT_EQUALS(cp.guestId[0], 587);
		TS_ASSERT_EQUALS(cp.guestSprite[0], 2);
		TS_ASSERT_EQUALS(cp.hiddenPersos.size(), 2u);
		TS_ASSERT_EQUALS(cp.hiddenPersos[1], 600);
		// not a checkpoint
		TS_ASSERT(!parseOriginalCheckpoint(b.data(), 2638, cp));
	}

	// The English build's players.atl has 6 chapter and 6 letter bytes (97),
	// the French one 5 and 5 (95, the letters at 0x5a) -- the French split is
	// [INFERRED] from the size of one third-party file (85 + 5 + 5), not read
	// from the French executable.
	void test_players_letters_both_builds() {
		using namespace CryOmni3D::Atlantis;
		for (int fr = 0; fr < 2; fr++) {
			const uint size = fr ? 95 : 97;
			Common::Array<byte> b(size, 0);
			memcpy(b.data(), "Alice", 5);
			memcpy(b.data() + 17, "#New Player", 11);
			memcpy(b.data() + (fr ? 0x5a : 0x5b), "edcba", 5);
			Common::MemoryReadStream in(b.data(), size);
			char letters[kSaveMaxPlayers];
			TS_ASSERT(readOriginalPlayersLetters(in, letters));
			TS_ASSERT_EQUALS(letters[0], 'e');
			TS_ASSERT_EQUALS(letters[4], 'a');
			Common::MemoryReadStream in2(b.data(), size);
			Common::String players[kSaveMaxPlayers];
			int chapters[kSaveMaxPlayers];
			TS_ASSERT(readPlayersFile(in2, players, chapters));
			TS_ASSERT_EQUALS(players[0], Common::String("Alice"));
			TS_ASSERT(players[1].empty());
		}
	}
};

#endif
