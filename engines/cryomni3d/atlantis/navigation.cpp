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

// Warp-view (panoramic) navigation.  This grows over several commits: it
// starts with FPS-style mouse-look, panorama zone hit-testing and click-to-
// navigate; the CURSEURS.SPR warp cursors, NPC hit-testing, the carried-object
// cursor, the toolbar and the in-game menu are layered on as their subsystems
// land.

#include "common/debug.h"
#include "common/events.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/system.h"

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

int CryOmni3DEngine_Atlantis::handleWarp() {
	bool leftButtonPressed = false;
	bool exit = false;
	uint actionId = 0;

	showMouse(true);
	setMousePos(Common::Point(320, 240));

	// Drain stale clicks from the place change / window focus.
	for (int grace = 0; grace < 5 && !shouldAbort(); grace++) {
		pollEvents();
		g_system->delayMillis(20);
	}
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	setMousePos(Common::Point(320, 240));

	// Lock the cursor into SDL relative mode for the look; warpMouse cannot
	// re-centre the pointer on Wayland (WSL), so we read pure per-frame motion.
	g_system->lockMouse(true);
	takeMouseRelative();   // discard motion accumulated before entry

	// FPS mouse-look sensitivity (atlantis.exe FUN_0041bfa0): 600 px/rad yaw,
	// 800 px/rad pitch (the exe's momentum integrator collapses within a frame
	// on modern hardware, so this is the equivalent direct mapping).
	static const double kLookYawDiv    = 600.0;
	static const double kLookPitchDiv  = 800.0;
	static const double kWarpBetaLimit = M_PI / 4.0;  // matches doPlaceChange

	while (!leftButtonPressed && !exit) {
		pollEvents();

		// This frame's accumulated relative motion.  WSLg's Wayland relative
		// pointer inverts X (Y is consistent everywhere); the Windows build
		// takes X as-is.  xDelta must be positive for a rightward look.
		Common::Point lookRel = takeMouseRelative();
#if defined(WIN32)
		int xDelta = lookRel.x;
#else
		int xDelta = -lookRel.x;
#endif
		int yDelta = -lookRel.y;

		// Pin the crosshair to screen centre (every hit-test is at the centre).
		setMousePos(Common::Point(320, 240));
		takeMouseRelative();

		leftButtonPressed = (getCurrentMouseButton() == 1);

		// Hit-test the panorama at the screen centre.
		actionId = 0;
		if (_currentPlace && _warpLoaded) {
			Common::Point pan = _omni3dMan.mapMouseCoords(Common::Point(320, 240));
			pan.y = 768 - pan.y; // flip Y to top-left origin
			int16 hit = _currentPlace->hitTest(pan.x, pan.y);
			if (hit > 0)
				actionId = (uint)hit;
		}

		if (shouldAbort()) {
			actionId = 66666;
			exit = true;
		}

		// Escape leaves the warp (run() turns this into a quit for now).
		if (getNextKey().keycode == Common::KEYCODE_ESCAPE) {
			actionId = 66666;
			exit = true;
			_abortCommand = kAbortQuit;
		}

		// Apply the look and render.
		double alpha = _omni3dMan.getAlpha() - xDelta / kLookYawDiv;
		double beta  = _omni3dMan.getBeta()  + yDelta / kLookPitchDiv;
		while (alpha >= 2.0 * M_PI) alpha -= 2.0 * M_PI;
		while (alpha < 0.0)         alpha += 2.0 * M_PI;
		if (beta < -kWarpBetaLimit) beta = -kWarpBetaLimit;
		if (beta >  kWarpBetaLimit) beta =  kWarpBetaLimit;
		_omni3dMan.setAlpha(alpha);
		_omni3dMan.setBeta(beta);
		// Keep angle vars in sync for CON conditions (AngleWarpX<N).
		_gameVars["anglewarpx"] = (int)(alpha * 180.0 / M_PI);
		_gameVars["anglewarpy"] = (int)(beta  * 180.0 / M_PI);

		if (_warpLoaded) {
			const Graphics::Surface *result = _omni3dMan.getSurface();
			if (result)
				g_system->copyRectToScreen(result->getPixels(), result->pitch,
				                           0, 0, result->w, result->h);
		}
		if (!exit)
			g_system->updateScreen();
		g_system->delayMillis(10);
	}

	g_system->lockMouse(false);
	showMouse(false);

	// Drain the click that ended the loop.
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}

	debugC(2, kDebugScript, "handleWarp: returning action %u", actionId);
	return (int)actionId;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
