#include "atlantis_test_helper.h"

#include "common/array.h"
#include "common/memstream.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/saveload_format.h"

#include "../../../system/null_osystem.h"

// Audit "ui", element 2: saves and player profiles.
//
// Expected values come from the ORIGINAL code of atlantis-cd-en-714240, run
// unmodified under Unicorn by atlantis-re/tools/emu/ui_saves.py:
//   FUN_0042d6b8 (checkpoint writer: p<L><N>.gam + players.atl),
//   0x0041916b..0x004192e0 (players.atl reader), FUN_0042de5c (gam reader),
//   0x0041d16a.. (load-picker sort), 0x0041d3a1..0x0041d431 (EPI.TXT name of
//   a save), FUN_0041e8a8 (delete player).
//
// Port code exercised directly: epiEpisodeName, readPlayersFile and
// checkpointWritten (saveload_format.cpp), which the engine's saveload.cpp
// and menus.cpp call.

namespace UiSaves {

// players.atl written by the original FUN_0042d6b8 for: names Alice, Bob,
// "#New Player"; resume chapters {3,7} with player 0 saving chapter 12;
// letters "abcdef".  97 bytes.
static const byte kOrigPlayersAtl[97] = {
	0x41,0x6c,0x69,0x63,0x65,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x42,0x6f,0x62,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x23,0x4e,0x65,0x77,0x20,0x50,0x6c,0x61,0x79,0x65,0x72,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x0c,0x07,0x00,0x00,0x00,0x00,
	0x61,0x62,0x63,0x64,0x65,0x66
};

// p<L><N>.gam fields in write (= read) order: source VA in the exe, size.
struct GamField { uint32 va; uint32 size; };
static const GamField kGamFields[] = {
	{0x6cdce0, 0x104}, {0x772909, 1}, {0x772907, 1}, {0x496920, 4}, {0x6cd7a8, 4},
	{0x6cd7b4, 4}, {0x6cd7c0, 4}, {0x49692c, 4}, {0x6cd7b8, 4}, {0x77290d, 1},
	{0x6cd7bc, 4}, {0x6ce138, 0x10e}, {0x6cc6cc, 0x5dc}, {0x611210, 0x1c},
	{0x77290e, 1}, {0x6cd7fc, 0x14}, {0x6cd390, 4}, {0x611010, 0x200},
	{0x4960a8, 4}, {0x4960ac, 4}, {0x495e70, 1}, {0x495e72, 1}, {0x495e73, 1},
	{0x495e74, 1}, {0x772913, 1}
};

// Original save-file name (FUN_0042d6b8 @ 0x42d7db..0x42d86b): install dir +
// "\p" + player letter + decimal chapter + ".gam".
static Common::String origSaveName(char letter, uint chapter) {
	return Common::String::format("p%c%u.gam", letter, chapter);
}

// Original players.atl decoding (reader block @ 0x41916b): row count = index of
// the first "#New Player" or empty name, plus one ("#New Player" row) unless 5.
struct OrigProfiles {
	Common::String names[5];
	byte chapters[6];
	byte letters[6];
	uint rows;
};
static OrigProfiles origDecodePlayersAtl(const byte *b) {
	OrigProfiles p;
	for (int i = 0; i < 5; i++)
		p.names[i] = Common::String((const char *)b + 17 * i);
	memcpy(p.chapters, b + 85, 6);
	memcpy(p.letters, b + 91, 6);
	uint n = 0;
	while (n < 5 && p.names[n] != "#New Player" && !p.names[n].empty())
		n++;
	p.rows = n < 5 ? n + 1 : n;
	return p;
}

// ---- Adapters over the port's save formats (saveload_format.cpp).
static void portLoadPlayers(Common::SeekableReadStream *in, Common::String players[5], int playerChapter[5]) {
	CryOmni3D::Atlantis::readPlayersFile(*in, players, playerChapter);
}

typedef Common::Array<byte> EpiText;
static EpiText portEpisodeNames(const byte *data, uint32 sz) {
	EpiText t;
	t.resize(sz);
	if (sz)
		memcpy(t.data(), data, sz);
	return t;
}
static Common::String portEpisodeName(const EpiText &text, uint chapter) {
	return CryOmni3D::Atlantis::epiEpisodeName(text.data(), text.size(), chapter);
}

static bool portAutosaveWrites(const EpiText &, uint chapter) {
	return CryOmni3D::Atlantis::checkpointWritten(chapter);
}

// Port-independent read of one member of BIGCD1.BIG (same layout as
// atlantis-re/tools/emu/ui_big.py).
static bool readBigMember(const char *member, Common::Array<byte> &out) {
	Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("BIGCD1.BIG"));
	Common::File f;
	if (!f.open(node))
		return false;
	f.seek(16);
	uint32 n = f.readUint32LE();
	f.seek(24);
	uint32 base = f.readUint32LE();
	f.seek(32);
	uint32 seq = 0;
	for (uint32 i = 0; i < n; i++) {
		uint32 rec[8];
		for (int k = 0; k < 8; k++)
			rec[k] = f.readUint32LE();
		Common::String name;
		for (uint32 k = 0; k < rec[0]; k++) {
			char c = (char)f.readByte();
			if (c)
				name += c;
		}
		if (name.equalsIgnoreCase(member)) {
			out.resize(rec[2]);
			f.seek(base + seq);
			return f.read(out.data(), rec[2]) == rec[2];
		}
		seq += rec[2];
	}
	return false;
}

} // namespace UiSaves

class UiSavesTestSuite : public CxxTest::TestSuite {
public:
	// The data test opens files through Common::FSNode, which needs g_system.
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Format documentation check: players.atl = 5 x 17-byte names, 6 resume
	// chapter bytes, 6 save-letter bytes (writer FUN_0042d6b8 @ 0x42dc75..).
	void test_original_players_atl_layout() {
		using namespace UiSaves;
		byte b[97];
		memset(b, 0, sizeof(b));
		const char *names[3] = {"Alice", "Bob", "#New Player"};
		for (int i = 0; i < 3; i++)
			memcpy(b + 17 * i, names[i], strlen(names[i]));
		b[85] = 12; b[86] = 7;
		memcpy(b + 91, "abcdef", 6);
		TS_ASSERT_EQUALS(memcmp(b, kOrigPlayersAtl, 97), 0);

		OrigProfiles p = origDecodePlayersAtl(kOrigPlayersAtl);
		// Emulated reader: 3 rows (Alice, Bob, "#New Player").
		TS_ASSERT_EQUALS(p.rows, 3u);
		TS_ASSERT_EQUALS(p.names[0], "Alice");
		TS_ASSERT_EQUALS(p.chapters[0], 12);
		TS_ASSERT_EQUALS(p.letters[1], 'b');
		// The resume save of a player is p<letter><chapter>.gam.
		TS_ASSERT_EQUALS(origSaveName(p.letters[0], p.chapters[0]), "pa12.gam");
		TS_ASSERT_EQUALS(origSaveName('c', 40), "pc40.gam");   // emulated writer, player 2, letters "edcba"
	}

	// Format documentation check: p<L><N>.gam is a raw dump of 25 globals,
	// 2639 bytes.  Each field filled with marker bytes by the harness; the
	// writer fixes chapter=12, 0x496920=0, 0x6cd7c0=0 and copies the old
	// 0x6cd7a8 into 0x6cd7b4.  MD5 of the emulated writer output embedded.
	void test_original_gam_layout() {
		using namespace UiSaves;
		Common::Array<byte> g;
		uint total = 0;
		for (uint k = 0; k < ARRAYSIZE(kGamFields); k++) {
			uint32 va = kGamFields[k].va, n = kGamFields[k].size;
			total += n;
			for (uint32 j = 0; j < n; j++) {
				byte v = (byte)(((k + 1) * 16 + j) & 0xff);
				if (va == 0x772909) v = 12;
				if (va == 0x496920 || va == 0x6cd7c0) v = 0;
				if (va == 0x6cd7b4) v = (byte)((5 * 16 + j) & 0xff);
				g.push_back(v);
			}
		}
		TS_ASSERT_EQUALS(total, 2639u);
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex(g.data(), g.size()),
		                 "ccc24a09a35582201e2818ff5f9f0c57");
	}

	// An original players.atl: the port reads its names and chapters (when
	// its own "atlantis-players.dat" is absent).
	void test_port_reads_original_players_atl() {
		using namespace UiSaves;
		Common::MemoryReadStream s(kOrigPlayersAtl, sizeof(kOrigPlayersAtl));
		Common::String players[5];
		int chapters[5];
		portLoadPlayers(&s, players, chapters);
		OrigProfiles want = origDecodePlayersAtl(kOrigPlayersAtl);
		TS_ASSERT_EQUALS(players[0], want.names[0]);   // "Alice"
		TS_ASSERT_EQUALS(players[1], want.names[1]);   // "Bob"
		TS_ASSERT_EQUALS(chapters[0], (int)want.chapters[0]);   // 12
	}

	// EPI.TXT: chapter N's save is shown with line N-1 (0-based).  Synthetic
	// CR/LF file; expected names from the emulated original.
	void test_port_episode_names_crlf() {
		using namespace UiSaves;
		const char epi[] = "\r\nTwo\r\n\r\nFour \r\nFive\r\n/FIN\r\n";
		EpiText n = portEpisodeNames((const byte *)epi, sizeof(epi) - 1);
		TS_ASSERT_EQUALS(portEpisodeName(n, 2), "Two");
		TS_ASSERT_EQUALS(portEpisodeName(n, 3), "");
		TS_ASSERT_EQUALS(portEpisodeName(n, 4), "Four ");   // trailing space kept by both
		TS_ASSERT_EQUALS(portEpisodeName(n, 5), "Five");
	}

	// Edge cases (no effect on the shipped EPI.TXT): the original returns the
	// "/FIN" line itself for the chapter after the last name, and splits
	// lines only at CR (a bare LF stays inside the name).
	void test_port_episode_names_edge_cases() {
		using namespace UiSaves;
		const char epi[] = "\r\nTwo\r\n\r\nFour \r\nFive\r\n/FIN\r\n";
		EpiText n = portEpisodeNames((const byte *)epi, sizeof(epi) - 1);
		TS_ASSERT_EQUALS(portEpisodeName(n, 6), "/FIN");
		const char lf[] = "\r\nTwo\nX\r\nThree\r\n/fin\r\n";
		EpiText m = portEpisodeNames((const byte *)lf, sizeof(lf) - 1);
		TS_ASSERT_EQUALS(portEpisodeName(m, 2), "Two\nX");
		TS_ASSERT_EQUALS(portEpisodeName(m, 3), "Three");
	}

	// The original writes a checkpoint on every CON chapter change
	// (FUN_0042a100 -> FUN_00427634 @ 0x42765f -> FUN_0042d6b8) unless a load
	// is in progress, even for a chapter with an empty EPI name.
	void test_port_checkpoint_rule() {
		using namespace UiSaves;
		const char epi[] = "\r\nTwo\r\n\r\nFour \r\n/FIN\r\n";
		EpiText n = portEpisodeNames((const byte *)epi, sizeof(epi) - 1);
		TS_ASSERT(portAutosaveWrites(n, 2));
		TS_ASSERT(portAutosaveWrites(n, 3));   // original: p<L>3.gam written
	}

	// Data test: every episode name of the shipped EPI.TXT, as the original
	// load picker extracts it, versus the port's epiEpisodeName.
	// Chapters 2..85 (all chapters the CON scripts set).
	void test_data_episode_names_vs_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("ui_saves: no game data configured, skipping");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("ui_saves_epi.txt", golden));
		Common::Array<byte> data;
		TS_ASSERT(UiSaves::readBigMember("SPRLIST\\EPI.TXT", data));
		if (data.empty())
			return;
		TS_ASSERT_EQUALS(AtlantisTest::md5Hex(data.data(), data.size()), golden["epi.txt"]);
		UiSaves::EpiText n = UiSaves::portEpisodeNames(data.data(), data.size());
		for (uint ch = 2; ch <= 85; ch++) {
			Common::String key = Common::String::format("chapter %u", ch);
			Common::String name = UiSaves::portEpisodeName(n, ch);
			TSM_ASSERT_EQUALS(key.c_str(),
			    AtlantisTest::md5Hex((const byte *)name.c_str(), name.size()), golden[key]);
		}
	}
};
