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
#include "common/fs.h"
#include "common/debug.h"
#include "common/func.h"
#include "common/events.h"
#include "common/timer.h"
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

#include "cryomni3d/atlantis/ambient_output.h"
#include "cryomni3d/atlantis/credits_layout.h"
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
	_isPlaying(false), _shotCountdown(0), _inputScriptPos(0), _pollCount(0), _scriptPendingUp(0),
	_testDialogLineDone(false),
	_puzzleCursor(),
	_abortCommand(kAbortQuit),
	_loadedSave(0),
	_conInitRunning(false),
	_currentPlayer(0),
	_heroPersoId(0),
	_crtRandSeed(1), _puzzleLcgSeed(0),
	_lastDialCam(0),
	_dialogGivenObjSprite(-1),
	_musicCurrentFile(nullptr),
	_musicRawData(nullptr),
	_musicRawSize(0),
	_musicTrackId(0),
	_musicHandleSet(false),
	_musicOpenTrack(0),
	_warpCursorShown(kWarpCursorUnset),
	_warpCursorNextMs(0),
	_mainPalette(nullptr),
	_warpLoaded(false) {
	memset(_spriteAnimLast, 0, sizeof(_spriteAnimLast));
	_spriteAnimCurrent.resize(512, 0);
	_placeVisits.resize(1500, 0);
	_conPlayCounters.resize(kConPlayCounterCount, 0);
	// malloc(0x12c000) @ 0x00418f83: both IX buffers, RGB565 640 x 480.
	_ixVideoBuffers = new byte[2 * 640 * 480 * 2]();
	for (int i = 0; i < 4; i++)
		_sonMenuKnobX[i] = 0;
}

const Graphics::Font *CryOmni3DEngine_Atlantis::subtitleFont() {
	if (!_subtitleFontTried) {
		_subtitleFontTried = true;
#ifdef USE_FREETYPE2
		// atlantis.exe FUN_0041ae20: CreateFontA(16, ..., "Arial"), a cell height
		// of 16, which Windows 98 renders at 13 pixels per em without smoothing
		// (atlantis-re experiments/2026-10-15-90-win98-subtitle-glyphs.md).  The
		// size is given in pixels: Liberation Sans has no VDMX table, so cell mode
		// would compute 14.  Arial is the player's own (arial.ttf with the game
		// files, as the Buried and ZVision engines do); otherwise Liberation Sans,
		// metric-compatible with it.
		if (Common::SeekableReadStream *arial = SearchMan.createReadStreamForMember("arial.ttf"))
			_subtitleTTF = Graphics::loadTTFFont(arial, DisposeAfterUse::YES, 13,
			                                     Graphics::kTTFSizeModeCharacter, 0, 0,
			                                     Graphics::kTTFRenderModeMonochrome);
		if (!_subtitleTTF)
			_subtitleTTF = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", 13,
			                                                 Graphics::kTTFSizeModeCharacter, 0, 0,
			                                                 Graphics::kTTFRenderModeMonochrome);
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
	g_system->getTimerManager()->removeTimerProc(&musicFadeTimer);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(&_viewKeys);
	delete[] _ixVideoBuffers;
	delete _subtitleTTF;
	stopAmbient();   // 0x004197a0, on the way out
	delete _ambientOut;
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

	Common::SeekableReadStream *s = _bigFile.createReadStreamForMember(fullName);
	debugC(1, kDebugFile, "openBigFileStream: %s%s", fullName.c_str(), s ? "" : " (missing)");
	return s;
}

void CryOmni3DEngine_Atlantis::setupPalette(const byte *colors, uint start, uint num) {
	// Atlantis runs in 16bpp true colour; the base palette helpers are
	// no-ops in that mode (see CryOmni3DEngine::setPalette).
	setPalette(colors, start, num);
}

void CryOmni3DEngine_Atlantis::syncSoundSettings() {
	CryOmni3DEngine::syncSoundSettings();
	musicSyncVolume();

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
	// FUN_0044a424 ends with the pointer at (0x140, 0x140), without
	// SetCursorPos (0x0044a70b / 0x0044a715); see warp_pointer.h.
	_warpPointer.panoramaLoaded();
	markWarpPointerMouse();
	debugC(1, kDebugScript, "loadCycloHNM: %s (%dx%d)", name.c_str(), _warpSurface.w, _warpSurface.h);
	return true;
#else
	warning("HNM support not compiled in; cannot load cyclo %s", name.c_str());
	return false;
#endif
}

void CryOmni3DEngine_Atlantis::loadInputScript(const Common::String &path) {
	_inputScript.clear();
	_inputScriptPos = 0;
	Common::FSNode node(Common::Path(path, '/'));
	Common::SeekableReadStream *in = node.createReadStream();
	if (!in) {
		warning("Atlantis: cannot read the input script '%s'", path.c_str());
		return;
	}
	while (!in->eos()) {
		Common::String line = in->readLine();
		Common::Array<Common::String> tok;
		Common::String cur;
		for (uint k = 0; k <= line.size(); k++) {
			if (k == line.size() || line[k] == ' ' || line[k] == '\t') {
				if (!cur.empty())
					tok.push_back(cur);
				cur.clear();
			} else {
				cur += line[k];
			}
		}
		if (tok.empty() || tok[0].hasPrefix("#"))
			continue;
		InputScriptAction a;
		a.poll = (uint)atoi(tok[0].c_str());
		a.x = a.y = a.button = 0;
		a.shot = (tok.size() >= 2 && tok[1] == "shot");
		if (!a.shot && tok.size() >= 4) {
			a.x = atoi(tok[1].c_str());
			a.y = atoi(tok[2].c_str());
			a.button = atoi(tok[3].c_str());
		} else if (!a.shot) {
			continue;
		}
		_inputScript.push_back(a);
	}
	delete in;
	warning("Atlantis: input script '%s', %u actions", path.c_str(), _inputScript.size());
}

bool CryOmni3DEngine_Atlantis::pollEvents() {
	// Testing aid (port-only): the scripted input.  A click is the button
	// down for one poll; clicks must be two polls apart.
	if (!_inputScript.empty()) {
		Common::EventManager *em = g_system->getEventManager();
		if (_scriptPendingUp) {
			Common::Event up;
			up.type = (_scriptPendingUp == 1) ? Common::EVENT_LBUTTONUP : Common::EVENT_RBUTTONUP;
			up.mouse = em->getMousePos();
			em->pushEvent(up);
			_scriptPendingUp = 0;
		}
		while (_inputScriptPos < _inputScript.size() && _inputScript[_inputScriptPos].poll <= _pollCount) {
			const InputScriptAction &a = _inputScript[_inputScriptPos++];
			if (a.shot) {
				g_system->saveScreenshot();
				warning("Atlantis: screenshot saved at poll %u", _pollCount);
				continue;
			}
			Common::Event mv;
			mv.type = Common::EVENT_MOUSEMOVE;
			mv.mouse = Common::Point((int16)a.x, (int16)a.y);
			g_system->warpMouse(a.x, a.y);
			em->pushEvent(mv);
			if (a.button == 1 || a.button == 2) {
				Common::Event dn;
				dn.type = (a.button == 1) ? Common::EVENT_LBUTTONDOWN : Common::EVENT_RBUTTONDOWN;
				dn.mouse = mv.mouse;
				em->pushEvent(dn);
				_scriptPendingUp = a.button;
			}
		}
		_pollCount++;
	}
	const bool r = CryOmni3DEngine::pollEvents();
	if (_shotCountdown > 0 && --_shotCountdown == 0) {
		g_system->saveScreenshot();
		warning("Atlantis: screenshot saved");
	}
	return r;
}

Common::Error CryOmni3DEngine_Atlantis::run() {
	CryOmni3DEngine::run();

	// The original's 200 Hz timer runs the music fades (FUN_0042f7a8,
	// FUN_0042f974); 0x00495e72 follows the music volume setting.
	musicSyncVolume();
	g_system->getTimerManager()->installTimerProc(&musicFadeTimer, 5000, this, "atlantis-music-fade");
	g_system->getEventManager()->getEventDispatcher()->registerObserver(&_viewKeys, 1, false);

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
	importOriginalSaves();
	loadObjectSprites();
	loadFontSpriteSet("SUIVEUR.SPR", _guestSprites);   // guest portraits (FUN_00427344)
	loadInventoryBarSprite();
	// 30 records: the original's table 0x006ce138 (0x10e bytes, all in the
	// checkpoint); the pick-up appends with no test (FUN_004278a0 @
	// 0x00427a7f..0x00427afa), the 31st would overwrite players.atl data.
	_inventory.init(30, new Common::Functor1Mem<uint, void, CryOmni3DEngine_Atlantis>(
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
	// Testing aid (port-only): the configuration key atlantis_puzzle=<n>
	// plays StartPuzzle n at once, with the object atlantis_puzzle_object
	// in hand when set, and reports its FinPuzzle before the game goes on.
	if (ConfMan.hasKey("atlantis_puzzle")) {
		const int puzzle = ConfMan.getInt("atlantis_puzzle");
		if (ConfMan.hasKey("atlantis_puzzle_object")) {
			const uint id = (uint)ConfMan.getInt("atlantis_puzzle_object");
			for (Object &o : _objects)
				if (o.idOBJ() == id) {
					_inventory.setSelectedObject(&o);
					break;
				}
		}
		// atlantis_puzzle_shot=<n> saves a screenshot (ScummVM's screenshotpath)
		// after n input polls of the puzzle.
		if (ConfMan.hasKey("atlantis_puzzle_shot"))
			_shotCountdown = ConfMan.getInt("atlantis_puzzle_shot");
		// atlantis_input_script=<file> replays "poll x y button" lines
		// (button 1 left, 2 right, 0 a move) and "poll shot" screenshots
		// from the puzzle's first input poll.
		if (ConfMan.hasKey("atlantis_input_script"))
			loadInputScript(ConfMan.get("atlantis_input_script"));
		_pollCount = 0;
		playPuzzle(puzzle);
		warning("Atlantis: puzzle %d ended with FinPuzzle %d", puzzle,
		        _gameVars.getValOrDefault("finpuzzle", 0));
	}

	uint nextStep = 0;
	int bootSlot = ConfMan.getInt("save_slot");
	// Cryo logo and pre-intro cinematics before the player screen (skipped
	// when launched directly into a save).
	if (bootSlot < 0) {
		// atlantis.exe @ 0x0041960a..0x004196cc: both through the transition
		// player at the cinema pace (65 ms), with a 200-tick (1 s) busy wait
		// between them that reads no input.
		playTransitionVideo("LOGO15N.HNM", uint(-1), uint(-1), kCinemaFrameMs);
		const uint32 waitStart = g_system->getMillis();
		while (g_system->getMillis() - waitStart < 1000 && !shouldAbort()) {
			Common::Event ev;
			while (g_system->getEventManager()->pollEvent(ev)) {
			}
			g_system->delayMillis(10);
		}
		playTransitionVideo("PREINTRO.HNM", uint(-1), uint(-1), kCinemaFrameMs);
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
		else {
			// FUN_0042de5c with no checkpoint: the saved disc is 1 (@ 0x0042e285
			// path, 0x00772914 = 1), asked for when another one is in use.
			if (_currentDisc != 1)
				changeDisc(0);
			changeChapter(1);             // new game (also the fallback)
		}

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
				stopAmbient();   // FUN_0041a66c @ 0x0041a692
				// The game is over: nothing is pending any more, so the wait and
				// the credits are not cut short by shouldAbort().
				_isPlaying = false;
				_abortCommand = kAbortNoAbort;
				// images\end.tga, shown (FUN_0041b654, FUN_004337d4).
				if (Graphics::Surface *end = loadTGA(kFileTypeImages, "END.TGA")) {
					g_system->copyRectToScreen(end->getPixels(), end->pitch, 0, 0, end->w, end->h);
					g_system->updateScreen();
					end->free();
					delete end;
				}
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

// Testing aid (port-only): atlantis_dialog_line="<section> <letter> <perso>
// <cam>" plays that dialog line once (wav\ata<section><letter>, the perso's
// UBB for the camera and its 3D head) over the first place entered, with the
// screenshot keys of the puzzle aid, then the game goes on.
void CryOmni3DEngine_Atlantis::playTestDialogLine() {
	_testDialogLineDone = true;
	int section = 0, perso = 0, cam = 0;
	char letter = 'A';
	if (sscanf(ConfMan.get("atlantis_dialog_line").c_str(), "%d %c %d %d", &section, &letter, &perso, &cam) != 4 ||
	        cam < 2 || cam > 5 || !_dialInitData.contains(perso)) {
		warning("Atlantis: atlantis_dialog_line '%s' not understood", ConfMan.get("atlantis_dialog_line").c_str());
		return;
	}
	const DialInitEntry &entry = _dialInitData[perso];
	Common::String ubb = entry.camUBB[cam - 2];
	ubb.toUppercase();
	if (ConfMan.hasKey("atlantis_puzzle_shot"))
		_shotCountdown = ConfMan.getInt("atlantis_puzzle_shot");
	if (ConfMan.hasKey("atlantis_input_script"))
		loadInputScript(ConfMan.get("atlantis_input_script"));
	_pollCount = 0;
	playSingleConLine(section, letter, "", "PERSO\\" + ubb + ".UBB", entry.camSPF[cam - 2],
	                  false, 0, 0, cam, true, perso);
	warning("Atlantis: dialog line %d%c played", section, letter);
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
			if (_currentPlace) {
				_inPlaceArrivalScan = true;   // FUN_00423ad8's own scan
				runArrivalSelSections((int)_currentPlaceId, (int)fromVue);
				_inPlaceArrivalScan = false;
			}
		} else if (_arrivalScanPending) {
			// A script Vue= already entered the place (enterPlaceFromScript);
			// the place loop now runs its arrival scan (atlantis.exe
			// FUN_00423ad8 @ 0x00423b48).
			_arrivalScanPending = false;
			musicUpdate();
			if (_currentPlace) {
				_inPlaceArrivalScan = true;
				runArrivalSelSections((int)_currentPlaceId, (int)_arrivalFromVue);
				_inPlaceArrivalScan = false;
			}
		}
		if (_nextPlaceId != uint(-1) || _arrivalScanPending)
			continue;

		if (!_testDialogLineDone && _warpLoaded && ConfMan.hasKey("atlantis_dialog_line"))
			playTestDialogLine();

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
			// FUN_00423ad8's i: the first transition of the place whose
			// destination is the zone (checked by findTransition above).
			uint transitionIndex = 0;
			while (_currentPlace->transitions[transitionIndex].dstPlaceId != (uint16)zone)
				transitionIndex++;
			_departAborted = false;
			runDepartureSelSections((int)actionId);
			if (_arrivalScanPending) {
				// A Vue= inside a departure section already entered its place
				// (CHAPI005 /113, CHAPI062 /864, CHAPI077 /010).  Game over and
				// AbortDepart end the move; otherwise the place loop goes on
				// with the turn and video of entry i of the new place's record.
				_nextPlaceId = uint(-1);
				if (!shouldAbort() && !_departAborted && _gameVars.getValOrDefault("gameover", 0) == 0)
					transitionAfterDepartureVue(transitionIndex);
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

// --- Credits ----------------------------------------------------------------
//
// SPRLIST\CREDITS.TXT holds 19 `$NN` page markers (some numbers repeat: the
// same picture with more text) between `<>TEXT` lines, `<>\TEXT` headings
// and blank lines that space the text; see atlantis-re
// functions/ui-FUN_0041a72c.md.

// atlantis.exe FUN_0041a72c, the credits (credits_layout.h has the events).
void CryOmni3DEngine_Atlantis::playQuitOutro() {
	// OBJETENMAIN = 0 (0x0041a744); the ambient sounds stop (FUN_00431e98
	// @ 0x0041a85e, when 0x00496980 == 1).
	_gameVars["objetenmain"] = 0;
	stopAmbient();
	_inventory.setSelectedObject(nullptr);

	// Music (0x0041a83e..0x0041a916): only when the music handle 0x006ce0c0
	// is set and the track 0x00772915 is not 22 (04GENERI.APC, set by
	// CHAPI085 before the end), switch to track 26 (09VOLTOT.APC,
	// FUN_00432ea8(0x19)); with track 22 it keeps playing; with no handle
	// the credits are silent.  After a failed StartMusik open the handle is
	// -1 (set) and 0x00772915 still names the previous track, so the test
	// uses those two, not the selected _musicTrackId.
	// See atlantis-re: functions/ui-FUN_0041a72c.md step 4.
	if (_musicHandleSet && _musicOpenTrack != 22)
		musicSwitchTrack(26);

	Common::Array<byte> txt;
	{
		Common::ScopedPtr<Common::SeekableReadStream> f(
		    openBigFileStream(kFileTypeSprite, "CREDITS.TXT"));
		if (f) {
			txt.resize((uint)f->size());
			f->read(txt.data(), txt.size());
		}
	}
	Common::Array<CreditsEvent> events;
	CreditsLayout::build(txt.data(), txt.size(), _creditCharWidths, events);

	showMouse(false);
	clearKeys();
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface buffer(640, 480, fmt);   // the work buffer 0x004960d8
	const uint32 start = g_system->getMillis();

	// Waits until the tick; false when Escape ends the credits (FUN_0041a298
	// clears 0x00496124 on an Escape edge; a click does nothing here).
	auto waitUntil = [&](uint32 tick) {
		while (g_system->getMillis() - start < tick * kSpriteTickMs && !shouldAbort()) {
			pollEvents();
			if (checkKeysPressed(1, Common::KEYCODE_ESCAPE))
				return false;
			g_system->delayMillis(10);
		}
		return !shouldAbort();
	};

	bool running = true;
	for (uint i = 0; i < events.size() && running; i++) {
		const CreditsEvent &e = events[i];
		switch (e.kind) {
		case CreditsEvent::kImage: {
			// FUN_0041ad7c: images\creditNN.tga into the work buffer.
			const Common::String name = Common::String::format("CREDIT%02d.TGA", e.image);
			Graphics::Surface *pic = loadTGA(kFileTypeImages, name.c_str());
			if (pic) {
				buffer.blitFrom(*pic);
				pic->free();
				delete pic;
			}
			break;
		}
		case CreditsEvent::kGlyph:
			drawCreditText(buffer, Common::String((char)(e.frame + 0x21)), e.x, e.y, e.blue);
			break;
		case CreditsEvent::kPresent:
			running = waitUntil(e.tick);
			if (running) {
				g_system->copyRectToScreen(buffer.getPixels(), buffer.pitch, 0, 0, buffer.w, buffer.h);
				g_system->updateScreen();
			}
			break;
		case CreditsEvent::kClear:
			running = waitUntil(e.tick);
			break;
		}
	}

	// After the loop: the music is faded out and stopped, the screen cleared
	// (FUN_00450ec0 over 640 x 480 x 2).
	musicStopCommand();
	buffer.clear(0);
	g_system->copyRectToScreen(buffer.getPixels(), buffer.pitch, 0, 0, buffer.w, buffer.h);
	g_system->updateScreen();
	clearKeys();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
