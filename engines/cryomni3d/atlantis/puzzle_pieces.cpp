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

#include "common/ptr.h"
#include "common/stream.h"
#include "common/system.h"

#include "graphics/managed_surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_pieces.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

RotatingPiecesPuzzle::RotatingPiecesPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _loaded(false) {
	bool ok = loadPuzzleTGA(engine, "PUZSRP2.TGA", _bgPixels);
	ok = loadPuzzleSpr(engine, "PUZSRP2.SPR", _pieceFrames) && ok;
	ok = loadPuzzleSpr(engine, "PUZSRP2C.SPR", _cursorFrames) && ok;
	Common::ScopedPtr<Common::SeekableReadStream> bmp(
	    engine->openBigFileStream(kFileTypePuzzles, "PUZSRP2.BMP"));
	if (bmp) {
		_hitMap.resize((uint)bmp->size());
		bmp->read(_hitMap.data(), _hitMap.size());
	}
	_loaded = ok && !_hitMap.empty();
}

// FUN_00445c00 (background and pieces) and FUN_00445e7c (the cursor, drawn
// into the frame).
void RotatingPiecesPuzzle::render(Graphics::ManagedSurface &out, int mouseX, int mouseY) {
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && out.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(out.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
	// Every piece is drawn with its hotspot at (320, 200): sprite i + slot * 6.
	for (int i = 0; i < RotatingPiecesRules::kNumPieces; i++) {
		const uint idx = (uint)(i + _rules.pieces[i] * 6);
		if (idx < _pieceFrames.size())
			blitFrame(out, _pieceFrames[idx], 320, 200);
	}
	int frame;
	const RotatingPiecesRules::Cursor c = _rules.cursor(_hitMap, mouseX, mouseY, frame);
	if (c == RotatingPiecesRules::kCursorPiece) {
		if ((uint)frame < _cursorFrames.size())
			blitFrame(out, _cursorFrames[frame], mouseX, mouseY);
	} else {
		const SprSprite *spr = _engine->cursorSprite((uint)frame);
		if (spr)
			drawSprSprite(*out.surfacePtr(), *spr, mouseX, mouseY);
	}
}

// FUN_004459c0: on each frame a click (FUN_004460c8), then the drawing and
// the win test, until a result.
int RotatingPiecesPuzzle::run() {
	if (!_loaded)
		return 1;
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	_engine->showMouse(false);   // the cursor is drawn into the frame

	int result = 0;
	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	while (result == 0 && !_engine->shouldAbort()) {
		_engine->pollEvents();
		const Common::Point m = _engine->getMousePos();
		const bool nowButton = (_engine->getCurrentMouseButton() == 1);
		if (nowButton && !prevButton) {
			switch (_rules.click(_hitMap, m.x, m.y)) {
			case RotatingPiecesRules::kClickLeave:
				result = 2;
				break;
			case RotatingPiecesRules::kClickMove:
				_engine->playPuzzleSound("SERP_2_1");
				break;
			case RotatingPiecesRules::kClickReset:
				_engine->playPuzzleSound("SERP_2_2");
				break;
			case RotatingPiecesRules::kClickBoard:
				_engine->playPuzzleSound("SERP_2_3");
				break;
			default:
				break;
			}
		}
		prevButton = nowButton;

		render(working, m.x, m.y);
		if (result == 0 && _rules.solved())
			result = 255;
		g_system->copyRectToScreen(working.getPixels(), working.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();
		g_system->delayMillis(15);
	}
	_engine->showMouse(true);
	return result;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
