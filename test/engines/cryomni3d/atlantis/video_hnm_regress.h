// Verifier "video": shared HNM decoder changes (video/hnm_decoder.cpp,
// commits 51bdcbcc84b "Detect and enable HNM6 warp mode" and 7228a61e156).
//
//  - test_versailles_hnm4_unchanged: every HNM4 file of the Versailles DOS
//    release is decoded by the current Video::HNMDecoder and by a verbatim
//    copy of the decoder before the Atlantis changes (video_upstream_hnm.h);
//    every frame and palette must be identical, in normal and in loop mode.
//  - test_ubb_vue_ix_vs_original (replaces test_atlantis_ix_unchanged, which
//    pinned the IX frames to the upstream rounding): the first frames of every
//    Atlantis UBB_VUE\*.HNM against the original video decoder FUN_004506ec.
//  - test_cyclo_warp_vs_original: every CYCLO\*.HNM decoded by the current
//    HNMDecoder (the way loadCycloHNM() does it) against the panorama decoded
//    by the original warp decoder FUN_0045084c (golden
//    evidence/golden/video_warp_frames.txt, tools/emu/video_warp.py).
//
// The Versailles data directory is read from "versailles=" in
// atlantis_test.cfg. Audio header fields are cleared in the test copies (both
// decoders identically) because the test OSystem has no mixer.

#include "atlantis_test_helper.h"
#include "video_test_big.h"
#include "video_upstream_hnm.h"
#include "../../../system/null_osystem.h"

#include "common/fs.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "video/hnm_decoder.h"

class VideoHnmRegressTestSuite : public CxxTest::TestSuite {
	// Number of superchunks before the zero terminator / end of file, and
	// whether a terminator is present (port-independent walk).
	static uint32 countSuperchunks(const byte *d, uint32 size, bool &terminated) {
		uint32 p = 64, n = 0;
		terminated = false;
		while (p + 4 <= size) {
			uint32 h = READ_LE_UINT32(d + p);
			if (!h) {
				terminated = true;
				break;
			}
			p += h & 0x00ffffff;
			if (p > size)
				break;
			n++;
		}
		return n;
	}

	static bool sameFrame(const Graphics::Surface *a, const Graphics::Surface *b) {
		if (!a || !b)
			return a == b;
		if (a->w != b->w || a->h != b->h || a->format != b->format)
			return false;
		for (int y = 0; y < a->h; y++)
			if (memcmp(a->getBasePtr(0, y), b->getBasePtr(0, y), a->w * a->format.bytesPerPixel))
				return false;
		return true;
	}

	// Returns the number of differing frames; frames decoded in *nframes.  A
	// file either decoder fails to load counts every expected frame as
	// differing (at least 1), so it cannot cancel a genuine difference.
	static int compare(const byte *data, uint32 size, const Graphics::PixelFormat &fmt,
	                   bool loop, uint32 frames, int &decoded) {
		byte *c1 = new byte[size];
		byte *c2 = new byte[size];
		memcpy(c1, data, size);
		memcpy(c2, data, size);
		Video::HNMDecoder cur(fmt, loop);
		VideoUpstream::HNMDecoder up(fmt, loop);
		if (!cur.loadStream(new Common::MemoryReadStream(c1, size, DisposeAfterUse::YES)) ||
		    !up.loadStream(new Common::MemoryReadStream(c2, size, DisposeAfterUse::YES)))
			return frames > 0 ? (int)frames : 1;
		int bad = 0;
		for (uint32 i = 0; i < frames; i++) {
			const Graphics::Surface *a = cur.decodeNextFrame();
			const Graphics::Surface *b = up.decodeNextFrame();
			decoded++;
			bool pa = cur.hasDirtyPalette(), pb = up.hasDirtyPalette();
			if (!sameFrame(a, b) || pa != pb ||
			    (pa && memcmp(cur.getPalette(), up.getPalette(), 768)))
				bad++;
		}
		return bad;
	}

	static void listHnm(const Common::FSNode &dir, Common::Array<Common::FSNode> &out) {
		Common::FSList children;
		if (!dir.getChildren(children, Common::FSNode::kListAll))
			return;
		for (uint i = 0; i < children.size(); i++) {
			if (children[i].isDirectory())
				listHnm(children[i], out);
			else if (children[i].getName().hasSuffixIgnoreCase(".hnm"))
				out.push_back(children[i]);
		}
	}

public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_versailles_hnm4_unchanged() {
		Common::String dir = AtlantisTest::configValue("versailles");
		if (dir.empty()) {
			TS_WARN("versailles= not configured: Versailles HNM4 regression test skipped");
			return;
		}
		Common::Array<Common::FSNode> files;
		listHnm(Common::FSNode(Common::Path(dir, '/')), files);
		TS_ASSERT_EQUALS(files.size(), 342u);
		int nfiles = 0, decoded = 0, bad = 0, badFiles = 0, loopSkipped = 0;
		Common::String report;
		for (uint i = 0; i < files.size(); i++) {
			Common::File f;
			if (!f.open(files[i]))
				continue;
			uint32 size = f.size();
			byte *d = new byte[size];
			f.read(d, size);
			if (READ_BE_UINT32(d) != MKTAG('H', 'N', 'M', '4')) {
				delete[] d;
				continue;
			}
			WRITE_LE_UINT32(d + 0x18, 0);   // soundBits/soundFormat: no audio track
			bool term;
			uint32 sc = countSuperchunks(d, size, term);
			uint32 hdrFrames = READ_LE_UINT32(d + 0x10);
			nfiles++;
			int b = compare(d, size, Graphics::PixelFormat::createFormatCLUT8(), false,
			                MIN(sc, hdrFrames), decoded);
			// Loop mode (Versailles dialog videos): wrap around twice. Only
			// meaningful with a terminator: without one the old decoder reads
			// an uninitialised value at EOF (Common::ReadStream::readUint32LE).
			if (term)
				b += compare(d, size, Graphics::PixelFormat::createFormatCLUT8(), true,
				             2 * sc + 3, decoded);
			else
				loopSkipped++;
			if (b) {
				badFiles++;
				bad += b;
				if (report.size() < 400)
					report += " " + files[i].getName();
			}
			delete[] d;
		}
		TS_WARN(Common::String::format("Versailles HNM4: %d files, %d frames compared, %d differ "
		        "(%d files)%s; loop mode skipped for %d unterminated file(s)",
		        nfiles, decoded, bad, badFiles, report.c_str(), loopSkipped).c_str());
		TS_ASSERT_EQUALS(nfiles, 342);
		TS_ASSERT_EQUALS(bad, 0);
		TS_ASSERT_EQUALS(badFiles, 0);
	}

	void test_cyclo_warp_vs_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: warp test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		if (!AtlantisTest::loadGolden("video_warp_frames.txt", golden)) {
			TS_FAIL("video_warp_frames.txt missing from the configured golden directory");
			return;
		}
		VideoTest::Big big;
		Common::Array<Common::String> names = big.names(".HNM");
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		int nfiles = 0, bad = 0;
		Common::String report;
		for (uint i = 0; i < names.size(); i++) {
			if (!names[i].hasPrefix("CYCLO\\"))
				continue;
			uint32 size;
			byte *d = big.get(names[i], size);
			Video::HNMDecoder dec(fmt);
			if (!dec.loadStream(new Common::MemoryReadStream(d, size, DisposeAfterUse::YES))) {
				bad++;
				continue;
			}
			nfiles++;
			const Graphics::Surface *f = dec.decodeNextFrame();
			Common::String key = names[i].substr(6);
			Common::String got;
			if (f) {
				Common::Array<byte> le(f->w * f->h * 2);
				for (int y = 0; y < f->h; y++) {
					const uint16 *row = (const uint16 *)f->getBasePtr(0, y);
					for (int x = 0; x < f->w; x++) {
						le[(y * f->w + x) * 2] = row[x] & 0xff;
						le[(y * f->w + x) * 2 + 1] = row[x] >> 8;
					}
				}
				got = AtlantisTest::md5Hex(le.data(), le.size());
				// Debug aid: "dump_warp=<NAME>" (or "SAMPLE": every 40th file)
				// + "dump_dir=<dir>" in atlantis_test.cfg writes the port's
				// frame as raw RGB565 LE (analysed by tools/emu/video_warp_diff.py).
				Common::String dw = AtlantisTest::configValue("dump_warp");
				if (dw == key || (dw == "SAMPLE" && nfiles % 40 == 1)) {
					Common::DumpFile df;
					if (df.open(Common::FSNode(Common::Path(AtlantisTest::configValue("dump_dir"), '/')
					            .appendComponent(key + ".port.raw"))))
						df.write(le.data(), le.size());
				}
			}
			if (!golden.contains(key) || golden[key] != got) {
				bad++;
				if (report.size() < 400)
					report += " " + key;
			}
		}
		TS_WARN(Common::String::format("CYCLO warp: %d files, %d differ from FUN_0045084c%s",
		        nfiles, bad, report.c_str()).c_str());
		TS_ASSERT_EQUALS(nfiles, 804);
		TS_ASSERT_EQUALS(bad, 0);
	}

	// The in-game videos (IX chunks of UBB_VUE\*.HNM) against the original's
	// decoder FUN_004506ec, run under emulation with the buffers swapped after
	// each frame as its caller does (atlantis-re tools/emu/video_ix.py; golden
	// video_ix_frames.txt: the first frames of every file).
	// The IX buffers kept from one video to the next (setHNM6VideoBuffers):
	// CINEM005 frames 0..3, then CINEM006 frames 0..3 in the same block, as
	// the game's never-cleared buffers [0x004960d4] (atlantis-re
	// experiments/2026-10-14-11); md5s from tools/emu/video_ix.py's decoder
	// run on one block without clearing it.
	void test_ubb_vue_ix_persistent_buffers() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: IX persistence test skipped");
			return;
		}
		static const char *const kWant[] = {
			"a950f43cca589dd51a63086590f648d2", "1aa0981d0d9c57f1104f073b7c58964f",
			"5fdea6775495fe0cd80b5aa7ab442075", "d44c3013dd5acaf9d0137d1f1c762835",
			"03a30e3b414e5ca08aa40046e0598373", "02c124b4fce69daaa16016939d9d4cbd",
			"245ab618e95664c0edbbd3a791e849cc", "389e8ddb627b2f81c7b0d62278ebb3eb"
		};
		static const char *const kFiles[] = { "CINEM005.HNM", "CINEM006.HNM" };
		VideoTest::Big big;
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		Common::Array<byte> block(2 * 640 * 480 * 2, 0);
		uint n = 0;
		for (uint v = 0; v < ARRAYSIZE(kFiles); v++) {
			uint32 size;
			byte *d = big.get(Common::String("UBB_VUE\\") + kFiles[v], size);
			TS_ASSERT(d != nullptr);
			if (!d)
				return;
			d[6] = 0;   // audioflags: no APC track
			Video::HNMDecoder dec(fmt);
			dec.setHNM6VideoBuffers(block.data());
			TS_ASSERT(dec.loadStream(new Common::MemoryReadStream(d, size, DisposeAfterUse::YES)));
			dec.start();
			for (uint k = 0; k < 4; k++, n++) {
				const Graphics::Surface *f = dec.decodeNextFrame();
				TS_ASSERT(f != nullptr);
				if (!f)
					return;
				Common::Array<byte> le(f->w * f->h * 2);
				for (int y = 0; y < f->h; y++) {
					const uint16 *row = (const uint16 *)f->getBasePtr(0, y);
					for (int x = 0; x < f->w; x++) {
						le[(y * f->w + x) * 2] = row[x] & 0xff;
						le[(y * f->w + x) * 2 + 1] = row[x] >> 8;
					}
				}
				TSM_ASSERT_EQUALS(Common::String::format("%s frame %u", kFiles[v], k).c_str(),
				                  AtlantisTest::md5Hex(le.data(), le.size()), Common::String(kWant[n]));
			}
		}
	}

	void test_ubb_vue_ix_vs_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("Atlantis data not configured: IX test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		if (!AtlantisTest::loadGolden("video_ix_frames.txt", golden)) {
			TS_FAIL("video_ix_frames.txt missing from the configured golden directory");
			return;
		}
		Common::HashMap<Common::String, uint> framesOf;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			const uint sp = it->_key.findFirstOf(' ');
			const Common::String name = it->_key.substr(0, sp);
			const uint n = (uint)atoi(it->_key.c_str() + sp + 1) + 1;
			if (!framesOf.contains(name) || framesOf[name] < n)
				framesOf[name] = n;
		}
		VideoTest::Big big;
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		int frames = 0, bad = 0;
		Common::String report;
		for (Common::HashMap<Common::String, uint>::const_iterator it = framesOf.begin(); it != framesOf.end(); ++it) {
			uint32 size;
			byte *d = big.get("UBB_VUE\\" + it->_key, size);
			if (!d) {
				bad++;
				continue;
			}
			d[6] = 0;   // audioflags: no APC track
			Video::HNMDecoder dec(fmt);
			if (!dec.loadStream(new Common::MemoryReadStream(d, size, DisposeAfterUse::YES))) {
				bad++;
				continue;
			}
			dec.start();
			for (uint k = 0; k < it->_value; k++) {
				const Graphics::Surface *f = dec.decodeNextFrame();
				if (!f) {
					// The decoder stopped early: every frame the golden
					// still expects counts as differing.
					bad += (int)(it->_value - k);
					if (report.size() < 300)
						report += Common::String::format(" %s %u (end)", it->_key.c_str(), k);
					break;
				}
				Common::Array<byte> le(f->w * f->h * 2);
				for (int y = 0; y < f->h; y++) {
					const uint16 *row = (const uint16 *)f->getBasePtr(0, y);
					for (int x = 0; x < f->w; x++) {
						le[(y * f->w + x) * 2] = row[x] & 0xff;
						le[(y * f->w + x) * 2 + 1] = row[x] >> 8;
					}
				}
				frames++;
				const Common::String key = Common::String::format("%s %u", it->_key.c_str(), k);
				// Debug aid: "dump_ix=<NAME>" + "dump_dir=<dir>" writes the port's
				// frames as raw RGB565 LE (<NAME>.<frame>.port.raw).
				if (AtlantisTest::configValue("dump_ix") == it->_key) {
					Common::DumpFile df;
					if (df.open(Common::FSNode(Common::Path(AtlantisTest::configValue("dump_dir"), '/')
					            .appendComponent(Common::String::format("%s.%u.port.raw", it->_key.c_str(), k)))))
						df.write(le.data(), le.size());
				}
				if (!golden.contains(key) || golden[key] != AtlantisTest::md5Hex(le.data(), le.size())) {
					bad++;
					if (report.size() < 300)
						report += " " + key;
				}
			}
		}
		TS_WARN(Common::String::format("UBB_VUE IX: %d frames, %d differ from FUN_004506ec%s",
		        frames, bad, report.c_str()).c_str());
		TS_ASSERT(frames > 0);
		TS_ASSERT_EQUALS(bad, 0);
	}
};
