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
#include "graphics/surface.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/con_script.h"
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

// One entry from the WAM's SPW sprite list (e.g. SPWATL1.TXT).
// NPC entries have type==0 and persoId>0; animated props (torches) have type!=0.
struct SpritePlaceEntry {
	uint16 placeId;
	int type;        // 0 = NPC, non-zero = animated prop / wspranim pose
	int frames;      // TXT frame field (animation length comes from the SPW header)
	int persoId;     // character ID (0 for props)
	float angle;     // camera look angle for NPC orientation
	Common::String spwFile;
	// The NPC's "home" place — clickable here, look-only elsewhere.  Encoded
	// as a leading '-' on the angle token in the SPW list.
	bool interactive;
	// Trailing '-' on the placeId token marks the sprite as starting INACTIVE
	// (atlantis.exe sprite-record field +0x04 = 0): it only renders after a
	// /set(ShowPerso=N) (or wspranim activation, added later) flips it active.
	bool startActive;
	mutable bool active;   // runtime render flag; reset to startActive on place load
	mutable int  frame;    // current animation frame
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

// Atlantis: The Lost Tale.
//
// This is the bootable skeleton: it mounts the BigFile archive(s), loads the
// chapter-1 navigation map (WAM) and displays the first place's panoramic
// cyclorama with mouse look.  Place transitions, CON scripting, dialog,
// sprites, menus, saves and the F3DC mouth pipeline are layered on top in
// subsequent commits.
class CryOmni3DEngine_Atlantis : public CryOmni3DEngine {
protected:
	Common::Error run() override;

public:
	CryOmni3DEngine_Atlantis(OSystem *syst, const CryOmni3DGameDescription *gamedesc);
	~CryOmni3DEngine_Atlantis() override;

	bool hasFeature(EngineFeature f) const override;

	// Build a path for a named file of the given type (for files outside the BigFile).
	Common::Path getFilePath(FileType fileType, const Common::String &baseName) const;

	// Open a file from the BigFile archive and return a new stream (caller owns it).
	// Returns nullptr if not found.
	Common::SeekableReadStream *openBigFileStream(FileType fileType,
	                                               const Common::String &baseName) const;

	// CryOmni3DEngine interface.
	void setupPalette(const byte *colors, uint start, uint num) override;
	void makeTranslucent(Graphics::Surface &dst, const Graphics::Surface &src) const override;
	bool displayToolbar(const Graphics::Surface *original) override { return false; }
	bool hasPlaceDocumentation() override { return false; }
	bool displayPlaceDocumentation() override { return false; }
	uint displayOptions() override { return 0; }

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
	void execConSetCommand(const Common::String &cmd);
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
	int pickDialCam(int persoId, int explicitCam);
	// Play one dialog line: stream its UBB talking-head video over the warp
	// panorama (or just the panorama when off-camera), play the matching APC
	// speech track and overlay the subtitle.  `camNum` (2..5) selects the
	// per-cam zoom; `hasAngle`/angleX/angleY rotate the panorama under the
	// head; `interactiveDialog` is reserved for the sprite-hiding the NPC
	// layer adds later.  The F3DC mouth pipeline and SPF overlays layer on
	// top once those subsystems land.
	void playSingleConLine(int sectionId, char track, const Common::String &text,
	                       const Common::String &videoName = "",
	                       bool hasAngle = false, int angleX = 0, int angleY = 0,
	                       int camNum = 2, bool interactiveDialog = true);
	// True when subtitles are enabled in the ScummVM options.
	bool showSubtitles() const;
	// Draw subject icon sprite `sprIdx` centred at (cx, cy) onto dst (RGB565).
	void drawSubjectSprite(Graphics::Surface &dst, uint sprIdx, int cx, int cy) const;
	// Show the subject (topic) grid for an NPC's enabled subjects and return
	// the chosen option index, or -1 on cancel (right-click).
	int showSubjectMenu(const Common::Array<ConSection *> &options);

	// --- 2D sprites (sprite_renderer.cpp) ---------------------------------
	// 2D sprite (SPR) decoded into screen-format pixels plus a parallel
	// per-pixel blend array (kSprNoBlend = opaque; otherwise a 5-bit edge
	// anti-alias coverage factor).  kSprTransp marks transparent pixels.
	struct SubjSprite {
		uint16 w, h;
		int16  xoff, yoff;
		Common::Array<uint16> pixels;  // RGB565; kSprTransp marks transparent pixels
		Common::Array<uint8>  blend;   // per-pixel blend factor; kSprNoBlend = opaque
	};
	static const uint16 kSprTransp = 0x0001;  // sentinel: not a real game colour

	// Decode one sprite's RLE rows (atlantis.exe FUN_0041047f) into spr.pixels
	// and spr.blend.  spr.w/h must already be set and the stream positioned at
	// the per-row offset table.  Shared by every SPR loader.  colorTable maps
	// the 8-bit indices to RGB565.
	void decodeSprite(Common::SeekableReadStream &stream, SubjSprite &spr,
	                  const uint16 *colorTable);
	// Load the subject-menu icon sprites from SPRITE\2D\SUJETS.SPR.
	void loadSubjectSprites();
	// Load inventory item icons from SPRITE\2D\OBJETS1.SPR into _objSprites.
	void loadObjectSprites();

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
	struct CursorSpr {
		uint16 w, h;
		int16  xoff, yoff;
		Common::Array<uint16> pixels;  // RGB565; kWarpCursorKey = transparent
		Common::Array<uint8>  blend;   // per-pixel blend factor; kSprNoBlend = opaque
	};
	void loadCursorSprites();
	// Install an alpha-blended hardware cursor from sprite pixels + blend factors.
	void setBlendedCursor(uint16 w, uint16 h, const uint16 *pixels,
	                      const uint8 *blend, int hotX, int hotY, uint16 transpKey);
	void setWarpCursor(int group);  // set cursor group + reset animation
	void tickWarpCursor();          // advance the animation frame (call each tick)
	void setArrowCursor();          // plain arrow (CURSEURS.SPR sprite 8)

	// --- NPC / prop scene sprites (sprite_renderer.cpp) -------------------
	// Parse the WAM's SPW sprite list (e.g. SPWATL1.TXT) into _spritePlaceList.
	void loadSpritePlaceList();
	// Reset each current-place sprite to its baseline, then (re)build the NPC
	// hit-test bounds and composite the sprite layer onto _warpSurface.  Called
	// after loadCycloHNM() enters a place.
	void compositeNPCSprites();
	// Rebuild the NPC click-target list (_npcPlaceBounds) for the current place.
	void rebuildNpcPlaceBounds();
	// Re-composite every active current-place sprite onto _warpSurface from the
	// clean _cycloSurface (so sprite show/hide changes take effect).
	void recompositeSpriteLayer();

	// Decode the first frame of a panoramic cyclorama HNM into _warpSurface
	// (and a clean copy in _cycloSurface), updating _mainPalette.
	bool loadCycloHNM(const Common::String &name);

	static const LevelInitialState kChapterInitialStates[];

	BigFileArchive _bigFile;
	AtlantisWAMParser _wam;
	Common::String _currentWAMName;

	Omni3DManager _omni3dMan;

	// CON scenario-script state.
	ConScript _conScript;
	uint _currentCONChapter;   // which CHAPI00N.CON is loaded (0 = none)
	uint32 _chapterStartMs;    // wall-clock at chapter load (for `chaptertime`)
	Common::HashMap<Common::String, int> _gameVars;
	Common::Array<byte> _placeVisits;          // per-place visit counts (nbvisite)
	mutable Common::RandomSource _conRng;      // for /sel `random` conditions

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
	Common::Array<SubjSprite> _objSprites;
	int _dialogGivenObjSprite;

	// Animated warp-view cursors (CURSEURS.SPR) + current group/frame state.
	Common::Array<CursorSpr> _cursorSprites;
	int    _warpCursorGroup;    // WarpCursorState: 0=default 1=navigate 2=object 3=talk
	int    _warpCursorFrame;    // current frame index within the group
	uint32 _warpCursorNextMs;   // getMillis() threshold for the next frame

	// NPC / prop scene sprites for the current chapter (parsed from the WAM's
	// SPW list) and the per-place NPC click-target bounds.
	Common::Array<SpritePlaceEntry> _spritePlaceList;
	struct NPCBound {
		int persoId;
		Common::Rect panoBounds;  // panoramic coords (Y=0 at top, exclusive right/bottom)
	};
	Common::Array<NPCBound> _npcPlaceBounds;
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

	byte *_mainPalette;

	// _cycloSurface holds the clean cyclo pixels; _warpSurface is the composited display copy.
	Graphics::Surface _cycloSurface;
	Graphics::Surface _warpSurface;
	bool _warpLoaded;

	int    _propAnimFrames;  // total frame count (0 = no animated prop at current place)
	int    _propAnimFrame;
	uint32 _propAnimNextMs;  // g_system->getMillis() threshold for next frame advance
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_ENGINE_H
