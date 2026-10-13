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

#include "cryomni3d/objects.h"
#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Sprite ranges of CURSEURS.SPR, for the fallback cursors.
enum WarpCursorGroup {
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

void CryOmni3DEngine_Atlantis::setItemCursor(const Object *obj, int overlayCursorFrame) {
	if (!obj || _objects.empty() || _objSprites.empty()) return;
	ptrdiff_t idx = obj - &_objects.front();
	if (idx < 0 || (uint)idx >= _objSprites.size()) return;
	const SubjSprite &spr = _objSprites[(uint)idx];
	if (spr.pixels.empty()) return;

	const bool overlay = overlayCursorFrame >= 0 &&
	    (uint)overlayCursorFrame < _cursorSprites.size() &&
	    !_cursorSprites[(uint)overlayCursorFrame].pixels.empty();

	if (!overlay) {
		// No action overlay: the object sprite alone is the cursor.
		setBlendedCursor(spr.w, spr.h, spr.pixels.data(), spr.blend.data(),
		                 spr.xoff, spr.yoff, kSprTransp);
		return;
	}

	// Carrying an object with an action available: composite the object sprite
	// with the action cursor on top, both anchored by their own hotspot.
	const CursorSpr &act = _cursorSprites[(uint)overlayCursorFrame];
	const int hx = MAX((int)spr.xoff, (int)act.xoff);
	const int hy = MAX((int)spr.yoff, (int)act.yoff);
	const int w  = hx + MAX((int)spr.w - spr.xoff, (int)act.w - act.xoff);
	const int h  = hy + MAX((int)spr.h - spr.yoff, (int)act.h - act.yoff);

	Common::Array<uint16> buf;
	Common::Array<uint8>  blendBuf;
	buf.resize((uint)(w * h), kSprTransp);
	blendBuf.resize((uint)(w * h), kSprNoBlend);

	const int ox = hx - spr.xoff, oy = hy - spr.yoff;   // object (base)
	for (int y = 0; y < (int)spr.h; y++)
		for (int x = 0; x < (int)spr.w; x++) {
			uint si = y * spr.w + x;
			if (spr.pixels[si] != kSprTransp) {
				uint di = (oy + y) * w + (ox + x);
				buf[di]      = spr.pixels[si];
				blendBuf[di] = spr.blend[si];
			}
		}
	const int ax = hx - act.xoff, ay = hy - act.yoff;   // action cursor (top)
	for (int y = 0; y < (int)act.h; y++)
		for (int x = 0; x < (int)act.w; x++) {
			uint si = y * act.w + x;
			if (act.pixels[si] != kWarpCursorKey) {
				uint di = (ay + y) * w + (ax + x);
				buf[di]      = act.pixels[si];
				blendBuf[di] = act.blend[si];
			}
		}
	setBlendedCursor((uint16)w, (uint16)h, buf.data(), blendBuf.data(),
	                 hx, hy, kSprTransp);
}

// Show the CURSEURS.SPR sprite FUN_00422fcc drew this frame; -1 hides the
// cursor (nothing drawn).
void CryOmni3DEngine_Atlantis::showWarpCursorSprite(int sprite) {
	if (sprite == _warpCursorShown)
		return;
	_warpCursorShown = sprite;
	if (sprite < 0) {
		showMouse(false);
		return;
	}
	showMouse(true);
	if ((uint)sprite < _cursorSprites.size()) {
		const CursorSpr &spr = _cursorSprites[(uint)sprite];
		if (!spr.pixels.empty())
			setBlendedCursor(spr.w, spr.h, spr.pixels.data(), spr.blend.data(),
			                 spr.xoff, spr.yoff, kWarpCursorKey);
		return;
	}

	// Fallback: hardcoded bitmask cursors when CURSEURS.SPR is not loaded.
	uint16 buf[16 * 16];
	Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
	const int group = (sprite >= 26) ? kWarpCursorTalk : (sprite >= 9 && sprite < 18) ? kWarpCursorNavigate
	                : kWarpCursorDefault;
	switch (group) {
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

	// The cursor follows FUN_00422fcc (warpCursorStep).  When the player is
	// carrying an object the item cursor replaces it.
	_warpCursorShown = kWarpCursorUnset;
	const Object *curHeldObj = nullptr;
	// The arrow frame starts at 0 on every warp entry (0x0041c017).
	_arrowFrame = 0;
	bool bowButtonHeld = (getCurrentMouseButton() == 1);

	while (!leftButtonPressed && !exit) {
		pollEvents();
		updateConClocks();   // atlantis.exe FUN_0041bfa0 @ 0x0041c086

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
		// Bow mode consumes the click (0x0041c631): no zone or character
		// action; the press edge feeds the shot test after the render.
		const bool bowClick = _bowMode && clicked && !bowButtonHeld;
		bowButtonHeld = clicked;
		if (_bowMode)
			clicked = false;

		// Hit-test the panorama at the screen centre: an NPC takes priority
		// over the navigation zone underneath it.
		actionId = 0;
		int centrePerso = 0;
		if (_currentPlace && _warpLoaded) {
			Common::Point pan = hitTestPoint(_omni3dMan.mapMouseCoords(Common::Point(320, 240)));
			npcPanoHitTest(pan, centrePerso);
			actionId = _currentPlace->hitTest(pan.x, pan.y);
		}

		// Cursor: a carried object shows the item cursor; otherwise it reflects
		// what the crosshair is over — talk (NPC) > navigate (zone) > default.
		const Object *heldObj = _inventory.selectedObject();
		if (heldObj) {
			if (heldObj != curHeldObj) {
				showMouse(true);
				setItemCursor(heldObj);
				curHeldObj = heldObj;
				_warpCursorShown = kWarpCursorUnset;   // re-apply once dropped
			}
		} else {
			if (curHeldObj)
				curHeldObj = nullptr;
			// [UNKNOWN] the original steps once per warp-loop frame; the
			// port keeps one step per 80 ms.
			const uint32 now = g_system->getMillis();
			if (_warpCursorShown == kWarpCursorUnset || now >= _warpCursorNextMs) {
				_warpCursorNextMs = now + 80;
				WarpCursorInput in;
				in.zone = actionId;
				in.id = (uint)MAX(centrePerso, 0);
				in.held = 0;
				in.hidden = false;
				in.edgeScroll = false;
				showWarpCursorSprite(warpCursorStep(_warpCursor, in).sprite);
			}
		}

		// A click on an NPC opens the conversation in place (no navigation):
		// leave warp's relative-look mode, run the dialog, then resume looking.
		// atlantis.exe FUN_004278a0: ids 0x100..0x1ff are objects (click scan
		// and pick-up), from 0x200 the conversation (FUN_00425828).
		if (clicked && centrePerso >= 0x100) {
			g_system->lockMouse(false);
			showMouse(true);   // the warp cursor may be hidden
			// FUN_004278a0 starts the click animation (0x004278b5).
			_warpCursor.clickAnim = true;
			if (centrePerso < 0x200)
				clickObject(centrePerso);
			else
				interactNPC(centrePerso);
			if (shouldAbort()) {
				actionId = 66666;
				exit = true;
			} else if (_arrivalScanPending) {
				// A Vue= in the dialog entered another place; the place loop
				// takes over (arrival scan).
				actionId = 66666;
				exit = true;
			} else {
				g_system->lockMouse(true);
				takeMouseRelative();
				setMousePos(Common::Point(320, 240));
				_dialogGivenObjSprite = -1;   // the conversation ended
				_warpCursorShown = kWarpCursorUnset;   // dialog changed the cursor
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

		// Right-click or Space opens the inventory toolbar; Escape leaves the
		// warp (run() turns this into a quit for now — the in-game menu lands
		// with the menus layer).
		Common::KeyCode kc = getNextKey().keycode;
		if (!exit && (getCurrentMouseButton() == 2 || kc == Common::KEYCODE_SPACE)) {
			// Opening the bar in bow mode lowers the bow first (0x0041c6af,
			// FUN_0042d368).
			if (_bowMode)
				lowerBow();
			// Show the current view, switch to a normal pointer, run the bar,
			// then re-arm relative-look mode.
			if (_warpLoaded) {
				const Graphics::Surface *bg = _omni3dMan.getSurface();
				if (bg)
					g_system->copyRectToScreen(bg->getPixels(), bg->pitch, 0, 0, bg->w, bg->h);
			}
			g_system->lockMouse(false);
			showMouse(true);   // the warp cursor may be hidden
			if (_inventory.selectedObject() != nullptr)
				setItemCursor(_inventory.selectedObject());
			else
				setArrowCursor();
			// The bar is drawn over the view with the guest portraits (atlantis.exe
			// FUN_0042c8ac draws them each frame, FUN_0042bd08).
			Graphics::ManagedSurface barFrame;
			const Graphics::Surface *barBg = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
			if (barBg) {
				barFrame.copyFrom(*barBg);
				drawGuests(barFrame);
				g_system->copyRectToScreen(barFrame.getPixels(), barFrame.pitch, 0, 0, barFrame.w, barFrame.h);
				barBg = &barFrame.rawSurface();
			}
			displayToolbar(barBg);
			// Closing the bar (FUN_0042c098): bow mode when the bow is in hand
			// and ArcMagique is set (FUN_0042d098), then the bow rises
			// (FUN_0042d198).
			enterBowMode();
			if (_bowMode)
				raiseBow();
			rebaseConClocks();
			if (_toolbar.guestClicked() != 0 && !shouldAbort()) {
				talkToGuest(_toolbar.guestClicked());
				if (_arrivalScanPending) {
					actionId = 66666;
					exit = true;
				}
			}
			if (shouldAbort()) {
				actionId = 66666;
				exit = true;
			}
			g_system->lockMouse(true);
			takeMouseRelative();
			setMousePos(Common::Point(320, 240));
			_warpCursorShown = kWarpCursorUnset;   // re-apply the warp cursor next frame
			curHeldObj = nullptr;
			while (getCurrentMouseButton() != 0 && !shouldAbort()) {
				pollEvents();
				g_system->delayMillis(10);
			}
		} else if (kc == Common::KEYCODE_ESCAPE) {
			// Open the in-game menu (resume / load / quit).
			g_system->lockMouse(false);
			showMouse(true);   // the warp cursor may be hidden
			uint act = displayInGameMenu();
			if (act == 40) {                 // quit
				actionId = 66666;
				exit = true;
				_abortCommand = kAbortQuit;
			} else if (act == 28) {          // load (displayInGameMenu set _loadedSave)
				actionId = 66666;
				exit = true;
				_abortCommand = kAbortLoadGame;
			} else if (shouldAbort()) {
				actionId = 66666;
				exit = true;
			} else {                          // resume
				rebaseConClocks();
				g_system->lockMouse(true);
				takeMouseRelative();
				setMousePos(Common::Point(320, 240));
				_warpCursorShown = kWarpCursorUnset;
				curHeldObj = nullptr;
			}
		}

		// Apply the look and render.  The OMNI 3D option inverts the pitch.
		int pitchDelta = omni3dInvertY() ? -yDelta : yDelta;
		double alpha = _omni3dMan.getAlpha() - xDelta / kLookYawDiv;
		double beta  = _omni3dMan.getBeta()  + pitchDelta / kLookPitchDiv;
		while (alpha >= 2.0 * M_PI) alpha -= 2.0 * M_PI;
		while (alpha < 0.0)         alpha += 2.0 * M_PI;
		if (beta < -kWarpBetaLimit) beta = -kWarpBetaLimit;
		if (beta >  kWarpBetaLimit) beta =  kWarpBetaLimit;
		_omni3dMan.setAlpha(alpha);
		_omni3dMan.setBeta(beta);
		// Keep angle vars in sync for CON conditions (AngleWarpX<N).
		_gameVars["anglewarpx"] = (int)(alpha * 180.0 / M_PI);
		_gameVars["anglewarpy"] = (int)(beta  * 180.0 / M_PI);

		// Timer sections (atlantis.exe FUN_0041bfa0 @ 0x0041c369).
		if (!exit && runConTimers()) {
			if (shouldAbort()) {
				actionId = 66666;
				exit = true;
			} else if (_arrivalScanPending) {
				// A Vue= in the timer section entered another place; return to
				// the place loop for its arrival scan.  When the original's warp
				// loop returns in this case is not established (atlantis-re:
				// experiments/2026-10-12-15-con-vue.md).
				actionId = 66666;
				exit = true;
			} else {
				g_system->lockMouse(true);
				takeMouseRelative();
				setMousePos(Common::Point(320, 240));
				_dialogGivenObjSprite = -1;
				_warpCursorShown = kWarpCursorUnset;
				while (getCurrentMouseButton() != 0 && !shouldAbort()) {
					pollEvents();
					g_system->delayMillis(10);
				}
			}
		}

		// Place sprites: FUN_0041bfa0 calls FUN_00425304 at 0x0041c3a9, after
		// the timers, unless GameOver or Exit is set (then it leaves, 0x0041c387).
		if (!exit && _gameVars.getValOrDefault("exit", 0) != 0) {
			actionId = 66666;
			exit = true;
		}
		if (!exit)
			animateSprites();

		if (_warpLoaded) {
			const Graphics::Surface *result = _omni3dMan.getSurface();
			if (result && (_bowMode || (_guestsShown && _guests.count))) {
				// The bow, ARCWARP.SPR frame _arrowFrame at (0x2e, 0x79)
				// (0x0041c40b), then the guest portraits (0x0041c489 ->
				// FUN_0042bc58).
				Graphics::ManagedSurface frame;
				frame.copyFrom(*result);
				if (_bowMode && _arrowFrame < _bowSprites.size())
					drawSprite(frame, _bowSprites[_arrowFrame], 0x2e, 0x79);
				if (_guestsShown && _guests.count)
					drawGuests(frame);
				g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
			} else if (result) {
				g_system->copyRectToScreen(result->getPixels(), result->pitch,
				                           0, 0, result->w, result->h);
			}
		}
		if (!exit)
			g_system->updateScreen();
		// Arrow shot (0x0041c61d..0x0041c6a0), after the frame is drawn.
		{
			int tir = _gameVars.getValOrDefault("tirfleche", 0);
			bowFrameStep(_bowMode, bowClick, _gameVars.getValOrDefault("anglewarpy", 0), tir, _arrowFrame);
			_gameVars["tirfleche"] = tir;
		}
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

void CryOmni3DEngine_Atlantis::enterBowMode() {
	// atlantis.exe FUN_0042d098: ObjetEnMain 334 (0x14e), ArcMagique set and
	// not yet in bow mode -> SPRITE\2D\ARCWARP.SPR, bow mode, empty hand.
	const Object *held = _inventory.selectedObject();
	if (!held || held->idOBJ() != 0x14e || _gameVars.getValOrDefault("arcmagique", 0) == 0 || _bowMode)
		return;
	loadFontSpriteSet("ARCWARP.SPR", _bowSprites);
	if (_bowSprites.empty())
		return;
	_bowMode = true;
	_gameVars["objetenmain"] = 0;
	_inventory.setSelectedObject(nullptr);
	debugC(1, kDebugScript, "enterBowMode");
}

void CryOmni3DEngine_Atlantis::showBowAt(int y) {
	animateSprites();
	const Graphics::Surface *view = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
	if (!view)
		return;
	Graphics::ManagedSurface frame;
	frame.copyFrom(*view);
	if (!_bowSprites.empty())
		drawSprite(frame, _bowSprites[0], 0x2e, y);
	g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
	g_system->updateScreen();
	g_system->delayMillis(10);
	pollEvents();
}

void CryOmni3DEngine_Atlantis::raiseBow() {
	// FUN_0042d198: frame 0 at y = 0x1e1 .. 0x83, 10 px per frame; the warp
	// loop then draws it at 0x79.
	for (int y = 0x1e1; y != 0x79 && !shouldAbort(); y -= 10)
		showBowAt(y);
}

void CryOmni3DEngine_Atlantis::lowerBow() {
	// FUN_0042d368: frame 0 at y = 0x79 .. 0x1d7, then bow mode ends and the
	// sprite is freed.
	for (int y = 0x79; y != 0x1e1 && !shouldAbort(); y += 10)
		showBowAt(y);
	_bowMode = false;
	_bowSprites.clear();
	debugC(1, kDebugScript, "lowerBow: bow mode ends");
}

bool CryOmni3DEngine_Atlantis::runPickupToolbar(Object *obj) {
	// The bar over the view and the guest portraits, as for the toolbar key.
	if (_warpLoaded) {
		const Graphics::Surface *bg = _omni3dMan.getSurface();
		if (bg)
			g_system->copyRectToScreen(bg->getPixels(), bg->pitch, 0, 0, bg->w, bg->h);
	}
	Graphics::ManagedSurface barFrame;
	const Graphics::Surface *barBg = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
	if (barBg) {
		barFrame.copyFrom(*barBg);
		drawGuests(barFrame);
		g_system->copyRectToScreen(barFrame.getPixels(), barFrame.pitch, 0, 0, barFrame.w, barFrame.h);
		barBg = &barFrame.rawSurface();
	}
	bool keep = _toolbar.displayPickup(barBg, obj);
	rebaseConClocks();
	return keep;
}

bool CryOmni3DEngine_Atlantis::npcPanoHitTest(const Common::Point &pan,
                                              int &outPersoId) const {
	// atlantis.exe FUN_00422d5c, see npc_click.h.
	uint32 id = npcHitTest(_wam.getNPCEntries(), _npcRects, _spritePlaceList,
	                       (uint16)_currentPlaceId, pan.x, pan.y);
	if (!id)
		return false;
	outPersoId = (int)id;
	return true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
