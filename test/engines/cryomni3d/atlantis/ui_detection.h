// "ui" audit: Atlantis detection entry and game title.
// See atlantis-re/audit/ui/REPORT.md, section "Detection and metaengine".
//
// The detection table is plain data (detection_tables.h), included here
// directly.  The metaengine (metaengine.cpp) cannot be linked into the test
// runner (createInstance pulls in the whole engine); it is reviewed statically.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/fs.h"

#include "engines/advancedDetector.h"
#include "cryomni3d/detection.h"
#include "cryomni3d/detection_tables.h"
#include "cryomni3d/atlantis/bigfile.h"

class UiDetectionTestSuite : public CxxTest::TestSuite {
	// ScummVM's AdvancedDetector hashes the first 5000 bytes by default
	// (AdvancedMetaEngineDetection::_md5Bytes; CryOmni3D does not override it).
	static Common::String md5Head(const Common::FSNode &node, int64 &size) {
		Common::File f;
		if (!f.open(node))
			return Common::String();
		size = f.size();
		return Common::computeStreamMD5AsString(f, 5000);
	}

	static Common::FSNode findCaseless(const Common::String &dir, const char *name) {
		Common::FSNode d(Common::Path(dir, '/'));
		Common::FSList list;
		d.getChildren(list, Common::FSNode::kListFilesOnly);
		for (uint i = 0; i < list.size(); i++)
			if (list[i].getName().equalsIgnoreCase(name))
				return list[i];
		return Common::FSNode();
	}

	static Common::String readSource(const char *rel) {
		Common::String src = AtlantisTest::configValue("src");
		Common::File f;
		if (src.empty() || !f.open(Common::FSNode(Common::Path(src + "/" + rel, '/'))))
			return Common::String();
		Common::String text;
		while (!f.eos())
			text += f.readLine() + "\n";
		return text;
	}

public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Every file of the Atlantis CD entry: size and 5000-byte MD5 against the
	// reference files (atlantis.exe 714240 B full md5 9c850caeb212b3096442222606b3d392).
	void test_cd_entry_matches_reference_files() {
		if (!AtlantisTest::haveData()) { TS_WARN("data not configured; skipped"); return; }
		int checked = 0;
		for (const CryOmni3D::CryOmni3DGameDescription *g = CryOmni3D::gameDescriptions; g->desc.gameId; g++) {
			if (strcmp(g->desc.gameId, "atlantis") || (g->desc.flags & ADGF_DEMO))
				continue;
			for (const ADGameFileDescription *fd = g->desc.filesDescriptions; fd->fileName; fd++) {
				Common::FSNode n = findCaseless(AtlantisTest::dataDir(), fd->fileName);
				TSM_ASSERT(fd->fileName, n.exists());
				if (!n.exists())
					continue;
				int64 size = -1;
				Common::String md5 = md5Head(n, size);
				TSM_ASSERT_EQUALS(fd->fileName, md5, Common::String(fd->md5));
				TSM_ASSERT_EQUALS(fd->fileName, size, (int64)fd->fileSize);
				checked++;
			}
			TS_ASSERT_EQUALS(g->desc.language, Common::EN_ANY);
			TS_ASSERT_EQUALS(g->desc.platform, Common::kPlatformWindows);
		}
		TS_ASSERT_EQUALS(checked, 2);
	}

	// The full reference executable (not only its first 5000 bytes).
	void test_reference_exe_full_md5() {
		if (!AtlantisTest::haveData()) { TS_WARN("data not configured; skipped"); return; }
		Common::File f;
		Common::FSNode n = findCaseless(AtlantisTest::dataDir(), "atlantis.exe");
		TS_ASSERT(f.open(n));
		TS_ASSERT_EQUALS(Common::computeStreamMD5AsString(f), Common::String("9c850caeb212b3096442222606b3d392"));
	}

	// The game names itself "ATLANTIS / The Lost Tales" (first two centred
	// lines of SPRLIST\CREDITS.TXT).  Every user-visible title string in the
	// port must say "The Lost Tales".
	void test_title_consistency() {
		if (!AtlantisTest::haveData()) { TS_WARN("data not configured; skipped"); return; }
		SearchMan.addDirectory("ui_det_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		CryOmni3D::Atlantis::BigFileArchive big;
		bool opened = big.open(Common::Path("BIGCD1.BIG"));
		SearchMan.remove("ui_det_data");
		TS_ASSERT(opened);
		if (!opened)
			return;
		Common::SeekableReadStream *s = big.createReadStreamForMember(Common::String("SPRLIST\\CREDITS.TXT"));
		TS_ASSERT(s);
		if (!s)
			return;
		Common::Array<Common::String> centred;
		while (!s->eos() && centred.size() < 2) {
			Common::String line = s->readLine();
			if (line.hasPrefix("<>"))
				centred.push_back(line.substr(2));
		}
		delete s;
		TS_ASSERT_EQUALS(centred.size(), 2u);
		if (centred.size() < 2)
			return;
		TS_ASSERT_EQUALS(centred[0], Common::String("ATLANTIS"));
		TS_ASSERT_EQUALS(centred[1], Common::String("The Lost Tales"));

		static const char *const files[] = {
			"engines/cryomni3d/configure.engine",
			"engines/cryomni3d/detection.cpp",
			"engines/cryomni3d/detection_tables.h",
			"engines/cryomni3d/metaengine.cpp",
			"engines/cryomni3d/atlantis/engine.h" };
		for (uint i = 0; i < ARRAYSIZE(files); i++) {
			Common::String text = readSource(files[i]);
			if (text.empty()) { TS_WARN(Common::String::format("%s not readable (src=)", files[i]).c_str()); continue; }
			int bad = 0;
			for (const char *p = strstr(text.c_str(), "Lost Tale"); p; p = strstr(p + 1, "Lost Tale"))
				if (p[9] != 's')
					bad++;
			TSM_ASSERT_EQUALS(Common::String::format("'Lost Tale' without 's' in %s", files[i]).c_str(), bad, 0);
		}
	}

	// The demo entry (ATLANTIS.UBB) cannot be checked: no demo files here.
	void test_demo_entry_not_verifiable() {
		TS_WARN("Atlantis demo entry (ATLANTIS.UBB f5b41b85..., 106611456 B) NOT VERIFIABLE: no demo files available");
	}
};
