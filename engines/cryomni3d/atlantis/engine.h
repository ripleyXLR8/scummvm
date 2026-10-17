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

#ifndef CRYOMNI3D_ATLANTIS_ENGINE_H
#define CRYOMNI3D_ATLANTIS_ENGINE_H

#include "common/array.h"
#include "common/error.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/events.h"
#include "common/mutex.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/bar_session.h"
#include "cryomni3d/atlantis/ambient_sounds.h"
#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/chapter_start.h"
#include "cryomni3d/atlantis/con_eval.h"
#include "cryomni3d/atlantis/dialog_view.h"
#include "cryomni3d/atlantis/music_fade.h"
#include "cryomni3d/atlantis/random.h"
#include "cryomni3d/atlantis/inventory.h"
#include "cryomni3d/atlantis/con_script.h"
#include "cryomni3d/atlantis/exe_build.h"
#include "cryomni3d/atlantis/menu_layout.h"
#include "cryomni3d/atlantis/npc_click.h"
#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/saveload_format.h"
#include "cryomni3d/atlantis/sprite_anim.h"
#include "cryomni3d/atlantis/sprite_decode.h"
#include "cryomni3d/atlantis/toolbar.h"
#include "cryomni3d/atlantis/warp_cursor.h"
#include "cryomni3d/atlantis/warp_momentum.h"
#include "cryomni3d/atlantis/warp_pointer.h"
#include "cryomni3d/atlantis/wam_parser.h"

namespace CryOmni3D {

struct CryOmni3DGameDescription;

namespace Atlantis {

// Logical file categories.  Each maps to a sub-directory inside the BigFile
// archive (see kBigFileSubdirs in engine.cpp); openBigFileStream() resolves a
// bare file name within one of these.
enum FileType {
	kFileTypeCyclo,        // CYCLO\        — panoramic HNM videos
	kFileTypeTransition,   // UBB_VUE\      — place-to-place transition HNM videos
	kFileTypeDialog,       // DIALOG\       — dialog UBB audio + font + init files
	kFileTypeSync,         // SYC\          — lip-sync timing data
	kFileTypeFont,         // FONTS\        (or game root) — font files
	kFileTypeSprite,       // SPRLIST\      — sprite list text files
	kFileTypeWAM,          // WAM\          — navigation map files
	kFileTypeObject,       // OBJETS\       — object images
	kFileTypeMenu,         // MENU\         — menu/UI images
	kFileTypeScript,       // SCENAR\       — chapter scenario scripts (.CON files)
	kFileTypeSound,        // WAV\          — APC audio files (music, ambient, dialog)
	kFileTypeSpriteCyclo,  // SPRITE\CYCLO\ — pre-rendered NPC/prop sprites (SPW files)
	kFileTypeSpriteUbb,    // SPRITE\UBB\   — per-transition NPC animations (SPF files)
	kFileTypeSprite2D,     // SPRITE\2D\    — 2D subject/cursor icon sprites (SPR files)
	kFileTypeImages,       // IMAGES\       — full-screen TGA background images
	kFileTypePuzzles       // PUZZLES\      — per-minigame sprites + TGAs (eclipse, chess, …)
};

// One entry from the WAM's SPF transition-sprite list (e.g. SPFATL1.TXT).
// Format per line: `fromPlace toPlace persoId spfFile`.  During a transition
// from fromPlace to toPlace the engine composites the matching SPF's frames
// onto the HNM transition video so NPCs remain visible as the camera moves.
// SPF on-disk format is identical to SPW (same header, frame table, RLE
// pixel stream), just authored against the 640×480 video frame instead of
// the 2048-wide cyclo panorama.
struct SpriteTransEntry {
	uint16 fromPlace;
	uint16 toPlace;
	int    persoId;
	Common::String spfFile;
};

// How the gameStep() loop ended — drives run()'s outer state machine.
enum AbortCommand {
	kAbortNoAbort     = 0,
	kAbortQuit        = 1,
	kAbortLoadGame    = 2,
	kAbortNewGame     = 3,
	kAbortNextChapter = 5,
	kAbortFinished    = 6,
	kAbortGameOver    = 7
};

// Atlantis: The Lost Tales.
//
// This is the bootable skeleton: it mounts the BigFile archive(s), loads the
// chapter-1 navigation map (WAM) and displays the first place's panoramic
// cyclorama with mouse look.  Place transitions, CON scripting, dialog,
// sprites, menus, saves and the F3DC mouth pipeline are layered on top in
// subsequent commits.
// F5..F8 held state (atlantis.exe FUN_0041a298 tests the keys' "down" bit
// every poll: they are not edge-mode keys).
class AtlantisViewKeys : public Common::EventObserver {
public:
	uint8 held = 0;   // bit 0 F5, 1 F6, 2 F7, 3 F8
	bool notifyEvent(const Common::Event &event) override {
		if (event.type != Common::EVENT_KEYDOWN && event.type != Common::EVENT_KEYUP)
			return false;
		int bit = -1;
		switch (event.kbd.keycode) {
		case Common::KEYCODE_F5: bit = 0; break;
		case Common::KEYCODE_F6: bit = 1; break;
		case Common::KEYCODE_F7: bit = 2; break;
		case Common::KEYCODE_F8: bit = 3; break;
		default: return false;
		}
		if (event.type == Common::EVENT_KEYDOWN)
			held |= (uint8)(1 << bit);
		else
			held &= (uint8)~(1 << bit);
		return false;   // not consumed
	}
};

class CryOmni3DEngine_Atlantis : public CryOmni3DEngine {
	friend class Atlantis_Toolbar;

protected:
	Common::Error run() override;

public:
	CryOmni3DEngine_Atlantis(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine_Atlantis() override;

	bool hasFeature(EngineFeature f) const override;
	// True when the engine should leave the current loop: a real quit, or
	// (while playing) any pending _abortCommand — load-game, game-over, etc.
	// The base class only checks shouldQuit(), which would not propagate a
	// mid-dialog load/game-over request out of handleWarp.
	bool shouldAbort() override;
	// The base poll, plus the port-only screenshot countdown (atlantis_puzzle_shot).
	bool pollEvents();
	Common::Error loadGameState(int slot) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	Common::String getSaveStateName(int slot) const override;
	// Atlantis writes its own per-episode checkpoint on chapter entry; disable
	// ScummVM's periodic autosave so it cannot overwrite a checkpoint slot.
	int getAutosaveSlot() const override { return -1; }

	// Build a path for a named file of the given type (for files outside the BigFile).
	Common::Path getFilePath(FileType fileType, const Common::String &baseName) const;

	// CURSEURS.SPR frame idx, or null (puzzles draw cursors into the frame).
	const SprSprite *cursorSprite(uint idx) const {
		return (idx < _cursorSprites.size() && !_cursorSprites[idx].pixels.empty()) ? &_cursorSprites[idx] : nullptr;
	}
	// The puzzles' sound players, on the six Miles samples they share with
	// the ambient sounds (miles_samples.h).
	enum PuzzleSamplePlayer {
		kPlayCa28,   // FUN_0043ca28: samples 0 / 1 (short) or 2, status 2
		kPlayCd60,   // FUN_0043cd60: samples 0..2, status 2 (FUN_00438bf0)
		kPlayCe80,   // FUN_0043ce80: sample 0, status 2 or 8 (crab click)
		kPlayCf70    // FUN_0043cf70: stop and start sample 1 (crab comments)
	};
	// Plays WAV\<name>.APC once with `player`'s choice of sample; returns
	// the slot, or -1 when the original drops the sound.
	int playPuzzleSound(const char *name, PuzzleSamplePlayer player = kPlayCa28);
	// AIL_sample_status of Miles sample `slot` (0..5).
	int puzzleSampleStatus(uint slot);
	// FUN_0043d0e0: AIL_stop_sample on samples 0..3 (status != 0, always).
	void stopPuzzleSamples();
	// The six Miles samples (created on first use).
	class AtlantisAmbientOutput &milesSamples();
	// Every puzzle's sample set-up (FUN_0043cb8c, FUN_0043d150, FUN_0043d204)
	// gives its Miles samples pan 0x40 and volume 0x6e; Miles volumes map
	// onto the mixer as AtlantisAmbientOutput::setVolume does (v * 255 / 127).
	static const byte kPuzzleSampleVolume = (byte)(0x6e * Audio::Mixer::kMaxChannelVolume / 127);
	// FUN_00432178's ambient part (place.cpp), at the end of a warp or bar frame.
	void ambientFrame();
	// The puzzle-cursor animation 0x00496ea8 (mode), 0x00496eaa (frame),
	// 0x00496eac (step): globals of the original, started by the puzzles'
	// click handlers (FUN_004462b0, FUN_00448a80, ...) and stepped by
	// FUN_00448958, never reset when a puzzle starts; a puzzle left during
	// the animation starts the next one with it running.  Zero at start.
	struct PuzzleCursorState {
		int16 mode, frame, step;
	} _puzzleCursor;

	// The original's two generators (random.h): the C rand, seeded 1 at
	// launch and shared by the rat (FUN_00438fd4), crab (FUN_0043ede8) and
	// ogre (FUN_00442a0c) puzzles; the puzzle LCG, seeded from the clock by
	// the dispatcher FUN_0043a1e0 at every puzzle launch.
	uint32 _crtRandSeed;
	int crtRand() { return crtRandNext(_crtRandSeed); }
	uint16 _puzzleLcgSeed;
	// The held object (the OBJETENMAIN variable), and emptying the hand.
	const Object *heldObject() const { return _inventory.selectedObject(); }
	uint currentPlaceId() const { return _currentPlaceId; }
	void setGameVar(const char *name, int value) { _gameVars[name] = value; }
	// FUN_004277c8: the first /sel entry whose condition holds runs (the
	// puzzle hooks call it too).
	void runSelScan();
	void clearHeldObject() {
		_inventory.setSelectedObject(nullptr);
		_gameVars["objetenmain"] = 0;
	}
	// Open a file from the BigFile archive and return a new stream (caller owns it).
	// Returns nullptr if not found.
	Common::SeekableReadStream *openBigFileStream(FileType fileType,
	                                               const Common::String &baseName) const;

	// CryOmni3DEngine interface.
	void setupPalette(const byte *colors, uint start, uint num) override;
	// Scale the per-channel mixer volumes by the SONMENU master slider.
	void syncSoundSettings() override;
	void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const override;
	bool displayToolbar(const Graphics::Surface *original) override { return _toolbar.displayToolbar(original); }
	bool hasPlaceDocumentation() override { return false; }
	bool displayPlaceDocumentation() override { return false; }
	uint displayOptions() override;

private:
	// --- Chapter / place lifecycle (place.cpp) ----------------------------
	// Set up the chapter's WAM navigation map and load its CON script.
	// keepState: the no-checkpoint branch of FUN_0042de5c reached in play
	// (changeChapter below).
	void setupChapterWAM(int chapter, bool keepState = false);
	// Switch to the given chapter: load its WAM/CON and enter the start place.
	// keepState (chapter 1 only): FUN_0042de5c's no-checkpoint branch reached
	// in play (game-over reload, load picker OK / RESTART EPISODE): the
	// main loop skips the new-game init FUN_00423760 (0x0041972b), so the
	// inventory, guests, hidden persos, /con counters, IncPerso, view angles
	// and the variables 1, 3..6 are kept; the CON and its /INIT run again.
	// See atlantis-re: experiments/2026-10-16-10-load-state.md.
	void changeChapter(int chapter, bool keepState = false);
	// Enter _nextPlaceId: load its cyclo panorama and point the viewer at it.
	void doPlaceChange();
	// Transition to the given place (currently a direct change; the UBB_VUE
	// transition video is added with the dialog/video layer).
	void executeTransition(uint nextPlaceId);
	void turnToTransition(float srcAlpha, float srcBeta);
	// Load a named WAM over the current one (a CON /set(changewam=…) sub-WAM or
	// a save's stored WAM), without touching the inventory or CON script.
	void setupWAMByName(const Common::String &wamName);

	// --- Save / load (saveload.cpp) ---------------------------------------
	// Serialize / restore the full game state to a 1-based save slot.
	void saveGame(uint saveNum, const Common::String &saveName);
	// The original's checkpoints: on a first run with only its players.atl in
	// the save folder, every p<letter><N>.gam of its players becomes a port
	// save in that player's episode slot (atlantis-re formats/ui-gam.md).
	void importOriginalSaves();
	void loadConVarNames();
	bool loadGame(uint saveNum);
	// Write the per-chapter checkpoint (called on chapter entry).
	void autosave();

	// --- Players / episodes (saveload.cpp) --------------------------------
	// Atlantis has no manual save: it keeps one named save per episode and
	// rewrites it when that episode is (re-)entered.  Up to kMaxPlayers named
	// players each own the absolute slot block [id*stride, id*stride+stride);
	// chapter N's save is local slot N of the block.
	static const uint kMaxPlayers       = kSaveMaxPlayers;
	static const uint kPlayerNameLen    = 16;
	static const uint kPlayerSlotStride = kSavePlayerSlotStride;
	int episodeSaveSlot(uint player, uint chapter) const {
		return (int)(player * kPlayerSlotStride + chapter);
	}
	void loadPlayers();
	void savePlayers();
	void deletePlayerSaves(uint player);
	// FUN_0041e8a8: delete a player and move the later ones up one slot.
	void deletePlayer(uint player);
	bool saveSlotExists(int absSlot) const;
	bool playerHasSave(uint player) const;
	uint playerResumeSave(uint player) const;   // 1-based slot, 0 = none
	// SPRLIST\EPI.TXT episode checkpoint names; episodeName serves chapter N
	// from index N-1 (empty = no checkpoint for that chapter).
	void loadEpisodeNames();
	Common::String episodeName(uint chapter) const;

	// --- Warp-view navigation (navigation.cpp) ----------------------------
	// Main gameplay loop: drive handleWarp() and place transitions until abort.
	void gameStep();
	// Panoramic mouse-look + zone hit-test; returns the clicked zone's action
	// (a destination place id), or 66666 when it bailed (abort / menu).
	int handleWarp();
	// True if `pan` (panoramic coords) lands on a clickable NPC at the current
	// place; outPersoId receives the perso.
	bool npcPanoHitTest(const Common::Point &pan, int &outPersoId) const;

	// --- CON scenario-script execution (con_exec.cpp) ---------------------
	// Load CHAPI<chapter>.CON, resetting the per-chapter game-variable state.
	void loadConScript(int chapter);
	// True for the 7 game variables that survive a chapter change.
	static bool isPersistentGameVar(const Common::String &key);
	// Run the /INIT block's /set commands once after a CON load.
	void execConInitCommands();
	// Apply one /set command string (e.g. "vue=12", "flagtemp3=1").
	// offset: position of the "/set" in the CON text (-1: parse cmd alone).
	void execConSetCommand(const Common::String &cmd, int32 offset = -1);
	void execConInventCommand(uint objId, int value);
	// FUN_0042a100 Vue=N: enter place N at once (no transition video), the
	// arrival /sel scan running when the place loop next comes round.
	void enterPlaceFromScript(uint placeId);
	// True while the place loop runs its own arrival scan (FUN_00423ad8
	// @ 0x00423b78), after which it clears ARRIVEEVUE (@ 0x00423be7).
	bool _inPlaceArrivalScan = false;
	AtlantisViewKeys _viewKeys;
	WarpMomentum _warpMomentum;   // 0x00496984 / 0x00496988
	WarpPointer _warpPointer;     // 0x004960ec / 0x004960f0, 0x0049698c / 0x00496990
	// The system mouse position when the port last wrote _warpPointer from
	// outside the warp look (warp exit, panorama load): a different position
	// at the next warp entry is a mouse motion in between (WM_MOUSEMOVE).
	Common::Point _warpPointerMouseMark;
	void markWarpPointerMouse() { _warpPointerMouseMark = getMousePos(); }
	// The mouse moved since the mark: the pointer is the last mouse position.
	void syncWarpPointerMouse() {
		const Common::Point m = getMousePos();
		if (m != _warpPointerMouseMark)
			_warpPointer.mouseMoved(m);
		_warpPointerMouseMark = m;
	}
	bool _arrivalScanPending = false;
	uint _arrivalFromVue = 0;
	// Set by a chapter load (loadGame, the new game): the next arrival scan
	// reads ArriveeVue = LastVue and is skipped when it is 0
	// (arrivalAfterChapterLoad).  See atlantis-re: functions/ui-FUN_0042de5c.md.
	bool _arrivalAfterLoad = false;
	bool takeArrivalAfterLoad(int &arrivee);
	// The CON `Vue` variable when it differs from the current place: a
	// script Vue=N whose place is not in the loaded WAM keeps the place
	// (FUN_00421b4c finds no record) but stores N in `Vue`
	// (@ 0x0042a723..0x0042a72c), e.g. CHAPI005 /113 (Vue=171 in ATLAN1.WAM).
	// -1 when `Vue` is the current place.
	int _vueVarOverride = -1;
	int vueVariable() const { return _vueVarOverride >= 0 ? _vueVarOverride : (int)_currentPlaceId; }
	// atlantis.exe place table 0x006ce274: one record per WAM place, by its
	// position in the WAM.  FUN_00421ebc writes each record's transitions
	// [0, count) and clears nothing, so an entry past the count keeps what an
	// earlier WAM of the run wrote there (atlantis-re
	// experiments/2026-10-15-08-chapi062-864.md).  Kept for the whole process:
	// FUN_004239f4's memsets (load, new game) do not cover it.
	Common::Array<Common::Array<AtlantisTransition> > _placeTableSlots;
	void recordPlaceTable();
	// Entry i of record r as the original reads it (zeroed if never written).
	AtlantisTransition placeTableEntry(uint record, uint i) const;
	// The place loop after a departure section whose Vue= entered a place.
	void transitionAfterDepartureVue(uint transitionIndex);
	// atlantis.exe DAT_0049692c, LastVue: 1 at game start (0x004195fb) and
	// after a load (0x0042e285); the place being left on a transition
	// (FUN_00423ad8 @ 0x00423dcf) and on a script Vue= outside loading
	// (FUN_0042a100 @ 0x0042a6b5).  A chapter change hands it to the /sel
	// scan as ARRIVEEVUE (FUN_00427634 @ 0x0042777d).
	uint _lastVue = 1;
	// CON /set(StartPuzzle=N) — run minigame N and write FinPuzzle back
	// (255 = win; smaller values denote per-puzzle loss variants).  Puzzles
	// without a handler auto-win so their CHAPI script chains keep
	// progressing past the gate.
	void playPuzzle(int puzzleId);
	void runPuzzleZone(int puzzleId);
	// Evaluate a /sel section's condition list (left-to-right short-circuit).
	// It uses the current scan's key buffer (_conScanKey).
	bool evalSelSection(const ConSection &sec, int currentVue) const;
	// The key buffer of FUN_004298ac kept across one scan (FUN_004280f8,
	// FUN_004281f8, FUN_0042bef4, FUN_0042bfa4 each call the evaluator from
	// one frame); beginConScan() starts a scan with it zeroed.
	mutable byte _conScanKey[ConEvaluator::kKeyBufferSize];
	void beginConScan() const { memset(_conScanKey, 0, sizeof(_conScanKey)); }
	// Execute a fired section's body (its /set commands and /go redirect; the
	// dialog lines are played once the dialog layer lands).
	// chain: the walk's entry angles and ClicPerso, carried into a /go target.
	void playConSection(ConSection &sec, const DialogView::Walk *chain = nullptr);
	// Fire the first matching arrival/departure /sel section, in file order.
	void runArrivalSelSections(int arrivedVue, int fromVue);
	void runDepartureSelSections();

	// Handle a click on NPC `persoId`: play the first condition-passing
	// /con(clicperso=N) section for them (the subject topic menu lands with
	// the cursor layer).  Mirrors atlantis.exe FUN_00425828.
	void interactNPC(int persoId);
	// ArcMagique=N: the 20-frame ARCANIM.SPR animation at the mouse.
	void playArcMagique();
	void enterBowMode();
	void raiseBow();
	void lowerBow();
	void showBowAt(int y);
	// atlantis.exe FUN_00427bd0: hide every sprite of perso and remember it.
	void hidePerso(int perso);
	// atlantis.exe FUN_004278a0 for an object id (0x100..0x1ff).
	void clickObject(int objId);
	// The pick-up toolbar (Atlantis_Toolbar::displayPickup) over the view;
	// true when the object stays in hand.
	bool runPickupToolbar(Object *obj);
	// FUN_00425cf4: after a click on a zone or with an object in hand, run the
	// first eligible /con entry that is not a subject entry, then
	// FUN_00425dd0 (drop the held object on its zone).
	void runConClick();
	void showPerso(int perso);

	// --- Dialog playback (dialog.cpp) -------------------------------------
	// Resolve a dialog line's speaker name to a perso ID.  Transcribes the
	// speaker-name → persoId strncmp chain from atlantis.exe (VA 0x428f58):
	// a fixed table keyed by a case-insensitive prefix of the speaker name.
	// The player character ("Seth") always resolves to _heroPersoId.
	int resolveSpeakerPersoId(const Common::String &speaker) const;
	// Parse DIALOG\INITDIAL.TXT into _dialInitData (per-perso resource table).
	void loadDialInitData();
	// Pick the talking-head camera (2..5) for a dialog line: honour an explicit
	// camN, else randomise like the original (avoiding the previous cam and any
	// cam whose UBB resource is missing).  See atlantis.exe FUN_0042844c.
	int pickDialCam(int persoId, int explicitCam, bool sethLine = false, bool offLine = false);
	// Play one dialog line: stream its UBB talking-head video over the warp
	// panorama (or just the panorama when off-camera), play the matching APC
	// speech track and overlay the subtitle.  `spfBaseName` names the per-cam
	// DIALOG\PERSO\<base>.SPF mouth/veil overlay composited onto every UBB
	// frame (empty = mouth baked into the UBB).  `camNum` (2..5) selects the
	// per-cam zoom; `hasAngle`/angleX/angleY rotate the panorama under the
	// head; `interactiveDialog` is reserved for the sprite-hiding the NPC
	// layer adds later.  `dialRow` is the INITDIAL row of the speaker: its 3D
	// head (DialogHead, the animated mouth) is drawn over every UBB frame
	// when the line has a .SYC lip-sync track (-1: no head).
	void playSingleConLine(int sectionId, char track, const Common::String &text,
	                       const Common::String &videoName = "",
	                       const Common::String &spfBaseName = "",
	                       bool hasAngle = false, int angleX = 0, int angleY = 0,
	                       int camNum = 2, bool interactiveDialog = true,
	                       int dialRow = -1);
	// True when subtitles are enabled in the ScummVM options.
	bool showSubtitles() const;
	// Draw subject icon sprite `sprIdx` centred at (cx, cy) onto dst (RGB565).
	void drawSubjectSprite(Graphics::Surface &dst, uint sprIdx, int cx, int cy) const;
	// Show the subject (topic) grid for an NPC's enabled subjects and return
	// the chosen option index, or -1 on cancel (right-click).
	int showSubjectMenu(const Common::Array<int> &sujIds);

	// --- 2D sprites (sprite_renderer.cpp) ---------------------------------
	// 2D sprite (SPR): sprite_decode.h.
	typedef SprSprite SubjSprite;
	// Load the subject-menu icon sprites from SPRITE\2D\SUJETS.SPR.
	void loadSubjectSprites();
	// Load inventory item icons from SPRITE\2D\OBJETS1.SPR into _objSprites.
	void loadObjectSprites();
	// Load the inventory-bar background sprite from SPRITE\2D\OBJETS0.SPR.
	void loadInventoryBarSprite();
	// Draw object `obj`'s icon centred in slot rect `r` (clipped to dst only).
	void drawInventoryIcon(Graphics::ManagedSurface &dst, const Object *obj,
	                       const Common::Rect &r);

	// --- FONTMAX gold menu font (sprite_renderer.cpp / menus.cpp) ---------
	// Decode one SPR glyph atlas (sprite_index = char_code - 0x20) into `out`.
	void loadFontSpriteSet(const char *spr, Common::Array<SubjSprite> &out);
	// Load FONTMAX.SPR (+ FONTMAX2.SPR hover) — the gold menu font.
	void loadFontMaxSprites();
	// Shared glyph-sprite blitter: layoutGlyphs drive the pen advance, drawGlyphs
	// supply the pixels (so a hover swap recolours without reflowing).
	void drawSprFontText(Graphics::ManagedSurface &dst, const Common::String &text,
	                     int anchorX, int anchorY,
	                     const Common::Array<SubjSprite> &drawGlyphs,
	                     const Common::Array<SubjSprite> &layoutGlyphs,
	                     int spaceAdvance, const uint8 *advanceTable = nullptr,
	                     uint advanceCount = 0) const;
	void drawFontMaxText(Graphics::ManagedSurface &dst, const Common::String &text,
	                     int anchorX, int anchorY, bool hover = false) const;
	// FONTMIN.SPR — the smaller red player-name font (FONTMIN2 = hover).
	void drawFontMinText(Graphics::ManagedSurface &dst, const Common::String &text,
	                     int anchorX, int anchorY, bool hover = false) const;
	// CREDBLAN.SPR / CREDBLEU.SPR — the credits-roll glyphs; advance widths come
	// from the exe char-width table.  `blue` selects the CREDBLEU bank.
	void loadCreditFontSprites();
	int  creditCharAdvance(unsigned char c) const;
	int  creditStringWidth(const Common::String &text) const;
	void drawCreditText(Graphics::ManagedSurface &dst, const Common::String &text,
	                    int anchorX, int anchorY, bool blue) const;
	// Quit credits slideshow: CREDIT01..10.TGA + CREDITS.TXT overlay text
	// (+ 04GENERI.APC on discs that have it), skippable by key or click.
	void playQuitOutro();

	// --- Music playlist (music.cpp) ----------------------------------------
	// The CON commands startmusik=N / stopmusik start and stop the track;
	// nothing else does: the original's stream goes on through a volume of 0,
	// a place change, a chapter change and a load (FUN_0042de5c), and a load
	// only sets the current volume to the music volume (0x0042e494).
	// Select and (re)play a numbered music track (1-based, matching the
	// startmusik CON command IDs).  The track loops until changed.
	void musicPlayTrack(int trackId);
	// Clear the selection entirely and stop playback — stopmusik (CON),
	// chapter change, and the credits sequence.
	void musicStop();
	// Stop mixer playback and free the decoded buffer without clearing the
	// selected track — used before a track switch.
	void musicHaltPlayback();
	// The original's music commands, with its fades:
	// startmusik (FUN_0042a100 @ 0x0042ad53): the same track only re-arms the
	// fade-in; another one first fades the playing track down to 1 %
	// (blocking), then starts the new one at volume 0 and fades it in.
	void musicStartCommand(int trackId);
	// The credits' switch (FUN_0041a72c @ 0x0041a868): the same, without the
	// same-track test.
	void musicSwitchTrack(int trackId);
	// stopmusik (@ 0x0042ae8a): blocking fade down to 1 %, stop.
	void musicStopCommand();
	// Ducking: fade down by the current percentage (voices: only when idle)
	// and back up.
	void musicDuck(bool onlyWhenIdle, int percent = -1);
	void musicUnduck(int percent = -1);
	AtlantisMusicFade _musicFade;
	static void musicFadeTimer(void *refCon);
	void musicSyncVolume();
	void musicWaitFadeEnd();
	// SPRMENU.SPR — the menu/toolbar decorative golden overlays.
	void loadMenuSprites();
	void blitMenuSprite(Graphics::ManagedSurface &dst, uint idx, int hx, int hy) const;
	// Parse a SPRLIST\*MENU.TXT layout file (base name only) into `out`.
	bool loadMenuLayout(const char *baseName, MenuLayout &out) const;
	// Render a parsed menu onto `dst` (SPRMENU sprites, then FONTMAX labels /
	// toggles; the hovered text item uses the bright FONTMAX2 glyphs), and
	// return each text/toggle item's click rect in `hitRectsOut`.
	void renderMenuLayout(Graphics::ManagedSurface &dst, const MenuLayout &layout,
	                      int hoveredTextIdx, Common::Array<Common::Rect> &hitRectsOut) const;
	// Dim the screen to black (menu/chapter transitions).
	void fadeScreenToBlack();
	// In-game (Escape) menu over the panorama; returns 0 = resume, 28 = load
	// (sets _loadedSave), 40 = quit.
	uint displayInGameMenu();
	// "ARE YOU SURE ?" quit confirmation over `bg`; true = confirmed.
	bool confirmQuit(const Graphics::ManagedSurface &bg);
	// atlantis.exe FUN_0041eda8: the "PLEASE INSERT CD n" prompt (CDnMENU.TXT,
	// disc = discIdx + 1, clamped to 4) over the dimmed screen, until OK or
	// 1000 ticks; then the disc becomes the current one.
	void changeDisc(uint discIdx);
	// 0x00772913: the disc in use, 1 at boot (WinMain takes the first of
	// bigcd1..4.big that opens; all are present here).
	uint8 _currentDisc = 1;
	// Options menu over `bg`: SUBTITLES + OMNI 3D (look-Y) toggles, persisted.
	void displayOptionsMenu(const Graphics::ManagedSurface &bg);
	// Sound menu over `bg`: four draggable volume sliders (master/music/sfx/
	// voices), Left/Right channel test, OK/Cancel.  OK persists and applies
	// the knob-derived volumes (exe formula round((X-305)/213*127)).
	void displaySoundMenu(const Graphics::ManagedSurface &bg);
	// Sound-menu knob X positions, session-persistent like the original's
	// menu record table; 0 = not seeded from SONMENU.TXT yet.
	int _sonMenuKnobX[4];
	// True when the OMNI 3D option inverts the vertical look.
	bool omni3dInvertY() const;
	// LOAD screen: the player's per-episode saves listed by EPI.TXT name, plus
	// RESTART EPISODE / OK / Cancel and a draggable scrollbar.  Returns the
	// 1-based slot to load, or 0 on cancel.  (saveMode/outSaveName reserved.)
	// 0 = Cancel, slot + 1 = load that save, kSavePickerNewGame = the
	// checkpoint to reload does not exist (FUN_0042de5c then starts a new
	// game at chapter 1).
	static const uint kSavePickerNewGame = 0xffffffffu;
	uint displaySavePicker(bool saveMode, Common::String &outSaveName);
	uint displayFilePicker(const Graphics::Surface *bgFrame, bool saveMode,
	                       Common::String &saveName);
	// Render player/save-row text in FONTMIN with the exe's player width table.
	void drawPlayerText(Graphics::ManagedSurface &dst, const Common::String &text,
	                    int anchorX, int anchorY, bool hover,
	                    FontMinRenderer renderer) const;
	// Startup player select / create / delete screen (SELEMENU.TXT over the
	// PREINTRO.TGA title art).  Returns the chosen player id, or -1 to quit.
	int displayPlayerScreen();

	// Game-over screen: full-screen IMAGES\GOVER<NN>.TGA + the narrator's
	// verdict (APC track + subtitle), then return so the caller reloads the
	// chapter checkpoint.  n is the GameOver value; sectionId/track locate the
	// narrator APC.
	void showGameOver(int sectionId, int n, const Common::String &narratorText,
	                  char narratorTrack);

	// --- Inventory / exe tables (exetables.cpp) ---------------------------
	// Extract the static lookup tables baked into atlantis.exe (currently the
	// inventory object-id table); grows as more tables are needed.
	void loadAtlantisExeTables();
	// Build the base _objects catalogue from _inventoryObjectIds (the OBJETS1
	// sprite order), so _objSprites[i] is the icon for _objects[i].
	void setupObjects();
	// Inventory-change callback (Inventory::add/remove invoke it).  A no-op
	// until the toolbar lands, when it will redraw the inventory bar.
	void refreshInventoryToolbar(uint changedSlot);
	// Load a 15-bit uncompressed TGA from the BigFile into a new screen-format
	// surface (caller owns it).  Returns nullptr on failure.
	Graphics::Surface *loadTGA(FileType ft, const char *name);

	// --- Cursors (navigation.cpp) -----------------------------------------
	// RGB565 colour key (pure blue) for the cursor sprites and CursorMan masks.
	static const uint16 kWarpCursorKey = 0x001Fu;
	// Animated warp-view cursor frames decoded from SPRITE\2D\CURSEURS.SPR.
	typedef SprSprite CursorSpr;
	void loadCursorSprites();
	// Install an alpha-blended hardware cursor from sprite pixels + blend factors.
	void setBlendedCursor(uint16 w, uint16 h, const uint16 *pixels,
	                      const uint8 *blend, int hotX, int hotY);
	void showWarpCursorSprite(int sprite);  // CURSEURS.SPR sprite, -1 hides
	static const int kWarpCursorUnset = -2;
public:
	void setArrowCursor();          // plain arrow (CURSEURS.SPR sprite 8)
private:
	// Set the carried-item cursor to object `obj`'s icon; when overlayCursorFrame
	// >= 0 that _cursorSprites frame (an action cursor) is composited on top.
	void setItemCursor(const Object *obj, int overlayCursorFrame = -1);
	// Draw the "just received" item icon centre-bottom (the dialog overlay an
	// NPC-given object shows for the rest of the conversation).
	void drawGivenObject(Graphics::Surface &dst) const;

	// --- NPC / prop scene sprites (sprite_renderer.cpp) -------------------
	// Parse the WAM's SPW sprite list (e.g. SPWATL1.TXT) into _spritePlaceList.
	void loadSpritePlaceList();
	// Parse the WAM's SPF transition-sprite list (e.g. SPFATL1.TXT) into
	// _spriteTransList.  Run once per chapter alongside loadSpritePlaceList.
	void loadSpfList();
	// Reset each current-place sprite to its baseline, then (re)build the NPC
	// hit-test bounds and composite the sprite layer onto _warpSurface.  Called
	// after loadCycloHNM() enters a place.
	void compositeNPCSprites();
	// FUN_00424a20: the WAM NPC rectangles, from the file then the list rows.
	void rebuildNpcRects();
	// Re-composite every active current-place sprite onto _warpSurface from the
	// clean _cycloSurface (so sprite show/hide changes take effect).
	void recompositeSpriteLayer();

	// Decode the first frame of a panoramic cyclorama HNM into _warpSurface
	// (and a clean copy in _cycloSurface), updating _mainPalette.
	bool loadCycloHNM(const Common::String &name);
	// Full-screen HNM playback for UBB_VUE place-to-place transitions and the
	// CINEM/intro cinematics, with the matching WAV\*.APC track when the disc
	// has one (AT*.HNM transitions are silent by design).  SPACE skips.
	// When (fromPlace, toPlace) name a real transition, the SPF walk-on
	// sprites listed for that pair composite onto each video frame so NPCs
	// remain visible as the camera moves between the two places.
	// frameMs: 0 keeps the decoder's default; see kTransitionFrameMs.
	void playTransitionVideo(const Common::String &videoName,
	                         uint fromPlace = uint(-1),
	                         uint toPlace   = uint(-1),
	                         uint frameMs   = 0);
	// FUN_0041bc74 waits until the 200 Hz tick counter 0x006ce118 has moved
	// by 6 ticks between frames, by 13 while CINEMA is playing (0x0049618c,
	// 0x0041bdfc..0x0041be1f); the timer rate is set with
	// AIL_set_timer_frequency(…, 200).
	static const uint kTransitionFrameMs = 6 * 5;
	static const uint kCinemaFrameMs = 13 * 5;

	BigFileArchive _bigFile;
	// The build of the player's atlantis.exe (exe_build.h), set by
	// loadAtlantisExeTables(); the reference until then.
	const AtlantisExeBuild *_exeBuild;
	// The player-row strings of atlantis.exe at 0x00496570: the marker
	// players.atl holds after the last name, then the label shown
	// (language-dependent: "#NOUVEAU JOUEUR" in the GOG French build).
	Common::String _newPlayerMarker, _newPlayerLabel;
	// 0x6cccac[section] of FUN_00426590: game overs narrated per section
	// (cleared at start-up only).
	Common::HashMap<int, uint8> _gameOverCount;
	AtlantisWAMParser _wam;
	// The places' ambient sounds (atlantis-re task 20): FUN_00431580 /
	// FUN_004315b4 after a WAM load (the centre table, never cleared),
	// FUN_00431a1c on entering the warp loop of a place, FUN_00432178 at the
	// end of each warp and bar frame, FUN_00431e98 on the puzzle, game-over,
	// credits, load and quit paths.
	AmbientSounds _ambient;
	class AtlantisAmbientOutput *_ambientOut = nullptr;
	Common::HashMap<int32, AtlantisCentrePos> _ambientCentres;
	void loadAmbientCentres();
	void ambientEnterPlace();
	// FUN_0043259c / FUN_00432758: the ambient glide over a video.
	void ambientPrepareGlide(uint toPlace, int32 frames);
	void ambientVideoFrame();
	void stopAmbient();
	static bool ambientLoadVariant(void *ctx, uint object, uint variant);
	Common::String _currentWAMName;

	Omni3DManagerAtlantis _omni3dMan;

	// CON scenario-script state.
	ConScript _conScript;
	uint _currentCONChapter;   // which CHAPI00N.CON is loaded (0 = none)
	// CON clocks (atlantis.exe FUN_0041c75c, every warp frame).
	ConClocks _conClocks;
	void updateConClocks();
	void restartConTimer(int timer, int value);
	void rebaseConClocks();
	// FUN_0042be3c: run the first matching /tim1, then /tim2, entry while
	// TestTime1 / TestTime2 is set.  True if a section played.  fromWarp:
	// called by the warp loop, whose pointer the section's mouse starts at.
	bool runConTimers(bool fromWarp = false);
	Common::HashMap<Common::String, int> _gameVars;
	ConVariableNames _conVarNames;             // VARIAS.CON, for conditions
	// /con play counters (atlantis.exe 0x006cc6cc), one byte per section:
	// incremented each time a /con entry plays and by Disable=N; zeroed when a
	// game starts, saved with the game.
	Common::Array<byte> _conPlayCounters;
	void bumpConPlayCounter(int section);

	// Per-character dialog resource table parsed from DIALOG\INITDIAL.TXT.
	// camUBB[0..3] = UBB talking-head video basenames for cameras 2..5 ("none"
	//   or empty = unavailable).  camSPF names the per-cam SPF overlay;
	//   anim3DCBase, anim3DABase, animMBase and s3dBase the 3D head's .3DC
	//   mesh, *A.3DA head animation, *M.3DA mouth shapes and .S3D tables
	//   (DialogHead, head3d.h).  Only the first sub-record exists: every
	//   shipped row is "1" and FUN_0042844c always selects sub-record 0
	//   (0x004287c8, 0x00428a8a).
	struct DialInitEntry {
		Common::String camUBB[4];
		Common::String camSPF[4];
		Common::String animMBase;
		Common::String s3dBase;
		Common::String anim3DCBase;
		Common::String anim3DABase;
	};
	Common::HashMap<int, DialInitEntry> _dialInitData;
	int _heroPersoId;          // player character's persoId (INITDIAL "hero" row)

	// Random camera selection per dialog line, and the previous line's cam so
	// consecutive lines never reuse the same shot (atlantis.exe FUN_0042844c).
	int _lastDialCam;          // 0 = no previous line in this dialog session

	// Subject-menu icon sprites decoded from SPRITE\2D\SUJETS.SPR.
	Common::Array<SubjSprite> _subjSprites;

	// Inventory: the object-id table extracted from atlantis.exe (canonical
	// OBJETS1.SPR sprite order), the matching item icons, and the index into
	// _objSprites of an item an NPC just gave the player this dialog (-1 = none).
	Common::Array<uint> _inventoryObjectIds;
	// Guests (atlantis-re: functions/scripts-guests.md).
	AtlantisGuests _guests;
	bool _guestsShown = true;                  // 0x0049613c, initially 1
	Common::Array<int32> _guestSpriteIds;      // exe table 0x00496884
	Common::Array<SubjSprite> _guestSprites;   // SPRITE\2D\SUIVEUR.SPR
	// Chapter 34 bow mode (atlantis.exe 0x00496164): ARCWARP.SPR at
	// (0x2e, 0x79), frame _arrowFrame (0x0049619d) while an arrow flies.
	bool _bowMode = false;
	// A CON section plays from a click (atlantis.exe 0x00496170, set by
	// FUN_00425828 @ 0x004258c4..0x00425b7a and FUN_00425cf4 @
	// 0x00425d2c..0x00425d82): a right click then does not toggle the bar
	// request (FUN_0041a298 @ 0x0041a305).
	bool _sectionPlay = false;
	// Escape in the warp loop opens the in-game menu (navigation.cpp).
	bool escapeOpensMenu() const;
	// IncPerso count (atlantis.exe 0x0077290d, saved in the .gam at 0x11e):
	// lines starting with "Seth" take their INITDIAL row from 512 + it.
	uint8 _incPersoCount = 0;
	// The next dialog line is an "off0" one: its subtitle goes over the
	// screen as it is, the view is not redrawn (FUN_004305e8).
	bool _dialKeepScreen = false;
	// The next dialog line is played by FUN_00430258 (an "off" line, or a line
	// without its .syc), which calls FUN_00425304 once before its wait loop.
	bool _dialVoiceOff = false;
	// Escape ended a talking-head line (0x00496134): playConSection then
	// skips the following lines (conEscapeSkip).
	bool _dialEscapeSkip = false;
	// atlantis.exe 0x00496188: set while a talking-head line plays; the place
	// sprites then leave out the persos (id >= 0x200, FUN_00425304).
	bool _spritesHideNpcs = false;
	// The options menu's SUBTITLES click counter (DAT_00496098, not saved).
	uint8 _subtitleToggleCount = 0;
	// The subtitle font: atlantis.exe FUN_0041ae20 creates GDI Arial, cell
	// height 16 (CreateFontA(16, ..., FW_DONTCARE, DEFAULT_CHARSET, ...,
	// "Arial"), called at 0x00418f7e).  The port uses the metric-compatible
	// Liberation Sans from fonts.dat (as other engines do for Arial), falling
	// back to the built-in GUI font without FreeType.
	Graphics::Font *_subtitleTTF = nullptr;
	bool _subtitleFontTried = false;
	const Graphics::Font *subtitleFont();
	Common::Array<SubjSprite> _bowSprites;
	uint _arrowFrame = 0;
	void drawSprite(Graphics::ManagedSurface &dst, const SubjSprite &spr, int x, int y);
	void drawGuests(Graphics::ManagedSurface &dst, int movingSprite = -1, int movingX = 0);
	// The view as rendered now (after animateSprites), for the bar loops.
	const Graphics::Surface *currentViewSurface() { return _warpLoaded ? _omni3dMan.getSurface() : nullptr; }
	void slideGuest(byte sprite, int fromX, int toX, int step);
	void addGuest(int32 guestId);
	int32 guestAt(int x, int y) const { return _guestsShown ? _guests.at(x, y) : 0; }
	// The portraits' y during a bar slide: -7 a frame up (FUN_0042c2c4),
	// +7 down while below 350 (FUN_0042c59c), only with guests shown.
	void slideGuestPortraits(int dy) {
		if (_guests.count == 0 || !_guestsShown)
			return;
		if (dy < 0 || _guests.portraitY < 350)
			_guests.portraitY += dy;
	}
	void talkToGuest(int32 guestId);
	void subGuest(int32 guestId);
	Common::Array<SubjSprite> _objSprites;
	int _dialogGivenObjSprite;
	SubjSprite _inventoryBarSprite;   // OBJETS0.SPR bar background
	Atlantis_Toolbar _toolbar;

	// Music playlist state (music.cpp).  The raw APC bytes stay resident for
	// the life of the looping stream.
	const char *_musicCurrentFile;
	Audio::SoundHandle _musicHandle;
	byte *_musicRawData;
	uint32 _musicRawSize;
	int _musicTrackId;   // selected startmusik track id (0 = stopped / none)
	// The original's own music state, which the credits test (FUN_0041a72c
	// @ 0x0041a83e): the file handle 0x006ce0c0 != 0 (an open was attempted
	// by FUN_00432ea8, -1 when it failed) and the track 0x00772915 (index + 1,
	// written only once the open succeeded @ 0x00433118).  StopMusik clears
	// both (@ 0x0042af0e / 0x0042af18).
	bool _musicHandleSet;
	int _musicOpenTrack;

	// Ambient SFX playback driven by CON `/set(newsound=N)`.  The id-to-APC
	// mapping lives in atlantis.exe at VA 0x004961e0: a pointer table that
	// starts with 17 WAM names then continues with APC filenames.  newsound
	// IDs are 0-based into the APC portion, so id N maps directly to
	// _newSoundApcNames[N] (== file-table index N+17).  Verified empirically:
	// CON `newsound=14` at CHAPI001/zone 168 plays Toctoc.apc, which is the
	// 15th APC entry in the table (array index 14).  Loaded once at engine
	// start by loadAtlantisExeTables().
	Common::Array<Common::String> _newSoundApcNames;
	Audio::SoundHandle _newSoundHandle;
	// FUN_00432a64, the NewSound poller: 0x006cd87c the value last started,
	// 0x00772912 the playing flag.
	int _newSoundLast = 0;
	bool _newSoundPlaying = false;
	void pollNewSound();
	// Fade the NewSound sample out: 0x006ce128 = the effects volume,
	// 0x006ce104 = the tick 0x006ce118, 0x00495eb0 = 2 (FUN_0041bc74 @
	// 0x0041be88, FUN_00426590 @ 0x00426631); FUN_0042f7a8 then lowers it.
	void fadeOutNewSound();
	uint8 effectsVolume127() const;
	void startNewSoundStream(Audio::AudioStream *apc);

	// FONTMAX gold menu font (FONTMAX.SPR) + its hover/highlight set
	// (FONTMAX2.SPR).  Glyph index = char_code - 0x20.
	Common::Array<SubjSprite> _fontMaxSprites;
	Common::Array<SubjSprite> _fontMaxHoverSprites;
	// FONTMIN player-name font (+ hover), the credits-roll glyph banks and the
	// exe char-width table, and the SPRMENU decorative menu/toolbar overlays.
	Common::Array<SubjSprite> _fontMinSprites;
	Common::Array<SubjSprite> _fontMin2Sprites;
	Common::Array<SubjSprite> _creditBlanSprites;
	Common::Array<SubjSprite> _creditBleuSprites;
	byte _creditCharWidths[256];   // populated by loadAtlantisExeTables
	Common::Array<SubjSprite> _menuSprites;

	// Animated warp-view cursors (CURSEURS.SPR) + current group/frame state.
	Common::Array<CursorSpr> _cursorSprites;
	WarpCursorState _warpCursor;  // FUN_00422fcc animation globals
	int    _warpCursorShown;    // sprite on screen, -1 hidden, kWarpCursorUnset
	uint32 _warpCursorNextMs;   // getMillis() threshold for the next frame

	// NPC / prop scene sprites for the current chapter (parsed from the WAM's
	// SPW list) and the per-place NPC click-target bounds.
	Common::Array<SpritePlaceEntry> _spritePlaceList;

	// Parsed SPF transition-sprite list (e.g. SPFATL1.TXT).
	Common::Array<SpriteTransEntry> _spriteTransList;

	// The WAM NPC entries' click rectangles (from the file, overwritten by
	// the sprite-list loader), parallel to _wam.getNPCEntries().
	Common::Array<NpcClickRect> _npcRects;
	// Persos hidden by /set(hideperso=N) / disableperso=N; showperso=N removes.
	// Cleared on chapter change.
	Common::Array<int> _hiddenPersos;

	// The per-NPC subject table (atlantis.exe 0x00602f10).
	ConSubjects _subjects;

	// The two IX video buffers, one block allocated once and never cleared
	// (atlantis.exe @ 0x00418f83, [0x004960d4]): every UBB_VUE video starts
	// on what the previous one, or the last music copy, left there.
	byte *_ixVideoBuffers;
	// LoadMusik / DelMusik (FUN_0041f1d0 / FUN_0041faa0): the original copies
	// the tracks to its install folder, through the first IX buffer
	// (FUN_0041f380), when they are not there, and deletes them.  The set of
	// tracks "on disk" is kept in the configuration, as the folder is.
	uint32 musicOnDisk() const;
	void setMusicOnDisk(uint32 bits);
	void musicCopyThroughScreenBuffer(uint idx);
	void execMusicList(bool load, const Common::Array<char> &text, uint32 pos);


	// Current chapter / place.
	uint _currentChapter;
	uint _currentPlaceId;
	uint _nextPlaceId;
	const AtlantisPlace *_currentPlace;

	bool _isPlaying;
	int _shotCountdown;   // port-only: polls left before g_system->saveScreenshot()
	// Port-only scripted input (atlantis_input_script): "poll x y button"
	// or "poll shot" lines, applied at that input poll of the puzzle.
	struct InputScriptAction {
		uint poll;
		int x, y, button;
		bool shot;
	};
	Common::Array<InputScriptAction> _inputScript;
	uint _inputScriptPos, _pollCount;
	int _scriptPendingUp;
	void loadInputScript(const Common::String &path);
	bool _testDialogLineDone;   // port-only: atlantis_dialog_line played
	bool _startSetsDone;        // port-only: atlantis_start_sets run
	void playTestDialogLine();
	// Testing aid (port-only): atlantis_state_dump=<file> writes the state
	// a load left (the rows of atlantis-re tools/emu/ui_load_state.py) and
	// ends the game.
	void dumpLoadState(const Common::String &path);
	AbortCommand _abortCommand;
	uint _loadedSave;          // 1-based slot a kAbortLoadGame should re-enter
	bool _conInitRunning;      // a CON /INIT block runs (0x00496168): no checkpoint
	Common::String _players[kMaxPlayers];   // named player profiles (empty = unused)
	int            _currentPlayer;          // owning player for autosave slots
	int            _playerChapter[kMaxPlayers];  // per-player resume chapter
	Common::Array<byte> _epiText;   // SPRLIST\EPI.TXT, read by episodeName

	byte *_mainPalette;

	// _cycloSurface holds the clean cyclo pixels; _warpSurface is the composited display copy.
	Graphics::Surface _cycloSurface;
	Graphics::Surface _warpSurface;
	bool _warpLoaded;

	// Place-sprite animation (atlantis.exe FUN_00425304): the per-slot tick
	// of the last frame step (0x006cc578, never reset) and each view's
	// current animation entry (place byte +4, set by NEWWSPRANIM).
	uint32 _spriteAnimLast[kSpriteAnimSlots];
	uint32 _spriteAnimTick = 0;   // the last tick animateSprites() evaluated
	Common::Array<uint> _spriteAnimCurrent;
	// One FUN_00425304 step for the current view; recomposites when a frame
	// or an active flag changed.
	void animateSprites();
	// One frame of the bar loops (FUN_0042c2c4, FUN_0042c59c, FUN_0042c8ac):
	// the place sprites, then the timers (FUN_0042be3c) with the clocks
	// frozen (no FUN_0041c75c).  played: a timer section ran.  True when the
	// bar must close: GameOver or Exit set, or the engine is quitting.
	bool barFrameTick(bool &played);
	// 0x004960c4 / 0x00496120 while the bar is up (bar_session.h).
	BarSession _barSession;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_ENGINE_H
