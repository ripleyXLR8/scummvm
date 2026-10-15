#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_VIDEO_DIALOG_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_VIDEO_DIALOG_H

// Verifier "video", element 3: dialogs.
//
// Original code (atlantis-cd-en-714240):
//   FUN_00428f58  speaker -> persoId chain (memcmp chain, last hit wins)
//   FUN_0042844c  dialog line player: camera choice, Seth reverse shot
//   FUN_0042ea18  DIALOG\INITDIAL.TXT parser
//   FUN_0042ffe8  subtitle line breaker (100 characters), FUN_00430178 draw
// Harnesses: atlantis-re/tools/emu/video_dialog_{speakers,initdial,subtitles}.py
// Report:    atlantis-re/audit/video/REPORT.md (dialogs section)
//
// PORT CODE UNDER TEST. The port functions are private members of
// CryOmni3DEngine_Atlantis and the engine cannot be constructed in the test
// runner (Engine needs a working mixer). The tests therefore call the REAL
// compiled port methods on zero-filled storage of the class, after
// placement-constructing only the members those methods touch
// (_bigFile, _dialInitData, _dialRng). resolveSpeakerPersoId, loadDialInitData
// and pickDialCam are non-virtual and touch nothing else. The subtitle wrap
// and the camera rule of the ORIGINAL are checked against values produced by
// emulating the original code.

#include "atlantis_test_helper.h"
#include "video_test_big.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/random.h"
#include "graphics/font.h"
#include "graphics/fontman.h"

#ifndef VIDEO_ENGINE_PEEK
#define VIDEO_ENGINE_PEEK
#define private public
#define protected public
#include "cryomni3d/atlantis/subtitle_layout.h"
#include "cryomni3d/atlantis/engine.h"
#undef private
#undef protected

#include <new>

namespace VideoPeek {

typedef CryOmni3D::Atlantis::CryOmni3DEngine_Atlantis Eng;

// Zero-filled storage for the engine object; never destroyed (leaks the few
// members constructed below, on purpose).
inline Eng *rawEngine() {
	void *mem = ::operator new(sizeof(Eng));
	memset(mem, 0, sizeof(Eng));
	return static_cast<Eng *>(mem);
}

inline void addDataDirToSearchMan() {
	if (AtlantisTest::haveData() && !SearchMan.hasArchive("video_test_data"))
		SearchMan.addDirectory("video_test_data",
		                       Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
}

} // End of namespace VideoPeek
#endif

namespace VideoDialogTest {

// Value written to CLICPERSO by the ORIGINAL FUN_00428f58 for "[<token><suffix>",
// emulated (tools/emu/video_dialog_speakers.py). Tokens are synthetic, built
// from the literals of the chain; the suffix (']' or ',off]') does not change
// the result for any of them.
struct SpeakerCase { const char *token; int original; };
static const SpeakerCase kSpeakerCases[] = {
	{ "Reinem", 561 }, { "Reinep", 562 }, { "Reine", 560 },
	{ "Seth", 514 }, { "seth", 514 }, { "SETH", 0 },
	{ "Sold", 604 }, { "SOLD", 0 }, { "sold", 0 },
	{ "Enne", 667 }, { "enne", 0 }, { "Gard", 604 }, { "GARD", 0 },
	{ "Passant9", 602 }, { "Passant6", 601 }, { "Passante7", 606 }, { "Passante9", 0 },
	{ "Lona", 576 }, { "Agat", 520 }, { "Agatha", 520 }, { "Xyz", 0 },
	{ "Anna", 663 }, { "Servante", 609 }, { "Servage", 582 }, { "JPil", 571 },
	{ "jpil", 571 }, { "Garc", 526 }, { "Gardx", 604 }, { "Carv", 602 },
};

// ORIGINAL FUN_0042ffe8 line split of " " + input (the byte after ']' is kept),
// emulated (tools/emu/video_dialog_subtitles.py).
struct WrapCase { const char *input; int n; const char *lines[3]; };
static const WrapCase kWrapCases[] = {
	{ "Short line.", 1, { " Short line." } },
	{ "The quick brown fox jumps over the lazy dog, then the lazy dog wakes up and chases the fox around the field.",
	  2, { " The quick brown fox jumps over the lazy dog, then the lazy dog wakes up and chases the fox around",
	       " the field." } },
	{ "word word word word word word word word word word word word word word word word word word word word word word word word word word word word word word",
	  2, { " word word word word word word word word word word word word word word word word word word word word",
	       " word word word word word word word word word word" } },
};

} // End of namespace VideoDialogTest

class VideoDialogSpeakerTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// Self-contained: synthetic speaker tokens, port's real
	// resolveSpeakerPersoId with _heroPersoId = 512 (the value the port's
	// loadDialInitData derives from the shipped INITDIAL.TXT, row "hero").
	// Expected: the ORIGINAL's result. Known differences (see REPORT):
	// first-match order (Reinem/Reinep -> 560), Seth -> hero 512 instead of
	// 514, case-insensitive matching (SOLD/sold/enne/GARD/SETH).
	void test_speaker_synthetic_vs_original() {
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		e->_heroPersoId = 512;
		int diffs = 0;
		for (uint i = 0; i < ARRAYSIZE(VideoDialogTest::kSpeakerCases); i++) {
			const VideoDialogTest::SpeakerCase &c = VideoDialogTest::kSpeakerCases[i];
			int port = e->resolveSpeakerPersoId(c.token);
			if (port != c.original) {
				diffs++;
				TS_WARN(Common::String::format("speaker '%s': original %d, port %d",
				                               c.token, c.original, port).c_str());
			}
		}
		TS_ASSERT_EQUALS(diffs, 0);
	}

	// Data test: every speaker token of the 76 CON files, as the port's own
	// ConScript extracts it, against evidence/golden/video_dialog_speakers.txt.
	void test_speaker_all_con_files_vs_golden() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("video_dialog_speakers.txt", golden));
		VideoTest::Big big;
		TS_ASSERT(!big.empty());
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		e->_heroPersoId = 512;
		Common::Array<Common::String> cons = big.names(".CON");
		TS_ASSERT_EQUALS(cons.size(), 76u);
		Common::HashMap<Common::String, bool> seen;
		int compared = 0, diffs = 0;
		for (uint f = 0; f < cons.size(); f++) {
			uint32 size = 0;
			byte *buf = big.get(cons[f], size);
			CryOmni3D::Atlantis::ConScript script;
			script.parse((const char *)buf, size);
			delete[] buf;
			for (const CryOmni3D::Atlantis::ConSection &sec : script.sections()) {
				for (const CryOmni3D::Atlantis::ConSectionItem &it : sec.items) {
					if (it.isSet || seen.contains(it.line.speaker))
						continue;
					seen[it.line.speaker] = true;
					if (!golden.contains(it.line.speaker)) {
						TS_WARN(("speaker not in golden: " + it.line.speaker).c_str());
						continue;
					}
					compared++;
					int orig = atoi(golden[it.line.speaker].c_str());
					int port = e->resolveSpeakerPersoId(it.line.speaker);
					if (orig != port) {
						diffs++;
						TS_WARN(Common::String::format("speaker '%s': original %d, port %d",
						        it.line.speaker.c_str(), orig, port).c_str());
					}
				}
			}
		}
		TS_ASSERT_EQUALS(compared, 68);
		TS_ASSERT_EQUALS(diffs, 0);
	}
};

class VideoDialogInitDialTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// Data test: the port's real loadDialInitData (BigFileArchive mounted on
	// the four CD archives) against what the ORIGINAL FUN_0042ea18 stores
	// (evidence/golden/video_dialog_initdial.txt). The original appends the
	// extension and keeps "none" as "none.<ext>"; the port stores the bare
	// basename and maps "none" to "" (SPF, 3DC, animM, s3d) or keeps "none"
	// (cam UBB). Both are normalised to the original's form before comparing.
	void test_initdial_vs_original_parser() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("video_dialog_initdial.txt", golden));
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		new (&e->_bigFile) CryOmni3D::Atlantis::BigFileArchive();
		new (&e->_dialInitData) Common::HashMap<int, VideoPeek::Eng::DialInitEntry>();
		VideoPeek::addDataDirToSearchMan();
		int mounted = 0;
		for (int cd = 1; cd <= 4; cd++)
			if (e->_bigFile.open(Common::Path(Common::String::format("BIGCD%d.BIG", cd))))
				mounted++;
		TS_ASSERT_EQUALS(mounted, 4);
		e->loadDialInitData();
		TS_ASSERT_EQUALS(e->_dialInitData.size(), 67u);
		TS_ASSERT_EQUALS(e->_heroPersoId, 512);

		auto norm = [](const Common::String &s, const char *ext) {
			Common::String r = s.empty() ? Common::String("none") : s;
			r.toLowercase();
			return r + ext;
		};
		int compared = 0, diffs = 0;
		for (Common::HashMap<int, VideoPeek::Eng::DialInitEntry>::const_iterator it =
		         e->_dialInitData.begin(); it != e->_dialInitData.end(); ++it) {
			const VideoPeek::Eng::DialInitEntry &d = it->_value;
			struct F { Common::String key, port; } fields[11] = {
				{ "s3d", norm(d.s3dBase, ".s3d") },
				{ "3dc", norm(d.anim3DCBase, ".3dc") },
				{ "3dam", norm(d.animMBase, ".3da") },
				{ "ubb2", norm(d.camUBB[0], ".ubb") }, { "ubb3", norm(d.camUBB[1], ".ubb") },
				{ "ubb4", norm(d.camUBB[2], ".ubb") }, { "ubb5", norm(d.camUBB[3], ".ubb") },
				{ "spf2", norm(d.camSPF[0], ".spf") }, { "spf3", norm(d.camSPF[1], ".spf") },
				{ "spf4", norm(d.camSPF[2], ".spf") }, { "spf5", norm(d.camSPF[3], ".spf") },
			};
			for (int k = 0; k < 11; k++) {
				Common::String key = Common::String::format("%d.", it->_key) + fields[k].key;
				if (!golden.contains(key)) {
					diffs++;
					TS_WARN(("not stored by the original: " + key).c_str());
					continue;
				}
				compared++;
				if (golden[key] != fields[k].port) {
					diffs++;
					TS_WARN((key + ": original " + golden[key] + ", port " + fields[k].port).c_str());
				}
			}
		}
		TS_ASSERT_EQUALS(compared, 67 * 11);
		TS_ASSERT_EQUALS(diffs, 0);
	}
};

class VideoDialogCameraTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// Rules of the ORIGINAL camera choice (FUN_0042844c @ 0x004287c8..0x00428949,
	// read in the disassembly, see REPORT):
	//  (a) "camN" at the end of the header: cam = N (2..5, else 2), used even if
	//      the INITDIAL UBB for that cam is "none";
	//  (b) persoId 560 (Reine, 0x230): cam forced to 2 whatever was chosen.
	// Checked here on the port's real pickDialCam with a synthetic table.
	void test_explicit_cam_on_unavailable_cam() {
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		new (&e->_dialInitData) Common::HashMap<int, VideoPeek::Eng::DialInitEntry>();
		VideoPeek::Eng::DialInitEntry d;
		d.camUBB[0] = "none";            // cam2 unavailable
		d.camUBB[1] = "pecan2c3";
		d.camUBB[2] = "pecan2c4";
		d.camUBB[3] = "pecan2c5";
		e->_dialInitData[524] = d;
		// Original: explicit cam2 is used as is (the UBB load of "none.ubb"
		// then fails); port: falls back to a random available cam.
		int cam = e->pickDialCam(524, 2);
		TS_ASSERT_EQUALS(cam, 2);
	}

	void test_reine_forced_cam2() {
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		new (&e->_dialInitData) Common::HashMap<int, VideoPeek::Eng::DialInitEntry>();
		VideoPeek::Eng::DialInitEntry d;
		d.camUBB[0] = "rhean1c2";
		d.camUBB[1] = "rhean1c3";
		d.camUBB[2] = "rhean1c4";
		d.camUBB[3] = "rhean1c5";
		e->_dialInitData[560] = d;
		int notTwo = 0;
		// the draws read the millisecond clock (as the original's)
		for (uint pass = 1; pass <= 32; pass++) {
			e->_lastDialCam = 0;
			if (e->pickDialCam(560, 0) != 2)
				notTwo++;
			if (e->pickDialCam(560, 4) != 2)
				notTwo++;
		}
		TS_ASSERT_EQUALS(notTwo, 0);
	}

	// FUN_0042844c @ 0x004287c8..0x004289ea: an off line draws nothing and
	// keeps the last cam (0x0077290a); 0x230 still forces cam 2.  (The Seth
	// line's second draw reads the clock, which the null OSystem does not
	// advance, so it is not checked here.)
	void test_off_line_keeps_cam() {
		VideoPeek::Eng *e = VideoPeek::rawEngine();
		new (&e->_dialInitData) Common::HashMap<int, VideoPeek::Eng::DialInitEntry>();
		VideoPeek::Eng::DialInitEntry d;
		d.camUBB[0] = "rhean1c2";
		d.camUBB[1] = "rhean1c3";
		d.camUBB[2] = "rhean1c4";
		d.camUBB[3] = "rhean1c5";
		e->_dialInitData[560] = d;
		int moved = 0;
		for (uint pass = 1; pass <= 32; pass++) {
			e->_lastDialCam = 4;
			if (e->pickDialCam(560, 0, false, true) != 2)   // 0x230 still forces 2
				moved++;
			e->_lastDialCam = 5;
			if (e->pickDialCam(524, 0, false, true) != 5)
				moved++;
		}
		TS_ASSERT_EQUALS(moved, 0);
	}
};

class VideoDialogSubtitleTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// The ORIGINAL breaks subtitles at the last space before character 100
	// (FUN_0042ffe8, emulated on these inputs by
	// atlantis-re tools/emu/video_dialog_subtitles.py). Port: splitSubtitle.
	void test_line_breaks_vs_original() {
		int diffs = 0;
		for (uint i = 0; i < ARRAYSIZE(VideoDialogTest::kWrapCases); i++) {
			const VideoDialogTest::WrapCase &c = VideoDialogTest::kWrapCases[i];
			Common::Array<Common::String> lines;
			CryOmni3D::Atlantis::splitSubtitle(c.input, lines);
			bool same = (int)lines.size() == c.n;
			for (int k = 0; same && k < c.n; k++)
				same = (lines[k] == c.lines[k]);
			if (!same) {
				diffs++;
				TS_WARN(Common::String::format("case %u: original %d line(s), port %u line(s), first '%s'",
				        i, c.n, lines.size(), lines.empty() ? "" : lines[0].c_str()).c_str());
			}
		}
		TS_ASSERT_EQUALS(diffs, 0);
	}

	// Line i of n at y = 0x1ce - (n-i+1)*14 (FUN_00430178): a single line at
	// 434, lines 14 px apart. Port: subtitleLineY.
	void test_single_line_y_vs_original() {
		TS_ASSERT_EQUALS(CryOmni3D::Atlantis::subtitleLineY(0, 1), 0x1ce - 2 * 14);
		TS_ASSERT_EQUALS(CryOmni3D::Atlantis::subtitleLineY(1, 2) - CryOmni3D::Atlantis::subtitleLineY(0, 2), 14);
		TS_ASSERT_EQUALS(CryOmni3D::Atlantis::subtitleLineY(1, 2), 0x1ce - 2 * 14);
	}
};

#endif
