// Verifier "video": the 3D talking head of the dialogs (the animated mouth).
//
// Original: atlantis-cd-en-714240 FUN_0042ee88 step 4 and its per-frame
// calls FUN_0044dd10, FUN_0044e860 and FUN_0044c9c0, run unmodified under
// emulation by atlantis-re/tools/emu/video_mouth.py; frames traced by
// mouth_trace.py and digested by mouth_golden.py into video_mouth.txt.
//
// Port: DialogHead (head3d.h), called directly.

#include "atlantis_test_helper.h"
#include "video_test_big.h"
#include "../../../system/null_osystem.h"

#include "cryomni3d/atlantis/head3d.h"

namespace VideoMouth {

class BigSource : public CryOmni3D::Atlantis::HeadFileSource {
public:
	explicit BigSource(const VideoTest::Big &big) : _big(big) {}
	bool readFile(const Common::String &name, Common::Array<byte> &out) override {
		uint32 size = 0;
		byte *buf = _big.get(name, size);
		if (!buf)
			return false;
		out.resize(size);
		memcpy(out.data(), buf, size);
		delete[] buf;
		return true;
	}
private:
	const VideoTest::Big &_big;
};

// The traced heads (INITDIAL first sub-record, ATA0001A.SYC) and frames
// (UBB frame index, elapsed ms) of mouth_trace.py.
static const int kHeads[][2] = {
	{ 539, 2 }, { 543, 2 }, { 551, 3 }, { 560, 4 },   // group type 2
	{ 512, 2 }, { 567, 3 }, { 650, 5 },               // group type 9
	{ 552, 3 },                                       // types 2 + 1
	{ 525, 2 },                                       // bottom clipping
};
static const int kFrames[][2] = { { 0, 0 }, { 3, 120 }, { 12, 500 }, { 25, 1000 }, { 40, 1600 }, { 60, 2400 } };

// INITDIAL.TXT: persoId unk 3dc A.3da M.3da s3d cam2..cam5 ...
static bool initdialNames(const VideoTest::Big &big, int perso, Common::String names[4]) {
	uint32 size = 0;
	byte *buf = big.get("DIALOG\\INITDIAL.TXT", size);
	if (!buf)
		return false;
	Common::String text((const char *)buf, size);
	delete[] buf;
	const Common::String id = Common::String::format("%d", perso);
	uint pos = 0;
	while (pos < text.size()) {
		uint end = pos;
		while (end < text.size() && text[end] != '\n')
			end++;
		Common::Array<Common::String> tok;
		Common::String cur;
		for (uint k = pos; k <= end; k++) {
			const char c = k < end ? text[k] : ' ';
			if (c == ' ' || c == '\t' || c == '\r') {
				if (!cur.empty())
					tok.push_back(cur);
				cur.clear();
			} else {
				cur += c;
			}
		}
		if (tok.size() >= 6 && tok[0] == id) {
			names[0] = tok[5];   // s3d
			names[1] = tok[2];   // 3dc
			names[2] = tok[3];   // A.3da
			names[3] = tok[4];   // M.3da
			return true;
		}
		pos = end + 1;
	}
	return false;
}

static Common::String vertsMd5(const Common::Array<CryOmni3D::Atlantis::DialogHead::ProjectedVertex> &pv) {
	Common::String txt;
	for (uint i = 0; i < pv.size(); i++)
		if (pv[i].flags & 0x40)
			txt += Common::String::format("%u %d %d\n", i, pv[i].x, pv[i].y);
	return AtlantisTest::md5Hex((const byte *)txt.c_str(), txt.size());
}

} // End of namespace VideoMouth

class VideoMouthTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Lip-sync, head animation, camera, object walk and projection: the
	// projected visible vertices of every traced frame, and the value
	// FUN_0044e860 returned.
	void test_projection_vs_original() {
		using namespace VideoMouth;
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: dialog head test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		if (!AtlantisTest::loadGolden("video_mouth.txt", golden)) {
			TS_FAIL("video_mouth.txt missing from the configured golden directory");
			return;
		}
		VideoTest::Big big;
		BigSource src(big);
		for (uint h = 0; h < ARRAYSIZE(kHeads); h++) {
			Common::String names[4];
			TS_ASSERT(initdialNames(big, kHeads[h][0], names));
			CryOmni3D::Atlantis::DialogHead head;
			TS_ASSERT(head.load(src, names[0], names[1], names[2], names[3], kHeads[h][1], "ATA0001A.SYC"));
			for (uint f = 0; f < ARRAYSIZE(kFrames); f++) {
				head.setFrame(kFrames[f][0]);
				const int r = head.lipsync(kFrames[f][1]);
				head.transform();
				const Common::String key = Common::String::format("%d %d %d %d", kHeads[h][0], kHeads[h][1], kFrames[f][0], kFrames[f][1]);
				TS_ASSERT_EQUALS(Common::String::format("%d", r), golden[key + " lipsync"]);
				TSM_ASSERT_EQUALS(key.c_str(), vertsMd5(head.projected()), golden[key + " verts"]);
			}
		}
	}

	// The rasteriser (set-ups FUN_00456d88 / FUN_004582f8 / FUN_00455cc0,
	// FUN_004567b4, FUN_0045c104, FUN_0045baa4, the fills of 0x004bad3c):
	// the head drawn into a zeroed 640x480 buffer, every traced frame.
	void test_pixels_vs_original() {
		using namespace VideoMouth;
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: dialog head test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		if (!AtlantisTest::loadGolden("video_mouth.txt", golden)) {
			TS_FAIL("video_mouth.txt missing from the configured golden directory");
			return;
		}
		VideoTest::Big big;
		BigSource src(big);
		Common::Array<uint16> buf(640 * 480);
		Common::Array<byte> le(640 * 480 * 2);
		for (uint h = 0; h < ARRAYSIZE(kHeads); h++) {
			Common::String names[4];
			TS_ASSERT(initdialNames(big, kHeads[h][0], names));
			CryOmni3D::Atlantis::DialogHead head;
			TS_ASSERT(head.load(src, names[0], names[1], names[2], names[3], kHeads[h][1], "ATA0001A.SYC"));
			for (uint f = 0; f < ARRAYSIZE(kFrames); f++) {
				head.setFrame(kFrames[f][0]);
				head.lipsync(kFrames[f][1]);
				head.transform();
				memset(buf.data(), 0, buf.size() * 2);
				head.render(buf.data(), 640);
				for (uint i = 0; i < buf.size(); i++)
					WRITE_LE_UINT16(&le[2 * i], buf[i]);
				const Common::String key = Common::String::format("%d %d %d %d", kHeads[h][0], kHeads[h][1], kFrames[f][0], kFrames[f][1]);
				TSM_ASSERT_EQUALS(key.c_str(), AtlantisTest::md5Hex(le.data(), le.size()), golden[key + " pixels"]);
			}
		}
	}
};
