#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"

#include "cryomni3d/atlantis/puzzle_dispatch.h"

// ui audit, task 18 — the puzzle dispatcher's checks.
//
// Expected values come from the ORIGINAL FUN_0043a1e0 of
// atlantis-cd-en-714240 run unmodified under Unicorn for StartPuzzle 0..17,
// with nothing in hand and with OBJETENMAIN = 7 (puzzle entries stubbed to
// return 0x55): atlantis-re/tools/emu/ui_puzzle_dispatch.py ->
// evidence/golden/ui_puzzle_dispatch.txt.
//
// Port code exercised directly: PuzzleDispatch (puzzle_dispatch.h), which
// CryOmni3DEngine_Atlantis::playPuzzle runs.

class UiPuzzleDispatchTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using CryOmni3D::Atlantis::PuzzleDispatch;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle_dispatch: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle_dispatch.txt", golden));
		Common::Array<Common::String> want, out;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i].hasPrefix("held "))
				want.push_back(golden[i]);
		const int helds[2] = { 0, 7 };
		for (int h = 0; h < 2; h++) {
			for (int n = 0; n < 18; n++) {
				int held = helds[h];
				const int r = PuzzleDispatch::check(n, held);
				const Common::String entry = r == PuzzleDispatch::kRun
				    ? Common::String::format("FUN_%08x", PuzzleDispatch::entry(n)) : Common::String("none");
				out.push_back(Common::String::format("held %d puzzle %d: entry %s result %d objetenmain %d",
				                                     helds[h], n, entry.c_str(),
				                                     r == PuzzleDispatch::kRun ? 0x55 : r, held));
			}
		}
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}
};
