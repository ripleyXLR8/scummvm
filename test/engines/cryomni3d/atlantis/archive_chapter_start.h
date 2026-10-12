#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_CHAPTER_START_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_CHAPTER_START_H

// Verifier "archive": chapter start place (engines/cryomni3d/atlantis/place.cpp
// kChapterInitialStates, changeChapter) against atlantis.exe.
//
// Original (atlantis-cd-en-714240): a new game (FUN_0042de5c, no pa<N>.gam,
// 0x0042e26e..0x0042e29e) loads "wam\atlan1.wam" and sets the requested vue
// DAT_0049692c = 0x6cd7f4 = 1; FUN_00423760 @ 0x004237b0 loads the WAM and
// FUN_00421b4c selects the first place record with that id.  There is no
// per-chapter start table: ATLAN2..5.WAM are entered through sub-WAM slots
// (FUN_00422ad0) or /set(changewam=...) followed by /set(vue=N) in the CON
// scripts.  Golden: evidence/golden/archive_chapter.txt, written by
// atlantis-re/tools/emu/archive_chapter_emu.py (static checks + FUN_00421ebc /
// FUN_00421b4c under Unicorn).  See atlantis-re/audit/archive/REPORT.md §3.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/fs.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/wam_parser.h"

class ArchiveChapterStartTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override {
		SearchMan.remove("archive_test_data");
		Common::uninstall_null_g_system();
	}

	// The port's start for chapter 1, COPIED from place.cpp
	// (CryOmni3DEngine_Atlantis::kChapterInitialStates[0] = { 1, 0.f, 0.f };
	// the table is a private static member and cannot be referenced here).
	// Original: vue = immediate stored in DAT_0049692c by the new-game branch;
	// camera angles 0x00bcb59c / 0x00bcb598 are not written on that path, so
	// they keep their .bss value (0.0 in a fresh process).
	void test_data_chapter1_start_vue() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping chapter start test");
			return;
		}
		AtlantisTest::Golden g;
		TS_ASSERT(AtlantisTest::loadGolden("archive_chapter.txt", g));
		const uint portChapter1PlaceId = 1;     // copy of place.cpp
		TS_ASSERT_EQUALS(g["exe|newgame_wam"], Common::String("atlan1.wam"));
		TS_ASSERT_EQUALS(Common::String::format("%u", portChapter1PlaceId), g["exe|newgame_vue"]);
	}

	// The port's parser resolves the original's start vue to the same record
	// and cyclorama as FUN_00421b4c, and the first record of each ATLANn.WAM
	// (the place the original shows after /set(changewam=...) until a vue is
	// set) has the id found by the original loader.
	void test_data_start_place_records() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping chapter start test");
			return;
		}
		if (!SearchMan.hasArchive("archive_test_data"))
			SearchMan.addDirectory("archive_test_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		AtlantisTest::Golden g;
		TS_ASSERT(AtlantisTest::loadGolden("archive_chapter.txt", g));
		CryOmni3D::Atlantis::BigFileArchive big;
		TS_ASSERT(big.open(Common::Path("BIGCD1.BIG")));
		for (int n = 1; n <= 5; n++) {
			Common::String name = Common::String::format("ATLAN%d.WAM", n);
			Common::SeekableReadStream *s = big.createReadStreamForMember("WAM\\" + name);
			TS_ASSERT(s);
			if (!s)
				continue;
			CryOmni3D::Atlantis::AtlantisWAMParser w;
			w.loadStream(*s);
			delete s;
			TS_ASSERT(!w.getPlaces().empty());
			TSM_ASSERT_EQUALS(name.c_str(), Common::String::format("%u", (uint)w.getPlaces()[0].placeId),
			                  g[name + "|record0_id"]);
			if (n == 1) {
				uint vue = (uint)atol(g["exe|newgame_vue"].c_str());
				const CryOmni3D::Atlantis::AtlantisPlace *p = w.findPlaceById((uint16)vue);
				TS_ASSERT(p);
				if (p) {
					TS_ASSERT_EQUALS(Common::String::format("%u", (uint)(p - &w.getPlaces()[0])), g[name + "|vue1_index"]);
					TS_ASSERT_EQUALS(p->cycloHnm, g[name + "|vue1_cyclo"]);
				}
			}
		}
	}
};

#endif
