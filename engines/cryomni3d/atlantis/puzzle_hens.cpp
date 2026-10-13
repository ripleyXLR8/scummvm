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

#include "common/system.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_hens.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

HensPuzzle::HensPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _working(nullptr), _loaded(false) {
	bool ok = loadPuzzleTGA(engine, "PUZPOUL1.TGA", _bgPixels);
	ok = loadPuzzleSpr(engine, "PUZPOUL0.SPR", _spr0) && ok;
	ok = loadPuzzleSpr(engine, "PUZPOUL1.SPR", _spr1) && ok;
	ok = loadPuzzleSpr(engine, "PUZPOUL2.SPR", _spr2) && ok;
	_loaded = ok;
	_rules.animator = this;
}

HensPuzzle::~HensPuzzle() {
	delete _working;
}

static void drawFrameOf(Graphics::ManagedSurface &out, const Common::Array<PuzzleSpr> &spr,
                        int frame, int x, int y) {
	if (frame >= 0 && (uint)frame < spr.size())
		blitFrame(out, spr[frame], x, y);
}

// FUN_00444460(a, b, c): a the tilt of the balance, b and c frames of the
// weighing animations.
void HensPuzzle::drawFrame(int a, int b, int c) {
	Graphics::ManagedSurface &out = *_working;
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && out.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(out.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
	for (int i = 0; i < HensRules::kNumMarks; i++) {
		const HensRules::Mark &m = _rules.marks[i];
		if (m.shown > 0)
			drawFrameOf(out, _spr0, i + 4, m.x, m.y);
	}
	drawFrameOf(out, _spr2, c, 0x11c, 0x19f);
	int tx, ty;
	HensRules::targetPos(1, tx, ty);
	drawFrameOf(out, _spr2, b + 0x2f, tx, ty);
	drawFrameOf(out, _spr2, _rules.won * 0x15 + 0x46 + (b < 0x28 ? b : 0), 0xf7, 0x3f);
	for (int n = 2; n < 10; n++) {
		if (n == _rules.carried)
			continue;
		const HensRules::Hen &h = _rules.hens[n];
		if (h.target == 0) {
			drawFrameOf(out, _spr0, n + 6, h.homeX, h.homeY);
		} else if (h.target < HensRules::kMoving) {
			int f = 0;
			if (h.tilt == 1)
				f = a;
			if (h.tilt == -1)
				f = -a;
			if (f < 0)
				f = ABS(f) + 15;
			HensRules::targetPos(h.target, tx, ty);
			drawFrameOf(out, _spr1, f + (n - 2) * 31, tx - 1, ty - 1);
		} else {
			// Falling home: from target n's place (not the hen's), 10 pixels a step.
			HensRules::targetPos(n, tx, ty);
			const int y = ty + (h.target - HensRules::kMoving) * 10;
			if (y >= h.homeY - 8)
				drawFrameOf(out, _spr0, n + 6, h.homeX, h.homeY);
			else
				drawFrameOf(out, _spr1, (n - 2) * 31, tx, y);
		}
	}
}

// FUN_0044484c: the carried hen at the mouse, then the exit cursor
// (CURSEURS.SPR frame 11) over the corners, else the normal one (frame 6,
// FUN_0042d558).
void HensPuzzle::drawCursor(int mouseX, int mouseY) {
	if (_rules.carried != 0)
		drawFrameOf(*_working, _spr1, (_rules.carried - 2) * 31, mouseX, mouseY);
	const SprSprite *spr = _engine->cursorSprite(HensRules::inCorner(mouseX, mouseY) ? 11 : 6);
	if (spr)
		drawSprSprite(*_working->surfacePtr(), *spr, mouseX, mouseY);
}

void HensPuzzle::present() {
	g_system->copyRectToScreen(_working->getPixels(), _working->pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// [UNKNOWN] the exe's frame period: it flips without a timer.
	g_system->delayMillis(15);
}

// A frame of a weighing animation; the cursor is not drawn.
void HensPuzzle::frame(int a, int b, int c) {
	_engine->pollEvents();
	drawFrame(a, b, c);
	present();
}

// FUN_004441e8: on each frame a click (FUN_00444a4c), the drawing
// (FUN_00444460, FUN_0044484c), the corner test, until four weighings won.
int HensPuzzle::run() {
	if (!_loaded)
		return 1;
	_working = new Graphics::ManagedSurface(kScreenW, kScreenH, g_system->getScreenFormat());

	g_system->lockMouse(false);
	_engine->showMouse(false);   // the cursor is drawn into the frame

	static const char *const kSounds[] = { nullptr, "POULE1", "POULE2", "POULE3", "POULE4", "POULE5", nullptr };
	int result = 0;
	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	while (result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		const Common::Point m = _engine->getMousePos();
		const bool nowButton = (_engine->getCurrentMouseButton() == 1);
		if (nowButton && !prevButton) {
			HensRules::Sound snd[2];
			// The exe plays the weighing sound before its animation.
			HensRules probe = _rules;
			probe.animator = nullptr;
			probe.click(m.x, m.y, snd);
			for (int i = 0; i < 2; i++)
				if (kSounds[snd[i]])
					_engine->playPuzzleSound(kSounds[snd[i]]);
			_rules.click(m.x, m.y, snd);
		}
		prevButton = nowButton;

		drawFrame(0, 0, 0);
		drawCursor(m.x, m.y);
		present();
		if (_rules.leave)
			result = 2;
		else if (_rules.solved())
			result = 255;
	}
	_engine->showMouse(true);
	return result;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
