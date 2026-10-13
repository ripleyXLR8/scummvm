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
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "engines/util.h"

#include "audio/mixer.h"
#include "audio/decoders/apc.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/omni3d.h"

#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"

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
	_currentChapter(uint(-1)),
	_currentPlaceId(uint(-1)),
	_nextPlaceId(uint(-1)),
	_currentPlace(nullptr),
	_isPlaying(false),
	_abortCommand(kAbortQuit),
	_loadedSave(0),
	_conInitRunning(false),
	_autosavePending(false),
	_currentPlayer(0),
	_heroPersoId(0),
	_dialRng("AtlantisDialCam"),
	_lastDialCam(0),
	_dialBaseAlpha(0.0),
	_dialBaseAlphaValid(false),
	_dialogGivenObjSprite(-1),
	_musicCurrentFile(nullptr),
	_musicRawData(nullptr),
	_musicRawSize(0),
	_musicTrackId(0),
	_warpCursorShown(kWarpCursorUnset),
	_warpCursorNextMs(0),
	_mainPalette(nullptr),
	_warpLoaded(false) {
	memset(_spriteAnimLast, 0, sizeof(_spriteAnimLast));
	_spriteAnimCurrent.resize(512, 0);
	_placeVisits.resize(1500, 0);
	_conPlayCounters.resize(kConPlayCounterCount, 0);
	for (int i = 0; i < 4; i++)
		_sonMenuKnobX[i] = 0;
}

const Graphics::Font *CryOmni3DEngine_Atlantis::subtitleFont() {
	if (!_subtitleFontTried) {
		_subtitleFontTried = true;
#ifdef USE_FREETYPE2
		_subtitleTTF = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", 16,
		                                                 Graphics::kTTFSizeModeCell);
#endif
		if (!_subtitleTTF)
			debugC(1, kDebugScript, "subtitleFont: LiberationSans unavailable, using the GUI font");
	}
	if (_subtitleTTF)
		return _subtitleTTF;
	const Graphics::Font *f = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	return f ? f : FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
}

CryOmni3DEngine_Atlantis::~CryOmni3DEngine_Atlantis() {
	delete _subtitleTTF;
	_mixer->stopHandle(_musicHandle);
	delete[] _musicRawData;
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
	// FUN_0042de5c sets LastVue to 1 (0x0042e285).
	_lastVue = 1;
	// The slot's block identifies its owning player.
	_currentPlayer = slot / (int)kPlayerSlotStride;
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

void CryOmni3DEngine_Atlantis::syncSoundSettings() {
	CryOmni3DEngine::syncSoundSettings();

	// Scale every channel by the SONMENU "Volume" master slider, which the base
	// class knows nothing about.  Absent key → full volume (no-op).
	const int maxVol = Audio::Mixer::kMaxMixerVolume;
	int master = ConfMan.hasKey("atlantis_master_volume")
	             ? ConfMan.getInt("atlantis_master_volume") : maxVol;
	master = CLIP(master, 0, maxVol);
	static const Audio::Mixer::SoundType kTypes[] = {
		Audio::Mixer::kMusicSoundType,
		Audio::Mixer::kSFXSoundType,
		Audio::Mixer::kSpeechSoundType,
	};
	for (int i = 0; i < 3; i++)
		_mixer->setVolumeForSoundType(kTypes[i],
		    _mixer->getVolumeForSoundType(kTypes[i]) * master / maxVol);
}

bool CryOmni3DEngine_Atlantis::shouldAbort() {
	if (g_engine->shouldQuit()) {
		_abortCommand = kAbortQuit;
		return true;
	}
	return _isPlaying && _abortCommand != kAbortNoAbort;
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

	_omni3dMan.init((double)kWarpFovDeg / 180. * M_PI);
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

	// Menu / credit fonts and the SPRMENU decorative overlays.
	loadFontMaxSprites();
	loadCreditFontSprites();
	loadMenuSprites();

	// Episode checkpoint names + player profiles (per-episode save backend).
	loadEpisodeNames();
	loadPlayers();

	// Inventory: extract the exe object table, build the object catalogue and
	// load the item icons.  20 carry slots; the toolbar refresh callback is
	// wired when the toolbar lands.
	loadAtlantisExeTables();
	setupObjects();
	loadObjectSprites();
	loadFontSpriteSet("SUIVEUR.SPR", _guestSprites);   // guest portraits (FUN_00427344)
	loadInventoryBarSprite();
	_inventory.init(20, new Common::Functor1Mem<uint, void, CryOmni3DEngine_Atlantis>(
	                        this, &CryOmni3DEngine_Atlantis::refreshInventoryToolbar));

	// The inventory bar: the OBJETS0.SPR background is added as the toolbar's
	// only menu sprite, hotspot at (0, 443) as in the original.  Item icons
	// composite on top.
	_toolbar.init(&_inventory, this);
	if (!_inventoryBarSprite.pixels.empty()) {
		const SubjSprite &s = _inventoryBarSprite;
		_toolbar.addMenuSprite(s.w, s.h, s.xoff, s.yoff,
		                       0, Atlantis_Toolbar::kBarHotspotY - Atlantis_Toolbar::kBarSurfaceTop,
		                       s.pixels.data(), s.blend.data());
	}

	_canLoadSave = true;

	// Outer state machine.  nextStep 0 = show the main menu; 27 = new game,
	// 28 = load (_loadedSave set), 40 = quit.  gameStep() returns when an
	// _abortCommand fires; the switch maps that back onto the next step.
	// If launched directly onto a save slot (ScummVM "play" with a save),
	// skip the menu and load it.
	uint nextStep = 0;
	int bootSlot = ConfMan.getInt("save_slot");
	// Cryo logo and pre-intro cinematics before the player screen (skipped
	// when launched directly into a save).
	if (bootSlot < 0) {
		playTransitionVideo("LOGO15N.HNM");
		playTransitionVideo("PREINTRO.HNM");
	}
	if (bootSlot >= 0) {
		// Launched directly into a save from the ScummVM launcher: skip the
		// player screen and take the player from the save slot's block.
		_currentPlayer = bootSlot / (int)kPlayerSlotStride;
		_loadedSave = (uint)bootSlot + 1;
		nextStep = 28;
	}
	bool stopGame = false;
	while (!stopGame && !shouldQuit()) {
		if (nextStep == 0) {
			_isPlaying = false;
			// Original-style startup player select / create / delete screen.
			// An existing player resumes their current episode (the latest
			// per-chapter checkpoint); a brand-new player starts a new game.
			// Either way the game begins at once — there is no New/Load/Quit
			// menu in the original.
			int chosen = displayPlayerScreen();
			if (chosen < 0)
				break;                    // quit (Escape / window close)
			_currentPlayer = chosen;
			uint resume = playerResumeSave((uint)chosen);
			if (resume) {
				_loadedSave = resume;
				nextStep = 28;
			} else {
				nextStep = 27;
			}
		}
		if (nextStep == 40 || shouldQuit())
			break;                        // quit

		_abortCommand = kAbortNoAbort;
		_isPlaying = true;
		if (nextStep == 28)
			loadGame(_loadedSave);        // load: restores state + _nextPlaceId
		else
			changeChapter(1);             // new game (also the fallback)

		gameStep();

		switch (_abortCommand) {
		case kAbortLoadGame:
			// In-game load / game-over checkpoint reload: _loadedSave is set.
			nextStep = 28;
			break;
		case kAbortNewGame:
			nextStep = 27;
			break;
		case kAbortQuit:
			stopGame = true;
			break;
		case kAbortFinished:
			// atlantis.exe main loop @ 0x00419775: Exit == 86 (CHAPI085, the
			// only value shipped) -> FUN_0041a66c: up to 0x578 ticks of the
			// 200 Hz timer (7 s) or a left click, then the credits
			// (FUN_0041a72c, playQuitOutro), then the program ends.
			if (_gameVars.getValOrDefault("exit", 0) == 86) {
				// The game is over: nothing is pending any more, so the wait and
				// the credits are not cut short by shouldAbort().
				_isPlaying = false;
				_abortCommand = kAbortNoAbort;
				uint32 start = g_system->getMillis();
				while (!shouldQuit() && g_system->getMillis() - start < 0x578 * kSpriteTickMs &&
				        getCurrentMouseButton() != 1) {
					pollEvents();
					g_system->delayMillis(10);
				}
				playQuitOutro();
				stopGame = true;
			} else {
				warning("gameStep: Exit=%d, only 86 is handled", _gameVars.getValOrDefault("exit", 0));
				_gameVars["exit"] = 0;
				nextStep = 0;
			}
			break;
		default:
			// Any other exit returns to the main menu.
			nextStep = 0;
			break;
		}
	}

	SearchMan.remove("Atlantis_BigFile");
	return Common::kNoError;
}

void CryOmni3DEngine_Atlantis::gameStep() {
	while (!_abortCommand) {
		// The place loop FUN_00423ad8 returns as soon as Exit is set
		// (0x00423b26, 0x00423bb3, 0x00423cf0); the main loop then ends the
		// game when it is 86 (0x00419787).
		if (_gameVars.getValOrDefault("exit", 0) != 0) {
			_abortCommand = kAbortFinished;
			break;
		}
		// Enter a pending place (set by changeChapter, a zone transition or a
		// CON /set(vue=N)), then run its arrival /sel sections — which may
		// chain on by setting _nextPlaceId again (the chapter intro flow).
		if (_nextPlaceId != uint(-1)) {
			uint fromVue = _currentPlaceId;
			doPlaceChange();
			_arrivalScanPending = false;
			// Music persists across places — only startmusik/stopmusik (run
			// by the new place's arrival CON sections, if any) change it.
			musicUpdate();
			if (_currentPlace)
				runArrivalSelSections((int)_currentPlaceId, (int)fromVue);
		} else if (_arrivalScanPending) {
			// A script Vue= already entered the place (enterPlaceFromScript);
			// the place loop now runs its arrival scan (atlantis.exe
			// FUN_00423ad8 @ 0x00423b48).
			_arrivalScanPending = false;
			musicUpdate();
			if (_currentPlace)
				runArrivalSelSections((int)_currentPlaceId, (int)_arrivalFromVue);
		}
		if (_nextPlaceId != uint(-1) || _arrivalScanPending)
			continue;

		uint actionId = handleWarp();
		musicUpdate();

		// 66666 = handleWarp bailed (abort / menu / window close).
		if (actionId == 66666) {
			if (_abortCommand || shouldAbort())
				break;
			continue;
		}

		// Any other return is a click with no NPC under the crosshair
		// (atlantis.exe FUN_00423ad8): the zone goes to ClicZone, and with a
		// zone or an object in hand FUN_004278a0 runs the /con click scan
		// (0x00427b8c..0x00427bc2).  A transition then needs an empty hand
		// (0x00423d2b).
		_gameVars["cliczone"] = (int)actionId;
		uint zone = actionId;
		// FUN_004240fc: zones 1000..1016 run puzzle zone - 1000 when it is the
		// armed one (StartPuzzle).
		if (zone >= 1000 && zone < 1017 &&
		        _gameVars.getValOrDefault("startpuzzle", 0) == (int)zone - 1000) {
			runPuzzleZone((int)zone - 1000);
			zone = 0;
			if (shouldAbort())
				break;
		}
		// Zone 999: a /sel scan with DepartVue = 999, no transition
		// (FUN_00423ad8 @ 0x00423c47..0x00423c9b).
		if (zone == 999) {
			runDepartureSelSections(999);
			zone = 0;
			if (shouldAbort())
				break;
		}
		if (_gameVars.getValOrDefault("cliczone", 0) != 0 || _inventory.selectedObject() != nullptr) {
			runConClick();
			if (shouldAbort())
				break;
		}
		_gameVars["cliczone"] = 0;
		if (_arrivalScanPending)
			continue;   // a Vue= in a section entered another place
		if (_inventory.selectedObject() != nullptr)
			continue;
		// FUN_00423ad8 @ 0x00423d4b: a transition needs an entry for the zone in
		// the current place.
		if (zone == 0 || !_currentPlace || !_currentPlace->findTransition((uint16)zone))
			continue;
		actionId = zone;

		// A zone action in the place-id range is a navigation click: run the
		// departure /sel sections (which may abort the move by clearing
		// _nextPlaceId, or redirect it via /set(vue=N)), then transition.
		if (actionId >= 1 && actionId < 10000) {
			_nextPlaceId = actionId;
			// LastVue = the place being left (0x00423dcf), before the
			// departure scan.
			_lastVue = _currentPlaceId;
			runDepartureSelSections((int)actionId);
			if (_arrivalScanPending) {
				// A Vue= inside a departure section already entered its place
				// (CHAPI005 /113, CHAPI062 /864, CHAPI077 /010).  Which
				// transition video the original then plays is not established
				// (atlantis-re: experiments/2026-10-12-15-con-vue.md); none is
				// played here.
				_nextPlaceId = uint(-1);
				continue;
			}
			if (!shouldAbort() && _nextPlaceId != uint(-1)) {
				// A departure that is not aborted stops timer 1 and restarts
				// Time1 (atlantis.exe FUN_00423ad8 @ 0x00423e8d).
				_gameVars["testtime1"] = 0;
				_conClocks.time1Start = g_system->getMillis();
				_conClocks.time1Offset = 0;
				uint dest = _nextPlaceId;
				_nextPlaceId = uint(-1);
				executeTransition(dest);
			} else {
				_nextPlaceId = uint(-1);
			}
		}
	}
}

// --- Quit credits slideshow -------------------------------------------------
//
// Asset notes:
//   * 04GENERI.APC is present on some disc images (it plays here on the
//     tested BIGCD1.BIG) but not all single-disc layouts; when it is absent
//     the slideshow runs in silence, matching the original game there.
//     END.TGA is skipped entirely.
//   * CREDIT01..10.TGA are 640×480; profiling the pixels confirms the right
//     half is uniformly black (intensity 0) starting around x=340.
//   * SPRLIST\CREDITS.TXT is segmented by `$NN` markers (10 sections);
//     within each section `<>TEXT` is a credit line and `<>\TEXT` is a
//     section label (rendered in the blue CREDBLEU bank — the leading `\`
//     is stripped).  Section NN corresponds to CREDITNN.TGA.

namespace {

// One parsed line of credits text.
struct CreditsLine {
	Common::String text;
	bool label;     // true if the original line started with `\`
};

// Split CREDITS.TXT into 10 sections keyed by `$NN`.  Lines that are not
// `$NN` or `<>...` are ignored (the file contains blank padding lines).
struct CreditsScript {
	Common::Array<CreditsLine> sections[10];
};

static void parseCreditsScript(Common::ReadStream &stream, CreditsScript &out) {
	Common::Array<byte> buf;
	byte chunk[256];
	while (!stream.eos()) {
		uint32 got = stream.read(chunk, sizeof(chunk));
		if (!got) break;
		for (uint32 i = 0; i < got; i++)
			buf.push_back(chunk[i]);
	}
	buf.push_back(0);

	int section = -1;       // 0..9, set when we see `$NN`
	const char *cur = (const char *)buf.data();
	while (*cur) {
		Common::String line;
		while (*cur && *cur != '\n' && *cur != '\r') {
			line += *cur;
			++cur;
		}
		while (*cur == '\n' || *cur == '\r') ++cur;

		if (line.empty())
			continue;

		// Section marker: `$NN` (one or two digits).
		if (line[0] == '$' && line.size() >= 2) {
			int n = 0;
			for (uint i = 1; i < line.size(); i++) {
				char c = line[i];
				if (c < '0' || c > '9') break;
				n = n * 10 + (c - '0');
			}
			if (n >= 1 && n <= 10)
				section = n - 1;
			continue;
		}

		// Body line: `<>TEXT` or `<>\TEXT`.
		if (section >= 0 && line.size() >= 2 && line[0] == '<' && line[1] == '>') {
			CreditsLine cl;
			const char *p = line.c_str() + 2;
			if (*p == '\\') {
				cl.label = true;
				++p;
			} else {
				cl.label = false;
			}
			cl.text = Common::String(p);
			out.sections[section].push_back(cl);
		}
	}
}

} // anonymous

void CryOmni3DEngine_Atlantis::playQuitOutro() {
	static const char *const kSlides[10] = {
		"CREDIT01.TGA", "CREDIT02.TGA", "CREDIT03.TGA", "CREDIT04.TGA",
		"CREDIT05.TGA", "CREDIT06.TGA", "CREDIT07.TGA", "CREDIT08.TGA",
		"CREDIT09.TGA", "CREDIT10.TGA",
	};
	static const uint32 kSlideDurationMs = 6000;
	// 04GENERI.APC ("generique" = credits) is the authentic credits music;
	// discs without it get silent credits.  We DO NOT substitute a different
	// track: a 33INTRO fallback fights the chapter music and produces an
	// obviously wrong soundscape.
	static const char *const kMusicApc = "04GENERI.APC";

	// Silence the chapter music first, regardless of whether the credits
	// APC loads — the original game's GereGameOver path stops sample
	// playback (FUN_0042b290 → _AIL_stop_sample_4) before drawing the
	// first credit slide.
	musicStop();

	// --- Music (credits track if present) ----------------------------
	Audio::SoundHandle musicHandle;
	bool musicPlaying = false;
	{
		Common::SeekableReadStream *apcFile =
		    openBigFileStream(kFileTypeSound, kMusicApc);
		if (apcFile) {
			Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
			if (apc) {
				int64 remaining = apcFile->size() - apcFile->pos();
				if (remaining > 0) {
					byte *buf = new byte[remaining];
					apcFile->read(buf, (uint32)remaining);
					apc->queuePacket(new Common::MemoryReadStream(
					    buf, remaining, DisposeAfterUse::YES));
				}
				apc->finish();
				_mixer->playStream(Audio::Mixer::kMusicSoundType,
				                   &musicHandle, apc);
				musicPlaying = true;
				debugC(1, kDebugScript, "playQuitOutro: %s playing", kMusicApc);
			}
			delete apcFile;
		} else {
			debugC(1, kDebugScript, "playQuitOutro: %s not on this disc (silent credits)",
			      kMusicApc);
		}
	}

	// --- Credits text -------------------------------------------------
	CreditsScript credits;
	{
		Common::ScopedPtr<Common::SeekableReadStream> txt(
		    openBigFileStream(kFileTypeSprite, "CREDITS.TXT"));
		if (txt)
			parseCreditsScript(*txt, credits);
	}

	// Centering matches FUN_0042073c (atlantis.exe @ 0x42073c):
	//   x = (0x3c0 - text_pixel_width) / 2
	// 0x3c0 = 960 is the original engine's text-area virtual width.  In
	// practice the CRED font lines are short enough that this places them
	// in the black band on the right of the slide art (artwork ends near
	// x≈340 per the per-column-brightness profile of CREDIT01.TGA).
	const int kCenterWidth = 0x3c0;

	// Vertical layout: stack lines with a step that fits the per-section
	// max line count (≈22 lines for the biggest sections) in 480 px, and
	// centre the block vertically.  Char height is ~18 in the original
	// SPR; 22 px step gives a comfortable readable gap.
	const int kLineStep = 22;

	showMouse(false);
	clearKeys();

	Graphics::PixelFormat fmt = g_system->getScreenFormat();

	bool skipped = false;
	for (uint i = 0; i < ARRAYSIZE(kSlides) && !skipped && !shouldAbort();
	     i++) {
		Graphics::Surface *slide = loadTGA(kFileTypeImages, kSlides[i]);
		if (!slide) {
			warning("playQuitOutro: missing slide '%s'", kSlides[i]);
			continue;
		}

		// Build the composite once: slide artwork + credit text overlay.
		Graphics::ManagedSurface frame;
		frame.create(slide->w, slide->h, fmt);
		frame.blitFrom(*slide);

		const Common::Array<CreditsLine> &lines = credits.sections[i];
		if (!lines.empty() && !_creditBlanSprites.empty()) {
			int totalH = (int)lines.size() * kLineStep;
			int y = (480 - totalH) / 2 + kLineStep;   // baseline of first line
			for (uint l = 0; l < lines.size(); l++) {
				const CreditsLine &cl = lines[l];
				int w = creditStringWidth(cl.text);
				int x = (kCenterWidth - w) / 2;        // authentic formula
				drawCreditText(frame, cl.text, x, y, cl.label);
				y += kLineStep;
				if (y > 478) break;
			}
		}

		g_system->copyRectToScreen(frame.getPixels(), frame.pitch,
		                            0, 0, frame.w, frame.h);
		g_system->updateScreen();

		slide->free();
		delete slide;

		// Per-slide wait; any key or mouse button skips the whole outro.
		// The engine-level queues must be read AFTER pollEvents() has
		// drained the backend events into them — polling the event manager
		// directly here would race pollEvents() for the same events.
		const uint32 deadline = g_system->getMillis() + kSlideDurationMs;
		while (g_system->getMillis() < deadline && !shouldAbort()) {
			pollEvents();
			if (checkKeysPressed() || getCurrentMouseButton() != 0) {
				skipped = true;
				break;
			}
			g_system->updateScreen();
			g_system->delayMillis(10);
		}
	}

	if (musicPlaying)
		_mixer->stopHandle(musicHandle);
	clearKeys();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
