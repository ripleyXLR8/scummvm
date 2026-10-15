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
#include "common/textconsole.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_crab.h"
#include "cryomni3d/atlantis/sprite_decode.h"

namespace CryOmni3D {
namespace Atlantis {

static const int kScreenW = 640;
static const int kScreenH = 480;

static const char *const kPictures[4] = { "CRBPANI.TGA", "CRBFOND1.TGA", "CRBFOND2.TGA", "CRBFOND3.TGA" };
// The spoken comments (the name table 0x004978bc), comment n = entry n - 1.
static const char *const kComments[26] = {
	"ATA0939A", "ATA0939B", "ATA0939C", "ATA0939D", "ATA0939E", "ATA0939F", "ATA0939G", "ATA0939H",
	"ATA0939I", "ATA0939J", "ATA0939K", "ATA0939L", "ATA0940A", "ATA0940B", "ATA0940C", "ATA0940D",
	"ATA0940E", "ATA0940F", "ATA0940G", "ATA0940H", "ATA0940I", "ATA0940J", "ATA0940K", "ATA0943A",
	"ATA0938F", "ATA0938G"
};

// The crab records are static data: their phase and timer carry over from
// one run of the puzzle to the next.
struct CrabMemory {
	bool valid;
	CrabGame::Crab crabs[CrabGame::kNumCrabs];
};
static CrabMemory g_crabMemory = { false, {} };

CrabPuzzle::CrabPuzzle(CryOmni3DEngine_Atlantis *engine)
	: _engine(engine), _picture(0), _loaded(false), _prevButton(false), _bgDone(false) {
	bool ok = loadPuzzleSpr(engine, "CRABE.SPR", _frames);
	for (int i = 0; i < 4; i++)
		ok = loadPuzzleTGA(engine, kPictures[i], _pictures[i]) && ok;
	_loaded = ok;
	if (!ok)
		warning("CrabPuzzle: missing assets (sprites %u)", _frames.size());
	if (g_crabMemory.valid)
		memcpy(crabs, g_crabMemory.crabs, sizeof(crabs));
}

int CrabPuzzle::play() {
	if (!_loaded)
		return 1;
	_working.create(kScreenW, kScreenH, g_system->getScreenFormat());
	g_system->lockMouse(false);
	_engine->showMouse(false);   // FUN_0043e5a4 draws the cursor into the frame
	_prevButton = (_engine->getCurrentMouseButton() != 0);
	// FUN_0043a1e0 seeded the puzzle LCG from the clock.
	lcgSeed = _engine->_puzzleLcgSeed;
	// the shared cursor animation (engine.h PuzzleCursorState)
	cursorMode = _engine->_puzzleCursor.mode;
	cursorFrame = _engine->_puzzleCursor.frame;
	cursorStep = _engine->_puzzleCursor.step;
	const int r = run();
	_engine->_puzzleCursor.mode = cursorMode;
	_engine->_puzzleCursor.frame = cursorFrame;
	_engine->_puzzleCursor.step = cursorStep;
	memcpy(g_crabMemory.crabs, crabs, sizeof(crabs));
	g_crabMemory.valid = true;
	_engine->showMouse(true);
	_working.free();
	return _engine->shouldAbort() ? 1 : r;
}

void CrabPuzzle::readInput(int &mx, int &my, bool &clicked) {
	_engine->pollEvents();
	const Common::Point m = _engine->getMousePos();
	const bool now = (_engine->getCurrentMouseButton() == 1);
	mx = m.x;
	my = m.y;
	clicked = now && !_prevButton;
	_prevButton = now;
}

void CrabPuzzle::startInput(int &mx, int &my, bool &clicked) {
	readInput(mx, my, clicked);
}

void CrabPuzzle::boundary(int &mx, int &my, bool &clicked) {
	readInput(mx, my, clicked);
}

void CrabPuzzle::loadPicture(const char *name) {
	for (int i = 0; i < 4; i++)
		if (scumm_stricmp(name, kPictures[i]) == 0)
			_picture = i;
}

void CrabPuzzle::drawBackground() {
	// The first copy of a pass restores the picture; the second is the
	// frame copy, done by present().
	if (_bgDone)
		return;
	_bgDone = true;
	const Common::Array<uint16> &bg = _pictures[_picture];
	if (bg.size() == (uint)kScreenW * kScreenH && _working.format.bytesPerPixel == 2)
		for (int y = 0; y < kScreenH; y++)
			memcpy(_working.getBasePtr(0, y), bg.begin() + (uint)y * kScreenW, kScreenW * 2);
}

void CrabPuzzle::drawSprite(char bank, int x, int y, int frame) {
	if (bank == 'A')
		return;   // the portraits, overwritten before they are shown
	if (bank == 'C') {
		if (frame < 0)
			return;
		if (const SprSprite *spr = _engine->cursorSprite((uint)frame))
			drawSprSprite(*_working.surfacePtr(), *spr, x, y);
		return;
	}
	if (frame >= 0 && (uint)frame < _frames.size())
		blitFrame(_working, _frames[frame], x, y);
}

void CrabPuzzle::present() {
	_bgDone = false;
	g_system->copyRectToScreen(_working.getPixels(), _working.pitch, 0, 0, kScreenW, kScreenH);
	g_system->updateScreen();
	// The original loop has no wait; its pace on the machines of the time
	// is unknown.
	g_system->delayMillis(15);
}

void CrabPuzzle::playSound(int slot) {
	if (slot == 0) {
		// FUN_0043ce80: the click sound plays on sample 0x006cc560 only when
		// its status is 2 or 8 (done or stopped); while the previous click
		// still plays, the sound is dropped.
		if (_engine->_mixer->isSoundHandleActive(_click))
			return;
		_click = _engine->playPuzzleSound("CRABE1");
		return;
	}
	_engine->playPuzzleSound("CRABE2");
}

void CrabPuzzle::playComment(int n) {
	if (n >= 1 && n <= 26)
		_comment = _engine->playPuzzleSound(kComments[n - 1]);
}

bool CrabPuzzle::commentBusy() {
	return _engine->_mixer->isSoundHandleActive(_comment);
}

void CrabPuzzle::stopSounds() {
	_engine->_mixer->stopHandle(_click);
	_engine->_mixer->stopHandle(_comment);
}

int CrabPuzzle::rand32767() {
	return _engine->crtRand();   // FUN_0043ede8 @ 0x0043ee25
}

bool CrabPuzzle::aborted() const {
	return _engine->shouldAbort();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
