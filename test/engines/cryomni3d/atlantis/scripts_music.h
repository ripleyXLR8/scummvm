#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_MUSIC_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_MUSIC_H

// Verifier "scripts", element 3: music and newsound tables.
//
// Original (atlantis-cd-en-714240):
//  - /set(StartMusik=N): FUN_0042a100 @ 0x0042ae0f calls FUN_00432ea8(N-1);
//    FUN_00432ea8 opens table[N-1], table = 13-byte records at 0x00495eec,
//    31 names (atlantis-re/tools/emu/scripts_music.py).
//  - /set(NewSound=N) only stores the variable; FUN_00432a64 (polled by
//    FUN_00428da8 after every script line) plays "wav\" + ptrtable[N],
//    ptrtable = 0x00496224 (N used as is, 0 = no sound).
// Port: kMusicTrackNames (music_tracks.cpp, linked) and readNewSoundNames
// (exe_newsound.h), which loadAtlantisExeTables calls.
//
// The music-table test compares the port's linked table with the golden
// scripts_music.txt (music_names: md5 of the 31 upper-cased names joined
// with '\n', as atlantis-re tools/emu/scripts_music.py computes it from the
// loaded image) and with the transcription kOrigMusic, which the range test
// below uses.  Without golden= the golden half is skipped.

#include "scripts_con_common.h"

#include "cryomni3d/atlantis/exe_newsound.h"
#include "cryomni3d/atlantis/music_tracks.h"

#include "common/system.h"

namespace ScriptsMusicTest {

// Original 0x00495eec table, index = StartMusik - 1 (scripts_music.py).
static const char *const kOrigMusic[] = {
	"01atlan1.apc", "10atlan2.apc", "23_pala1.apc", "02auberg.apc", "24_rats.apc",
	"dauphins.apc", "05sstron.apc", "metamorf.apc", "labyrint.apc", "31cranes.apc",
	"stoneheg.apc", "ascense1.apc", "foret_35.apc", "23_pret1.apc", "20sorcie.apc",
	"06spitz2.apc", "30shaman.apc", "21pacque.apc", "08tisse2.apc", "33intro.apc",
	"hangar1.apc", "04generi.apc", "tete1.apc", "tete2.apc", "03attack.apc",
	"09voltot.apc", "cine101.apc", "lac.apc", "cine147.apc", "cine172.apc",
	"cine194.apc"
};


// The port's newsound table reader (exe_newsound.h).
inline void portNewSoundNames(Common::File &exe, Common::Array<Common::String> &names) {
	CryOmni3D::Atlantis::readNewSoundNames(exe, names);
}

} // End of namespace ScriptsMusicTest

class ScriptsMusicTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// kMusicTrackNames[N] (port, 1-based) must name the file the original
	// opens for StartMusik=N, i.e. table[N-1]; entry 0 unused.
	void test_music_table_matches_original() {
		using CryOmni3D::Atlantis::kMusicTrackNames;
		using CryOmni3D::Atlantis::kMusicTrackCount;
		TS_ASSERT_EQUALS((uint)kMusicTrackCount, ARRAYSIZE(ScriptsMusicTest::kOrigMusic) + 1);
		TS_ASSERT(kMusicTrackNames[0] == nullptr);
		Common::String joined;
		for (uint n = 1; n < (uint)kMusicTrackCount; n++) {
			TS_ASSERT(kMusicTrackNames[n] != nullptr);
			if (!kMusicTrackNames[n])
				continue;
			if (n <= ARRAYSIZE(ScriptsMusicTest::kOrigMusic))
				TS_ASSERT(Common::String(kMusicTrackNames[n]).equalsIgnoreCase(ScriptsMusicTest::kOrigMusic[n - 1]));
			Common::String s = kMusicTrackNames[n];
			s.toUppercase();
			if (n > 1)
				joined += "\n";
			joined += s;
		}
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("scripts_music: no golden= in atlantis_test.cfg, golden comparison skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("scripts_music.txt", golden));
		TS_ASSERT(golden.contains("music_names"));
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex((const byte *)joined.c_str(), joined.size()),
		                 golden["music_names"]);
	}

	// Data test: the port's newsound name list (readNewSoundNames, reading the
	// reference atlantis.exe) equals the original pointer table 0x00496224.
	void test_newsound_table_matches_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("scripts_music: no game data configured, skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("scripts_music.txt", golden));
		Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("atlantis.exe"));
		Common::File exe;
		TS_ASSERT(exe.open(node));
		if (!exe.isOpen())
			return;
		Common::Array<Common::String> names;
		ScriptsMusicTest::portNewSoundNames(exe, names);
		Common::String joined;
		for (uint i = 0; i < names.size(); i++) {
			Common::String s = names[i];
			s.toUppercase();
			if (i)
				joined += "\n";
			joined += s;
		}
		TS_ASSERT_EQUALS(Common::String::format("%u", names.size()), golden["newsound_count"]);
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex((const byte *)joined.c_str(), joined.size()),
		                 golden["newsound_names"]);
	}

	// Data test: every StartMusik / NewSound value used by the CON scripts
	// names an existing entry (1..31 / 1..136) and the file exists in WAV\.
	void test_script_sound_ids_in_range() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("scripts_music: no game data configured, skipped");
			return;
		}
		ScriptsTest::Big big;
		Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("atlantis.exe"));
		Common::File exe;
		TS_ASSERT(exe.open(node));
		if (!exe.isOpen())
			return;
		Common::Array<Common::String> sounds;
		ScriptsMusicTest::portNewSoundNames(exe, sounds);
		Common::Array<Common::String> files = big.names("SCENAR\\CHAPI", ".CON");
		uint bad = 0, seen = 0;
		for (uint f = 0; f < files.size(); f++) {
			Common::Array<char> data;
			big.get(files[f], data);
			Common::String t(data.begin(), data.size());
			t.toLowercase();
			const char *keys[] = { "startmusik=", "newsound=" };
			for (int k = 0; k < 2; k++) {
				for (const char *p = strstr(t.c_str(), keys[k]); p; p = strstr(p + 1, keys[k])) {
					int v = atoi(p + strlen(keys[k]));
					seen++;
					if (k == 0) {
						if (v < 1 || v > (int)ARRAYSIZE(ScriptsMusicTest::kOrigMusic)) { bad++; continue; }
						Common::String n = Common::String("WAV\\") + ScriptsMusicTest::kOrigMusic[v - 1];
						n.toUppercase();
						if (!big.has(n)) bad++;
					} else {
						if (v < 1 || v >= (int)sounds.size()) { bad++; continue; }
						Common::String n = "WAV\\" + sounds[v];
						n.toUppercase();
						if (!big.has(n)) bad++;
					}
				}
			}
		}
		TS_ASSERT(seen > 400);
		TS_ASSERT_EQUALS(bad, 0u);
	}
};

#endif
