#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"

#include "cryomni3d/atlantis/subject_menu.h"

// video audit — the dialog subject menu.
//
// Expected values come from the ORIGINAL FUN_004261a0 (draw) and
// FUN_004211f0 (hit test, with FUN_00425828's arguments) of
// atlantis-cd-en-714240 run unmodified under Unicorn for 1..16 subjects:
// atlantis-re/tools/emu/video_subject_menu.py ->
// evidence/golden/video_subject_menu.txt.
//
// Port code exercised directly: SubjectMenu (subject_menu.h), which
// CryOmni3DEngine_Atlantis::showSubjectMenu uses.

class VideoSubjectMenuTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using CryOmni3D::Atlantis::SubjectMenu;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("video_subject_menu: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("video_subject_menu.txt", golden));
		Common::Array<Common::String> out;
		for (int n = 1; n <= 16; n++) {
			Common::Array<int> ids;
			for (int i = 0; i < n; i++)
				ids.push_back(i + 1);
			Common::Array<SubjectMenu::Draw> d;
			int gx, gy;
			SubjectMenu::draws(ids, d, gx, gy);
			Common::String dl = Common::String::format("n %d draws", n);
			for (uint i = 0; i < d.size(); i++)
				dl += Common::String::format(" %d,%d,%d", d[i].x, d[i].y, d[i].frame);
			out.push_back(dl);
			out.push_back(Common::String::format("n %d grid x0 %d y0 %d cols %d rows %d", n, gx, gy, n / 5 + 1, MIN(n, 4)));
			Common::String listing;
			bool seen[256] = {};
			for (int y = 0; y < 480; y += 5) {
				if (y)
					listing += '\n';
				for (int x = 320; x < 640; x += 5) {
					const int idx = SubjectMenu::hit(n, gx, gy, x, y);
					char c;
					if (idx == -1) {
						c = '.';
					} else {
						const int s = SubjectMenu::subject(ids, idx);
						c = (s < 0 || s >= 36) ? '?' : "0123456789abcdefghijklmnopqrstuvwxyz"[s];
					}
					listing += c;
					seen[(byte)c] = true;
				}
			}
			Common::String syms;
			for (int c = 0; c < 256; c++)
				if (seen[c])
					syms += (char)c;
			out.push_back(Common::String::format("n %d clickmap md5 ", n) +
			              AtlantisTest::md5Hex((const byte *)listing.c_str(), listing.size()) + " symbols " + syms);
		}
		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}

	// FUN_00425828 Quirk 5 + FUN_004264a8 (atlantis-re
	// functions/scripts-FUN_00425828.md, STATIC): with 5 subjects the grid
	// has 2 x 4 cells; the cells past the subjects give subject 0, which no
	// /suj list holds, so the walk goes on to an entry without /suj.
	void test_empty_cell_subject_zero() {
		using CryOmni3D::Atlantis::SubjectMenu;
		Common::Array<int> ids;
		for (int i = 0; i < 5; i++)
			ids.push_back(i + 1);
		Common::Array<SubjectMenu::Draw> d;
		int gx, gy;
		SubjectMenu::draws(ids, d, gx, gy);
		// Column 1, row 1 (cell 5): no subject drawn there.
		const int idx = SubjectMenu::hit(5, gx, gy, gx + 75, gy + 75);
		TS_ASSERT_EQUALS(idx, 5);
		TS_ASSERT_EQUALS(SubjectMenu::subject(ids, idx), 0);
		// Outside the grid: -1, the walk stops at the first /suj entry.
		TS_ASSERT_EQUALS(SubjectMenu::subject(ids, SubjectMenu::hit(5, gx, gy, 10, 10)), -1);

		Common::Array<int> list;
		list.push_back(1);
		list.push_back(3);
		TS_ASSERT(SubjectMenu::listFits(list, ids, 3));
		TS_ASSERT(!SubjectMenu::listFits(list, ids, 2));
		TS_ASSERT(!SubjectMenu::listFits(list, ids, 0));
		// A listed number that is not enabled for the NPC does not fit.
		Common::Array<int> enabled;
		enabled.push_back(1);
		TS_ASSERT(!SubjectMenu::listFits(list, enabled, 3));
	}
};
