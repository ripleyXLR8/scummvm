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

// Click-to-abort strip bounds (FUN_00443c3c tail: x < 0x32 or x > 0x24e).
static const int kAbortX0 = 0x32;
static const int kAbortX1 = 0x24e;

// Result codes.
static const int kFinWin   = 255;
static const int kFinLose  = 2;
static const int kFinAbort = 1;

OgreGamePuzzle::OgreGamePuzzle(CryOmni3DEngine_Atlantis *engine)
    : _engine(engine), _moveCounter(2), _holding(false), _bgLoaded(false),
      _rng("AtlantisOgreGame") {
	loadPuzzleSpr(engine, "PUZCYCLO.SPR", _pieceFrames);
	_bgLoaded = loadPuzzleTGA(engine, "PUZCYCLO.TGA", _bgPixels);

	// Records as FUN_0044203c initializes them: hero (0,0), queen placed
	// by placeQueen() with flag 0 (never a mover, never drawn outside the
	// blink/win reveals), ogres at (6,6) and (0,6).
	_pieces[0].flag = 1; _pieces[0].x = 0; _pieces[0].y = 0;
	_pieces[1].flag = 0;
	_pieces[2].flag = 1; _pieces[2].x = 6; _pieces[2].y = 6;
	_pieces[3].flag = 1; _pieces[3].x = 0; _pieces[3].y = 6;
	placeQueen();

	memset(_occupied, 0, sizeof(_occupied));
	_occupied[_pieces[2].y * 7 + _pieces[2].x] = 1;
	_occupied[_pieces[3].y * 7 + _pieces[3].x] = 1;

	memset(_moveMask, 0, sizeof(_moveMask));
	for (int i = 0; i < 3; i++) {
		_pathX[i] = -1;
		_pathY[i] = -1;
	}
}

OgreGamePuzzle::~OgreGamePuzzle() {
}

// FUN_004422b8: roll until qx + qy == 10 -- one of (4,6) / (5,5) / (6,4),
// the corner region opposite the hero.
void OgreGamePuzzle::placeQueen() {
	do {
		_pieces[1].x = (int)_rng.getRandomNumber(6);
		_pieces[1].y = (int)_rng.getRandomNumber(6);
	} while (_pieces[1].x + _pieces[1].y != 10);
}

// FUN_00443f94, run per frame while the hero is on the cursor: mark the
// legal path for the cell under the mouse.  A destination is legal iff
// its Manhattan distance from the hero is 1 or 2; the path the move
// sweeps is origin + elbow + destination, and crossing the queen with
// any of the three is the catch.
void OgreGamePuzzle::buildMoveMask(int mouseX, int mouseY) {
	memset(_moveMask, 0, sizeof(_moveMask));
	for (int i = 0; i < 3; i++) {
		_pathX[i] = -1;
		_pathY[i] = -1;
	}

	const int cx = (mouseX - kBoardX) / kCellSize;
	const int cy = (mouseY - kBoardY) / kCellSize;
	if (mouseX < kBoardX || mouseY < kBoardY || cx > 6 || cy > 6)
		return;

	const int hx = _pieces[0].x, hy = _pieces[0].y;
	const int dx = cx - hx, dy = cy - hy;
	const int dist = ABS(dx) + ABS(dy);
	if (dist >= 1 && dist <= 2) {
		int midX, midY;
		if (dx == 0 || dy == 0) {
			// Straight move: the midpoint (== origin for 1-square moves).
			midX = hx + dx / 2;
			midY = hy + dy / 2;
		} else {
			// Diagonal move: the elbow cell, picked per FUN_00443f94.
			if (ABS(dx) < ABS(dy)) {
				midX = cx;
				midY = hy;
			} else {
				midX = hx;
				midY = cy;
			}
		}
		_moveMask[midY * 7 + midX] = 1;
		_moveMask[cy * 7 + cx]     = 1;
		_pathX[1] = midX; _pathY[1] = midY;
		_pathX[2] = cx;   _pathY[2] = cy;
	}
	_moveMask[hy * 7 + hx] = 1;
	_pathX[0] = hx; _pathY[0] = hy;
}

// FUN_00442360: each ogre steps sign(hero - ogre) per axis, diverting to
// a free 3x3 neighbour when its target cell is occupied, sliding 60 px
// on screen.  Landing on the hero's cell sets the loss.
void OgreGamePuzzle::moveOgres(Graphics::ManagedSurface &working, int &result) {
	for (int o = 2; o < kNumPieces && result == 0; o++) {
		Piece &ogre = _pieces[o];
		if (ogre.flag != 1)
			continue;

		int sx = 0, sy = 0;
		if (ogre.x < _pieces[0].x) sx = 1;
		else if (ogre.x > _pieces[0].x) sx = -1;
		if (ogre.y < _pieces[0].y) sy = 1;
		else if (ogre.y > _pieces[0].y) sy = -1;
		if (ogre.x + sx < 0 || ogre.x + sx > 6) sx = 0;
		if (ogre.y + sy < 0 || ogre.y + sy > 6) sy = 0;

		if (_occupied[(ogre.y + sy) * 7 + ogre.x + sx]) {
			// Target taken (the other ogre): take a free cell from the
			// clamped 3x3 neighbourhood instead (the exe keeps the last
			// free cell its row-major scan finds), or stay.
			int nx = ogre.x, ny = ogre.y;
			const int x0 = MAX(ogre.x - 1, 0), x1 = MIN(ogre.x + 1, 6);
			const int y0 = MAX(ogre.y - 1, 0), y1 = MIN(ogre.y + 1, 6);
			for (int yy = y0; yy <= y1; yy++)
				for (int xx = x0; xx <= x1; xx++)
					if (!_occupied[yy * 7 + xx]) {
						nx = xx;
						ny = yy;
					}
			sx = nx - ogre.x;
			sy = ny - ogre.y;
		}

		if (sx != 0 || sy != 0) {
			_occupied[ogre.y * 7 + ogre.x] = 0;
			// 60-px slide, mirroring the exe's per-pixel redraw loop
			// (5-px steps keep it equally smooth at our frame pacing).
			const int fromX = ogre.x * kCellSize + kBoardX;
			const int fromY = ogre.y * kCellSize + kBoardY;
			ogre.flag = 2;   // exe marks the slider with flag 2
			for (int step = 5; step <= kCellSize && !_engine->shouldAbort();
			     step += 5) {
				_engine->pollEvents();
				renderFrame(working, -1, -1);
				if (kFrameOgre < (int)_pieceFrames.size())
					blitFrame(working, _pieceFrames[kFrameOgre],
					          fromX + sx * step, fromY + sy * step);
				presentFrame(working);
				g_system->delayMillis(10);
			}
			ogre.flag = 1;
			ogre.x += sx;
			ogre.y += sy;
			_occupied[ogre.y * 7 + ogre.x] = 1;
		}

		if (ogre.x == _pieces[0].x && ogre.y == _pieces[0].y)
			result = kFinLose;
	}
}

// FUN_00442a0c: after every hero move the queen drifts +/-1 on each axis
// (50% sign flips, bouncing off the borders), retrying up to 200 times
// for a target that avoids all three cells of the hero's move path; she
// stays put when that fails.
void OgreGamePuzzle::walkQueen() {
	int sx = 1, sy = 1;
	for (int tries = 0; ; tries++) {
		if (_rng.getRandomNumber(32767) > 16000) sx = -sx;
		if (_rng.getRandomNumber(32767) > 16000) sy = -sy;
		if (_pieces[1].x + sx > 6 || _pieces[1].x + sx < 0) sx = -sx;
		if (_pieces[1].y + sy > 6 || _pieces[1].y + sy < 0) sy = -sy;
		if (tries >= 200) {
			sx = 0;
			sy = 0;
			break;
		}
		const int qx = _pieces[1].x + sx, qy = _pieces[1].y + sy;
		bool onPath = false;
		for (int i = 0; i < 3; i++)
			if (qx == _pathX[i] && qy == _pathY[i])
				onPath = true;
		if (!onPath)
			break;
	}
	_pieces[1].x += sx;
	_pieces[1].y += sy;
}

// FUN_004436f4: three visibility flashes -- skipped when the queen shares
// a cell with an ogre (she would read as a phantom ogre).
void OgreGamePuzzle::blinkQueen(Graphics::ManagedSurface &working) {
	for (int o = 2; o < kNumPieces; o++)
		if (_pieces[1].x == _pieces[o].x && _pieces[1].y == _pieces[o].y)
			return;
	for (int flash = 0; flash < 3 && !_engine->shouldAbort(); flash++) {
		_pieces[1].flag = 1;
		renderFrame(working, -1, -1);
		presentFrame(working);
		g_system->delayMillis(200);
		_pieces[1].flag = 0;
		renderFrame(working, -1, -1);
		presentFrame(working);
		g_system->delayMillis(200);
		_engine->pollEvents();
	}
}

// Mirrors FUN_00442b88: background, path markers while holding, then the
// pieces.  The queen only draws when a reveal set her flag.  mouseX < 0
// suppresses the held-piece overlay (used inside animations).
void OgreGamePuzzle::renderFrame(Graphics::ManagedSurface &out,
                                 int mouseX, int mouseY) {
	const Graphics::PixelFormat fmt = out.format;

	if (_bgLoaded && fmt.bytesPerPixel == 2 &&
	    _bgPixels.size() == (uint)kScreenW * kScreenH) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(out.getBasePtr(0, y),
			       _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	} else {
		out.fillRect(Common::Rect(0, 0, kScreenW, kScreenH),
		             fmt.RGBToColor(24, 16, 8));
	}

	if (_holding && mouseX >= 0) {
		buildMoveMask(mouseX, mouseY);
		// Path markers: the cell's own map tile re-blitted with a small
		// lift so the legal cells read as raised.
		for (int cy = 0; cy < 7; cy++)
			for (int cx = 0; cx < 7; cx++) {
				if (!_moveMask[cy * 7 + cx])
					continue;
				const int tile = kFrameTiles + cy * 7 + cx;
				if (tile < (int)_pieceFrames.size())
					blitFrame(out, _pieceFrames[tile],
					          cx * kCellSize + kBoardX - 2,
					          cy * kCellSize + kBoardY - 2);
			}
	}

	// Hero (hidden while on the cursor -- his record flag is 0 then).
	if (_pieces[0].flag == 1 && kFrameHero < (int)_pieceFrames.size())
		blitFrame(out, _pieceFrames[kFrameHero],
		          _pieces[0].x * kCellSize + kBoardX,
		          _pieces[0].y * kCellSize + kBoardY);

	// Queen, only during a reveal.
	if (_pieces[1].flag == 1 && kFrameQueen < (int)_pieceFrames.size())
		blitFrame(out, _pieceFrames[kFrameQueen],
		          _pieces[1].x * kCellSize + kBoardX,
		          _pieces[1].y * kCellSize + kBoardY);

	// Ogres (flag 2 = mid-slide, drawn by the animation instead).
	for (int o = 2; o < kNumPieces; o++)
		if (_pieces[o].flag == 1 && kFrameOgre < (int)_pieceFrames.size())
			blitFrame(out, _pieceFrames[kFrameOgre],
			          _pieces[o].x * kCellSize + kBoardX,
			          _pieces[o].y * kCellSize + kBoardY);

	// The held hero rides the cursor, clamped to the board strip the way
	// FUN_004439c4 clamps the real Windows cursor.
	if (_holding && mouseX >= 0 && kFrameHero < (int)_pieceFrames.size()) {
		const int hxp = CLIP(mouseX, 0x5a, 0x1f4 + 0x71);
		const int hyp = CLIP(mouseY, 0x16, 0x190 + 0x1d);
		blitFrame(out, _pieceFrames[kFrameHero], hxp, hyp);
	}
}

void OgreGamePuzzle::presentFrame(Graphics::ManagedSurface &working) {
	g_system->copyRectToScreen(working.getPixels(), working.pitch,
	                           0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
}

int OgreGamePuzzle::run() {
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	_engine->setMousePos(Common::Point(kScreenW / 2, kScreenH / 2));
	_engine->setArrowCursor();
	_engine->showMouse(true);

	int  result = 0;
	bool prevButton = (_engine->getCurrentMouseButton() != 0);

	while (result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		Common::Point m = _engine->getMousePos();

		bool nowButton = (_engine->getCurrentMouseButton() == 1);
		if (nowButton && !prevButton) {
			if (!_holding) {
				// Pick-up: the hero piece's inner rect, exactly as
				// FUN_00443c3c tests it.
				const int px = _pieces[0].x * kCellSize;
				const int py = _pieces[0].y * kCellSize;
				if (m.x > px + 0x78 && m.x < px + 0x9f &&
				    m.y > py + 0x1d && m.y < py + 0x54) {
					_holding = true;
					_pieces[0].flag = 0;
				}
			} else {
				const int cx = (m.x - kBoardX) / kCellSize;
				const int cy = (m.y - kBoardY) / kCellSize;
				if (m.x >= kBoardX && m.y >= kBoardY && cx < 7 && cy < 7 &&
				    !_occupied[cy * 7 + cx] && _moveMask[cy * 7 + cx] == 1) {
					// Drop accepted: the path arrays already describe this
					// move (buildMoveMask ran on this frame's mouse pos).
					_moveCounter++;
					_pieces[0].x = cx;
					_pieces[0].y = cy;
					_pieces[0].flag = 1;
					_holding = false;

					if (_moveCounter == 4) {
						blinkQueen(working);
						_moveCounter = 0;
					}

					// The catch: the queen stands on a cell of the move's
					// path (FUN_00443c3c's three-record comparison).
					bool caught = false;
					for (int i = 0; i < 3; i++)
						if (_pieces[1].x == _pathX[i] && _pieces[1].y == _pathY[i])
							caught = true;
					if (caught) {
						// Win reveal in place of the unreversed
						// FUN_00443378 celebration: show her and pause.
						result = kFinWin;
						_pieces[1].flag = 1;
						renderFrame(working, -1, -1);
						presentFrame(working);
						g_system->delayMillis(1200);
					} else {
						moveOgres(working, result);
						walkQueen();
					}
				}
			}

			// Any click outside the board strip aborts (FUN_00443c3c tail).
			if (result == 0 && (m.x < kAbortX0 || m.x > kAbortX1))
				result = kFinAbort;
		}
		prevButton = nowButton;

		if (_engine->getNextKey().keycode == Common::KEYCODE_ESCAPE)
			result = kFinAbort;

		renderFrame(working, m.x, m.y);
		presentFrame(working);
		g_system->delayMillis(15);
	}

	if (result == kFinLose) {
		// Lose beat in place of the unreversed FUN_00442d78 sequence.
		renderFrame(working, -1, -1);
		presentFrame(working);
		g_system->delayMillis(1200);
	}
	if (result == 0)
		result = kFinAbort;

	debugC(1, kDebugScript, "OgreGamePuzzle::run: hero=(%d,%d) queen=(%d,%d) "
	         "ogres=(%d,%d)/(%d,%d) moves=%d result=%d",
	      _pieces[0].x, _pieces[0].y, _pieces[1].x, _pieces[1].y,
	      _pieces[2].x, _pieces[2].y, _pieces[3].x, _pieces[3].y,
	      _moveCounter, result);
	return result;
}

} // namespace Atlantis
} // namespace CryOmni3D
