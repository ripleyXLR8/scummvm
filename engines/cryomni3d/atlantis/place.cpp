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

#include "common/debug.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "audio/mixer.h"
#include "audio/decoders/apc.h"
#include "graphics/paletteman.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"

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

// Chapter initial states: placeId, alpha (horizontal angle), beta (vertical angle).
const LevelInitialState CryOmni3DEngine_Atlantis::kChapterInitialStates[] = {
	{  1,  0.f,  0.f },  // chapter 1 — ATLAN1.WAM, first place id=1
	{ 22,  0.f,  0.f },  // chapter 2 — ATLAN2.WAM, first place id=22
	{ 58,  0.f,  0.f },  // chapter 3 — ATLAN3.WAM, first place id=58
	{  1,  0.f,  0.f },  // chapter 4 — ATLAN4.WAM, first place id unknown
	{  1,  0.f,  0.f },  // chapter 5 — ATLAN5.WAM, first place id unknown
};

void CryOmni3DEngine_Atlantis::setupChapterWAM(int chapter) {
	if (chapter < 1 || chapter >= (int)ARRAYSIZE(kWAMNames) || !kWAMNames[chapter])
		error("No WAM for chapter %d", chapter);

	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeWAM, kWAMNames[chapter]));
	if (!s)
		error("Cannot open WAM file for chapter %d (%s)", chapter, kWAMNames[chapter]);

	_wam.loadStream(*s);
	_currentWAMName = kWAMNames[chapter];
	debugC(1, kDebugScript, "Loaded WAM for chapter %d: %u places", chapter, _wam.getPlaces().size());

	// Per-chapter NPC / prop scene-sprite list (SPW) and the transition
	// walk-on list (SPF).
	loadSpritePlaceList();
	loadSpfList();
	// The carried inventory persists across chapters; only a new game (chapter 1)
	// starts empty.  The CON /INIT then repopulates chapter 1's starting items.
	if (chapter == 1)
		_inventory.clear();

	if ((uint)chapter != _currentCONChapter) {
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
	_currentWAMName = wamName;
	// Each WAM references its own SPW/SPF lists; reload them so the new
	// area's NPCs composite.  (Inventory and CON script are untouched by a
	// sub-WAM swap.)
	loadSpritePlaceList();
	loadSpfList();
	debugC(1, kDebugScript, "Loaded WAM '%s': %u places", wamName.c_str(), _wam.getPlaces().size());
}

void CryOmni3DEngine_Atlantis::changeChapter(int chapter) {
	_currentChapter = (uint)chapter;
	musicStop();   // a new chapter's CON re-selects its music via startmusik
	// Arm the chapter checkpoint; doPlaceChange writes it once the start place
	// is entered (the original autosaves as it enters a new chapter's place).
	_autosavePending = true;

	// setupChapterWAM loads the CON and runs its /INIT block, which may set the
	// start place via /set(vue=N) (e.g. chapter 1 opens on the place-450
	// cinematic).  Clear _nextPlaceId first so we can detect that.
	_nextPlaceId = uint(-1);
	setupChapterWAM(chapter);

	const LevelInitialState &init = kChapterInitialStates[chapter - 1];
	if (_nextPlaceId == uint(-1))
		_nextPlaceId = init.placeId;   // fall back to the hardcoded start
	_omni3dMan.setAlpha(init.alpha);
	_omni3dMan.setBeta(init.beta);
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
	_dialogGivenObjSprite = -1;   // any pending given-item overlay ends here

	// Load the panoramic cyclo HNM and point the viewer at it.
	if (loadCycloHNM(nextPlace->cycloHnm)) {
		_omni3dMan.setSourceSurface(&_warpSurface);
		setupPalette(_mainPalette, 0, 256);
		// Composite the place's standing NPCs / props onto the warp surface.
		compositeNPCSprites();
	}

	// Write the chapter checkpoint once, on entering the chapter's first place.
	if (_autosavePending) {
		_autosavePending = false;
		autosave();
	}

	// Full 360-degree horizontal rotation; vertical tilt limited to +/-45 degrees.
	_omni3dMan.setBetaMinConstraint(-M_PI / 4.);
	_omni3dMan.setBetaMaxConstraint( M_PI / 4.);
	setMousePos(Common::Point(320, 240));

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
		playTransitionVideo(trans->videoName, _currentPlaceId, nextPlaceId);
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
			_currentPlace   = nullptr;
			_currentPlaceId = uint(-1);
			_nextPlaceId    = dstPlaceId;
			return;
		}
	}

	_nextPlaceId = nextPlaceId;
}

void CryOmni3DEngine_Atlantis::playTransitionVideo(const Common::String &videoName,
                                                    uint fromPlace, uint toPlace) {
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
	if (!decoder->loadStream(stream)) {
		delete decoder;
		return;
	}

	decoder->start();

	// Play matching APC audio if present (e.g. WAV\CINEM020.APC for
	// CINEM020.HNM).  AT*.HNM place-to-place transitions are intentionally
	// silent — they have no APC on the disc.
	Audio::SoundHandle apcHandle;
	{
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
				_mixer->playStream(Audio::Mixer::kSFXSoundType, &apcHandle, apc);
			}
			delete apcFile;
		}
	}

	uint spfFrameIdx = 0;
	Graphics::Surface composite;  // per-frame scratch when sprites are active
	while (!shouldAbort() && !decoder->endOfVideo()) {
		if (decoder->needsUpdate()) {
			const Graphics::Surface *frame = decoder->decodeNextFrame();
			if (frame) {
				if (decoder->hasDirtyPalette())
					setPalette(decoder->getPalette(), 0, 256);

				const Graphics::Surface *toBlit = frame;
				if (!spfStreams.empty() && fmt.bytesPerPixel >= 2) {
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
						compositeSpfFrame(spf, spfFrameIdx, composite);
					toBlit = &composite;
				}

				g_system->copyRectToScreen(toBlit->getPixels(), toBlit->pitch,
				                           0, 0, toBlit->w, toBlit->h);
				g_system->updateScreen();
				++spfFrameIdx;
			}
		}
		pollEvents();
		if (checkKeysPressed(1, Common::KEYCODE_SPACE))
			break;
		g_system->delayMillis(10);
	}
	_mixer->stopHandle(apcHandle);
	delete decoder;
	for (Common::SeekableReadStream *spf : spfStreams)
		delete spf;
	composite.free();
#else
	debugC(1, kDebugScript, "playTransitionVideo: %s (HNM not compiled)", videoName.c_str());
#endif
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
