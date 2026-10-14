#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_BIGFILE_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_BIGFILE_H

// Verifier "archive": BigFileArchive (engines/cryomni3d/atlantis/bigfile.cpp)
// against cryo.dll's BigFile reader (_GL_OpenBigFile@4 @ 0x00416052,
// _GL_OpenFileBigFile@4 @ 0x00416363, _GL_ReadFileBigFile@12 @ 0x0041653f,
// cryo.dll md5 543d8db266e4cba81be984b50a201752).
//
// Self-contained tests: tiny synthetic archives (built by
// atlantis-re/tools/emu/archive_big_synth.py) and the results cryo.dll gives
// for them when run under Unicorn.
// Data tests: every member of BIGCD1..4.BIG against
// evidence/golden/archive_big.txt, produced by atlantis-re/tools/emu/archive_big_emu.py
// (cryo.dll run under Unicorn).  See atlantis-re/audit/archive/REPORT.md §1.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"

#include "cryomni3d/atlantis/bigfile.h"

namespace ArchiveBigTest {

// Synthetic archives (not game data).
	static const byte k_standard[] = {0x42,0x69,0x67,0x46,0x69,0x6c,0x65,0x20,0x31,0x2e,0x30,0x30,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0xc4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x34,0x12,0x00,0x00,0x44,0x49,0x52,0x5c,0x41,0x2e,0x54,0x58,0x54,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x35,0x12,0x00,0x00,0x44,0x49,0x52,0x5c,0x42,0x2e,0x42,0x49,0x4e,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x36,0x12,0x00,0x00,0x43,0x2e,0x44,0x41,0x54,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x44,0x49,0x52,0x5c,0x41,0x2e,0x54,0x58,0x54,0x00,0x61,0x6c,0x70,0x68,0x61,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x53,0x45,0x43,0x4f,0x4e,0x44};
	static const byte k_adr_order[] = {0x42,0x69,0x67,0x46,0x69,0x6c,0x65,0x20,0x31,0x2e,0x30,0x30,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x8e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x34,0x12,0x00,0x00,0x4f,0x4e,0x45,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x35,0x12,0x00,0x00,0x54,0x57,0x4f,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x54,0x48,0x52,0x45,0x45,0x00,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x32,0x32,0x31,0x31,0x31,0x31,0x31};
	static const byte k_header_fields[] = {0x42,0x69,0x67,0x46,0x69,0x6c,0x65,0x20,0x31,0x2e,0x30,0x30,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x34,0x12,0x00,0x00,0x46,0x49,0x52,0x53,0x54,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x53,0x45,0x43,0x4f,0x4e,0x44,0x00,0x66,0x69,0x72,0x73,0x74,0x21,0x32,0x6e,0x64};
	static const byte k_lowercase_name[] = {0x42,0x69,0x67,0x46,0x69,0x6c,0x65,0x20,0x31,0x2e,0x30,0x30,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x4c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x64,0x69,0x72,0x5c,0x6c,0x6f,0x77,0x2e,0x74,0x78,0x74,0x00,0x6c,0x6f,0x77,0x65,0x72};

inline void ensureSearchPaths() {
	if (!SearchMan.hasArchive("archive_test_cwd"))
		SearchMan.addDirectory("archive_test_cwd", Common::FSNode(Common::Path(".")));
	if (AtlantisTest::haveData() && !SearchMan.hasArchive("archive_test_data"))
		SearchMan.addDirectory("archive_test_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
}

// Writes a synthetic archive to the current directory and mounts it.
inline bool mountSynthetic(CryOmni3D::Atlantis::BigFileArchive &big, const char *name,
                           const byte *data, uint32 size) {
	ensureSearchPaths();
	Common::DumpFile out;
	if (!out.open(Common::Path(name)))
		return false;
	out.write(data, size);
	out.finalize();
	out.close();
	return big.open(Common::Path(name));
}

// Bytes returned by the port for `name`, or "<absent>".
inline Common::String member(CryOmni3D::Atlantis::BigFileArchive &big, const char *name) {
	Common::SeekableReadStream *s = big.createReadStreamForMember(Common::Path(name, '\\'));
	if (!s)
		return "<absent>";
	Common::String r;
	while (true) {
		byte b = s->readByte();
		if (s->eos())
			break;
		r += (char)b;
	}
	delete s;
	return r;
}

} // End of namespace ArchiveBigTest

class ArchiveBigFileTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override {
		SearchMan.remove("archive_test_cwd");
		SearchMan.remove("archive_test_data");
		Common::uninstall_null_g_system();
	}

	// cryo.dll: 'DIR\A.TXT' size 5 "alpha" (first of two entries with that
	// name), 'DIR\B.BIN' size 7 00..06, 'C.DAT' size 0, 'dir\b.bin' found
	// (query upper-cased), 'NOPE' absent.
	void test_synthetic_standard() {
		CryOmni3D::Atlantis::BigFileArchive big;
		TS_ASSERT(ArchiveBigTest::mountSynthetic(big, "archive_synth_standard.big",
		          ArchiveBigTest::k_standard, sizeof(ArchiveBigTest::k_standard)));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "DIR\\A.TXT"), Common::String("alpha"));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "DIR\\B.BIN"), Common::String("\x00\x01\x02\x03\x04\x05\x06", 7));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "C.DAT"), Common::String(""));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "dir\\b.bin"), Common::String("\x00\x01\x02\x03\x04\x05\x06", 7));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "NOPE"), Common::String("<absent>"));
		TS_ASSERT_EQUALS(big.numFiles(), 3u);
	}

	// cryo.dll seeks to header_len + entry.adr (dword 6), so data stored out of
	// directory order is still found: ONE "11111", TWO "22", THREE "333333333".
	// The port assumes members are stored back to back in directory order.
	void test_synthetic_adr_order() {
		CryOmni3D::Atlantis::BigFileArchive big;
		TS_ASSERT(ArchiveBigTest::mountSynthetic(big, "archive_synth_adr.big",
		          ArchiveBigTest::k_adr_order, sizeof(ArchiveBigTest::k_adr_order)));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "ONE"), Common::String("11111"));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "TWO"), Common::String("22"));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "THREE"), Common::String("333333333"));
	}

	// cryo.dll walks the directory until an entry's 'next' dword is 0 and
	// computes the header length (0x20 + sum(namelen + 0x20)); the header's
	// file count (1 here) and header-length field (0 here) are not used.
	// Expected: FIRST "first!", SECOND "2nd".
	void test_synthetic_header_fields() {
		CryOmni3D::Atlantis::BigFileArchive big;
		TS_ASSERT(ArchiveBigTest::mountSynthetic(big, "archive_synth_hdr.big",
		          ArchiveBigTest::k_header_fields, sizeof(ArchiveBigTest::k_header_fields)));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "FIRST"), Common::String("first!"));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "SECOND"), Common::String("2nd"));
	}

	// cryo.dll upper-cases stored names (FUN_00415dac -> FUN_0041d0d9), so a
	// lower-case stored name is found by an upper-case query: "lower".
	void test_synthetic_lowercase_stored_name() {
		CryOmni3D::Atlantis::BigFileArchive big;
		TS_ASSERT(ArchiveBigTest::mountSynthetic(big, "archive_synth_low.big",
		          ArchiveBigTest::k_lowercase_name, sizeof(ArchiveBigTest::k_lowercase_name)));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "DIR\\LOW.TXT"), Common::String("lower"));
		TS_ASSERT_EQUALS(ArchiveBigTest::member(big, "dir\\low.txt"), Common::String("lower"));
	}

	// Every distinct name of each disc, mounted alone (the original mounts one
	// BIGCDn.BIG at a time): size and md5 of the bytes must equal what
	// cryo.dll's _GL_GetFileSize/_GL_ReadFile return.  Also checks that no
	// entry carries the LZW flag 0x1000 (the port has no LZW decoder).
	void test_data_every_member_per_disc() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping BIG data test");
			return;
		}
		ArchiveBigTest::ensureSearchPaths();
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("archive_big.txt", golden));
		for (int d = 1; d <= 4; d++) {
			CryOmni3D::Atlantis::BigFileArchive big;
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
			Common::String prefix = Common::String::format("cd%d|", d);
			uint expected = 0, bad = 0, packed = 0;
			for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
				const Common::String &k = it->_key;
				if (!k.hasPrefix(prefix) || k.contains("|#"))
					continue;
				expected++;
				// key = cdN|NAME|size|flags
				Common::String rest = k.substr(prefix.size());
				uint p2 = rest.findLastOf('|');
				uint p1 = rest.substr(0, p2).findLastOf('|');
				Common::String name = rest.substr(0, p1);
				uint32 size = (uint32)atol(rest.substr(p1 + 1, p2 - p1 - 1).c_str());
				uint32 flags = (uint32)atol(rest.substr(p2 + 1).c_str());
				if (flags & 0x1000)
					packed++;
				Common::SeekableReadStream *s = big.createReadStreamForMember(name);
				if (!s) {
					if (++bad <= 10)
						TS_FAIL(Common::String::format("disc %d: '%s' missing in port", d, name.c_str()).c_str());
					continue;
				}
				Common::String md5 = Common::computeStreamMD5AsString(*s);
				if ((uint32)s->size() != size || md5 != it->_value) {
					if (++bad <= 10)
						TS_FAIL(Common::String::format("disc %d: '%s' port size %u md5 %s, original size %u md5 %s",
						        d, name.c_str(), (uint)s->size(), md5.c_str(), size, it->_value.c_str()).c_str());
				}
				delete s;
			}
			TS_ASSERT_EQUALS(bad, 0u);
			TS_ASSERT_EQUALS(packed, 0u);
			TS_ASSERT(expected > 0);
			TS_ASSERT_EQUALS(big.numFiles(), expected);
		}
	}

	// The engine mounts BIGCD1..4 into one index (first mounted wins).  The
	// original only ever has the inserted disc's BIG open, so a name present on
	// several discs is read from the current disc.  This checks, for every
	// name on disc d, that the merged index returns disc d's bytes.  The menu
	// files (SPRLIST\*MENU.TXT, WAIT, ERROR*) are the exception: FUN_00426f20
	// reads them once, at start-up (call @ 0x00419439), from the boot disc,
	// which is disc 1 when every disc is present, as in the port.
	void test_data_merged_index_vs_per_disc() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping merged BIG test");
			return;
		}
		ArchiveBigTest::ensureSearchPaths();
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("archive_big.txt", golden));
		CryOmni3D::Atlantis::BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		// Only names whose md5 differs between discs can be affected.
		Common::HashMap<Common::String, Common::String> firstMd5;
		Common::HashMap<Common::String, bool> varies;
		for (int d = 1; d <= 4; d++) {
			Common::String prefix = Common::String::format("cd%d|", d);
			for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
				if (!it->_key.hasPrefix(prefix) || it->_key.contains("|#"))
					continue;
				Common::String rest = it->_key.substr(prefix.size());
				Common::String name = rest.substr(0, rest.substr(0, rest.findLastOf('|')).findLastOf('|'));
				if (!firstMd5.contains(name))
					firstMd5[name] = it->_value;
				else if (firstMd5[name] != it->_value)
					varies[name] = true;
			}
		}
		uint bad = 0;
		for (int d = 1; d <= 4; d++) {
			Common::String prefix = Common::String::format("cd%d|", d);
			for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
				if (!it->_key.hasPrefix(prefix) || it->_key.contains("|#"))
					continue;
				Common::String rest = it->_key.substr(prefix.size());
				Common::String name = rest.substr(0, rest.substr(0, rest.findLastOf('|')).findLastOf('|'));
				if (!varies.contains(name))
					continue;
				Common::String upper(name);
				upper.toUppercase();
				const bool bootOnly = upper.hasPrefix("SPRLIST\\") &&
				                      (upper.hasSuffix("MENU.TXT") || upper == "SPRLIST\\WAIT.TXT" ||
				                       upper.hasPrefix("SPRLIST\\ERROR"));
				if (bootOnly && d != 1)
					continue;   // read at start-up from the boot disc only
				Common::SeekableReadStream *s = big.createReadStreamForMember(name);
				Common::String md5 = s ? Common::computeStreamMD5AsString(*s) : Common::String("<absent>");
				delete s;
				if (md5 != it->_value) {
					bad++;
					TS_FAIL(Common::String::format("merged index: '%s' gives md5 %s, disc %d copy is %s",
					        name.c_str(), md5.c_str(), d, it->_value.c_str()).c_str());
				}
			}
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}
};

#endif
