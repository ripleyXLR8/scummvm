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

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_meta.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

// The state atlantis.exe keeps in its memory between the stages: the
// pieces, their count, the picture, the hand and the cursor animation.
struct MetaMemory {
	bool valid;
	MetaGame::Piece pieces[MetaGame::kNumPieces];
	int16 count, picture;
	int32 grabX, grabY, repeat, unkF4, held;
	int16 cursorMode, cursorFrame, cursorStep;
};
static MetaMemory g_metaMemory = { false, {}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

MetaPuzzle::MetaPuzzle(CryOmni3DEngine_Atlantis *engine, int stage)
	: _engine(engine), _loaded(false), _prevButton(false), _bgDone(false), _variant(stage),
	  _prevRight(false) {
	bool ok = loadPuzzleSpr(engine, "PZMETAP1.SPR", _frames);
	Common::ScopedPtr<Common::SeekableReadStream> spr(engine->openBigFileStream(kFileTypePuzzles, "PZMETAP1.SPR"));
	if (spr) {
		Common::Array<byte> d((uint)spr->size());
		spr->read(d.data(), d.size());
		ok = loadCoverage(d.data(), d.size()) && ok;
	} else {
		ok = false;
	}
	// FUN_004472cc reads PZMETAM.BMP whole; the class map is its pixels.
	Common::ScopedPtr<Common::SeekableReadStream> bmp(engine->openBigFileStream(kFileTypePuzzles, "PZMETAM.BMP"));
	if (bmp && bmp->size() >= 0x436 + (int64)kMapSize) {
		bmp->seek(0x436);
		_mapFile.resize(kMapSize);
		bmp->read(_mapFile.data(), _mapFile.size());
	} else {
		ok = false;
	}
	if (g_metaMemory.valid) {
		memcpy(pieces, g_metaMemory.pieces, sizeof(pieces));
		count = g_metaMemory.count;
		picture = g_metaMemory.picture;
		grabX = g_metaMemory.grabX;
		grabY = g_metaMemory.grabY;
		repeat = g_metaMemory.repeat;
		unkF4 = g_metaMemory.unkF4;
		held = g_metaMemory.held;
		cursorMode = g_metaMemory.cursorMode;
		cursorFrame = g_metaMemory.cursorFrame;
		cursorStep = g_metaMemory.cursorStep;
	}
	_loaded = ok;
}

MetaPuzzle::~MetaPuzzle() {
	g_metaMemory.valid = true;
	memcpy(g_metaMemory.pieces, pieces, sizeof(pieces));
	g_metaMemory.count = count;
	g_metaMemory.picture = picture;
	g_metaMemory.grabX = grabX;
	g_metaMemory.grabY = grabY;
	g_metaMemory.repeat = repeat;
	g_metaMemory.unkF4 = unkF4;
	g_metaMemory.held = held;
	g_metaMemory.cursorMode = cursorMode;
	g_metaMemory.cursorFrame = cursorFrame;
	g_metaMemory.cursorStep = cursorStep;
}

int MetaPuzzle::play() {
	if (!_loaded)
		return 1;
	const Object *sel = _engine->heldObject();
	objectInHand = (sel && sel->idOBJ() != uint(-1)) ? (int)sel->idOBJ() : 0;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_00447e40 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	_prevRight = false;
	const int r = run(_variant);
	// FUN_004478d4 emptied the hand and may have set FlagTemp6.
	_engine->clearHeldObject();
	if (broughtFlag)
		_engine->setGameVar("flagtemp6", 1);
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void MetaPuzzle::readInput(int &mx, int &my, bool &click, bool &right) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const uint btn = _engine->getCurrentMouseButton();
	const bool now = (btn == 1), nowRight = (btn == 2);
	mx = m.x;
	my = m.y;
	click = now && !_prevButton;
	// FUN_0041a298 reports the right button while it is down; FUN_00448084
	// keeps its own repeat counter.
	right = nowRight;
	_prevButton = now;
	_prevRight = nowRight;
}

void MetaPuzzle::startInput(int &mx, int &my, bool &click, bool &right) {
	readInput(mx, my, click, right);
}

void MetaPuzzle::boundary(int &mx, int &my, bool &click, bool &right) {
	readInput(mx, my, click, right);
}

void MetaPuzzle::loadPicture(int n) {
	static const char *const kPics[4] = { "PZMETA1.TGA", "PZMETA2.TGA", "PZMETA3.TGA", "PZMETA4.TGA" };
	if (n >= 1 && n <= 4)
		loadPuzzleTGA(_engine, kPics[n - 1], _bgPixels);
}

void MetaPuzzle::reloadMap() {
	map = _mapFile;
}

int MetaPuzzle::currentPlace() {
	return (int)_engine->currentPlaceId();
}

void MetaPuzzle::drawStandardCursor(int mx, int my) {
	// FUN_0042d558 with no object in hand: CURSEURS frame 6.
	if (const SprSprite *spr = _engine->cursorSprite(6))
		drawSprSprite(*_working.surfacePtr(), *spr, mx, my);
}

void MetaPuzzle::drawBackground() {
	// The first copy of a pass restores the background; the second is the
	// frame copy, done by present().
	if (_bgDone)
		return;
	_bgDone = true;
	if (_bgPixels.size() == (uint)kScreenW * kScreenH && _working.format.bytesPerPixel == 2) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(_working.getBasePtr(0, y), _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	}
}

void MetaPuzzle::drawSprite(char bank, int x, int y, int frame) {
	if (bank == 'C') {
		// What FUN_00410430 does with frame -1 (the animation's last step)
		// is unknown; nothing is drawn.
		if (frame < 0)
			return;
		if (const SprSprite *spr = _engine->cursorSprite((uint)frame))
			drawSprSprite(*_working.surfacePtr(), *spr, x, y);
		return;
	}
	if (frame >= 0 && (uint)frame < _frames.size())
		blitFrame(_working, _frames[frame], x, y);
}

void MetaPuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void MetaPuzzle::playSound(int slot) {
	static const char *const kNames[] = { "META1", "META2", "META3", "META4", "META5" };
	if (slot >= 0 && slot < 5)
		_engine->playPuzzleSound(kNames[slot]);
}

bool MetaPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
