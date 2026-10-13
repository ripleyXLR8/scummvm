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
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/con_eval.h"
#include "cryomni3d/atlantis/inventory.h"
#include "cryomni3d/atlantis/con_script.h"
#include "cryomni3d/atlantis/menu_layout.h"
#include "cryomni3d/atlantis/npc_click.h"
#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/saveload_format.h"
#include "cryomni3d/atlantis/sprite_anim.h"
#include "cryomni3d/atlantis/sprite_decode.h"
#include "cryomni3d/atlantis/toolbar.h"
#include "cryomni3d/atlantis/warp_cursor.h"
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

// The place each chapter starts in (placeId) and the initial camera angles.
struct LevelInitialState {
	uint placeId;
	float alpha;
	float beta;
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
	Common::Error loadGameState(int slot) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	Common::String getSaveStateName(int slot) const override;
	// Atlantis writes its own per-episode checkpoint on chapter entry; disable
	// ScummVM's periodic autosave so it cannot overwrite a checkpoint slot.
	int getAutosaveSlot() const override { return -1; }

	// Build a path for a named file of the given type (for files outside the BigFile).
	Common::Path getFilePath(FileType fileType, const Common::String &baseName) const;

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
	void setupChapterWAM(int chapter);
	// Switch to the given chapter: load its WAM/CON and enter the start place.
	void changeChapter(int chapter);
	// Enter _nextPlaceId: load its cyclo panorama and point the viewer at it.
	void doPlaceChange();
	// Transition to the given place (currently a direct change; the UBB_VUE
	// transition video is added with the dialog/video layer).
	void executeTransition(uint nextPlaceId);
	// Load a named WAM over the current one (a CON /set(changewam=…) sub-WAM or
	// a save's stored WAM), without touching the inventory or CON script.
	void setupWAMByName(const Common::String &wamName);

	// --- Save / load (saveload.cpp) ---------------------------------------
	// Serialize / restore the full game state to a 1-based save slot.
	void saveGame(uint saveNum, const Common::String &saveName);
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
	bool _arrivalScanPending = false;
	uint _arrivalFromVue = 0;
	// CON /set(StartPuzzle=N) — run minigame N and write FinPuzzle back
	// (255 = win; smaller values denote per-puzzle loss variants).  Puzzles
	// without a handler auto-win so their CHAPI script chains keep
	// progressing past the gate.
	void playPuzzle(int puzzleId);
	void runPuzzleZone(int puzzleId);
	void runSelScan();
	// Evaluate a /sel section's condition list (left-to-right short-circuit).
	bool evalSelSection(const ConSection &sec, int departZone, int currentVue) const;
	// Execute a fired section's body (its /set commands and /go redirect; the
	// dialog lines are played once the dialog layer lands).
	void playConSection(ConSection &sec);
	// Fire the first matching arrival/departure /sel section, in file order.
	void runArrivalSelSections(int arrivedVue, int fromVue);
	void runDepartureSelSections(int departZone);

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
	// Per-NPC enabled-subject table (atlantis.exe 0x602f10): /set(enablesuj)
	// appends, /set(disablesuj) removes.  sujsForPerso creates-on-demand.
	Common::Array<int> &sujsForPerso(int persoId);
	bool isSujEnabled(int persoId, int sujId) const;

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
	int pickDialCam(int persoId, int explicitCam, bool sethLine = false);
	// Play one dialog line: stream its UBB talking-head video over the warp
	// panorama (or just the panorama when off-camera), play the matching APC
	// speech track and overlay the subtitle.  `spfBaseName` names the per-cam
	// DIALOG\PERSO\<base>.SPF mouth/veil overlay composited onto every UBB
	// frame (empty = mouth baked into the UBB).  `camNum` (2..5) selects the
	// per-cam zoom; `hasAngle`/angleX/angleY rotate the panorama under the
	// head; `interactiveDialog` is reserved for the sprite-hiding the NPC
	// layer adds later.  The F3DC mouth pipeline layers on top once that
	// subsystem lands.
	void playSingleConLine(int sectionId, char track, const Common::String &text,
	                       const Common::String &videoName = "",
	                       const Common::String &spfBaseName = "",
	                       bool hasAngle = false, int angleX = 0, int angleY = 0,
	                       int camNum = 2, bool interactiveDialog = true);
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
	// The CON commands startmusik=N / stopmusik own the *selection*
	// (_musicTrackId); musicUpdate() keeps the mixer in sync every frame
	// (pausing while music is muted, resuming the selected track on unmute
	// or after a chapter change).
	// Select and (re)play a numbered music track (1-based, matching the
	// startmusik CON command IDs).  The track loops until changed.
	void musicPlayTrack(int trackId);
	// Clear the selection entirely and stop playback — stopmusik (CON),
	// chapter change, and the credits sequence.
	void musicStop();
	// Stop mixer playback and free the decoded buffer without clearing the
	// selected track — used for the mute state and before a track switch.
	void musicHaltPlayback();
	// Per-frame mixer sync (see above).
	void musicUpdate();
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
	void playTransitionVideo(const Common::String &videoName,
	                         uint fromPlace = uint(-1),
	                         uint toPlace   = uint(-1));

	static const LevelInitialState kChapterInitialStates[];

	BigFileArchive _bigFile;
	AtlantisWAMParser _wam;
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
	// TestTime1 / TestTime2 is set.  True if a section played.
	bool runConTimers();
	Common::HashMap<Common::String, int> _gameVars;
	ConVariableNames _conVarNames;             // VARIAS.CON, for conditions
	// /con play counters (atlantis.exe 0x006cc6cc), one byte per section:
	// incremented each time a /con entry plays and by Disable=N; zeroed when a
	// game starts, saved with the game.
	Common::Array<byte> _conPlayCounters;
	void bumpConPlayCounter(int section);
	Common::Array<byte> _placeVisits;          // per-place visit counts (nbvisite)

	// Per-character dialog resource table parsed from DIALOG\INITDIAL.TXT.
	// camUBB[0..3] = UBB talking-head video basenames for cameras 2..5 ("none"
	//   or empty = unavailable).  camSPF/animMBase/s3dBase/anim3DCBase name the
	//   per-cam SPF overlay, mouth vertex animation, scene-graph mesh and body
	//   mesh used by the F3DC mouth pipeline (consumed once that layer lands).
	struct DialInitEntry {
		Common::String camUBB[4];
		Common::String camSPF[4];
		Common::String animMBase;
		Common::String s3dBase;
		Common::String anim3DCBase;
	};
	Common::HashMap<int, DialInitEntry> _dialInitData;
	int _heroPersoId;          // player character's persoId (INITDIAL "hero" row)

	// Random camera selection per dialog line, and the previous line's cam so
	// consecutive lines never reuse the same shot (atlantis.exe FUN_0042844c).
	Common::RandomSource _dialRng;
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
	// IncPerso count (atlantis.exe 0x0077290d, saved in the .gam at 0x11e):
	// lines starting with "Seth" take their INITDIAL row from 512 + it.
	uint8 _incPersoCount = 0;
	// The next dialog line is an "off0" one: its subtitle goes over the
	// screen as it is, the view is not redrawn (FUN_004305e8).
	bool _dialKeepScreen = false;
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
	void slideGuest(byte sprite, int fromX, int toX, int step);
	void addGuest(int32 guestId);
	int32 guestAt(int x, int y) const { return _guestsShown ? _guests.at(x, y) : 0; }
	void talkToGuest(int32 guestId);
	void subGuest(int32 guestId);
	Common::Array<SubjSprite> _objSprites;
	int _dialogGivenObjSprite;
	SubjSprite _inventoryBarSprite;   // OBJETS0.SPR bar background
	Atlantis_Toolbar _toolbar;

	// Music playlist state (music.cpp).  The raw APC bytes stay resident for
	// the life of the looping stream; _musicTrackId survives a halt so a
	// muted/halted track resumes on the next musicUpdate().
	const char *_musicCurrentFile;
	Audio::SoundHandle _musicHandle;
	byte *_musicRawData;
	uint32 _musicRawSize;
	int _musicTrackId;   // selected startmusik track id (0 = stopped / none)

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

	// Per-NPC enabled-subject state (keyed by persoId).
	struct SujState {
		int persoId;
		Common::Array<int> sujs;
	};
	Common::Array<SujState> _sujEnabled;

	// Panorama alpha captured when an NPC interaction starts (the direction
	// the player faced on the click); the baseline for a line's default
	// angle.  Set true only during an NPC interaction — the sprite/NPC layer
	// that drives that lands later, so for now /sel-triggered dialog falls
	// back to the live panorama alpha.
	double _dialBaseAlpha;
	bool   _dialBaseAlphaValid;

	// Current chapter / place.
	uint _currentChapter;
	uint _currentPlaceId;
	uint _nextPlaceId;
	const AtlantisPlace *_currentPlace;

	bool _isPlaying;
	AbortCommand _abortCommand;
	uint _loadedSave;          // 1-based slot a kAbortLoadGame should re-enter
	bool _conInitRunning;      // a CON /INIT block runs (0x00496168): no checkpoint
	bool _autosavePending;     // a chapter change armed the checkpoint write
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
	Common::Array<uint> _spriteAnimCurrent;
	// One FUN_00425304 step for the current view; recomposites when a frame
	// or an active flag changed.
	void animateSprites();
	// One frame of the bar loops (FUN_0042c2c4, FUN_0042c59c, FUN_0042c8ac):
	// the place sprites, then the timers (FUN_0042be3c) with the clocks
	// frozen (no FUN_0041c75c).  played: a timer section ran.  True when the
	// bar must close: GameOver or Exit set, or the engine is quitting.
	bool barFrameTick(bool &played);
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_ENGINE_H
