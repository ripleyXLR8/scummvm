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
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

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

	// Per-chapter NPC / prop scene-sprite list (SPW).
	loadSpritePlaceList();
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
	// Each WAM references its own SPW list; reload it so the new area's NPCs
	// composite.  (Inventory and CON script are untouched by a sub-WAM swap.)
	loadSpritePlaceList();
	debugC(1, kDebugScript, "Loaded WAM '%s': %u places", wamName.c_str(), _wam.getPlaces().size());
}

void CryOmni3DEngine_Atlantis::changeChapter(int chapter) {
	_currentChapter = (uint)chapter;
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
	// The UBB_VUE place-to-place transition video is played here once the
	// video layer lands; for now this is a direct change — gameStep() enters
	// the new place on its next iteration.
	_nextPlaceId = nextPlaceId;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
