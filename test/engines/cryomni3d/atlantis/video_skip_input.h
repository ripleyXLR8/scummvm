// Verifier "video": what ends a video (transition, Cinema=, start-up).
//
// Original: atlantis-cd-en-714240 FUN_0041bc74 with the real input poll
// FUN_0041a298 and key poll FUN_0042e624, run unmodified under Unicorn by
// atlantis-re/tools/emu/video_skip_input.py (golden video_skip_input.txt,
// experiment 2026-10-16-06): 7 inputs x 128 flag states, a 10-frame video.
//
// Port: videoSkipPoll / videoSkipFades (video_skip.h), as playTransitionVideo
// runs them once per frame.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "cryomni3d/atlantis/video_skip.h"

class VideoSkipInputTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_skip_input_vs_original() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("video skip: no golden configured, skipped");
			return;
		}
		Common::Array<Common::String> lines;
		TS_ASSERT(AtlantisTest::loadGoldenLines("video_skip_input.txt", lines));
		const uint kFrames = 10;
		uint rows = 0, bad = 0;
		for (uint li = 0; li < lines.size(); li++) {
			const Common::String &l = lines[li];
			if (!l.hasPrefix("V "))
				continue;
			char input[32];
			int bow, bar, objet, head, section, cinema, menu;
			int frames, ended, keep, menuAfter, barAfter, fade, click, skipflag;
			if (sscanf(l.c_str(), "V %31s bow %d bar %d objet %d head %d section %d cinema %d menu %d: "
			           "frames %d ended %d keep %d menu %d bar %d fade %d click %d skipflag %d",
			           input, &bow, &bar, &objet, &head, &section, &cinema, &menu,
			           &frames, &ended, &keep, &menuAfter, &barAfter, &fade, &click, &skipflag) != 16) {
				TS_FAIL(l.c_str());
				continue;
			}
			rows++;
			const Common::String in(input);
			VideoSkipState s;
			s.bowMode = bow;
			s.barRequest = bar;
			s.objectInHand = objet;
			s.talkingHead = head;
			s.sectionPlay = section;
			s.menuRequest = menu;
			// One poll after each presented frame; the presses come at polls
			// 2 (and 4 for the second key of rbutton+escape).
			uint presented = kFrames;
			bool stop = false;
			for (uint poll = 1; poll < kFrames && !stop; poll++) {
				const bool left = in == "lbutton" && poll == 2;
				const bool right = (in == "rbutton" || in == "rbutton+escape") && poll == 2;
				const bool esc = (in == "escape" && poll == 2) || (in == "rbutton+escape" && poll == 4);
				if (videoSkipPoll(s, left, right, esc)) {
					stop = true;
					presented = poll + 1;   // the frame presented after the loop
				}
			}
			const int gotFade = videoSkipFades(s, cinema != 0) ? 2 : 0;
			// FUN_0041bc74's end clears these whatever happened.
			if ((int)presented != frames || (int)stop != ended || gotFade != fade ||
			        keep != 1 || menuAfter != 0 || barAfter != 0 || click != 0 || skipflag != 0) {
				bad++;
				if (bad < 10)
					warning("video skip: %s: port frames %u ended %d fade %d", l.c_str(), presented, stop, gotFade);
			}
		}
		TS_ASSERT_EQUALS(rows, 7u * 128u);
		TS_ASSERT_EQUALS(bad, 0u);
	}
};
