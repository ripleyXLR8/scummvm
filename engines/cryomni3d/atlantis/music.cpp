/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "audio/audiostream.h"
#include "audio/decoders/apc.h"
#include "audio/decoders/raw.h"
#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/mutex.h"
#include "common/memstream.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/util.h"

#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

// Music track table — 1-based, matching the startmusik / stopmusik / loadmusik
// CON command IDs.  Transcribed in full from atlantis.exe's table at VA
// 0x00495eec (13-byte records).  Filenames live in the BigFile's WAV\ dir.
static const char *const kMusicTrackNames[] = {
	nullptr,           // index 0 unused
	"01ATLAN1.APC",    // track  1 — Atlantis area 1 ambient
	"10ATLAN2.APC",    // track  2
	"23_PALA1.APC",    // track  3
	"02AUBERG.APC",    // track  4
	"24_RATS.APC",     // track  5
	"DAUPHINS.APC",    // track  6
	"05SSTRON.APC",    // track  7
	"METAMORF.APC",    // track  8
	"LABYRINT.APC",    // track  9
	"31CRANES.APC",    // track 10
	"STONEHEG.APC",    // track 11
	"ASCENSE1.APC",    // track 12
	"FORET_35.APC",    // track 13
	"23_PRET1.APC",    // track 14
	"20SORCIE.APC",    // track 15
	"06SPITZ2.APC",    // track 16
	"30SHAMAN.APC",    // track 17
	"21PACQUE.APC",    // track 18
	"08TISSE2.APC",    // track 19
	"33INTRO.APC",     // track 20 — narration for CINEM020.HNM
	"HANGAR1.APC",     // track 21
	"04GENERI.APC",    // track 22
	"TETE1.APC",       // track 23
	"TETE2.APC",       // track 24
	"03ATTACK.APC",    // track 25
	"09VOLTOT.APC",    // track 26
	"CINE101.APC",     // track 27
	"LAC.APC",         // track 28
	"CINE147.APC",     // track 29
	"CINE172.APC",     // track 30
	"CINE194.APC",     // track 31
};

// Number of per-channel samples to fade in/out at each loop boundary (~46ms at 22050 Hz).
// Both ends ramp to zero so the loop-point discontinuity (IMA predictor reset) is inaudible.
// Decode an APC file from raw bytes into a PCM buffer, apply a linear fade-in at the
// start and fade-out at the end, and return an infinitely-looping RewindableAudioStream.
// The caller must not free `data` while the stream is alive (it reads only during this call).
static Audio::AudioStream *buildLoopingMusicStream(const byte *data, uint32 size) {
	Common::MemoryReadStream *raw = new Common::MemoryReadStream(data, size, DisposeAfterUse::NO);
	Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*raw);
	if (!apc) {
		delete raw;
		return nullptr;
	}

	const bool stereo = apc->isStereo();
	const int  rate   = apc->getRate();

	// Queue all remaining compressed data for decode.
	int64 remaining = (int64)size - (int64)raw->pos();
	if (remaining > 0) {
		byte *buf = new byte[(uint32)remaining];
		raw->read(buf, (uint32)remaining);
		apc->queuePacket(new Common::MemoryReadStream(buf, (uint32)remaining,
		                                              DisposeAfterUse::YES));
	}
	apc->finish();
	delete raw;

	// Each compressed byte → 2 int16 output samples (one per nibble).
	int64 totalSamples = remaining * 2;
	byte *pcmBytes = new byte[(uint32)(totalSamples * 2)];  // int16 = 2 bytes
	int got = apc->readBuffer((int16 *)pcmBytes, (int)totalSamples);
	delete apc;

	// Apply fade-in to the first kLoopFadeSamples per-channel frames and
	// fade-out to the last kLoopFadeSamples per-channel frames, so the loop
	// restarts cleanly even though the IMA predictor is reset from the header.
	// No fade at the loop point: the original seeks the file back to 0 and
	// restarts the decoder (FUN_0043318c).

	byte flags = Audio::FLAG_16BITS;
	if (stereo)
		flags |= Audio::FLAG_STEREO;
#ifdef SCUMM_LITTLE_ENDIAN
	flags |= Audio::FLAG_LITTLE_ENDIAN;
#endif

	Audio::SeekableAudioStream *rawStream = Audio::makeRawStream(
	    pcmBytes, (uint32)(got * 2), rate, flags, DisposeAfterUse::YES);
	return Audio::makeLoopingAudioStream(rawStream, 0);
}

// The music sample at the original's volume: the gain is the AIL volume
// (0x00495e71, clamped to 127) over the music volume (0x00495e72), which the
// mixer's music volume already applies.
class FadedMusicStream : public Audio::AudioStream {
public:
	FadedMusicStream(Audio::AudioStream *in, AtlantisMusicFade *fade) : _in(in), _fade(fade) {}
	~FadedMusicStream() override { delete _in; }
	int readBuffer(int16 *buffer, const int numSamples) override {
		const int got = _in->readBuffer(buffer, numSamples);
		int cur, vol;
		{
			Common::StackLock lock(_fade->mutex);
			cur = MIN<int>(_fade->cur, 127);
			vol = _fade->vol;
		}
		if (vol <= 0) {
			memset(buffer, 0, got * sizeof(int16));
		} else if (cur != vol) {
			for (int i = 0; i < got; i++)
				buffer[i] = (int16)CLIP<int>(buffer[i] * cur / vol, -32768, 32767);
		}
		return got;
	}
	bool isStereo() const override { return _in->isStereo(); }
	int getRate() const override { return _in->getRate(); }
	bool endOfData() const override { return _in->endOfData(); }
private:
	Audio::AudioStream *_in;
	AtlantisMusicFade *_fade;
};

static void startMusicFromBuffer(Audio::Mixer *mixer, Audio::SoundHandle &handle,
                                 const byte *data, uint32 size, AtlantisMusicFade *fade) {
	Audio::AudioStream *stream = buildLoopingMusicStream(data, size);
	if (stream)
		mixer->playStream(Audio::Mixer::kMusicSoundType, &handle, new FadedMusicStream(stream, fade));
}

void CryOmni3DEngine_Atlantis::musicFadeTimer(void *refCon) {
	((CryOmni3DEngine_Atlantis *)refCon)->_musicFade.tick();
}

// 0x00495e72 from the music volume setting (the sound menu stores the AIL
// volume rescaled to the mixer's 0..255).
void CryOmni3DEngine_Atlantis::musicSyncVolume() {
	const int v = ConfMan.hasKey("music_volume") ? ConfMan.getInt("music_volume") : Audio::Mixer::kMaxMixerVolume;
	Common::StackLock lock(_musicFade.mutex);
	_musicFade.vol = (uint8)CLIP(v * 127 / Audio::Mixer::kMaxMixerVolume, 0, 127);
}

// The original spins on the fade (calling only the music refill) until the
// timer callback has finished it; the screen does not change meanwhile.
void CryOmni3DEngine_Atlantis::musicWaitFadeEnd() {
	for (;;) {
		{
			Common::StackLock lock(_musicFade.mutex);
			if (_musicFade.mode == 0)
				return;
		}
		if (shouldQuit())
			return;
		Common::Event ev;
		while (g_system->getEventManager()->pollEvent(ev)) {
		}
		g_system->delayMillis(5);
	}
}

void CryOmni3DEngine_Atlantis::musicSwitchTrack(int trackId) {
	if (_musicCurrentFile && _mixer->isSoundHandleActive(_musicHandle)) {
		{
			Common::StackLock lock(_musicFade.mutex);
			_musicFade.percent = 99;
			_musicFade.mode = 2;
		}
		musicWaitFadeEnd();
		musicHaltPlayback();
		Common::StackLock lock(_musicFade.mutex);
		_musicFade.percent = 80;
	}
	musicPlayTrack(trackId);
	if (_musicCurrentFile) {
		Common::StackLock lock(_musicFade.mutex);
		_musicFade.mode = 1;
		_musicFade.cur = 0;
		_musicFade.upTarget = _musicFade.vol;
	}
}

void CryOmni3DEngine_Atlantis::musicStartCommand(int trackId) {
	if (trackId == _musicTrackId && _musicCurrentFile) {
		Common::StackLock lock(_musicFade.mutex);
		_musicFade.mode = 1;
		_musicFade.upTarget = _musicFade.vol;
		return;
	}
	musicSwitchTrack(trackId);
}

void CryOmni3DEngine_Atlantis::musicStopCommand() {
	if (_musicCurrentFile && _mixer->isSoundHandleActive(_musicHandle)) {
		uint8 saved;
		{
			Common::StackLock lock(_musicFade.mutex);
			saved = _musicFade.cur;
			_musicFade.percent = 99;
			_musicFade.mode = 2;
		}
		musicWaitFadeEnd();
		musicHaltPlayback();
		Common::StackLock lock(_musicFade.mutex);
		_musicFade.cur = saved;
		_musicFade.percent = 80;
	}
	_musicTrackId = 0;
}

void CryOmni3DEngine_Atlantis::musicDuck(bool onlyWhenIdle, int percent) {
	Common::StackLock lock(_musicFade.mutex);
	if (percent >= 0)
		_musicFade.percent = (uint8)percent;
	if (!onlyWhenIdle || _musicFade.mode == 0)
		_musicFade.mode = 2;
}

void CryOmni3DEngine_Atlantis::musicUnduck(int percent) {
	Common::StackLock lock(_musicFade.mutex);
	_musicFade.mode = 1;
	if (percent >= 0)
		_musicFade.percent = (uint8)percent;
}

// Stop mixer playback and release the decoded buffer, keeping _musicTrackId so
// the selected track can be resumed later (e.g. after unmuting).
void CryOmni3DEngine_Atlantis::musicHaltPlayback() {
	// The fades are the original's (AtlantisMusicFade); this stops at once.
	_mixer->stopHandle(_musicHandle);
	delete[] _musicRawData;
	_musicRawData = nullptr;
	_musicRawSize = 0;
	_musicCurrentFile = nullptr;
}

// stopmusik (CON), chapter change, and the credits sequence — clear the music
// selection entirely and stop playback.
void CryOmni3DEngine_Atlantis::musicStop() {
	_musicTrackId = 0;
	musicHaltPlayback();
}

// startmusik=N (CON) — select track N and start it looping.  Also used by
// musicUpdate() to (re)start the selected track after a mute or chapter change.
void CryOmni3DEngine_Atlantis::musicPlayTrack(int trackId) {
	if (trackId < 1 || trackId >= (int)ARRAYSIZE(kMusicTrackNames)
	        || !kMusicTrackNames[trackId]) {
		warning("musicPlayTrack: invalid track %d", trackId);
		return;
	}
	_musicTrackId = trackId;

	const char *musicFile = kMusicTrackNames[trackId];
	// Already playing this track — leave it (a place re-issuing the same
	// startmusik must not cause a restart hiccup).
	if (_musicCurrentFile == musicFile && _mixer->isSoundHandleActive(_musicHandle))
		return;

	musicHaltPlayback();

	Common::SeekableReadStream *apcFile = openBigFileStream(kFileTypeSound, musicFile);
	if (!apcFile) {
		warning("musicPlayTrack: cannot open track %d '%s'", trackId, musicFile);
		return;
	}
	_musicRawSize = (uint32)apcFile->size();
	_musicRawData = new byte[_musicRawSize];
	apcFile->read(_musicRawData, _musicRawSize);
	delete apcFile;

	_musicCurrentFile = musicFile;
	startMusicFromBuffer(_mixer, _musicHandle, _musicRawData, _musicRawSize, &_musicFade);
	debugC(1, kDebugMusic, "musicPlayTrack: track %d '%s'", trackId, musicFile);
}

// Called every frame.  The music *selection* is owned by the startmusik /
// stopmusik CON commands (_musicTrackId); this only keeps the mixer in sync —
// pausing playback while music is muted and resuming the selected track on
// unmute or after a chapter change.
void CryOmni3DEngine_Atlantis::musicUpdate() {
	const bool muted = _mixer->isSoundTypeMuted(Audio::Mixer::kMusicSoundType)
	                || _mixer->getVolumeForSoundType(Audio::Mixer::kMusicSoundType) == 0;
	if (muted) {
		musicHaltPlayback();   // keep _musicTrackId so unmuting resumes it
		return;
	}
	if (_musicTrackId <= 0)
		return;                // nothing selected, or stopped by stopmusik
	if (_mixer->isSoundHandleActive(_musicHandle))
		return;                // selected track already looping
	musicPlayTrack(_musicTrackId);  // first play, or resume after a mute
	// A track restarted from volume 0 (a loaded game) fades in, as the
	// chapter's startmusik does in the original (@ 0x0042ae23).
	Common::StackLock lock(_musicFade.mutex);
	if (_musicCurrentFile && _musicFade.cur == 0 && _musicFade.mode == 0) {
		_musicFade.mode = 1;
		_musicFade.upTarget = _musicFade.vol;
	}
}

// The tracks the original would find in its install folder, as a bit set
// in the configuration (it outlives the session, like the folder).
static const char *const kMusicOnDiskKey = "atlantis_music_on_disk";

uint32 CryOmni3DEngine_Atlantis::musicOnDisk() const {
	return ConfMan.hasKey(kMusicOnDiskKey)
	       ? (uint32)strtoul(ConfMan.get(kMusicOnDiskKey).c_str(), nullptr, 16) : 0;
}

void CryOmni3DEngine_Atlantis::setMusicOnDisk(uint32 bits) {
	if (ConfMan.hasKey(kMusicOnDiskKey) && musicOnDisk() == bits)
		return;
	ConfMan.set(kMusicOnDiskKey, Common::String::format("%x", bits));
	ConfMan.flushToDisk();
}

// FUN_0041f380: the track is copied in 0x80000-byte reads through the first
// IX buffer until a read returns nothing, so the buffer ends with the file's
// last chunk at its start, then the rest of the chunk before.
void CryOmni3DEngine_Atlantis::musicCopyThroughScreenBuffer(uint idx) {
	if (idx + 1 >= ARRAYSIZE(kMusicTrackNames) || !kMusicTrackNames[idx + 1])
		return;
	Common::ScopedPtr<Common::SeekableReadStream> f(openBigFileStream(kFileTypeSound, kMusicTrackNames[idx + 1]));
	if (!f)
		return;
	while (f->read(_ixVideoBuffers, 0x80000) != 0) {
	}
	debugC(1, kDebugMusic, "loadmusik: track %u '%s' copied", idx + 1, kMusicTrackNames[idx + 1]);
}

// FUN_0041f1d0 (load) / FUN_0041faa0 (del), without the WAIT menu a copy
// shows (decisions/0014-music-copy-model.md).
void CryOmni3DEngine_Atlantis::execMusicList(bool load, const Common::Array<char> &text, uint32 pos) {
	uint32 onDisk = musicOnDisk();
	Common::Array<uint8> copies;
	conMusicApply(load, conMusicList(text, pos), _musicTrackId, onDisk, copies);
	for (uint i = 0; i < copies.size(); i++)
		musicCopyThroughScreenBuffer(copies[i]);
	setMusicOnDisk(onDisk);
}

} // namespace Atlantis
} // namespace CryOmni3D
