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

#include "common/error.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/random.h"
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
