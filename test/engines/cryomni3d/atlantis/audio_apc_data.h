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
};
