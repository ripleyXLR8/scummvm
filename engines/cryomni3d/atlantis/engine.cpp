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

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/func.h"
#include "common/events.h"
#include "common/file.h"
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "engines/util.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

// BigFile sub-directory for each FileType (see openBigFileStream()).  Indexed
// by the FileType enum; names are stored uppercase as in the DOS archive.
static const char *const kBigFileSubdirs[] = {
	"CYCLO\\",         // kFileTypeCyclo
	"UBB_VUE\\",       // kFileTypeTransition
	"DIALOG\\",        // kFileTypeDialog
	"SYC\\",           // kFileTypeSync
	"FONTS\\",         // kFileTypeFont
	"SPRLIST\\",       // kFileTypeSprite
	"WAM\\",           // kFileTypeWAM
	"OBJETS\\",        // kFileTypeObject
	"MENU\\",          // kFileTypeMenu
	"SCENAR\\",        // kFileTypeScript
	"WAV\\",           // kFileTypeSound
	"SPRITE\\CYCLO\\", // kFileTypeSpriteCyclo
	"SPRITE\\UBB\\",   // kFileTypeSpriteUbb
	"SPRITE\\2D\\",    // kFileTypeSprite2D
	"IMAGES\\",        // kFileTypeImages
	"PUZZLES\\",       // kFileTypePuzzles
};

CryOmni3DEngine_Atlantis::CryOmni3DEngine_Atlantis(OSystem *syst,
        const CryOmni3DGameDescription *gamedesc) :
	CryOmni3DEngine(syst, gamedesc),
	_currentCONChapter(0),
	_chapterStartMs(0),
	_currentChapter(uint(-1)),
	_currentPlaceId(uint(-1)),
	_nextPlaceId(uint(-1)),
	_currentPlace(nullptr),
	_isPlaying(false),
	_abortCommand(kAbortQuit),
	_loadedSave(0),
	_autosavePending(false),
	_conRng("AtlantisConRandom"),
	_heroPersoId(0),
	_dialRng("AtlantisDialCam"),
	_lastDialCam(0),
	_dialBaseAlpha(0.0),
	_dialBaseAlphaValid(false),
	_dialogGivenObjSprite(-1),
	_warpCursorGroup(0),
	_warpCursorFrame(-1),
	_warpCursorNextMs(0),
	_mainPalette(nullptr),
	_warpLoaded(false),
	_propAnimFrames(0),
	_propAnimFrame(0),
	_propAnimNextMs(0) {
	_placeVisits.resize(1500, 0);
}

CryOmni3DEngine_Atlantis::~CryOmni3DEngine_Atlantis() {
	_cycloSurface.free();
	_warpSurface.free();
	delete[] _mainPalette;
}

bool CryOmni3DEngine_Atlantis::hasFeature(EngineFeature f) const {
	// No kSupportsSavingDuringRuntime: Atlantis autosaves on chapter
	// transitions and offers no manual save anywhere — not even via the
	// ScummVM global menu.  Loading is still permitted.
	return CryOmni3DEngine::hasFeature(f)
	       || (f == kSupportsLoadingDuringRuntime);
}

Common::String CryOmni3DEngine_Atlantis::getSaveStateName(int slot) const {
	return Common::String::format("atlantis.%04d", slot + 1);
}

Common::Error CryOmni3DEngine_Atlantis::loadGameState(int slot) {
	if (loadGame((uint)slot + 1)) {
		// Defer the place re-entry to the game loop: loadGame() set _nextPlaceId,
		// so abort the current step and re-run it.
		_abortCommand = kAbortLoadGame;
		_loadedSave = (uint)slot + 1;
		return Common::kNoError;
	}
	return Common::kReadingFailed;
}

Common::Error CryOmni3DEngine_Atlantis::saveGameState(int slot, const Common::String &desc,
        bool isAutosave) {
	(void)isAutosave;
	saveGame((uint)slot + 1, desc);
	return Common::kNoError;
}

Common::Path CryOmni3DEngine_Atlantis::getFilePath(FileType fileType,
        const Common::String &baseName) const {
	// Files outside the BigFile (none currently); placeholder for future expansion.
	return Common::Path(baseName);
}

Common::SeekableReadStream *CryOmni3DEngine_Atlantis::openBigFileStream(FileType fileType,
        const Common::String &baseName) const {
	uint typeIdx = (uint)fileType;
	if (typeIdx >= ARRAYSIZE(kBigFileSubdirs))
		return nullptr;

	Common::String fullName = kBigFileSubdirs[typeIdx];
	fullName += baseName;
	// The BigFile stores names case-sensitively as they appear in the DOS archive.
	// Convert to uppercase to match archive entries.
	fullName.toUppercase();

	return _bigFile.createReadStreamForMember(fullName);
}

void CryOmni3DEngine_Atlantis::setupPalette(const byte *colors, uint start, uint num) {
	// Atlantis runs in 16bpp true colour; the base palette helpers are
	// no-ops in that mode (see CryOmni3DEngine::setPalette).
	setPalette(colors, start, num);
}

bool CryOmni3DEngine_Atlantis::showSubtitles() const {
	return ConfMan.getBool("subtitles");
}

void CryOmni3DEngine_Atlantis::refreshInventoryToolbar(uint changedSlot) {
	// Keep the toolbar's visible slot window in sync when an item is added or
	// removed (it pages forward when a new item lands outside the window).
	_toolbar.inventoryChanged(changedSlot);
}

void CryOmni3DEngine_Atlantis::makeTranslucent(Graphics::Surface &dst,
        const Graphics::Surface &src) const {
	// Translucency mapping is paletted-only; unused by the 16bpp skeleton.
	(void)dst;
	(void)src;
}

bool CryOmni3DEngine_Atlantis::loadCycloHNM(const Common::String &name) {
#ifdef USE_HNM
	Common::SeekableReadStream *stream = openBigFileStream(kFileTypeCyclo, name);
	if (!stream && !name.hasSuffix(".hnm") && !name.hasSuffix(".HNM")) {
		// Some WAM entries use .tga extension but the archive only has .hnm
		Common::String hnmName = name;
		const char *dot = strrchr(hnmName.c_str(), '.');
		if (dot) {
			hnmName = Common::String(hnmName.c_str(), dot - hnmName.c_str()) + ".hnm";
			stream = openBigFileStream(kFileTypeCyclo, hnmName);
		}
	}
	if (!stream) {
		warning("Cannot open cyclo HNM: %s", name.c_str());
		return false;
	}

	// HNM6 panoramas are 16-bit true color; use the screen pixel format.
	// HNMDecoder takes ownership of the stream when loadStream() is called.
	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Video::HNMDecoder *decoder = new Video::HNMDecoder(fmt);

	// loadStream takes ownership of stream immediately (even on failure).
	if (!decoder->loadStream(stream)) {
		warning("Cannot load cyclo HNM: %s", name.c_str());
		delete decoder;
		return false;
	}

	decoder->start();

	const Graphics::Surface *frame = nullptr;
	while (!frame && !decoder->endOfVideo()) {
		if (decoder->needsUpdate())
			frame = decoder->decodeNextFrame();
		else
			decoder->decodeNextFrame();
	}

	if (!frame) {
		warning("No frames in cyclo HNM: %s", name.c_str());
		delete decoder;
		return false;
	}

	_cycloSurface.free();
	_cycloSurface.copyFrom(*frame);
	_warpSurface.free();
	_warpSurface.copyFrom(*frame);

	if (decoder->hasDirtyPalette())
		memcpy(_mainPalette, decoder->getPalette(), 3 * 256);

	delete decoder;
	_warpLoaded = true;
	_propAnimFrames = 0;
	_propAnimFrame  = 0;
	_propAnimNextMs = 0;
	debugC(1, kDebugScript, "loadCycloHNM: %s (%dx%d)", name.c_str(), _warpSurface.w, _warpSurface.h);
	return true;
#else
	warning("HNM support not compiled in; cannot load cyclo %s", name.c_str());
	return false;
#endif
}

Common::Error CryOmni3DEngine_Atlantis::run() {
	CryOmni3DEngine::run();

	// Open the game's data archives.  The full game spans four discs
	// (BIGCD1-4.BIG), all merged into one index.  Disc 1 is required; discs
	// 2-4 are mounted if present.
	Common::Path bigFilePath("BIGCD1.BIG");
	if (!_bigFile.open(bigFilePath)) {
		bigFilePath = Common::Path("bigcd1.big");
		if (!_bigFile.open(bigFilePath))
			return Common::Error(Common::kPathDoesNotExist, "Cannot open BIGCD1.BIG");
	}
	for (int disc = 2; disc <= 4; disc++) {
		if (!_bigFile.open(Common::Path(Common::String::format("BIGCD%d.BIG", disc)))
		        && !_bigFile.open(Common::Path(Common::String::format("bigcd%d.big", disc))))
			warning("Atlantis: BIGCD%d.BIG not found - its chapters will be unavailable", disc);
	}
	debugC(1, kDebugScript, "Atlantis: data archives mounted, %u files total", _bigFile.numFiles());

	// Register with SearchMan so Common::File::open() can find archive files.
	SearchMan.add("Atlantis_BigFile", &_bigFile, 0, false);

	_omni3dMan.init(75. / 180. * M_PI);
	_mainPalette = new byte[3 * 256]();

	// Atlantis uses HNM6 16-bit true colour panoramas.
	Graphics::PixelFormat screenFmt(2, 5, 6, 5, 0, 11, 5, 0, 0); // RGB565
	initGraphics(640, 480, &screenFmt);
	_omni3dMan.setPixelFormat(screenFmt);

	// Per-character dialog resource table (speaker → UBB/cam basenames).
	loadDialInitData();

	// Subject-menu icon sprites (shared by the NPC topic menu).
	loadSubjectSprites();

	// Animated warp-view / arrow cursors.
	loadCursorSprites();

	// FONTMAX gold menu font.
	loadFontMaxSprites();

	// Inventory: extract the exe object table, build the object catalogue and
	// load the item icons.  20 carry slots; the toolbar refresh callback is
	// wired when the toolbar lands.
	loadAtlantisExeTables();
	setupObjects();
	loadObjectSprites();
	loadInventoryBarSprite();
	_inventory.init(20, new Common::Functor1Mem<uint, void, CryOmni3DEngine_Atlantis>(
	                        this, &CryOmni3DEngine_Atlantis::refreshInventoryToolbar));

	// The inventory bar: the OBJETS0.SPR background is added as the toolbar's
	// only menu sprite, left edge centred at x=20 (the sprite carries
	// xoff=-20, so tx=0 yields draw_x=20).  Item icons composite on top.
	_toolbar.init(&_inventory, this);
	if (!_inventoryBarSprite.pixels.empty()) {
		const SubjSprite &s = _inventoryBarSprite;
		_toolbar.addMenuSprite(s.w, s.h, s.xoff, s.yoff, kSprTransp,
		                       0, s.yoff, s.pixels.data(), s.blend.data());
	}

	// Enter chapter 1: load its WAM navigation map and CON scenario script,
	// then run the warp-navigation loop.  (CON arrival/departure section
	// evaluation, the /set interpreter, transition videos, NPC dialog and the
	// warp-cursor sprites land in later commits.)
	changeChapter(1);
	_canLoadSave = true;

	_isPlaying = true;
	// Outer state machine: gameStep() returns when something sets _abortCommand.
	// A load-game request (from the GMM, or a game-over checkpoint reload) has
	// already restored the state and set _nextPlaceId, so we clear the abort and
	// re-enter the loop, which steps into the loaded place.  gameStep() always
	// runs at least once (do/while, not a shouldQuit()-gated head test).
	_abortCommand = kAbortNoAbort;
	do {
		gameStep();
		if (_abortCommand == kAbortLoadGame) {
			_abortCommand = kAbortNoAbort;
			continue;
		}
		break;   // quit / unhandled abort
	} while (!shouldQuit());

	SearchMan.remove("Atlantis_BigFile");
	return Common::kNoError;
}

void CryOmni3DEngine_Atlantis::gameStep() {
	while (!_abortCommand) {
		// Enter a pending place (set by changeChapter, a zone transition or a
		// CON /set(vue=N)), then run its arrival /sel sections — which may
		// chain on by setting _nextPlaceId again (the chapter intro flow).
		if (_nextPlaceId != uint(-1)) {
			uint fromVue = _currentPlaceId;
			doPlaceChange();
			if (_currentPlace)
				runArrivalSelSections((int)_currentPlaceId, (int)fromVue);
		}
		if (_nextPlaceId != uint(-1))
			continue;

		uint actionId = handleWarp();

		// 66666 = handleWarp bailed (abort / menu / window close).
		if (actionId == 66666) {
			if (_abortCommand || shouldAbort())
				break;
			continue;
		}

		// A zone action in the place-id range is a navigation click: run the
		// departure /sel sections (which may abort the move by clearing
		// _nextPlaceId, or redirect it via /set(vue=N)), then transition.
		if (actionId >= 1 && actionId < 10000) {
			_nextPlaceId = actionId;
			runDepartureSelSections((int)actionId);
			if (!shouldAbort() && _nextPlaceId != uint(-1)) {
				uint dest = _nextPlaceId;
				_nextPlaceId = uint(-1);
				executeTransition(dest);
			} else {
				_nextPlaceId = uint(-1);
			}
		}
	}
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
