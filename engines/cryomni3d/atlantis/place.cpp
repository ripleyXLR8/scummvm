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

// Chapter / place lifecycle.  This grows over several commits: it starts with
// WAM + CON loading and entering a place's cyclorama; CON arrival/departure
// sections, NPC sprite compositing, transition videos and autosave are layered
// on as their subsystems land.

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "audio/mixer.h"
#include "cryomni3d/atlantis/ambient_output.h"
#include "audio/decoders/apc.h"
#include "graphics/paletteman.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/transition_turn.h"
#include "cryomni3d/atlantis/video_skip.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/inventory.h"

namespace CryOmni3D {
namespace Atlantis {

// WAM (navigation map) file for each chapter, indexed by chapter number.
static const char *const kWAMNames[] = {
	nullptr,
	"ATLAN1.WAM",
	"ATLAN2.WAM",
	"ATLAN3.WAM",
	"ATLAN4.WAM",
	"ATLAN5.WAM",
};

void CryOmni3DEngine_Atlantis::setupChapterWAM(int chapter, bool keepState) {
	if (chapter < 1 || chapter >= (int)ARRAYSIZE(kWAMNames) || !kWAMNames[chapter])
		error("No WAM for chapter %d", chapter);

	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeWAM, kWAMNames[chapter]));
	if (!s)
		error("Cannot open WAM file for chapter %d (%s)", chapter, kWAMNames[chapter]);

	_wam.loadStream(*s);
	_currentWAMName = kWAMNames[chapter];
	loadAmbientCentres();   // FUN_00431580, last step of the WAM loader
	debugC(1, kDebugScript, "Loaded WAM for chapter %d: %u places", chapter, _wam.getPlaces().size());

	// Per-chapter NPC / prop scene-sprite list (SPW) and the transition
	// walk-on list (SPF).
	loadSpritePlaceList();
	loadSpfList();
	// The carried inventory persists across chapters; a new game (chapter 1)
	// starts from the record atlantis.exe FUN_0042c054 sets up before the
	// player menu: object 257 alone.  See atlantis-re:
	// functions/graphics-FUN_0042b328.md.
	if (chapter == 1 && keepState) {
		// FUN_0042de5c without a file (0x0042e249..0x0042e29e): LastVue = 1.
		_lastVue = 1;
	} else if (chapter == 1) {
		inventoryReset(_inventory, _objects);
		// FUN_00423760 zeroes the /con play counters and the timer offsets,
		// and restarts Time1, when a game starts.
		for (byte &b : _conPlayCounters)
			b = 0;
		_conClocks.time1Offset = _conClocks.time2Offset = 0;
		_conClocks.time1Start = g_system->getMillis();
		// The guest list is zeroed at boot (FUN_004239f4); the shown flag
		// starts at 1.  Whether a second new game in one session starts clean
		// is not established; the port starts clean.
		_guests.clear();
		_guestsShown = true;
		// FUN_00423760 @ 0x004237a0: the IncPerso count starts at 0.
		_incPersoCount = 0;
		// LastVue = 1 next to that set-up (0x004195fb, before FUN_0042c054).
		_lastVue = 1;
	}

	// FUN_00427634 reloads the CON and runs its /INIT even when the chapter
	// does not change (the in-play no-checkpoint branch, in chapter 1).
	if ((uint)chapter != _currentCONChapter || keepState) {
		_currentCONChapter = (uint)chapter;
		loadConScript(chapter);
	}
}

void CryOmni3DEngine_Atlantis::setupWAMByName(const Common::String &wamName) {
	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeWAM, wamName));
	if (!s) {
		warning("Cannot open WAM file '%s'", wamName.c_str());
		return;
	}
	_wam.loadStream(*s);
	recordPlaceTable();
	_currentWAMName = wamName;
	loadAmbientCentres();   // FUN_00431580, last step of the WAM loader
	// Each WAM references its own SPW/SPF lists; reload them so the new
	// area's NPCs composite.  (Inventory and CON script are untouched by a
	// sub-WAM swap.)
	loadSpritePlaceList();
	loadSpfList();
	debugC(1, kDebugScript, "Loaded WAM '%s': %u places", wamName.c_str(), _wam.getPlaces().size());
}

void CryOmni3DEngine_Atlantis::changeChapter(int chapter, bool keepState) {
	_currentChapter = (uint)chapter;
	// The music is not touched: a chapter change keeps it (FUN_00427634);
	// the new CON's startmusik continues the same track or switches.
	// A direct chapter start (new game) begins with no hidden NPCs; the CON
	// /INIT block establishes the initial set.  In-game /set(chapter=N)
	// switches deliberately do NOT clear this list (see loadConScript).
	if (!keepState)
		_hiddenPersos.clear();
	// FUN_0042de5c step 4 (both branches): ZOOM = NEWSOUND = 0; NEWSOUND is
	// one of the 7 variables the chapter change keeps.
	_gameVars["zoom"] = 0;
	_gameVars["newsound"] = 0;
	// No checkpoint: the new-game start FUN_00423760 sets the loading flag
	// 0x00496168 (0x00423778) before its chapter change FUN_00427634, whose
	// write is skipped when the flag is set (0x00427656).

	// setupChapterWAM loads the CON and runs its /INIT block, which may set the
	// start place via /set(vue=N) (e.g. chapter 1 opens on the place-450
	// cinematic).  Clear _nextPlaceId first so we can detect that.
	_nextPlaceId = uint(-1);
	_arrivalScanPending = false;
	const LevelInitialState &init = kChapterInitialStates[chapter - 1];
	// The view angles: no function of the load path writes them, only an
	// /INIT AngleWarpX / AngleWarpY (FUN_0042a100 @ 0x0042a9a9 / 0x0042a9e3);
	// at boot they are 0 (.bss), kept otherwise.
	if (!keepState) {
		_omni3dMan.setAlpha(init.alpha);
		_omni3dMan.setBeta(init.beta);
	}
	setupChapterWAM(chapter, keepState);

	// A Vue= in /INIT has entered its place already (enterPlaceFromScript).
	if (_nextPlaceId == uint(-1) && !_arrivalScanPending)
		_nextPlaceId = init.placeId;   // fall back to the hardcoded start
	// FUN_0042de5c step 7: CHAPTER = the chapter byte after the /INIT.
	_gameVars["chapter"] = (int)_currentCONChapter;
}

void CryOmni3DEngine_Atlantis::doPlaceChange() {
	const AtlantisPlace *nextPlace = _wam.findPlaceById((uint16)_nextPlaceId);
	if (!nextPlace) {
		warning("doPlaceChange: place %u not found in WAM", _nextPlaceId);
		_nextPlaceId = uint(-1);
		return;
	}

	_currentPlace = nextPlace;
	_currentPlaceId = _nextPlaceId;
	_nextPlaceId = uint(-1);
	_vueVarOverride = -1;   // `Vue` follows the place again
	_dialogGivenObjSprite = -1;   // any pending given-item overlay ends here

	// Load the panoramic cyclo HNM and point the viewer at it.
	if (loadCycloHNM(nextPlace->cycloHnm)) {
		_omni3dMan.setSourceSurface(&_warpSurface);
		setupPalette(_mainPalette, 0, 256);
		// Composite the place's standing NPCs / props onto the warp surface.
		compositeNPCSprites();
	}

	// Full 360-degree horizontal rotation; vertical tilt limited to +/-45 degrees.
	_omni3dMan.setBetaMinConstraint(-M_PI / 4.);
	_omni3dMan.setBetaMaxConstraint( M_PI / 4.);
	setMousePos(Common::Point(320, 240));
	markWarpPointerMouse();   // the port's own warp is no mouse motion

	debugC(1, kDebugScript, "doPlaceChange: at place %u (%s)",
	      _currentPlaceId, nextPlace->cycloHnm.c_str());
}

void CryOmni3DEngine_Atlantis::executeTransition(uint nextPlaceId) {
	// _currentPlace can be null after /set(changewam=...) — the script's
	// next /set(vue=N) is the destination, and there is no transition video
	// between the old and new WAMs anyway.  Skip the transition lookup and
	// the cross-WAM hand-off (the new WAM is already loaded) and just hand
	// the destination to gameStep's outer loop via _nextPlaceId so
	// doPlaceChange runs.
	if (!_currentPlace) {
		_nextPlaceId = nextPlaceId;
		return;
	}

	const AtlantisTransition *trans = _currentPlace->findTransition((uint16)nextPlaceId);
	if (trans) {
		// FUN_00423ad8 @ 0x00423ec0: FUN_004212cc turns the view to the
		// transition's source angles before the video.
		turnToTransition(trans->srcAlpha, trans->srcBeta);
		playTransitionVideo(trans->videoName, _currentPlaceId, nextPlaceId, kTransitionFrameMs);
		_omni3dMan.setAlpha(trans->dstAlpha);
		_omni3dMan.setBeta(trans->dstBeta);
	}

	// Cross-WAM transition: sub-WAMs hot-swap the navigation map when a
	// (src, dst) pair crosses into a different WAM.  Even chapter-named WAMs
	// (atlan2/3/4/5.wam) are hot-swapped here without resetting chapter
	// state — those WAMs serve as sub-areas of chapter 1.  Actual chapter
	// progression is driven by CON `/set(chapter=N)` once gameplay in the
	// new sub-area triggers it (e.g. arriving at vue=14 advances chapter 1
	// to chapter 2 via CHAPI001.CON section 10).
	for (const AtlantisSubWAM &sub : _wam.getSubWAMs()) {
		if (sub.srcPlaceId == _currentPlaceId && sub.dstPlaceId == nextPlaceId) {
			// Copy fields to locals BEFORE setupWAMByName: that call invokes
			// _wam.clear() which destroys _subWAMs, making sub a dangling ref.
			Common::String wamName = sub.wamName;
			uint16 dstPlaceId     = sub.dstPlaceId;

			debugC(1, kDebugScript, "Sub-WAM transition: place %u → WAM '%s' place %u",
			      nextPlaceId, wamName.c_str(), dstPlaceId);
			setupWAMByName(wamName);
			// _wam._places was cleared; _currentPlace is now a dangling pointer.
			// The place id stays the one being left: FUN_00423ad8 stores
			// ARRIVEEVUE = 0x006cd7c0 (0x00423def..0x00423df4) before the swap
			// FUN_00422ad0, so gameStep's arrival scan takes it as the origin.
			// See atlantis-re: experiments/2026-10-16-01-subwam-arrivee.md.
			_currentPlace   = nullptr;
			_nextPlaceId    = dstPlaceId;
			return;
		}
	}

	_nextPlaceId = nextPlaceId;
}

// atlantis.exe FUN_004212cc (transition_turn.h): the turn's frames, each
// drawn as the warp loop draws one (place sprites, the view, the guest
// portraits) without input or cursor.  With the CON variable ZOOM set, the
// field of view narrows by 3 degrees a frame from 75.187; the turn ends with
// FUN_0045179a(75.187), which lasts through the video until FUN_0044a424
// sets the warp's 75.137 again, and ZOOM = 0.
void CryOmni3DEngine_Atlantis::turnToTransition(float srcAlpha, float srcBeta) {
	if (!_warpLoaded)
		return;
	const bool zoom = _gameVars.getValOrDefault("zoom", 0) != 0;
	Common::Array<TransitionTurn::Frame> frames;
	float endAlpha, endBeta;
	TransitionTurn::run(Omni3DManagerAtlantis::exePi(), (float)_omni3dMan.getAlpha(),
	                    (float)_omni3dMan.getBeta(), srcAlpha, srcBeta, zoom, frames, endAlpha, endBeta);
	const double savedHFov = _omni3dMan.getHFov();
	showMouse(false);
	for (uint i = 0; i < frames.size() && !shouldAbort(); i++) {
		pollEvents();
		if (frames[i].fovSet)
			_omni3dMan.setHFov((double)frames[i].fov * M_PI / 180.0);
		_omni3dMan.setAlpha((double)frames[i].alpha);
		_omni3dMan.setBeta((double)frames[i].beta);
		animateSprites();
		const Graphics::Surface *view = _omni3dMan.getSurface();
		if (!view)
			continue;
		if (_guestsShown && _guests.count) {
			Graphics::ManagedSurface frame;
			frame.copyFrom(*view);
			drawGuests(frame);
			g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		} else {
			g_system->copyRectToScreen(view->getPixels(), view->pitch, 0, 0, view->w, view->h);
		}
		g_system->updateScreen();
		// [UNKNOWN] the exe's turn has no timer of its own; the port's warp
		// loop pace (10 ms) is used.
		g_system->delayMillis(10);
	}
	_omni3dMan.setHFov(savedHFov);
	showMouse(true);
	_gameVars["zoom"] = 0;
}

void CryOmni3DEngine_Atlantis::playTransitionVideo(const Common::String &videoName,
                                                    uint fromPlace, uint toPlace, uint frameMs) {
#ifdef USE_HNM
	// WAM records use .UBB extension but the BigFile stores them as .HNM.
	Common::String hnmName = videoName;
	{
		const char *dot = strrchr(hnmName.c_str(), '.');
		if (dot && !scumm_stricmp(dot, ".UBB"))
			hnmName = Common::String(hnmName.c_str(), dot - hnmName.c_str()) + ".HNM";
	}

	Common::SeekableReadStream *stream = openBigFileStream(kFileTypeTransition, hnmName);
	if (!stream) {
		debugC(1, kDebugScript, "playTransitionVideo: '%s' not found", hnmName.c_str());
		return;
	}

	// Open SPF transition-sprite streams that apply to this (from, to) pair.
	// Matches the original engine's FUN_0041b81c: scan the SPF table for
	// entries keyed by (current place, target place) and load each.  Streams
	// stay open for the duration of the transition so per-frame decode is
	// cheap; closed afterwards.  A no-op when either place ID is unknown
	// (cinematics played via the single-arg form).
	Common::Array<Common::SeekableReadStream *> spfStreams;
	if (fromPlace != uint(-1) && toPlace != uint(-1)) {
		for (const SpriteTransEntry &e : _spriteTransList) {
			if (e.fromPlace != fromPlace || e.toPlace != toPlace) continue;
			// Honour the hidden-perso list so /set(HidePerso=N) suppresses
			// transition sprites for that NPC just like static sprites.
			bool hidden = false;
			for (int h : _hiddenPersos)
				if (h == e.persoId) { hidden = true; break; }
			if (hidden) continue;
			Common::SeekableReadStream *spf =
			    openBigFileStream(kFileTypeSpriteUbb, e.spfFile);
			if (spf) {
				spfStreams.push_back(spf);
				debugC(2, kDebugScript, "playTransitionVideo: opened SPF '%s' (perso=%d)",
				      e.spfFile.c_str(), e.persoId);
			}
		}
	}

	Graphics::PixelFormat screenFmt = g_system->getScreenFormat();
	Graphics::PixelFormat fmt = (screenFmt.bytesPerPixel == 1)
	    ? Graphics::PixelFormat::createFormatCLUT8()
	    : screenFmt;
	byte *savedPalette = nullptr;
	if (screenFmt.bytesPerPixel == 1) {
		savedPalette = new byte[3 * 256];
		g_system->getPaletteManager()->grabPalette(savedPalette, 0, 256);
	}

	// HNMDecoder takes ownership of savedPalette and (via loadStream) stream.
	Video::HNMDecoder *decoder = new Video::HNMDecoder(fmt, false, savedPalette);
	decoder->setSoundType(Audio::Mixer::kSFXSoundType);
	// Atlantis' HNM6 files have no sound, so the decoder paces them with
	// this delay (it must be set before loadStream).
	if (frameMs)
		decoder->setRegularFrameDelay(frameMs);
	// The game's IX buffers, kept from video to video (RGB565 only: their
	// content is the original's 16-bit pixels).
	if (fmt.bytesPerPixel == 2)
		decoder->setHNM6VideoBuffers(_ixVideoBuffers);
	if (!decoder->loadStream(stream)) {
		delete decoder;
		return;
	}

	decoder->start();

	// FUN_0043259c between the HNM open (FUN_00448c70) and the play loop,
	// for a move (FUN_00423ad8); a cinematic keeps the last steps.
	if (fromPlace != uint(-1) && toPlace != uint(-1))
		ambientPrepareGlide(toPlace, (int32)decoder->getFrameCount());

	// The same-name sound of a cinematic or a start-up video (e.g.
	// WAV\CINEM020.APC for CINEM020.HNM): the Cinema= handler names
	// 0x007728c7 "cinem" + N + ".apc" and sets NEWSOUND = 1 (FUN_0042a100
	// @ 0x0042a7c0..0x0042a881, poller FUN_00432a64); the start-up videos
	// set NEWSOUND = 18 / 19 (logo15n.apc / preintro.apc, 0x00419653 /
	// 0x004196bd).  A move's video (FUN_00423ad8) opens no sound: the 7
	// transition-named APCs (AT099100, AT100099, AT110109, CP026126,
	// CP030130, FO050051, SP026027) are NewSound entries 15..17, 98, 99,
	// 108, 120 that the departure /sel sections play.
	// See atlantis-re: experiments/2026-10-16-08-transition-sounds.md.
	const bool isMove = fromPlace != uint(-1) && toPlace != uint(-1);
	if (!isMove) {
		const char *dot = strrchr(hnmName.c_str(), '.');
		Common::String apcName = dot
		    ? Common::String(hnmName.c_str(), dot - hnmName.c_str()) + ".APC"
		    : hnmName + ".APC";
		Common::SeekableReadStream *apcFile = openBigFileStream(kFileTypeSound, apcName);
		if (apcFile) {
			// makeAPCStream reads the file header; queue remaining bytes as one packet.
			Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
			if (apc) {
				int64 remaining = apcFile->size() - apcFile->pos();
				if (remaining > 0) {
					byte *buf = new byte[remaining];
					apcFile->read(buf, (uint32)remaining);
					apc->queuePacket(new Common::MemoryReadStream(buf, remaining,
					                                              DisposeAfterUse::YES));
				}
				apc->finish();
				_mixer->stopHandle(_newSoundHandle);
				_mixer->playStream(Audio::Mixer::kSFXSoundType, &_newSoundHandle, apc);
				_newSoundLast = 1;
				_newSoundPlaying = true;
				_gameVars["newsound"] = 1;
			}
			delete apcFile;
		}
	}

	uint spfFrameIdx = 0;
	bool firstFrame = true;
	Graphics::ManagedSurface composite;  // per-frame scratch when sprites are active
	// FUN_0041bc74's input: a left click, or Escape with the conditions of
	// FUN_0041a298 (video_skip.h); after that poll the next frame is still
	// presented.  Space is not read.
	VideoSkipState skip;
	skip.bowMode = _bowMode;
	skip.objectInHand = _inventory.selectedObject() != nullptr ||
	                    _gameVars.getValOrDefault("objetenmain", 0) != 0;
	skip.sectionPlay = _sectionPlay;
	// The bar request as it stands: 1 for a Cinema= of a timer run by the
	// bar loop (FUN_0042c8ac), so Escape does not end it then.
	skip.barRequest = _barSession.request;
	bool stopAfterFrame = false, skipped = false;
	uint prevButton = getCurrentMouseButton();
	while (!shouldAbort() && !decoder->endOfVideo()) {
		if (decoder->needsUpdate()) {
			const Graphics::Surface *frame = decoder->decodeNextFrame();
			// FUN_0041bc74 decodes the first frame before its loop; each
			// loop pass decodes the next one, then runs FUN_00432758.
			if (frame && !firstFrame)
				ambientVideoFrame();
			if (frame)
				firstFrame = false;
			if (frame) {
				if (decoder->hasDirtyPalette())
					setPalette(decoder->getPalette(), 0, 256);

				const Graphics::Surface *toBlit = frame;
				// The frame presenter FUN_0041b064 draws the SPF sprites
				// (FUN_004256e8), then the following guests' portraits
				// (FUN_0042bd08 @ 0x0041b192 on a 16-bit frame, FUN_0042bc58
				// @ 0x0041b133 otherwise; both test the shown flag
				// 0x0049613c and 0x00496138 == 0), then presents: the
				// portraits stay over every frame of a move video or a
				// cinematic.
				// See atlantis-re: functions/scripts-guests.md.
				const bool guestsOver = _guestsShown && _guests.count != 0;
				if ((!spfStreams.empty() || guestsOver) && fmt.bytesPerPixel >= 2) {
					// Composite the SPF sprites for this frame into a
					// throwaway copy of the video frame.  The SPF pixel
					// stream is RGB565, so this only happens when the
					// target surface is at least 16bpp — at 8bpp the
					// formats don't mix and the raw video shows instead.
					if (composite.w != frame->w || composite.h != frame->h
					 || composite.format != fmt) {
						composite.free();
						composite.create(frame->w, frame->h, fmt);
					}
					const uint rowBytes = (uint)frame->w * fmt.bytesPerPixel;
					for (int row = 0; row < frame->h; ++row)
						memcpy(composite.getBasePtr(0, row),
						       frame->getBasePtr(0, row), rowBytes);
					for (Common::SeekableReadStream *spf : spfStreams)
						compositeSpfFrame(spf, spfFrameIdx, *composite.surfacePtr());
					if (guestsOver)
						drawGuests(composite);
					toBlit = composite.surfacePtr();
				}

				g_system->copyRectToScreen(toBlit->getPixels(), toBlit->pitch,
				                           0, 0, toBlit->w, toBlit->h);
				g_system->updateScreen();
				++spfFrameIdx;
				if (stopAfterFrame)
					break;   // the frame FUN_0041bc74 presents at 0x0041be2e
			}
		}
		pollEvents();
		if (!stopAfterFrame) {
			const uint button = getCurrentMouseButton();
			const bool left = button == 1 && prevButton != 1;
			const bool right = button == 2 && prevButton != 2;
			prevButton = button;
			const bool escape = checkKeysPressed(1, Common::KEYCODE_ESCAPE);
			if (videoSkipPoll(skip, left, right, escape))
				stopAfterFrame = skipped = true;
		}
		g_system->delayMillis(10);
	}
	clearKeys();
	// A video that ends by itself leaves its sound playing to its end
	// (FUN_0042a100 @ 0x0042a907..0x0042a954 stops nothing); one cut short
	// fades it out, 2 volume steps every 25 ms from the effects volume
	// (FUN_0041bc74 @ 0x0041be69..0x0041be9e arms 0x00495eb0 = 2).
	if (skipped && videoSkipFades(skip, frameMs == kCinemaFrameMs) &&
	        _mixer->isSoundHandleActive(_newSoundHandle))
		fadeOutNewSound();
	delete decoder;
	for (Common::SeekableReadStream *spf : spfStreams)
		delete spf;
	composite.free();
	BarSessionRules::flagsReset(_barSession);   // 0x0041bf5b..0x0041bf8d
#else
	debugC(1, kDebugScript, "playTransitionVideo: %s (HNM not compiled)", videoName.c_str());
#endif
}

void CryOmni3DEngine_Atlantis::fadeOutNewSound() {
	const int sfx = ConfMan.hasKey("sfx_volume") ? ConfMan.getInt("sfx_volume") : Audio::Mixer::kMaxMixerVolume;
	Common::StackLock lock(_musicFade.mutex);
	_musicFade.sfxVol0 = MAX(1, sfx * 127 / Audio::Mixer::kMaxMixerVolume);
	_musicFade.sfxVol = _musicFade.sfxVol0;
	_musicFade.sfxLast = _musicFade.ticks;
	_musicFade.sfxMode = 2;
}

// FUN_004315b4: the WAM's file reference 2 (by position) names the centre
// table, `sprlist\` + name; entries are added to the table, never cleared.
void CryOmni3DEngine_Atlantis::loadAmbientCentres() {
	const Common::Array<Common::String> &refs = _wam.getFileRefs();
	if (refs.size() < 3 || refs[2].empty())
		return;
	Common::ScopedPtr<Common::SeekableReadStream> f(openBigFileStream(kFileTypeSprite, refs[2]));
	if (!f) {
		warning("Atlantis: cannot open SPRLIST\\%s", refs[2].c_str());
		return;
	}
	Common::Array<byte> buf((uint)f->size());
	f->read(buf.data(), buf.size());
	parseCentreVue(buf.data(), buf.size(), _ambientCentres);
}

namespace {

uint32 ambientClock(void *) {
	return g_system->getMillis();   // timeGetTime - 0x00bcb5b8 (FUN_00451d0c)
}

struct AmbientLoadCtx {
	CryOmni3DEngine_Atlantis *engine;
	AtlantisAmbientOutput *out;
	const Common::Array<AtlantisAmbientObject> *objects;
};

} // End of anonymous namespace

bool CryOmni3DEngine_Atlantis::ambientLoadVariant(void *ctx, uint object, uint variant) {
	AmbientLoadCtx *c = (AmbientLoadCtx *)ctx;
	const Common::String &name = (*c->objects)[object].variants[variant];
	Common::ScopedPtr<Common::SeekableReadStream> f(c->engine->openBigFileStream(kFileTypeSound, name));
	if (!f || !c->out->load(object, variant, *f)) {
		warning("Atlantis: cannot load the ambient sound WAV\\%s", name.c_str());
		return false;
	}
	return true;
}

// FUN_00431a1c, at the start of the warp loop FUN_0041bfa0 (0x0041c012).
void CryOmni3DEngine_Atlantis::ambientEnterPlace() {
	const AtlantisPlace *place = _wam.findPlaceById((uint16)_currentPlaceId);
	if (!place)
		return;
	milesSamples();
	AmbientLoadCtx ctx = { this, _ambientOut, &_wam.getAmbientObjects() };
	_ambient.enterPlace(*_ambientOut, *place, _wam.getAmbientObjects(), &ambientLoadVariant, &ctx);
}

// FUN_00432178's ambient part, at the end of a warp or bar frame.
void CryOmni3DEngine_Atlantis::ambientFrame() {
	pollNewSound();   // FUN_00432178's tail calls FUN_00432a64 in every frame
	if (!_ambientOut || !_ambient.active())
		return;
	const AtlantisPlace *place = _wam.findPlaceById((uint16)_currentPlaceId);
	if (!place)
		return;
	// 0x00554510 + 12 * place id: zero when the table has no entry
	AtlantisCentrePos cam = { 0.0f, 0.0f, 0.0f };
	if (_ambientCentres.contains((int32)_currentPlaceId))
		cam = _ambientCentres[(int32)_currentPlaceId];
	_ambient.frame(*_ambientOut, *place, _wam.getAmbientObjects(), cam,
	               (float)_omni3dMan.getAlpha(), (float)_omni3dMan.getBeta(),
	               _conClocks.chapterTime(g_system->getMillis()));
}

// FUN_0043259c, called by FUN_00423ad8 after the HNM is opened and before
// FUN_0041bc74: the steps toward the gains at the destination's camera
// (0x00554510 + 12 * [0x006cd7b8]) for the sounds of the current place
// record 0x006cd7a8.  Not gated by 0x00496980.
void CryOmni3DEngine_Atlantis::ambientPrepareGlide(uint toPlace, int32 frames) {
	if (!_ambientOut || frames <= 0)
		return;
	const AtlantisPlace *place = _wam.findPlaceById((uint16)_currentPlaceId);
	if (!place)
		return;
	AtlantisCentrePos cam = { 0.0f, 0.0f, 0.0f };
	if (_ambientCentres.contains((int32)toPlace))
		cam = _ambientCentres[(int32)toPlace];
	_ambient.prepareGlide(*place, _wam.getAmbientObjects(), cam,
	                      (float)_omni3dMan.getAlpha(), (float)_omni3dMan.getBeta(), frames);
}

// FUN_00432758, called by the HNM play loop FUN_0041bc74 on each frame of
// its loop (every video, cinematics included, with the last steps).  The
// port only runs it when the place's sounds are loaded (0x00496980), which
// is the case whenever the current place has any.
void CryOmni3DEngine_Atlantis::ambientVideoFrame() {
	if (!_ambientOut || !_ambient.active())
		return;
	const AtlantisPlace *place = _wam.findPlaceById((uint16)_currentPlaceId);
	if (!place)
		return;
	_ambient.videoFrame(*_ambientOut, *place, _wam.getAmbientObjects(),
	                    _conClocks.chapterTime(g_system->getMillis()));
}

// The six Miles samples (FUN_00430bf0 / FUN_00430d20 at start-up).
AtlantisAmbientOutput &CryOmni3DEngine_Atlantis::milesSamples() {
	if (!_ambientOut) {
		_ambientOut = new AtlantisAmbientOutput(_mixer);
		_ambient.setClock(&ambientClock, nullptr);
	}
	return *_ambientOut;
}

// FUN_00431e98, called when 0x00496980 == 1: AIL_stop_sample on the samples
// of the place's records only, so a sample that was playing reads 8 and the
// puzzles' players skip it (miles_samples.h); the others keep their status.
void CryOmni3DEngine_Atlantis::stopAmbient() {
	if (!_ambientOut)
		return;
	if (_ambient.active())
		_ambient.stopAll(*_ambientOut);
}

void CryOmni3DEngine_Atlantis::recordPlaceTable() {
	const Common::Array<AtlantisPlace> &places = _wam.getPlaces();
	if (_placeTableSlots.size() < places.size())
		_placeTableSlots.resize(places.size());
	for (uint r = 0; r < places.size(); r++) {
		const Common::Array<AtlantisTransition> &t = places[r].transitions;
		Common::Array<AtlantisTransition> &slots = _placeTableSlots[r];
		if (slots.size() < t.size())
			slots.resize(t.size());
		for (uint i = 0; i < t.size(); i++)
			slots[i] = t[i];
	}
}

AtlantisTransition CryOmni3DEngine_Atlantis::placeTableEntry(uint record, uint i) const {
	if (record < _placeTableSlots.size() && i < _placeTableSlots[record].size())
		return _placeTableSlots[record][i];
	// Never written in this run: the zeroed table of a fresh start (empty
	// name, destination 0, angles 0).
	AtlantisTransition t;
	t.dstPlaceId = 0;
	t.flags = 0;
	t.srcAlpha = t.srcBeta = t.dstAlpha = t.dstBeta = 0.0f;
	return t;
}

void CryOmni3DEngine_Atlantis::transitionAfterDepartureVue(uint transitionIndex) {
	// atlantis.exe FUN_00423ad8 after a departure section that ran Vue=N
	// without AbortDepart (CHAPI062 /864; atlantis-re
	// experiments/2026-10-15-08-chapi062-864.md): the Vue= handler has
	// already pointed the place record 0x006cd7a8 at N and loaded its
	// panorama, and the loop goes on with the index i of the clicked
	// transition of the place left: TESTTIME1 and Time1 restart
	// (@ 0x00423e8d), the turn FUN_004212cc(i), then the video
	// "ubb_vue\" + the name of entry i of N's record, a stale slot past N's
	// transition count.  An empty name has no '.', so FUN_00422f2c returns -1
	// and no video plays (the fresh-start case).  FUN_00421b4c then enters N
	// again.
	_gameVars["testtime1"] = 0;
	_conClocks.time1Start = g_system->getMillis();
	_conClocks.time1Offset = 0;
	if (!_currentPlace)
		return;
	const uint record = uint(_currentPlace - _wam.getPlaces().begin());
	const AtlantisTransition t = placeTableEntry(record, transitionIndex);
	turnToTransition(t.srcAlpha, t.srcBeta);
	if (t.videoName.contains('.'))
		playTransitionVideo(t.videoName, _currentPlaceId, _currentPlaceId, kTransitionFrameMs);
	_omni3dMan.setAlpha(t.dstAlpha);
	_omni3dMan.setBeta(t.dstBeta);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
