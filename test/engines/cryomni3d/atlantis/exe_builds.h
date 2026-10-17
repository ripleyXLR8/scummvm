// Builds of atlantis.exe (exe_build.h) and the GOG release's file layout.
//
// The reference build (atlantis-cd-en-714240) and the GOG build (one 1999
// executable for five languages, atlantis-re experiments 2026-10-17-05 ..
// -08): the port's tables at each build's addresses must hold the same
// bytes; the 1999 build's alternative game-over narrations; the unpacked
// tree read behind the stub archives.
//
// Port code exercised directly: atlantisExeBuild (exe_build.h),
// conAltGameOverNarration (con_script.h), BigFileArchive::addLooseRoot
// (bigfile.cpp).  Real files: `data` (reference) and `gog` (one
// sub-directory per language) in atlantis_test.cfg; skipped when absent.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/con_script.h"
#include "cryomni3d/atlantis/exe_build.h"

class AtlantisExeBuildsTestSuite : public CxxTest::TestSuite {
	static uint32 fileOff(uint32 va) { return va - 0x400000 - 0x90000 + 0x71200; }

	static bool readExe(const Common::String &path, Common::Array<byte> &out) {
		Common::File f;
		if (!f.open(Common::FSNode(Common::Path(path, '/'))))
			return false;
		out.resize((uint)f.size());
		return f.read(out.data(), out.size()) == out.size();
	}

	static Common::String cstr(const Common::Array<byte> &b, uint32 off) {
		Common::String s;
		while (off < b.size() && b[off])
			s += (char)b[off++];
		return s;
	}

	static CryOmni3D::Atlantis::ConSectionItem line(const char *speaker, const char *text) {
		CryOmni3D::Atlantis::ConSectionItem it;
		it.line.speaker = speaker;
		it.line.text = text;
		return it;
	}

public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_build_by_size() {
		using namespace CryOmni3D::Atlantis;
		TS_ASSERT_EQUALS(Common::String(atlantisExeBuild(714240).name), "atlantis-cd-en-714240");
		TS_ASSERT_EQUALS(Common::String(atlantisExeBuild(714752).name), "atlantis-gog-714752");
		TS_ASSERT_EQUALS(Common::String(atlantisExeBuild(1).name), "atlantis-cd-en-714240");
		TS_ASSERT(atlantisExeBuild(714240).cursorIdRule && atlantisExeBuild(714240).rheaCamera2);
		TS_ASSERT(!atlantisExeBuild(714240).altGameOverNarration);
		TS_ASSERT(!atlantisExeBuild(714752).cursorIdRule && !atlantisExeBuild(714752).rheaCamera2);
		TS_ASSERT(atlantisExeBuild(714752).altGameOverNarration);
	}

	// Each GOG language's executable: its build is the GOG one, and the
	// three tables at the GOG addresses equal the reference's tables; the
	// player-row strings at 0x00496570 (marker, then label).
	void test_gog_tables_equal_reference_tables() {
		using namespace CryOmni3D::Atlantis;
		const Common::String gog = AtlantisTest::configValue("gog");
		if (!AtlantisTest::haveData() || gog.empty()) { TS_WARN("data / gog not configured; skipped"); return; }
		Common::Array<byte> ref;
		TS_ASSERT(readExe(AtlantisTest::dataDir() + "/atlantis.exe", ref));
		const AtlantisExeBuild &rb = atlantisExeBuild(ref.size());
		TS_ASSERT_EQUALS(cstr(ref, fileOff(0x00496570)), "#New Player");
		static const char *const kLangs[] = { "en-US", "fr-FR", "de-DE", "es-ES", "it-IT" };
		for (uint l = 0; l < ARRAYSIZE(kLangs); l++) {
			Common::Array<byte> g;
			TSM_ASSERT(kLangs[l], readExe(gog + "/" + kLangs[l] + "/Atlantis.exe", g));
			if (g.empty())
				continue;
			const AtlantisExeBuild &gb = atlantisExeBuild(g.size());
			TSM_ASSERT_EQUALS(kLangs[l], Common::String(gb.name), "atlantis-gog-714752");
			TS_ASSERT(!memcmp(&ref[fileOff(rb.inventoryVA)], &g[fileOff(gb.inventoryVA)], 52 * 4));
			TS_ASSERT(!memcmp(&ref[fileOff(rb.guestVA)], &g[fileOff(gb.guestVA)], 64 * 4));
			TS_ASSERT(!memcmp(&ref[fileOff(rb.creditWidthsVA)], &g[fileOff(gb.creditWidthsVA)], 256));
			const Common::String marker = cstr(g, fileOff(0x00496570));
			TS_ASSERT(marker.hasPrefix("#"));
			// the label follows the marker's NUL
			TS_ASSERT(!cstr(g, fileOff(0x00496570) + marker.size() + 1).empty());
			if (l == 1)
				TS_ASSERT_EQUALS(marker, "#NOUVEAU JOUEUR");
		}
	}

	// FUN_00426570 of the 1999 build: deaths in one section alternate
	// between the first narrator line and the next one; with no second
	// narrator line the first is always used.
	void test_alt_game_over_narration() {
		using namespace CryOmni3D::Atlantis;
		Common::Array<ConSectionItem> items;
		ConSectionItem set;
		set.isSet = true;
		set.setCmd = "gameover=3";
		items.push_back(set);
		items.push_back(line("Narratrice", "first"));
		items.push_back(line("Narratrice", "second"));
		uint8 count = 0;
		TS_ASSERT_EQUALS(conAltGameOverNarration(items, 0, count), 0u);
		TS_ASSERT_EQUALS(count, 1);
		TS_ASSERT_EQUALS(conAltGameOverNarration(items, 0, count), 1u);
		TS_ASSERT_EQUALS(count, 2);
		TS_ASSERT_EQUALS(conAltGameOverNarration(items, 0, count), 0u);   // 2 -> 0
		TS_ASSERT_EQUALS(count, 1);
		items.pop_back();
		items.push_back(line("Seth", "not a narrator"));
		TS_ASSERT_EQUALS(conAltGameOverNarration(items, 0, count), 0u);   // reset
		TS_ASSERT_EQUALS(count, 1);
	}

	// GOG French: the stub BIGCD1.BIG holds 14 sounds; everything else comes
	// from the unpacked tree through addLooseRoot.
	void test_gog_loose_tree() {
		using namespace CryOmni3D::Atlantis;
		const Common::String gog = AtlantisTest::configValue("gog");
		if (gog.empty()) { TS_WARN("gog not configured; skipped"); return; }
		const Common::FSNode dir(Common::Path(gog + "/fr-FR", '/'));
		SearchMan.addDirectory("atl_gog_fr", dir);
		BigFileArchive big;
		const bool opened = big.open(Common::Path("BIGCD1.BIG"));
		SearchMan.remove("atl_gog_fr");
		TS_ASSERT(opened);
		TS_ASSERT_EQUALS(big.numFiles(), 14u);
		TS_ASSERT(big.hasFile(Common::String("WAV\\CRAK_1.APC")));
		TS_ASSERT(!big.hasFile(Common::String("SCENAR\\CHAPI001.CON")));
		big.addLooseRoot(dir);
		TS_ASSERT(big.hasFile(Common::String("SCENAR\\CHAPI001.CON")));
		Common::ScopedPtr<Common::SeekableReadStream> s(
		    big.createReadStreamForMember(Common::String("SCENAR\\CHAPI001.CON")));
		TS_ASSERT(s);
		Common::File f;
		TS_ASSERT(f.open(dir.getChild("SCENAR").getChild("CHAPI001.CON")));
		if (s)
			TS_ASSERT_EQUALS(s->size(), f.size());
		// a name in a deeper sub-directory, read through the tree too
		TS_ASSERT(big.hasFile(Common::String("SPRITE\\2D\\FONTMIN.SPR")));
	}
};
