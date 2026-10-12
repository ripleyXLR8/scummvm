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
// Port: music.cpp kMusicTrackNames (static, read from the source file because
// it cannot be linked) and exetables.cpp loadAtlantisExeTables (engine
// method; FAITHFUL COPY of its newsound loop below, guarded by a span md5).

#include "scripts_con_common.h"

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

// md5 of the loadAtlantisExeTables span of exetables.cpp.  Updated when the
// inventory-table read moved to inventory.cpp; the newsound loop is unchanged.
static const char *const kExeTablesMd5 = "64a84740f38ea930bc818d5ad5f51231";

inline bool readSource(const char *rel, Common::String &out) {
	Common::String src = AtlantisTest::configValue("src");
	if (src.empty())
		return false;
	Common::FSNode node(Common::Path(src, '/').join(rel));
	Common::File f;
	if (!f.open(node))
		return false;
	Common::Array<char> buf(f.size() + 1);
	f.read(buf.begin(), f.size());
	buf[f.size()] = 0;
	out = buf.begin();
	return true;
}

inline Common::String spanMd5(const Common::String &text, const char *start) {
	const char *st = strstr(text.c_str(), start);
	if (!st)
		return Common::String();
	st++;
	const char *en = strstr(st, "\n}\n");
	if (!en)
		return Common::String();
	en += 3;
	return AtlantisTest::md5Hex((const byte *)st, en - st);
}

// FAITHFUL COPY of the newsound loop of loadAtlantisExeTables (exetables.cpp).
inline void portNewSoundNames(Common::File &exe, Common::Array<Common::String> &names) {
	const uint32 kImageBase    = 0x400000;
	const uint32 kDgroupVaddr   = 0x90000;
	const uint32 kDgroupRawPtr  = 0x71200;
	const uint32 kTableFileOff = 0x773e0;
	const int kSkipWams = 17;

	exe.seek(kTableFileOff + kSkipWams * 4);
	for (int i = 0; i < 200 && !exe.eos(); i++) {
		uint32 va = exe.readUint32LE();
		if (va < 0x4b0000 || va > 0x4d0000) break;
		uint32 fileOff = (va - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
		int64 saved = exe.pos();
		exe.seek(fileOff);
		Common::String s;
		while (!exe.eos()) {
			byte b = exe.readByte();
			if (b == 0) break;
			s += (char)b;
		}
		if (s.equalsIgnoreCase("FIN")) break;
		names.push_back(s);
		exe.seek(saved);
	}
}

} // End of namespace ScriptsMusicTest

class ScriptsMusicTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// kMusicTrackNames[N] (port, 1-based) must name the file the original
	// opens for StartMusik=N, i.e. table[N-1]; entry 0 unused.
	void test_music_table_matches_original() {
		Common::String text;
		if (!ScriptsMusicTest::readSource("engines/cryomni3d/atlantis/music.cpp", text)) {
			TS_WARN("scripts_music: no src= in atlantis_test.cfg, skipped");
			return;
		}
		const char *st = strstr(text.c_str(), "kMusicTrackNames[] = {");
		TS_ASSERT(st);
		if (!st)
			return;
		const char *en = strstr(st, "};");
		TS_ASSERT(en);
		Common::Array<Common::String> port;   // index 0 = first initializer
		for (const char *p = st + 22; p < en; p++) {
			if (!strncmp(p, "nullptr", 7)) {
				port.push_back("");
				p += 6;
			} else if (*p == '"') {
				const char *q = strchr(p + 1, '"');
				port.push_back(Common::String(p + 1, q - p - 1));
				p = q;
			} else if (p[0] == '/' && p[1] == '/') {
				while (p < en && *p != '\n')
					p++;
			}
		}
		TS_ASSERT_EQUALS(port.size(), ARRAYSIZE(ScriptsMusicTest::kOrigMusic) + 1);
		TS_ASSERT(port.size() > 0 && port[0].empty());
		for (uint n = 1; n < port.size() && n <= ARRAYSIZE(ScriptsMusicTest::kOrigMusic); n++)
			TS_ASSERT(port[n].equalsIgnoreCase(ScriptsMusicTest::kOrigMusic[n - 1]));
	}

	void test_exetables_copy_is_current() {
		Common::String text;
		if (!ScriptsMusicTest::readSource("engines/cryomni3d/atlantis/exetables.cpp", text)) {
			TS_WARN("scripts_music: no src= in atlantis_test.cfg, skipped");
			return;
		}
		TS_ASSERT_EQUALS(ScriptsMusicTest::spanMd5(text, "\nvoid CryOmni3DEngine_Atlantis::loadAtlantisExeTables("),
		                 Common::String(ScriptsMusicTest::kExeTablesMd5));
	}

	// Data test: the port's newsound name list (copied loop, reading the
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
