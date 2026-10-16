#include "atlantis_test_helper.h"

#include <math.h>

#include "cryomni3d/atlantis/dialog_view.h"

// video audit — the view behind each dialog line.
//
// Expected values come from the ORIGINAL section executor FUN_0042844c of
// atlantis-cd-en-714240 run unmodified under Unicorn on the shipped CON text
// (atlantis-re tools/emu/video_dialog_line_view.py ->
// evidence/golden/video_dialog_line_view.txt, player at 30 / 5 degrees,
// ANGLEWARPX / ANGLEWARPY = 0).
//
// Port code exercised directly: DialogView::lineView (dialog_view.h), driven
// as CryOmni3DEngine_Atlantis::playConSection drives it: the kept angles
// carry from line to line, the entry angles follow entryUpdate.

class VideoDialogViewTestSuite : public CxxTest::TestSuite {
	struct L {
		bool seth, off;
		int perso;
		bool hasAngle;
		int x, y;
		int viewX, viewY;   // degrees, the original's view behind the line
	};

	static double rad(double d) { return d * M_PI / 180.0; }
	static int deg(double r) {
		double d = fmod(r * 180.0 / M_PI, 360.0);
		if (d < 0)
			d += 360.0;
		return (int)floor(d + 0.5) % 360;
	}

	void walk(const char *name, const L *lines, int n, int endX, int endY) {
		using namespace CryOmni3D::Atlantis;
		DialogView::Angles kept = {rad(30), rad(5)};
		DialogView::Angles entry = kept;
		for (int i = 0; i < n; i++) {
			const L &l = lines[i];
			DialogView::Line v = DialogView::lineView(kept, l.seth && !l.off, l.perso,
			    l.hasAngle, rad(l.x), rad(l.y), rad(50) * 0.9);
			if (v.entryUpdate)
				entry = kept;
			const DialogView::Angles &shown = l.off ? v.kept : v.view;
			if (deg(shown.alpha) != l.viewX || deg(shown.beta) != l.viewY)
				TS_FAIL(Common::String::format("%s line %c: view %d/%d, original %d/%d", name,
				    'A' + i, deg(shown.alpha), deg(shown.beta), l.viewX, l.viewY).c_str());
			kept = v.kept;
		}
		TS_ASSERT_EQUALS(deg(entry.alpha), endX);
		TS_ASSERT_EQUALS(deg(entry.beta), endY);
	}

public:
	// CHAPI003 /35 (Companions' quarters, timer): Agatha's "You just found
	// them" (35C) is shown at the player's angle, not at the reverse shot of
	// the Seth line before it.
	void test_chapi003_35() {
		static const L lines[] = {
			{false, false, 520, false, 0, 0, 30, 0},    // [Agathe,cam3]
			{true,  false, 514, false, 0, 0, 210, 0},   // [Seth]
			{false, false, 520, false, 0, 0, 30, 0},    // [Agathe] You just found them
			{true,  false, 514, false, 0, 0, 210, 0},   // [Seth]
			{false, false, 520, false, 0, 0, 30, 0},    // [Agathe,cam2]
		};
		walk("CHAPI003 /35", lines, ARRAYSIZE(lines), 30, 5);
	}

	// CHAPI003 /43: a Seth line turns from the last explicit angles (122).
	void test_chapi003_43() {
		static const L lines[] = {
			{true,  false, 514, false, 0, 0, 210, 0},   // [Seth]
			{false, false, 522, true, 122, 0, 122, 0},  // [Meljanz,122,0]
			{true,  false, 514, false, 0, 0, 302, 0},   // [Seth]
			{false, false, 520, true, 325, 0, 325, 0},  // [Agathe,325,0,cam2]
			{false, false, 522, true, 122, 0, 122, 0},  // [Meljanz,122,0]
			{false, false, 522, true, 122, 0, 122, 0},  // [Meljanz,122,0]
			{false, false, 520, true, 325, 0, 325, 0},  // [Agathe,325,0]
		};
		walk("CHAPI003 /43", lines, ARRAYSIZE(lines), 122, 0);
	}

	// CHAPI016 /206: a voice-only line is redrawn at the kept angles, pitch
	// included, after a talking head shown at pitch 0.
	void test_chapi016_206() {
		static const L lines[] = {
			{false, false, 520, false, 0, 0, 30, 0},    // [Agathe,cam2]
			{false, true,  527, false, 0, 0, 30, 5},    // [Lascoyt,off]
			{false, false, 520, false, 0, 0, 30, 0},    // [Agathe,cam3]
		};
		walk("CHAPI016 /206", lines, ARRAYSIZE(lines), 30, 5);
	}

	// CHAPI033 /450 A [SoldatF1,0,128]: FUN_00451915 clamps the header pitch
	// to 0.9 x 50 degrees; the clamped angle is kept (emulated: 0/45).
	void test_chapi033_450_pitch_clamp() {
		static const L lines[] = {
			{false, false, 604, true, 0, 128, 0, 45},   // [SoldatF1,0,128]
		};
		walk("CHAPI033 /450", lines, ARRAYSIZE(lines), 30, 5);
	}

	// The INITDIAL row (0x0077290c): ClicPerso's low byte for other lines
	// (0x00428924), the IncPerso count for Seth lines (0x00428a91).
	// Rows from the golden (video_dialog_line_view.txt): CHAPI056 /744 A
	// [Anna] with ClicPerso 329 -> 585, CHAPI065 /901 A [Sorciere] with
	// ClicPerso 323 -> 579, CHAPI016 /406 C [Seth] with IncPerso 2 -> 514.
	// Agatha clicked as 539 -> 539 is not in the golden (the shipped
	// ClicPerso-539 sections /207, /209, /210 open with a Seth line); it is
	// the low-byte store at 0x00428924 (539 = 0x21b -> row 0x200 + 0x1b),
	// emulated ad hoc with tools/emu/video_dialog_line_view.py CHAPI016
	// --fixed --click 539 206 -> 206A [Agathe,cam2] orig head row 539.
	void test_dial_row() {
		using CryOmni3D::Atlantis::DialogView::dialRow;
		TS_ASSERT_EQUALS(dialRow(false, 329, 0), 585);
		TS_ASSERT_EQUALS(dialRow(false, 323, 0), 579);
		TS_ASSERT_EQUALS(dialRow(false, 539, 0), 539);
		TS_ASSERT_EQUALS(dialRow(true, 520, 2), 514);
		TS_ASSERT_EQUALS(dialRow(false, 0, 0), 512);
	}
};
