#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_WAM_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_ARCHIVE_WAM_H

// Verifier "archive": AtlantisWAMParser / AtlantisPlace::hitTest
// (engines/cryomni3d/atlantis/wam_parser.cpp) against atlantis.exe's WAM loader
// FUN_00421ebc and zone hit-test FUN_00422bb0 (atlantis-cd-en-714240).
//
// Self-contained tests: synthetic WAMs / zones (atlantis-re/tools/emu/
// archive_wam_synth.py, archive_hit_synth.py) with the results the original
// code gives when run under Unicorn.
// Data tests: every .WAM of BIGCD1..4 against evidence/golden/archive_wam.txt
// (atlantis-re/tools/emu/archive_wam_emu.py).  See atlantis-re/audit/archive/REPORT.md §2.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/wam_parser.h"

namespace ArchiveWamTest {

using CryOmni3D::Atlantis::AtlantisPlace;
using CryOmni3D::Atlantis::AtlantisWAMParser;

// Synthetic WAMs (not game data) and the canonical text of the tables
// atlantis.exe's FUN_00421ebc builds from them.
static const byte k_wam_standard[] = {0x73,0x79,0x6e,0x74,0x68,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x61,0x74,0x6c,0x31,0x36,0x30,0x30,0x31,0x2e,0x68,0x6e,0x6d,0x00,0x01,0x00,0x00,0x02,0x61,0x74,0x30,0x30,0x31,0x30,0x30,0x32,0x2e,0x75,0x62,0x62,0x00,0x02,0x00,0x01,0x00,0x00,0xc0,0x3f,0x00,0x00,0x80,0xbe,0x00,0x00,0x40,0x40,0x00,0x00,0x00,0x3e,0x61,0x74,0x30,0x30,0x31,0x30,0x30,0x33,0x2e,0x75,0x62,0x62,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc8,0x40,0x00,0x00,0x80,0xbf,0x02,0x64,0x00,0x78,0x00,0x32,0x00,0x3c,0x00,0x02,0x00,0xc6,0x07,0xc8,0x00,0x39,0x00,0x2c,0x01,0x03,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x74,0x6c,0x31,0x36,0x30,0x30,0x32,0x2e,0x68,0x6e,0x6d,0x00,0x02,0x00,0x00,0x01,0x61,0x74,0x30,0x30,0x32,0x30,0x30,0x31,0x2e,0x75,0x62,0x62,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x3f,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x01,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x61,0x74,0x6c,0x31,0x36,0x30,0x30,0x33,0x2e,0x68,0x6e,0x6d,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x28,0x00,0x6f,0x74,0x68,0x65,0x72,0x2e,0x77,0x61,0x6d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x01,0x73,0x70,0x77,0x73,0x79,0x6e,0x2e,0x74,0x78,0x74,0x00,0x00,0x00,0x02,0x73,0x70,0x66,0x73,0x79,0x6e,0x2e,0x74,0x78,0x74,0x00,0x00,0x00,0x03,0x73,0x79,0x6e,0x63,0x65,0x6e,0x74,0x2e,0x76,0x75,0x65,0x00,0x00,0x02,0x01,0x00,0x59,0x02,0x00,0x00,0x61,0x30,0x30,0x31,0x71,0x30,0x30,0x31,0x2e,0x73,0x70,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x09,0x03,0x00,0x00,0x62,0x30,0x30,0x32,0x78,0x30,0x30,0x32,0x2e,0x73,0x70,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const char k_canon_standard[] =
	"P 1 atl16001.hnm 2 2\n"
	"P 2 atl16002.hnm 1 1\n"
	"P 3 atl16003.hnm 0 0\n"
	"T 1 at001002.ubb 2 1 3fc00000 be800000 40400000 3e000000\n"
	"T 1 at001003.ubb 3 0 00000000 00000000 40c80000 bf800000\n"
	"T 2 at002001.ubb 1 1 3f000000 00000000 3f400000 00000000\n"
	"Z 1 100 120 50 60 2\n"
	"Z 1 1990 200 57 300 3\n"
	"Z 2 0 1 -1 -1 0\n"
	"S 2 40 other.wam\n"
	"R 1 spwsyn.txt\n"
	"R 2 spfsyn.txt\n"
	"R 3 syncent.vue\n"
	"N 601 a001q001.spw\n"
	"N 777 b002x002.spw\n"
;
static const byte k_wam_leading_nul[] = {0x73,0x79,0x6e,0x74,0x68,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x61,0x74,0x6c,0x31,0x36,0x30,0x31,0x30,0x2e,0x68,0x6e,0x6d,0x00,0x0a,0x00,0x00,0x00,0x01,0x05,0x00,0x05,0x00,0x0a,0x00,0x0a,0x00,0x0b,0x00,0x00,0x00,0x74,0x6c,0x31,0x36,0x30,0x31,0x31,0x2e,0x68,0x6e,0x6d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x73,0x70,0x77,0x78,0x2e,0x74,0x78,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const char k_canon_leading_nul[] =
	"P 10 atl16010.hnm 0 1\n"
	"P 0  0 0\n"
	"Z 10 5 5 10 10 11\n"
	"R 1 spwx.txt\n"
;

inline uint32 floatBits(float f) {
	uint32 u;
	memcpy(&u, &f, 4);
	return u;
}

// Canonical text, mirrored from archive_wam_emu.py canon_*().
inline Common::String canonPlaces(const AtlantisWAMParser &w) {
	Common::String s;
	for (const AtlantisPlace &p : w.getPlaces())
		s += Common::String::format("P %u %s %u %u\n", (uint)p.placeId, p.cycloHnm.c_str(),
		                            (uint)p.transitions.size(), (uint)p.zones.size());
	return s;
}

inline Common::String canonTransitions(const AtlantisWAMParser &w) {
	Common::String s;
	for (const AtlantisPlace &p : w.getPlaces())
		for (const CryOmni3D::Atlantis::AtlantisTransition &t : p.transitions)
			s += Common::String::format("T %u %s %u %u %08x %08x %08x %08x\n", (uint)p.placeId,
			                            t.videoName.c_str(), (uint)t.dstPlaceId, (uint)t.flags,
			                            floatBits(t.srcAlpha), floatBits(t.srcBeta),
			                            floatBits(t.dstAlpha), floatBits(t.dstBeta));
	return s;
}

inline Common::String canonZones(const AtlantisWAMParser &w) {
	Common::String s;
	for (const AtlantisPlace &p : w.getPlaces())
		for (const CryOmni3D::Atlantis::AtlantisZone &z : p.zones)
			s += Common::String::format("Z %u %d %d %d %d %d\n", (uint)p.placeId,
			                            z.x, z.y, z.w, z.h, z.action);
	return s;
}

inline Common::String canonSubwams(const AtlantisWAMParser &w) {
	Common::String s;
	for (const CryOmni3D::Atlantis::AtlantisSubWAM &sw : w.getSubWAMs())
		s += Common::String::format("S %u %u %s\n", (uint)sw.srcPlaceId, (uint)sw.dstPlaceId, sw.wamName.c_str());
	return s;
}

inline Common::String canonRefs(const AtlantisWAMParser &w) {
	Common::String s;
	if (!w.spwListFile.empty())
		s += "R 1 " + w.spwListFile + "\n";
	if (!w.spfListFile.empty())
		s += "R 2 " + w.spfListFile + "\n";
	if (!w.centVueFile.empty())
		s += "R 3 " + w.centVueFile + "\n";
	return s;
}

inline Common::String canonNpcs(const AtlantisWAMParser &w) {
	Common::String s;
	for (const CryOmni3D::Atlantis::AtlantisNPCEntry &n : w.getNPCEntries())
		s += Common::String::format("N %u %s\n", (uint)n.persoId, n.spriteFile.c_str());
	return s;
}

inline Common::String canonAll(const AtlantisWAMParser &w) {
	return canonPlaces(w) + canonTransitions(w) + canonZones(w) + canonSubwams(w) + canonRefs(w) + canonNpcs(w);
}

inline Common::String md5Of(const Common::String &s) {
	return AtlantisTest::md5Hex((const byte *)s.c_str(), s.size());
}

// Points of archive_wam_emu.py hit_points(): a 16-px grid over the 2048x768
// panorama, then each zone's edges and the pixels just outside, clipped.
inline void appendHits(const AtlantisPlace &p, Common::Array<byte> &out) {
	Common::Array<Common::Point> pts;
	for (int y = 0; y < 768; y += 16)
		for (int x = 0; x < 2048; x += 16)
			pts.push_back(Common::Point(x, y));
	for (const CryOmni3D::Atlantis::AtlantisZone &z : p.zones) {
		const int xs[4] = { z.x - 1, z.x, z.x + z.w, z.x + z.w + 1 };
		const int ys[4] = { z.y - 1, z.y, z.y + z.h, z.y + z.h + 1 };
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				if (xs[i] >= 0 && xs[i] < 2048 && ys[j] >= 0 && ys[j] < 768)
					pts.push_back(Common::Point(xs[i], ys[j]));
	}
	for (const Common::Point &pt : pts) {
		uint16 r = (uint16)p.hitTest(pt.x, pt.y);
		out.push_back(r & 0xff);
		out.push_back(r >> 8);
	}
}

inline void parse(AtlantisWAMParser &w, const byte *data, uint32 size) {
	Common::MemoryReadStream s(data, size);
	w.loadStream(s);
}

} // End of namespace ArchiveWamTest

class ArchiveWamTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override {
		SearchMan.remove("archive_test_cwd");
		SearchMan.remove("archive_test_data");
		Common::uninstall_null_g_system();
	}

	// Places, transitions (float bit patterns), zones, sub-WAM slots, file refs
	// and NPC entries of a synthetic WAM, as built by FUN_00421ebc.
	void test_synthetic_parse_standard() {
		CryOmni3D::Atlantis::AtlantisWAMParser w;
		ArchiveWamTest::parse(w, ArchiveWamTest::k_wam_standard, sizeof(ArchiveWamTest::k_wam_standard));
		TS_ASSERT_EQUALS(ArchiveWamTest::canonAll(w), Common::String(ArchiveWamTest::k_canon_standard));
	}

	// A place whose 13-byte name slot starts with NUL: the original keeps an
	// empty name ("P 0  0 0"), and so does the port since ScummVM 93229c165cc
	// (readFixedName13 copies up to the first NUL, as FUN_00421ebc).
	void test_synthetic_parse_leading_nul_name() {
		CryOmni3D::Atlantis::AtlantisWAMParser w;
		ArchiveWamTest::parse(w, ArchiveWamTest::k_wam_leading_nul, sizeof(ArchiveWamTest::k_wam_leading_nul));
		TS_ASSERT_EQUALS(ArchiveWamTest::canonAll(w), Common::String(ArchiveWamTest::k_canon_leading_nul));
	}

	// FUN_00422bb0 on zones {100,100,50,50,5} {120,120,50,50,3} {300,300,20,20,2}
	// {310,310,20,20,9} {-1,400,50,50,7} {0,1,-1,-1,0}: bounds are inclusive
	// (x <= px <= x+w), the result is the largest action among all zones that
	// contain the point, and coordinates are compared unsigned (a zone with
	// x = -1 never matches).
	void test_synthetic_hittest() {
		CryOmni3D::Atlantis::AtlantisPlace p;
		const int16 zones[6][5] = { { 100, 100, 50, 50, 5 }, { 120, 120, 50, 50, 3 }, { 300, 300, 20, 20, 2 },
		                            { 310, 310, 20, 20, 9 }, { -1, 400, 50, 50, 7 }, { 0, 1, -1, -1, 0 } };
		for (int i = 0; i < 6; i++) {
			CryOmni3D::Atlantis::AtlantisZone z;
			z.x = zones[i][0]; z.y = zones[i][1]; z.w = zones[i][2]; z.h = zones[i][3]; z.action = zones[i][4];
			p.zones.push_back(z);
		}
		static const int expected[][3] = {
			{ 150, 100, 5 },
			{ 149, 149, 5 },
			{ 150, 150, 5 },
			{ 99, 100, 0 },
			{ 100, 151, 0 },
			{ 130, 130, 5 },
			{ 165, 165, 3 },
			{ 170, 170, 3 },
			{ 315, 315, 9 },
			{ 305, 305, 2 },
			{ 325, 325, 9 },
			{ 0, 410, 0 },
			{ 10, 410, 0 },
			{ 48, 449, 0 },
			{ 500, 500, 0 },
			{ 0, 0, 0 },
			{ 2047, 767, 0 },
		};
		for (uint i = 0; i < ARRAYSIZE(expected); i++)
			TSM_ASSERT_EQUALS(Common::String::format("point (%d,%d)", expected[i][0], expected[i][1]).c_str(),
			                  (int)p.hitTest(expected[i][0], expected[i][1]), expected[i][2]);
	}

	// The places' ambient sounds (task 20): the objects, the per-place
	// references, the file reference 2 and the centre .vue tables (FUN_004315b4)
	// of every WAM that has them, against archive_wam_ambient.txt
	// (atlantis-re tools/emu/archive_wam_ambient.py --golden).
	void test_data_ambient_sounds() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping the ambient-sound data test");
			return;
		}
		if (!SearchMan.hasArchive("archive_test_data"))
			SearchMan.addDirectory("archive_test_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		Common::File gf;
		TS_ASSERT(gf.open(Common::FSNode(Common::Path(AtlantisTest::goldenDir(), '/').appendComponent("archive_wam_ambient.txt"))));
		CryOmni3D::Atlantis::BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		Common::HashMap<Common::String, CryOmni3D::Atlantis::AtlantisWAMParser *> wams;
		auto wam = [&](const Common::String &path) -> CryOmni3D::Atlantis::AtlantisWAMParser * {
			if (!wams.contains(path)) {
				Common::SeekableReadStream *st = big.createReadStreamForMember(path);
				CryOmni3D::Atlantis::AtlantisWAMParser *w = new CryOmni3D::Atlantis::AtlantisWAMParser();
				if (st)
					w->loadStream(*st);
				delete st;
				wams[path] = w;
			}
			return wams[path];
		};
		auto g = [](float v) { return Common::String::format("%.9g", (double)v); };
		uint lines = 0, bad = 0;
		while (!gf.eos()) {
			Common::String line = gf.readLine();
			if (line.empty() || line[0] == '#')
				continue;
			Common::Array<Common::String> tok;
			Common::String cur;
			for (uint k = 0; k <= line.size(); k++) {
				if (k == line.size() || line[k] == ' ') {
					if (!cur.empty())
						tok.push_back(cur);
					cur.clear();
				} else {
					cur += line[k];
				}
			}
			lines++;
			Common::String got;
			if (tok[0] == "fileref2") {
				const Common::Array<Common::String> &refs = wam(tok[1])->getFileRefs();
				got = refs.size() > 2 ? refs[2] : "-";
				if (got != tok[2])
					bad++;
			} else if (tok[0] == "obj") {
				const Common::Array<CryOmni3D::Atlantis::AtlantisAmbientObject> &objs = wam(tok[1])->getAmbientObjects();
				bool found = false;
				for (const CryOmni3D::Atlantis::AtlantisAmbientObject &o : objs) {
					if (Common::String::format("%u", o.id) != tok[2])
						continue;
					found = true;
					Common::String v;
					for (int k = 0; k < 4; k++)
						v += (k ? "," : "") + (o.variants[k].empty() ? Common::String("-") : o.variants[k]);
					got = Common::String::format("%s %s %s %s %s %u %u %u %u %s", o.name.empty() ? "-" : o.name.c_str(),
					      g(o.pos[0]).c_str(), g(o.pos[1]).c_str(), g(o.pos[2]).c_str(), g(o.loudness).c_str(),
					      o.weights[0], o.weights[1], o.weights[2], o.weights[3], v.c_str());
				}
				Common::String want;
				for (uint k = 3; k < tok.size(); k++)
					want += (k > 3 ? " " : "") + tok[k];
				if (!found || got != want) {
					bad++;
					TS_WARN(("ambient object differs: " + line + " / port: " + got).c_str());
				}
			} else if (tok[0] == "ref") {
				const CryOmni3D::Atlantis::AtlantisPlace *pl = wam(tok[1])->findPlaceById((uint16)atoi(tok[2].c_str()));
				const uint k = (uint)atoi(tok[3].c_str());
				if (!pl || k >= pl->ambientRefs.size()) {
					bad++;
					continue;
				}
				const CryOmni3D::Atlantis::AtlantisAmbientRef &r = pl->ambientRefs[k];
				got = Common::String::format("%u %s %s %s", r.objectId, g(r.t).c_str(), g(r.m).c_str(), g(r.c).c_str());
				if (got != tok[4] + " " + tok[5] + " " + tok[6] + " " + tok[7]) {
					bad++;
					TS_WARN(("ambient reference differs: " + line + " / port: " + got).c_str());
				}
			} else if (tok[0] == "centre") {
				Common::SeekableReadStream *st = big.createReadStreamForMember("SPRLIST\\" + tok[1]);
				TS_ASSERT(st);
				if (!st)
					continue;
				Common::Array<byte> buf(st->size());
				st->read(buf.data(), buf.size());
				delete st;
				Common::HashMap<int32, CryOmni3D::Atlantis::AtlantisCentrePos> table;
				CryOmni3D::Atlantis::parseCentreVue(buf.data(), buf.size(), table);
				const int32 n = atoi(tok[2].c_str());
				if (!table.contains(n)) {
					bad++;
					continue;
				}
				const CryOmni3D::Atlantis::AtlantisCentrePos &c = table[n];
				got = g(c.x) + " " + g(c.y) + " " + g(c.z);
				if (got != tok[3] + " " + tok[4] + " " + tok[5]) {
					bad++;
					TS_WARN(("centre entry differs: " + line + " / port: " + got).c_str());
				}
			}
		}
		for (Common::HashMap<Common::String, CryOmni3D::Atlantis::AtlantisWAMParser *>::iterator it = wams.begin(); it != wams.end(); ++it)
			delete it->_value;
		TS_ASSERT(lines > 200);
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Every .WAM of the four discs: each section of the parsed tables and the
	// hit-test results over the point grid must match the original's.
	void test_data_all_wams() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("archive: no game data configured, skipping WAM data test");
			return;
		}
		if (!SearchMan.hasArchive("archive_test_data"))
			SearchMan.addDirectory("archive_test_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("archive_wam.txt", golden));
		CryOmni3D::Atlantis::BigFileArchive big;
		for (int d = 1; d <= 4; d++)
			TS_ASSERT(big.open(Common::Path(Common::String::format("BIGCD%d.BIG", d))));
		uint wams = 0;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			if (!it->_key.hasSuffix("|places"))
				continue;
			Common::String name = it->_key.substr(0, it->_key.size() - 7);
			wams++;
			Common::SeekableReadStream *s = big.createReadStreamForMember("WAM\\" + name);
			TS_ASSERT(s);
			if (!s)
				continue;
			CryOmni3D::Atlantis::AtlantisWAMParser w;
			w.loadStream(*s);
			delete s;
			struct { const char *sec; Common::String text; } secs[] = {
				{ "places", ArchiveWamTest::canonPlaces(w) },
				{ "transitions", ArchiveWamTest::canonTransitions(w) },
				{ "zones", ArchiveWamTest::canonZones(w) },
				{ "subwams", ArchiveWamTest::canonSubwams(w) },
				{ "refs", ArchiveWamTest::canonRefs(w) },
				{ "npcs", ArchiveWamTest::canonNpcs(w) },
			};
			for (uint i = 0; i < ARRAYSIZE(secs); i++) {
				Common::String key = name + "|" + secs[i].sec;
				if (ArchiveWamTest::md5Of(secs[i].text) != golden[key])
					TS_FAIL(Common::String::format("%s: section '%s' differs from the original", name.c_str(), secs[i].sec).c_str());
			}
			// The port ignores the place 'type' byte (0 cyclo, 1 ubb\vue, 2 images):
			// valid only while every place has type 0.
			TSM_ASSERT_EQUALS((name + ": places with non-zero type").c_str(),
			                  golden[name + "|nonzero_type_places"], Common::String("0"));
			// The ambient-sound references and objects (task 20); their
			// content is checked by test_data_ambient_sounds.
			uint ambientRefs = 0;
			for (const CryOmni3D::Atlantis::AtlantisPlace &p : w.getPlaces())
				ambientRefs += p.ambientRefs.size();
			TSM_ASSERT_EQUALS((name + ": ambient references").c_str(),
			                  Common::String::format("%u", ambientRefs), golden[name + "|ambient_records"]);
			TSM_ASSERT_EQUALS((name + ": ambient objects").c_str(),
			                  Common::String::format("%u", w.getAmbientObjects().size()), golden[name + "|objects3d"]);
			Common::Array<byte> hits;
			for (const CryOmni3D::Atlantis::AtlantisPlace &p : w.getPlaces())
				ArchiveWamTest::appendHits(p, hits);
			TSM_ASSERT_EQUALS((name + ": hit-test point count").c_str(),
			                  Common::String::format("%u", hits.size() / 2), golden[name + "|hitcount"]);
			if (AtlantisTest::md5Hex(hits.data(), hits.size()) != golden[name + "|hit"])
				TS_FAIL(Common::String::format("%s: hit-test results differ from FUN_00422bb0", name.c_str()).c_str());
		}
		TS_ASSERT_EQUALS(wams, 38u);
	}
};

#endif
