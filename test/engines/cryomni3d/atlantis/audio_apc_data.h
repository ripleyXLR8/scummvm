// Verifier "audio": every APC file of the game through ScummVM's APC decoder.
//
// Original: atlantis-cd-en-714240 FUN_00417dd0 (header) and FUN_004101ce
// (decode loop), run unmodified under Unicorn on all 2468 .APC members of
// BIGCD1..4.BIG by atlantis-re/tools/emu/apc_whole_game.py; golden
// apc_whole_game.txt holds, per file, the frame count it produced and the
// MD5 of its int16 PCM (experiment 2026-10-16-02).
//
// Port: Audio::makeAPCStream (audio/decoders/apc.cpp), fed the 32-byte
// header and the rest of the file as one packet, as the engine does.

#include "atlantis_test_helper.h"
#include "scripts_con_common.h"
#include "../../../system/null_osystem.h"

#include "audio/audiostream.h"
#include "audio/decoders/apc.h"
#include "common/memstream.h"

#include "cryomni3d/atlantis/apc_stream.h"
#include "cryomni3d/atlantis/music_ring.h"
#include "common/md5.h"

namespace AudioApcDataTest {

// The heard PCM of a MusicRing as a byte stream: one 5 ms period at a time,
// each period's frames read before its release and refill (the model's
// order), little-endian int16.
class RingPcm : public Common::ReadStream {
public:
	RingPcm(CryOmni3D::Atlantis::MusicRing &ring) : _ring(ring), _t(0), _have(0), _at(0) {}
	bool eos() const override { return false; }
	uint32 read(void *dataPtr, uint32 dataSize) override {
		byte *dst = (byte *)dataPtr;
		uint32 done = 0;
		while (done < dataSize) {
			if (_at == _have) {
				const uint nf = _ring.framesOfTick(_t++);
				_ring.tick(nf, _pcm);
				for (uint i = 0; i < 2 * nf; i++)
					WRITE_LE_UINT16(_bytes + 2 * i, (uint16)_pcm[i]);
				_have = 4 * nf;
				_at = 0;
			}
			const uint32 n = MIN(dataSize - done, _have - _at);
			memcpy(dst + done, _bytes + _at, n);
			done += n;
			_at += n;
		}
		return done;
	}
private:
	CryOmni3D::Atlantis::MusicRing &_ring;
	uint32 _t, _have, _at;
	int16 _pcm[2 * 256];
	byte _bytes[4 * 256];
};

} // End of namespace AudioApcDataTest

class AudioApcDataTestSuite : public CxxTest::TestSuite {
public:
	// File access and the APC stream's queue need an OSystem.
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_every_apc_vs_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("audio_apc_data: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("apc_whole_game.txt", golden));
		ScriptsTest::Big big;
		uint files = 0, mismatches = 0;
		Common::String list;
		for (uint g = 0; g < golden.size(); g++) {
			// F <name> <kind> <stereo|mono> frames <n> md5 <hex> ...
			char name[64], kind[16], chans[16], md5[40];
			unsigned long frames = 0;
			if (sscanf(golden[g].c_str(), "F %63s %15s %15s frames %lu md5 %39s", name, kind, chans, &frames, md5) != 5)
				continue;
			files++;
			Common::Array<char> data;
			TS_ASSERT(big.get(Common::String("WAV\\") + name, data));
			if (data.size() < 32) {
				mismatches++;
				continue;
			}
			const uint32 samples = (uint32)frames * (strcmp(chans, "stereo") ? 1 : 2);
			Common::MemoryReadStream header((const byte *)data.begin(), 32);
			Audio::PacketizedAudioStream *s = Audio::makeAPCStream(header);
			TS_ASSERT(s != nullptr);
			if (!s) {
				mismatches++;
				continue;
			}
			byte *body = (byte *)malloc(data.size() - 32);
			memcpy(body, data.begin() + 32, data.size() - 32);
			s->queuePacket(new Common::MemoryReadStream(body, data.size() - 32, DisposeAfterUse::YES));
			s->finish();
			Common::Array<int16> pcm;
			pcm.resize(samples);
			uint32 got = 0;
			while (got < samples) {
				const int n = s->readBuffer(pcm.begin() + got, MIN<uint32>(samples - got, 65536));
				if (n <= 0)
					break;
				got += (uint32)n;
			}
			delete s;
			// The golden MD5 is over little-endian int16.
			for (uint32 i = 0; i < got; i++)
				pcm[i] = (int16)TO_LE_16((uint16)pcm[i]);
			if (got != samples || AtlantisTest::md5Hex((const byte *)pcm.begin(), samples * 2) != md5) {
				mismatches++;
				if (mismatches <= 10)
					list += Common::String(" ") + name;
			}
		}
		if (mismatches)
			TS_WARN(Common::String::format("audio_apc_data: %u of %u files differ:", mismatches, files) + list);
		TS_ASSERT_EQUALS(files, 2468u);
		TS_ASSERT_EQUALS(mismatches, 0u);
	}

	// makeAtlantisAPCStream (apc_stream.cpp), the decoder every Atlantis
	// sound now goes through, ends where FUN_004101ce ends: the header's
	// frame count, not the extra data byte every file has.  Expected: the
	// original's frame count and PCM MD5 of apc_whole_game.txt.
	void test_atlantis_stream_stops_at_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("audio_apc_data: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("apc_whole_game.txt", golden));
		ScriptsTest::Big big;
		uint files = 0, mismatches = 0;
		Common::String list;
		for (uint g = 0; g < golden.size(); g++) {
			char name[64], kind[16], chans[16], md5[40];
			unsigned long frames = 0;
			if (sscanf(golden[g].c_str(), "F %63s %15s %15s frames %lu md5 %39s", name, kind, chans, &frames, md5) != 5)
				continue;
			files++;
			Common::Array<char> data;
			TS_ASSERT(big.get(Common::String("WAV\\") + name, data));
			Common::MemoryReadStream file((const byte *)data.begin(), data.size());
			Audio::AudioStream *s = CryOmni3D::Atlantis::makeAtlantisAPCStream(file);
			if (!s) {
				mismatches++;
				continue;
			}
			const uint32 samples = (uint32)frames * (strcmp(chans, "stereo") ? 1 : 2);
			Common::Array<int16> pcm;
			pcm.resize(samples + 64);
			uint32 got = 0;
			for (;;) {
				const int n = s->readBuffer(pcm.begin() + got, MIN<uint32>(pcm.size() - got, 65536));
				if (n <= 0)
					break;
				got += (uint32)n;
				if (got >= pcm.size())
					break;
			}
			const bool ended = s->endOfData();
			delete s;
			for (uint32 i = 0; i < MIN(got, samples); i++)
				pcm[i] = (int16)TO_LE_16((uint16)pcm[i]);
			if (got != samples || !ended || AtlantisTest::md5Hex((const byte *)pcm.begin(), samples * 2) != md5) {
				mismatches++;
				if (mismatches <= 10)
					list += Common::String::format(" %s(%u/%u)", name, got, samples);
			}
		}
		if (mismatches)
			TS_WARN(Common::String::format("audio_apc_data: %u of %u streams differ:", mismatches, files) + list);
		TS_ASSERT_EQUALS(files, 2468u);
		TS_ASSERT_EQUALS(mismatches, 0u);
	}

	// The music ring of the original (MusicRing: FUN_00432ea8, FUN_0043318c,
	// FUN_00433310, FUN_004101ce) with the position law measured under Wine
	// (play + 0x8000, rounded down to 0x4000): the PCM heard over two loops
	// of each of the 31 tracks, against the original's code run under
	// Unicorn with the same law and the same 5 ms periods (golden
	// audio_music_ring_wine.txt, atlantis-re tools/emu/audio_music_stream.py
	// --wine --pcm, experiment 2026-10-16-28; decisions/0016 row 1).
	// The position law (measured under Wine, experiment 2026-10-16-17) and
	// the 5 ms timer period (experiment 2026-10-16-25) are inputs of both
	// sides of the comparison; this test does not verify them.
	void test_music_ring_vs_original() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("audio_apc_data: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("audio_music_ring_wine.txt", golden));
		ScriptsTest::Big big;
		uint tracks = 0, mismatches = 0;
		Common::String list;
		for (uint g = 0; g < golden.size(); g++) {
			char name[64], md5[40];
			unsigned long long heard = 0;
			if (sscanf(golden[g].c_str(), "M %63s heard_bytes %llu md5 %39s", name, &heard, md5) != 3)
				continue;
			tracks++;
			Common::Array<char> data;
			TS_ASSERT(big.get(Common::String("WAV\\") + name, data));
			CryOmni3D::Atlantis::MusicRing ring((const byte *)data.begin(), data.size());
			if (!ring.start()) {
				mismatches++;
				list += Common::String::format(" %s(start)", name);
				continue;
			}
			// The model's stop rule: whole periods until two loops are heard.
			uint64 frames = 0;
			for (uint32 t = 0; frames < 2ULL * ring.headerFrames(); t++)
				frames += ring.framesOfTick(t);
			AudioApcDataTest::RingPcm pcm(ring);
			const Common::String got = Common::computeStreamMD5AsString(pcm, (uint32)(frames * 4));
			if (frames * 4 != heard || got != md5) {
				mismatches++;
				if (mismatches <= 10)
					list += Common::String::format(" %s", name);
			}
		}
		if (mismatches)
			TS_WARN(Common::String::format("audio_apc_data: %u of %u music rings differ:", mismatches, tracks) + list);
		TS_ASSERT_EQUALS(tracks, 31u);
		TS_ASSERT_EQUALS(mismatches, 0u);
	}
};
