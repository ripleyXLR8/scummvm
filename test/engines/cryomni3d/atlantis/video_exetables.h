#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_VIDEO_EXETABLES_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_VIDEO_EXETABLES_H

// Verifier "video", element 4: tables read from atlantis.exe by the port
// (engines/cryomni3d/atlantis/exetables.cpp loadAtlantisExeTables) against the
// code of atlantis-cd-en-714240 that consumes each table:
//   0x004961e0 file-name pointer table; NEWSOUND=N reads entry
//              [0x00496224 + 4*N] = entry 17+N (FUN_00432a64 @ 0x00432bea),
//              N = 0 plays nothing (test at 0x00432a9c)
//   0x004967b0 inventory object ids, scanned until -1 (FUN_0042b328,
//              FUN_0042a100 @ 0x0042b201); 0 entries are holes, not the end
//   0x004966fc credits char widths, indexed by the raw byte; a space advances
//              6 px, the table is not read for it (FUN_0042073c @ 0x00420774,
//              FUN_0041a72c @ 0x0041ab0d)
//   0x00496080 per-camera FOV floats, read as [0x00496078 + 4*cam], cam 2..5
//              (FUN_0042844c @ 0x00428a0e / 0x00428ad4)
// The expected values are read from atlantis.exe in the test itself (file
// offset = VA - 0x400000 - 0x90000 + 0x71200, DGROUP), following the
// ORIGINAL consumer's rules; the port side is the REAL loadAtlantisExeTables /
// creditCharAdvance, called on zero-filled engine storage (see video_dialog.h).

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/file.h"
#include "common/fs.h"

#ifndef VIDEO_ENGINE_PEEK
#define VIDEO_ENGINE_PEEK
#define private public
#define protected public
#include "cryomni3d/atlantis/engine.h"
#undef private
#undef protected

#include <new>

namespace VideoPeek {

typedef CryOmni3D::Atlantis::CryOmni3DEngine_Atlantis Eng;

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

namespace VideoExeTest {

inline uint32 fileOff(uint32 va) { return va - 0x400000 - 0x90000 + 0x71200; }

struct Exe {
	Common::File f;
	bool open() {
		return f.open(Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')
		                             .appendComponent("atlantis.exe")));
	}
	uint32 u32(uint32 va) { f.seek(fileOff(va)); return f.readUint32LE(); }
	byte u8(uint32 va) { f.seek(fileOff(va)); return f.readByte(); }
	Common::String cstr(uint32 va) {
		f.seek(fileOff(va));
		Common::String s;
		for (byte b = f.readByte(); b && !f.eos(); b = f.readByte())
			s += (char)b;
		return s;
	}
};

inline VideoPeek::Eng *loadedEngine() {
	VideoPeek::addDataDirToSearchMan();
	VideoPeek::Eng *e = VideoPeek::rawEngine();
	e->loadAtlantisExeTables();
	return e;
}

} // End of namespace VideoExeTest

class VideoExeTablesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// NEWSOUND=N: original file name = *(char **)(0x00496224 + 4*N) for
	// N >= 1 until the "FIN" entry; port = _newSoundApcNames[N].
	void test_newsound_names() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		VideoExeTest::Exe exe;
		TS_ASSERT(exe.open());
		VideoPeek::Eng *e = VideoExeTest::loadedEngine();
		uint n = 0;
		while (!exe.cstr(exe.u32(0x00496224 + 4 * n)).equalsIgnoreCase("FIN"))
			n++;
		TS_ASSERT_EQUALS(n, 137u);
		TS_ASSERT_EQUALS(e->_newSoundApcNames.size(), n);
		int diffs = 0;
		for (uint i = 1; i < n && i < e->_newSoundApcNames.size(); i++)
			if (e->_newSoundApcNames[i] != exe.cstr(exe.u32(0x00496224 + 4 * i)))
				diffs++;
		TS_ASSERT_EQUALS(diffs, 0);
	}

	// Inventory ids: the original scans until -1; zeros inside are holes.
	void test_inventory_ids() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		VideoExeTest::Exe exe;
		TS_ASSERT(exe.open());
		VideoPeek::Eng *e = VideoExeTest::loadedEngine();
		Common::Array<int32> orig;
		for (uint i = 0; i < 256; i++) {
			int32 v = (int32)exe.u32(0x004967b0 + 4 * i);
			if (v == -1)
				break;
			orig.push_back(v);
		}
		TS_ASSERT_EQUALS(orig.size(), 51u);
		TS_ASSERT_EQUALS(e->_inventoryObjectIds.size(), orig.size());
		uint common = MIN<uint>(orig.size(), e->_inventoryObjectIds.size());
		for (uint i = 0; i < common; i++)
			TS_ASSERT_EQUALS((int32)e->_inventoryObjectIds[i], orig[i]);
		for (uint i = common; i < orig.size(); i++)
			TS_WARN(Common::String::format("slot %u: object id %d missing in the port",
			                               i, orig[i]).c_str());
	}

	// Credits widths: same 256 bytes; advance of a space is 6 in the original.
	void test_credit_widths() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		VideoExeTest::Exe exe;
		TS_ASSERT(exe.open());
		VideoPeek::Eng *e = VideoExeTest::loadedEngine();
		int diffs = 0;
		for (uint c = 0; c < 256; c++)
			if (c != ' ' && e->_creditCharWidths[c] != exe.u8(0x004966fc + c))
				diffs++;
		TS_ASSERT_EQUALS(diffs, 0);
		TS_ASSERT_EQUALS(e->creditCharAdvance('A'), (int)exe.u8(0x004966fc + 'A'));
		TS_ASSERT_EQUALS(e->creditCharAdvance(' '), 6);
	}

	// Per-camera FOV: the port's kDialogCamFovDeg (atlantis/omni3d.h).
	void test_camera_fov_table() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("no game data configured; skipping");
			return;
		}
		VideoExeTest::Exe exe;
		TS_ASSERT(exe.open());
		for (int cam = 2; cam <= 5; cam++) {
			uint32 bits = exe.u32(0x00496078 + 4 * cam);
			float v;
			memcpy(&v, &bits, 4);
			TS_ASSERT_EQUALS(v, CryOmni3D::Atlantis::kDialogCamFovDeg[cam - 2]);
		}
	}
};

#endif
