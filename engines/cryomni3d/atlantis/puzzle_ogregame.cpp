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

// The rat-hunter's "ogre & hero" pursuit game (StartPuzzle=2, place 290,
// CHAPI018 subjects 16/17).  Reverse-engineered from atlantis.exe:
//
//   FUN_0044203c  main loop: init, per-frame render + click dispatch,
//                 win/lose/abort return codes.
//   FUN_004422b8  queen placement: random until qx + qy == 10.
//   FUN_00443c3c  click handler: click ON the hero picks him up onto the
//                 cursor; a second click drops him on a masked cell, then
//                 runs the move counter / win check / ogre turn / queen walk.
//   FUN_00443f94  path builder, run per frame while holding: the cell under
//                 the mouse is a legal destination iff its Manhattan
//                 distance from the hero is 1 or 2; the path is origin +
//                 elbow + destination, the elbow being the midpoint of a
//                 straight move or the bend of a diagonal one.  The mask
//                 cells get "lifted tile" markers.
//   FUN_00442360  ogre turn: each ogre steps sign(hero - ogre) on both
//                 axes (clamped to the board); if the target cell is
//                 occupied it takes a free cell from its 3x3 neighbourhood
//                 instead; the move is animated as a 60-px slide.  An ogre
//                 arriving on the hero's cell is the loss.
//   FUN_00442a0c  queen random-walk after every hero move: +/-1 on each
//                 axis (50% sign flips, bouncing off the borders), retried
//                 up to 200 times until the target avoids all three path
//                 cells of the hero's move; she stays put if that fails.
//   FUN_004436f4  every fourth hero move (counter starts at 2): the queen
//                 blinks visible three times -- the player's only clue --
//                 unless she currently shares a cell with an ogre.
//
// Result codes (exe data section): win = 255 (DAT_00496d14), lose = 2
// (DAT_00496d10), abort = 1 (DAT_00496d0c).  Any click outside the board
// strip (x < 0x32 or x > 0x24e) aborts, matching FUN_00443c3c's tail.

#include "common/debug.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_ogregame.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

// Board geometry: 7x7 cells of 60 px (0x3c) anchored at screen (113, 29)
// (0x71, 0x1d) -- confirmed by screenshot match against the original.
static const int kCellSize = 60;
static const int kBoardX   = 113;
static const int kBoardY   = 29;

// PUZCYCLO.SPR frame map.
static const int kFrameHero  = 0;
static const int kFrameQueen = 1;
static const int kFrameOgre  = 2;
static const int kFrameTiles = 3;   // 49 cell tiles, row-major


// Result codes.
static const int kFinWin   = 255;
static const int kFinLose  = 2;
static const int kFinAbort = 1;

OgreGamePuzzle::OgreGamePuzzle(CryOmni3DEngine_Atlantis *engine)
    : _engine(engine), _moveCounter(2), _holding(false), _bgLoaded(false) {
	loadPuzzleSpr(engine, "PUZCYCLO.SPR", _pieceFrames);
	_bgLoaded = loadPuzzleTGA(engine, "PUZCYCLO.TGA", _bgPixels);

	// Records as FUN_0044203c initializes them: hero (0,0), queen placed
	// by placeQueen() with flag 0 (never a mover, never drawn outside the
	// blink/win reveals), ogres at (6,6) and (0,6).
	_rules.p[0].flag = 1; _rules.p[0].x = 0; _rules.p[0].y = 0;
	_rules.p[1].flag = 0;
	_rules.p[2].flag = 1; _rules.p[2].x = 6; _rules.p[2].y = 6;
	_rules.p[3].flag = 1; _rules.p[3].x = 0; _rules.p[3].y = 6;
	placeQueen();

	_rules.occ[_rules.p[2].y * 7 + _rules.p[2].x] = 1;
	_rules.occ[_rules.p[3].y * 7 + _rules.p[3].x] = 1;
}

OgreGamePuzzle::~OgreGamePuzzle() {
}

void OgreGamePuzzle::playSound(const char *name) {
	_sounds.push_back(_engine->playPuzzleSound(name));
}

void OgreGamePuzzle::stopSounds() {
	for (uint i = 0; i < _sounds.size(); i++)
		_engine->_mixer->stopHandle(_sounds[i]);
	_sounds.clear();
}

// FUN_004422b8: two draws of the puzzle LCG per try until qx + qy == 10 --
// one of (4,6) / (5,5) / (6,4), the corner region opposite the hero.
void OgreGamePuzzle::placeQueen() {
	int x, y;
	ogreQueenCell(_engine->_puzzleLcgSeed, x, y);
	_rules.p[1].x = x;
	_rules.p[1].y = y;
}

// FUN_00442360: each ogre steps sign(hero - ogre) per axis, diverting to
// a free 3x3 neighbour when its target cell is occupied, sliding 60 px
// on screen.  Landing on the hero's cell sets the loss.
void OgreGamePuzzle::moveOgres(Graphics::ManagedSurface &working, int &result) {
	for (int o = OgreRules::kOgreA; o < OgreRules::kNumPieces; o++) {
		OgreRules::Piece &ogre = _rules.p[o];
		if (ogre.flag == 1) {
			int sx, sy;
			_rules.ogreStep(o, sx, sy);
			// The exe slides the ogre 60 frames, one pixel per frame, even
			// for a zero step, then takes the new cell.
			const int fromX = ogre.x * kCellSize + kBoardX;
			const int fromY = ogre.y * kCellSize + kBoardY;
			ogre.flag = 0;   // port-only: hidden from the board while it slides
			for (int step = 5; step <= kCellSize && !_engine->shouldAbort();
			     step += 5) {
				_engine->pollEvents();
				renderBoard(working);
				if (kFrameOgre < (int)_pieceFrames.size())
					blitFrame(working, _pieceFrames[kFrameOgre],
					          fromX + sx * step, fromY + sy * step);
				presentFrame(working);
				g_system->delayMillis(10);
			}
			ogre.flag = 1;
			_rules.moveOgre(o, sx, sy);
		}
		if (ogre.x == _rules.p[0].x && ogre.y == _rules.p[0].y) {
			result = kFinLose;
			return;
		}
	}
}

// FUN_00442a0c: after every hero move the queen drifts +/-1 on each axis
// (50% sign flips, bouncing off the borders), retrying up to 200 times
// for a target that avoids all three cells of the hero's move path; she
// stays put when that fails.
void OgreGamePuzzle::walkQueen() {
	CryOmni3DEngine_Atlantis *engine = _engine;
	auto rand32767 = [engine]() { return engine->crtRand(); };   // the C rand
	_rules.walkQueen(rand32767);
}

// Plays a list of OgreDraw events into the working surface: a background
// event is the board's background, unless it comes right before a present,
// where it is the copy of the finished frame to the screen.
void OgreGamePuzzle::play(Graphics::ManagedSurface &working, const OgreDraw::Events &ev) {
	static const char *const kSounds[] = { nullptr, "CYCLO1", "CYCLO2", "CYCLO3", "CYCLO4", "CYCLO5" };
	for (uint i = 0; i < ev.size() && !_engine->shouldAbort(); i++) {
		const OgreEvent &e = ev[i];
		switch (e.kind) {
		case OgreEvent::kBackground:
			if (i + 1 < ev.size() && ev[i + 1].kind == OgreEvent::kPresent)
				break;
			if (_bgLoaded && working.format.bytesPerPixel == 2 && _bgPixels.size() == (uint)kScreenW * kScreenH) {
				for (int y = 0; y < kScreenH; y++)
					memcpy(working.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
			} else {
				working.fillRect(Common::Rect(0, 0, kScreenW, kScreenH), working.format.RGBToColor(24, 16, 8));
			}
			break;
		case OgreEvent::kSprite:
			if (e.set == 'P') {
				if (e.frame >= 0 && e.frame < (int)_pieceFrames.size())
					blitFrame(working, _pieceFrames[e.frame], e.x, e.y);
			} else if (const SprSprite *spr = _engine->cursorSprite((uint)e.frame)) {
				drawSprSprite(*working.surfacePtr(), *spr, e.x, e.y);
			}
			break;
		case OgreEvent::kStdCursor: {
			// FUN_0042d558: CURSEURS frame 6 at the mouse.
			const Common::Point m = _engine->getMousePos();
			if (const SprSprite *spr = _engine->cursorSprite(6))
				drawSprSprite(*working.surfacePtr(), *spr, m.x, m.y);
			break;
		}
		case OgreEvent::kSetCursorPos:
			_engine->setMousePos(Common::Point(e.x, e.y));
			break;
		case OgreEvent::kSound:
			if (e.sound >= 1 && e.sound <= 5)
				playSound(kSounds[e.sound]);
			break;
		case OgreEvent::kPresent:
			presentFrame(working);
			break;
		case OgreEvent::kWait: {
			// e.frame ticks of the 200 Hz timer 0x006ce118.
			const uint32 until = g_system->getMillis() + (uint32)e.frame * 5;
			while (g_system->getMillis() < until && !_engine->shouldAbort()) {
				_engine->pollEvents();
				g_system->delayMillis(10);
			}
			break;
		}
		}
	}
}

void OgreGamePuzzle::presentFrame(Graphics::ManagedSurface &working) {
	g_system->copyRectToScreen(working.getPixels(), working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
}

// FUN_00442b88 without the held overlay, for the ogres' slide.
void OgreGamePuzzle::renderBoard(Graphics::ManagedSurface &working) {
	OgreDraw::Events ev;
	OgreDraw::board(ev, _rules, false, 0, 0);
	play(working, ev);
}

int OgreGamePuzzle::run() {
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	_engine->setMousePos(Common::Point(kScreenW / 2, kScreenH / 2));
	_engine->showMouse(false);   // FUN_004439c4 draws the cursor into the frame

	int  result = 0;
	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	int grabX = 0, grabY = 0;     // 0x007bebe8 / 0x007bebec
	// 0x004960ec / 0x004960f0 as the previous frame left them, after the
	// FUN_004439c4 clamp (0x00442178).
	Common::Point lastPos(kScreenW / 2, kScreenH / 2);

	// FUN_0044203c: a click (FUN_00443c3c), the board (FUN_00442b88), the
	// cursor (FUN_004439c4), the present; it reads no key.
	while (result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		_engine->getNextKey();
		Common::Point m = _engine->getMousePos();

		bool nowButton = (_engine->getCurrentMouseButton() == 1);
		if (nowButton && !prevButton) {
			// On the frame that raises the click flag FUN_0041a298 returns at
			// 0x0041a2be without pumping messages, so the WM_MOUSEMOVE store
			// (0x00419c14) does not run: the click test (0x0044215b..0x00442164)
			// and the rest of that frame read the position the previous frame
			// left, i.e. the one clamped by FUN_004439c4 while the hero is held.
			// See atlantis-re: functions/puzzles-FUN_0044203c.md (FUN_00443c3c).
			m = lastPos;
			if (!_holding) {
				if (_rules.pickUp(m.x, m.y)) {
					// cyclo1; the grab offset is the mouse minus the piece.
					playSound("CYCLO1");
					grabX = m.x - OgreDraw::pieceX(_rules.p[0].x);
					grabY = m.y - OgreDraw::pieceY(_rules.p[0].y);
					_holding = true;
					_rules.p[0].flag = 0;
				}
			} else if (_rules.drop(m.x, m.y)) {
				playSound("CYCLO2");
				_moveCounter++;
				_holding = false;
				if (_moveCounter == 4) {
					OgreDraw::Events ev;
					OgreDraw::reveal(ev, _rules);
					play(working, ev);
					_moveCounter = 0;
				}
				if (_rules.queenCaught()) {
					result = kFinWin;
				} else {
					moveOgres(working, result);
					walkQueen();
				}
			}
			// Any click outside the board strip leaves (FUN_00443c3c tail).
			if (result == 0 && OgreRules::abortClick(m.x))
				result = kFinAbort;
		}
		prevButton = nowButton;
		if (result != 0)
			break;

		OgreDraw::Events ev;
		OgreDraw::board(ev, _rules, _holding, m.x, m.y);
		int cmx = m.x, cmy = m.y;
		OgreDraw::cursor(ev, _holding, grabX, grabY, cmx, cmy);
		lastPos = Common::Point(cmx, cmy);
		play(working, ev);
		presentFrame(working);
		g_system->delayMillis(15);
	}

	// FUN_0044203c after the loop: cyclo4 then the win sequence
	// (FUN_00443378(0), @ 0x00442254..0x00442259), or cyclo6 then the loss
	// sequence (FUN_00442d78, @ 0x00442281..0x00442286); each ends with
	// FUN_0043d0e0, which stops every playing sample (0x00442265, 0x00442295).
	if (result == kFinWin || result == kFinLose) {
		OgreDraw::Events ev;
		if (result == kFinWin) {
			playSound("CYCLO4");
			OgreDraw::win(ev, _rules);
		} else {
			playSound("CYCLO6");
			OgreDraw::lose(ev, _rules);
		}
		play(working, ev);
		stopSounds();
	}
	if (result == 0)
		result = kFinAbort;
	_engine->showMouse(true);

	debugC(1, kDebugScript, "OgreGamePuzzle::run: hero=(%d,%d) queen=(%d,%d) "
	         "ogres=(%d,%d)/(%d,%d) moves=%d result=%d",
	      _rules.p[0].x, _rules.p[0].y, _rules.p[1].x, _rules.p[1].y,
	      _rules.p[2].x, _rules.p[2].y, _rules.p[3].x, _rules.p[3].y,
	      _moveCounter, result);
	return result;
}

} // namespace Atlantis
} // namespace CryOmni3D
