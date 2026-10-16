#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_GAMEOVER_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_GAMEOVER_H

// Verifier "scripts": the game-over screen at each of the 144 game overs.
//
// Original: atlantis-cd-en-714240 FUN_00426590 run whole under Unicorn by
// atlantis-re/tools/emu/scripts_gameover.py (golden scripts_gameover.txt,
// experiment 2026-10-16-09) on the narrator pointer the executor passes at
// every gameover= of the 75 CON files: image, voice, subtitle blits for
// SUBTITLES x counter 0x00496098, the NewSound fade, and the wait for 7
// inputs with and without the voice.
//
// Port: conGameOverNarrator / conTrackLetter (con_script.h) as
// playConSection captures the game over, gameOverImageName,
// gameOverVoiceName, gameOverSubtitleDrawn, gameOverFadesNewSound,
// gameOverWaitEnds (gameover.h) and splitSubtitle / subtitleLineY as
// showGameOver runs them.

#include "scripts_con_common.h"
#include "../../../system/null_osystem.h"

#include "cryomni3d/atlantis/gameover.h"
#include "cryomni3d/atlantis/subtitle_layout.h"

#define TS_WARN_FMT(...) TS_WARN(Common::String::format(__VA_ARGS__).c_str())

class ScriptsGameOverTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_game_over_vs_original() {
		using namespace CryOmni3D::Atlantis;
		if (!AtlantisTest::haveData()) {
			TS_WARN("game over: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> lines;
		TS_ASSERT(AtlantisTest::loadGoldenLines("scripts_gameover.txt", lines));
		ScriptsTest::Big big;
		Common::HashMap<Common::String, ConScript *> scripts;
		// (con, offset) -> the port's capture.
		struct Capture {
			int section = 0, n = 0;
			char letter = 0;
			bool narrator = false;
			Common::String text;
		};
		auto capture = [&](const Common::String &con, int noff, Capture &out) -> bool {
			if (!scripts.contains(con)) {
				Common::Array<char> data;
				if (!big.get(Common::String::format("SCENAR\\%s.CON", con.c_str()), data))
					return false;
				ConScript *s = new ConScript();
				s->parse(data.begin(), data.size());
				scripts[con] = s;
			}
			const ConScript &script = *scripts[con];
			const Common::Array<char> &text = script.text();
			for (const ConSection &sec : script.sections()) {
				uint lineIdx = 0;
				for (uint k = 0; k < sec.items.size(); k++) {
					const ConSectionItem &it = sec.items[k];
					if (!it.isSet) {
						lineIdx++;
						continue;
					}
					if (scumm_strnicmp(it.setCmd.c_str(), "gameover=", 9) != 0 ||
					        atoi(it.setCmd.c_str() + 9) == 0)
						continue;
					// The executor passes the byte after the /set line's LF.
					int p = it.offset;
					while (p < (int)text.size() && text[p] != '\n')
						p++;
					if (p + 1 != noff)
						continue;
					out.section = sec.id;
					out.n = atoi(it.setCmd.c_str() + 9);
					out.narrator = conGameOverNarrator(sec.items, k, out.text);
					out.letter = conTrackLetter(lineIdx);
					return true;
				}
			}
			return false;
		};
		uint gos = 0, waits = 0, bad = 0;
		Common::HashMap<Common::String, Capture> caps;
		for (uint li = 0; li < lines.size(); li++) {
			const Common::String &l = lines[li];
			char con[16];
			int noff = 0;
			if (l.hasPrefix("G ")) {
				int sec = 0, n = 0, f5 = 0, vol = 0, last = 0, f0 = 0, amb = 0, ret = 0;
				char image[64], apc[64];
				int sub[6];
				if (sscanf(l.c_str(), "G %15s @%d sec %d n %d: image %63s apc %63s sub a0c0=%d a0c1=%d a0c2=%d "
				           "a1c0=%d a1c1=%d a1c2=%d newsound5 fade %d sfxvol %d sfxlast %d newsound0 fade %d "
				           "ambient1 stop %d ret %d", con, &noff, &sec, &n, image, apc, &sub[0], &sub[1], &sub[2],
				           &sub[3], &sub[4], &sub[5], &f5, &vol, &last, &f0, &amb, &ret) != 18) {
					TS_FAIL(l.c_str());
					continue;
				}
				gos++;
				Capture c;
				if (!capture(con, noff, c)) {
					bad++;
					TS_WARN_FMT("game over %s @%d: not found by the port", con, noff);
					continue;
				}
				caps[Common::String::format("%s %d", con, noff)] = c;
				Common::String img = Common::String("IMAGES\\") + gameOverImageName(c.n);
				Common::String voice = Common::String("WAV\\") + gameOverVoiceName(c.section, c.letter);
				bool ok = c.n == n && c.section == sec && c.narrator &&
				          img.equalsIgnoreCase(image) && voice.equalsIgnoreCase(apc);
				for (int a = 0; a < 2; a++)
					for (int k = 0; k < 3; k++)
						ok = ok && gameOverSubtitleDrawn(a != 0) == (sub[a * 3 + k] != 0);
				ok = ok && gameOverFadesNewSound(5) == (f5 == 2) && gameOverFadesNewSound(0) == (f0 == 2);
				if (!ok) {
					bad++;
					TS_WARN_FMT("game over differs: %s (port %s %s n %d sec %d)", l.c_str(), img.c_str(),
					        voice.c_str(), c.n, c.section);
				}
			} else if (l.hasPrefix("T ")) {
				if (sscanf(l.c_str(), "T %15s @%d:", con, &noff) != 2) {
					TS_FAIL(l.c_str());
					continue;
				}
				const Common::String key = Common::String::format("%s %d", con, noff);
				if (!caps.contains(key))
					continue;
				// The blits: shadow (x 6, y + 1, black) then text (x 5, y,
				// white) per line, '|'-separated, each x,y,colour,"text".
				Common::Array<Common::String> got;
				splitSubtitle(caps[key].text, got);
				Common::String want;
				for (uint i = 0; i < got.size(); i++) {
					const int y = subtitleLineY(i, got.size());
					want += Common::String::format("%s%d,%d,000000,\"%s\" | %d,%d,ffffff,\"%s\"", i ? " | " : "",
					                               kSubtitleX + 1, y + 1, got[i].c_str(), kSubtitleX, y, got[i].c_str());
				}
				// The port's CON parser drops the trailing spaces of a line
				// (con_script.cpp rtrim); the original draws them, which shows
				// nothing.  Compared without them.
				Common::String blits(strchr(strchr(l.c_str(), '"') + 1, '"') + 2);
				for (;;) {
					const uint sp = blits.find(" \"");
					if (sp == Common::String::npos)
						break;
					blits.erase(sp, 1);
				}
				if (want != blits) {
					bad++;
					TS_WARN_FMT("game over text differs: %s\n  port %s", l.c_str(), want.c_str());
				}
			} else if (l.hasPrefix("W ")) {
				char kind[16], input[32];
				int polls = 0, ms = 0, started = 0;
				if (sscanf(l.c_str(), "W %15s @%d %15s %31[^:]: polls %d ms %d started %d", con, &noff, kind, input,
				           &polls, &ms, &started) != 7) {
					TS_FAIL(l.c_str());
					continue;
				}
				waits++;
				const Common::String in(input);
				const bool withVoice = !strcmp(kind, "sound");
				// The harness: presses at the 2nd poll; the voice reads
				// "playing" 6 times then "done"; the clock +250 ms per read.
				VideoSkipState s;
				s.sectionPlay = true;
				s.objectInHand = in == "escape+objet";
				uint poll = 0;
				int pass = 0;
				for (;;) {
					pass++;
					const bool pressNow = poll + 1 == 2;
					const bool left = pressNow && in == "lbutton";
					const bool right = pressNow && in == "rbutton";
					const bool esc = pressNow && (in == "escape" || in == "escape+objet");
					const VideoSkipState before = s;
					const bool ends = gameOverWaitEnds(s, withVoice, pass > 6, (uint32)(250 * pass), left, right, esc);
					// Without the voice the poll happens only on a pass that
					// goes on; with it, on every pass.
					if (withVoice || !ends)
						poll++;
					else
						s = before;
					if (ends || pass > 100)
						break;
				}
				if ((int)poll != polls) {
					bad++;
					TS_WARN_FMT("game over wait differs: %s (port %u polls)", l.c_str(), poll);
				}
			}
		}
		TS_ASSERT_EQUALS(gos, 144u);
		TS_ASSERT_EQUALS(waits, 144u * 14);
		TS_ASSERT_EQUALS(bad, 0u);
		for (auto &kv : scripts)
			delete kv._value;
	}
};

#endif
