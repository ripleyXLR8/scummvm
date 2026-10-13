// Verifier "video": Atlantis dialog video (UBB2 / HNM5) decoding.
//
// Data test (frames decoded twice, loop mode): every DIALOG\PERSO\*.UBB is decoded with the port's
// Video::HNMDecoder and each frame (640x480 CLUT8) is compared with the
// digest of the frame produced by the ORIGINAL decoder atlantis-cd-en-714240
// FUN_0041101f, emulated unmodified by atlantis-re/tools/emu/video_ubb_codec.py
// (golden file evidence/golden/video_ubb_frames.txt).
//
// The port is checked as dialog.cpp playSingleConLine() builds the decoder
// (HNMDecoder(CLUT8, loop=true) + setCaptureOpcodeStream(true)): the stock
// HNM5 decodeFrame() decodes the IV chunks.

#include "atlantis_test_helper.h"
#include "video_test_big.h"

#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "video/hnm_decoder.h"
#include "../../../system/null_osystem.h"

class VideoHnmTestSuite : public CxxTest::TestSuite {
	// Decodes all frames of one UBB; returns the number of frames whose
	// digest differs from the golden one (or -1 if the file can't be loaded).
	int checkUbb(const VideoTest::Big &big, const Common::String &path,
	             const AtlantisTest::Golden &golden,
	             int &framesChecked, Common::String &firstDiff) {
		uint32 size = 0;
		byte *data = big.get(path, size);
		if (!data)
			return -1;
		uint32 nframes = READ_LE_UINT32(data + 0x10);
		Common::String base = path;
		const char *bs = strrchr(path.c_str(), '\\');
		if (bs)
			base = bs + 1;

		// HNMDecoder::loadStream reads header words +0x18/+0x1a as
		// soundBits/soundFormat; in UBB2 +0x1a is the frame-buffer count (2,
		// read by the original at 0x0043473f), which makes the port add an
		// (empty) DPCM audio track, and adding an audio track needs a mixer
		// that the test OSystem lacks. Clear these two words in the test copy
		// only: they are not used by the video track.
		WRITE_LE_UINT32(data + 0x18, 0);
		Common::SeekableReadStream *s =
		    new Common::MemoryReadStream(data, size, DisposeAfterUse::YES);
		Video::HNMDecoder dec(Graphics::PixelFormat::createFormatCLUT8(), true);
		if (!dec.loadStream(s))
			return -1;
		dec.setCaptureOpcodeStream(true);

		int bad = 0;
		byte *flat = new byte[640 * 480];
		// Two passes: loop=true must wrap to frame 0 after the last frame,
		// like the original (frame counter vs header frame count, seek back
		// to the first superchunk: FUN_0042ee88 at 0x0042f617-0x0042f651). Frame
		// 0 does not depend on the buffers' previous contents (checked by
		// video_ubb_codec.py --fill/--guard), so pass 2 must equal pass 1.
		for (uint32 fi = 0; fi < 2 * nframes; fi++) {
			const Graphics::Surface *f = dec.decodeNextFrame();
			Common::String key = Common::String::format("%s#%u", base.c_str(), fi % nframes);
			if (!f || f->w != 640 || f->h != 480) {
				bad++;
				continue;
			}
			for (int y = 0; y < 480; y++)
				memcpy(flat + y * 640, f->getBasePtr(0, y), 640);
			Common::String got = AtlantisTest::md5Hex(flat, 640 * 480);
			framesChecked++;
			if (!golden.contains(key) || golden[key] != got) {
				if (firstDiff.empty())
					firstDiff = key;
				bad++;
			}
		}
		delete[] flat;
		return bad;
	}

	void runAll() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: UBB frame test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		if (!AtlantisTest::loadGolden("video_ubb_frames.txt", golden)) {
			TS_WARN("video_ubb_frames.txt missing: UBB frame test skipped");
			return;
		}
		VideoTest::Big big;
		Common::Array<Common::String> ubbs = big.names(".UBB");
		TS_ASSERT_EQUALS(ubbs.size(), 124u);
		int files = 0, filesBad = 0, framesBad = 0, framesChecked = 0;
		Common::String report;
		for (uint i = 0; i < ubbs.size(); i++) {
			Common::String firstDiff;
			int bad = checkUbb(big, ubbs[i], golden, framesChecked, firstDiff);
			files++;
			if (bad != 0) {
				filesBad++;
				framesBad += bad < 0 ? 1 : bad;
				if (report.size() < 600)
					report += Common::String::format(" %s(%d bad, first %s)",
					                                 ubbs[i].c_str(), bad, firstDiff.c_str());
			}
		}
		TS_WARN(Common::String::format("%s: %d files, %d frames checked, %d files / %d frames differ from FUN_0041101f.%s",
		        "engine path", files, framesChecked,
		        filesBad, framesBad, report.c_str()).c_str());
		TS_ASSERT_EQUALS(framesBad, 0);
	}

public:
	// FSNode (used by the shared helper) needs g_system.
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_ubb_frames_engine_path() { runAll(); }
};
