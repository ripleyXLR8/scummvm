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

#include "common/array.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/util.h"

#include "graphics/cursorman.h"
#include "graphics/pixelformat.h"

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Cursor group indices matching CURSEURS.SPR sprite groups.
enum WarpCursorState {
	kWarpCursorDefault  = 0,  // sprites 0–8   — no interactive zone (static)
	kWarpCursorNavigate = 1,  // sprites 9–17  — place transition
	kWarpCursorObject   = 2,  // sprites 18–25 — object interaction
	kWarpCursorTalk     = 3   // sprites 26–45 — NPC interaction
};

// Fallback bitmask cursors used when CURSEURS.SPR is unavailable.
static void buildWarpCursorFallback(uint16 *out, const uint16 *rowMasks, uint16 color, uint16 key) {
	for (int r = 0; r < 16; r++) {
		uint16 mask = rowMasks[r];
		for (int c = 0; c < 16; c++)
			out[r * 16 + c] = (mask & (0x8000u >> c)) ? color : key;
	}
}
static const uint16 kCursorDefaultMask[16] = {
	0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
	0xE000, 0xC800, 0x8400, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000
};
static const uint16 kCursorNavigateMask[16] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0010, 0x0030, 0x0070, 0xFFF0,
	0xFFF8, 0xFFF0, 0x0070, 0x0030, 0x0010, 0x0000, 0x0000, 0x0000
};
static const uint16 kCursorTalkMask[16] = {
	0x0000, 0x1FF0, 0x2004, 0x2448, 0x2448, 0x2004, 0x2814, 0x27E4,
	0x2004, 0x1FF0, 0x0080, 0x0100, 0x0200, 0x0000, 0x0000, 0x0000
};

// Convert one RGB565 sprite pixel to straight-alpha ARGB8888 for a hardware
// cursor.  Blend pixels carry a premultiplied colour (src*factor/32), so the
// colour is un-premultiplied and the factor becomes the alpha.
static uint32 spriteToArgb(uint16 p, uint8 factor, uint16 transpKey) {
	if (p == transpKey)
		return 0;  // fully transparent
	uint r = (p >> 11) & 0x1f;
	uint g = (p >>  5) & 0x3f;
	uint b =  p        & 0x1f;
	uint a = 0xff;
	if (factor != kSprNoBlend) {
		uint f = factor & 0x1f;
		if (f == 0)
			return 0;  // factor 0 == fully transparent edge pixel
		r = MIN<uint>(0x1f, r * 32 / f);
		g = MIN<uint>(0x3f, g * 32 / f);
		b = MIN<uint>(0x1f, b * 32 / f);
		a = f * 255 / 32;
	}
	uint r8 = (r << 3) | (r >> 2);
	uint g8 = (g << 2) | (g >> 4);
	uint b8 = (b << 3) | (b >> 2);
	return (a << 24) | (r8 << 16) | (g8 << 8) | b8;
}

void CryOmni3DEngine_Atlantis::setBlendedCursor(uint16 w, uint16 h,
        const uint16 *pixels, const uint8 *blend, int hotX, int hotY,
        uint16 transpKey) {
	const uint n = (uint)w * h;
	if (g_system->hasFeature(OSystem::kFeatureCursorAlpha)) {
		// ARGB8888 cursor: the backend alpha-composites it, so anti-aliased
		// edges blend the way the original game does.
		Graphics::PixelFormat fmt(4, 8, 8, 8, 8, 16, 8, 0, 24);
		Common::Array<uint32> buf(n);
		for (uint i = 0; i < n; i++)
			buf[i] = spriteToArgb(pixels[i],
			                      blend ? blend[i] : (uint8)kSprNoBlend, transpKey);
		CursorMan.replaceCursor(buf.data(), w, h, hotX, hotY, 0, &fmt);
	} else {
		// No alpha-cursor support: fall back to a key-colour RGB565 cursor.
		Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		Common::Array<uint16> buf;
		buf.assign(pixels, pixels + n);
		for (uint16 &p : buf)
			if (p == transpKey)
				p = kWarpCursorKey;
		CursorMan.replaceCursor(buf.data(), w, h, hotX, hotY, kWarpCursorKey, &fmt);
	}
}

void CryOmni3DEngine_Atlantis::setWarpCursor(int group) {
	_warpCursorGroup = group;
	_warpCursorFrame = -1;   // tickWarpCursor advances to 0 on first call
	_warpCursorNextMs = 0;   // trigger an immediate update
	tickWarpCursor();
}

// The plain Atlantis pointer: CURSEURS.SPR sprite 8 (last frame of the default
// group) — the static arrow used for menus and the subject grid.
void CryOmni3DEngine_Atlantis::setArrowCursor() {
	if (_cursorSprites.size() <= 8)
		return;
	const CursorSpr &spr = _cursorSprites[8];
	if (spr.pixels.empty())
		return;
	setBlendedCursor(spr.w, spr.h, spr.pixels.data(), spr.blend.data(),
	                 spr.xoff, spr.yoff, kWarpCursorKey);
}

void CryOmni3DEngine_Atlantis::tickWarpCursor() {
	static const int kGroupFirst[] = {  0,  9, 18, 26 };
	static const int kGroupCount[] = {  9,  9,  8, 20 };

	if (!_cursorSprites.empty()) {
		int grp = CLIP(_warpCursorGroup, 0, 3);
		// The default cursor is static in the original; only navigate / object
		// / talk animate.
		if (grp == kWarpCursorDefault) {
			if (_warpCursorFrame == 0)
				return;
			_warpCursorFrame = 0;
		} else {
			uint32 now = g_system->getMillis();
			if (now < _warpCursorNextMs) return;
			_warpCursorNextMs = now + 80;  // ~12 fps
			_warpCursorFrame = (_warpCursorFrame + 1) % kGroupCount[grp];
		}
		const CursorSpr &spr = _cursorSprites[kGroupFirst[grp] + _warpCursorFrame];
		if (!spr.pixels.empty())
			setBlendedCursor(spr.w, spr.h, spr.pixels.data(), spr.blend.data(),
			                 spr.xoff, spr.yoff, kWarpCursorKey);
		return;
	}

	// Fallback: hardcoded bitmask cursors when CURSEURS.SPR is not loaded.
	if (_warpCursorFrame == 0) return;
	_warpCursorFrame = 0;
	uint16 buf[16 * 16];
	Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
	switch (_warpCursorGroup) {
	case kWarpCursorNavigate:
		buildWarpCursorFallback(buf, kCursorNavigateMask, 0xFFE0u, kWarpCursorKey);
		CursorMan.replaceCursor(buf, 16, 16, 0, 7, kWarpCursorKey, &fmt);
		break;
	case kWarpCursorTalk:
		buildWarpCursorFallback(buf, kCursorTalkMask, 0x07FFu, kWarpCursorKey);
		CursorMan.replaceCursor(buf, 16, 16, 7, 4, kWarpCursorKey, &fmt);
		break;
	default:
		buildWarpCursorFallback(buf, kCursorDefaultMask, 0xFFFFu, kWarpCursorKey);
		CursorMan.replaceCursor(buf, 16, 16, 0, 0, kWarpCursorKey, &fmt);
		break;
	}
}

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

	// Cursor group reflecting what the crosshair is over (talk / navigate /
	// default), re-applied only when it changes; tickWarpCursor animates it.
	int curCursorGroup = -1;

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

		bool clicked = (getCurrentMouseButton() == 1);

		// Hit-test the panorama at the screen centre: an NPC takes priority
		// over the navigation zone underneath it.
		actionId = 0;
		int centrePerso = 0;
		if (_currentPlace && _warpLoaded) {
			Common::Point pan = _omni3dMan.mapMouseCoords(Common::Point(320, 240));
			pan.y = 768 - pan.y; // flip Y to top-left origin
			npcPanoHitTest(pan, centrePerso);
			int16 hit = _currentPlace->hitTest(pan.x, pan.y);
			if (hit > 0)
				actionId = (uint)hit;
		}

		// Cursor reflects what the crosshair is over: talk (NPC) > navigate
		// (transition zone) > default.  tickWarpCursor() animates it below.
		int wantCursor = (centrePerso > 0) ? kWarpCursorTalk
		               : (actionId > 0)    ? kWarpCursorNavigate
		               :                     kWarpCursorDefault;
		if (wantCursor != curCursorGroup) {
			setWarpCursor(wantCursor);
			curCursorGroup = wantCursor;
		} else {
			tickWarpCursor();
		}

		// A click on an NPC opens the conversation in place (no navigation):
		// leave warp's relative-look mode, run the dialog, then resume looking.
		if (clicked && centrePerso > 0) {
			g_system->lockMouse(false);
			interactNPC(centrePerso);
			if (shouldAbort()) {
				actionId = 66666;
				exit = true;
			} else {
				g_system->lockMouse(true);
				takeMouseRelative();
				setMousePos(Common::Point(320, 240));
				curCursorGroup = -1;   // dialog changed the cursor; re-apply
				// Drain the skip click the dialog ended on so it isn't read as
				// a fresh navigation press next frame.
				while (getCurrentMouseButton() != 0 && !shouldAbort()) {
					pollEvents();
					g_system->delayMillis(10);
				}
				clicked = false;
			}
		}

		leftButtonPressed = clicked;

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

bool CryOmni3DEngine_Atlantis::npcPanoHitTest(const Common::Point &pan,
                                              int &outPersoId) const {
	for (const NPCBound &nb : _npcPlaceBounds) {
		if (nb.panoBounds.contains(pan)) {
			outPersoId = nb.persoId;
			return true;
		}
	}
	return false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
