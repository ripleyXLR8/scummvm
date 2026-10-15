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

#include "cryomni3d/atlantis/puzzle_eclipse.h"

#include "cryomni3d/atlantis/engine.h"

#include "audio/audiostream.h"
#include "audio/decoders/apc.h"
#include "audio/mixer.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/memstream.h"
#include "common/rect.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace CryOmni3D {
namespace Atlantis {

// ===========================================================================
// Constants reverse-engineered from atlantis.exe.
// ===========================================================================

static const int kScreenW = 640;
static const int kScreenH = 480;

// Return codes -- .rdata at 0x496d0c (abort) and 0x496d14 (win).
static const int kFinPuzzleWin   = 255;
static const int kFinPuzzleAbort = 1;

// Layer-swap thresholds for the moon (record 5) vs earth (record 6)
// rendering order -- FUN_00441828 lines 441956-44196b.
static const int kLayerSwapLo = 0x60;    // 96
static const int kLayerSwapHi = 0x83;    // 131

// Special PZ1L sprite frames used by the win highlight overlay --
// FUN_00441828 lines 441a8c / 441ab8.
static const int kHighlightFrameSun  = 0xd5;  // 213
static const int kHighlightFrameMoon = 0xd6;  // 214

// Win-animation length -- FUN_00441724 lines 441813-441818 (`< 0x50` iterations).
static const int kWinAnimFrames = 0x50;     // 80

static const bool kRecordUsesPz1l[7] = { false, false, false, false, true, true, true };

// The record table is static data (0x0049745e): records 0 and 1, which
// cycle on their own, keep their frame from one entry of the puzzle to the
// next (FUN_0044154c resets only records 2..6).
struct EclipseMemory {
	bool valid;
	int curr0, curr1;
};
static EclipseMemory g_eclipseMemory = { false, 0, 0 };

// ===========================================================================
EclipsePuzzle::EclipsePuzzle(CryOmni3DEngine_Atlantis *engine)
    : _engine(engine), _frameCounter(0), _bgLoaded(false) {
	loadPuzzleSpr(engine, "PZ1L.SPR", _pz1lFrames);
	loadPuzzleSpr(engine, "PZ2L.SPR", _pz2lFrames);
	_bgLoaded = loadPuzzleTGA(engine, "PZLION.TGA", _bgPixels);

}

EclipsePuzzle::~EclipsePuzzle() {
}

// Mirrors FUN_00441828 in full.
void EclipsePuzzle::renderFrame(Graphics::ManagedSurface &out) {
	const Graphics::PixelFormat fmt = out.format;

	// 1. Background.
	if (_bgLoaded && fmt.bytesPerPixel == 2 &&
	    _bgPixels.size() == (uint)kScreenW * kScreenH) {
		for (int y = 0; y < kScreenH; y++)
			memcpy(out.getBasePtr(0, y),
			       _bgPixels.begin() + (uint)y * kScreenW, kScreenW * 2);
	} else {
		out.fillRect(Common::Rect(0, 0, kScreenW, kScreenH),
		             fmt.RGBToColor(8, 12, 32));
	}

	// 2. Records 0..3: tick + blit PZ2L (FUN_00441828 lines 38212-38216).
	for (int i = 0; i < 4; i++) {
		_rules.tick(i);
		if (_rules.r[i].curr >= 0 && (uint)_rules.r[i].curr < _pz2lFrames.size())
			blitFrame(out, _pz2lFrames[_rules.r[i].curr],
			          _rules.r[i].x, _rules.r[i].y);
	}

	// 3. Records 4..6: tick first (FUN_00441828 lines 38217-38219).
	_rules.tick(4);
	_rules.tick(5);
	_rules.tick(6);

	// 4. Layer-swap: default render order is (record6 back, record4
	// middle, record5 front).  When record5.CURR is in (96, 131) the
	// exe swaps records 5 and 6 -- mirrors lines 441948-441974.
	int backIdx  = 6;
	int frontIdx = 5;
	if (_rules.r[5].curr > kLayerSwapLo && _rules.r[5].curr < kLayerSwapHi) {
		backIdx  = 5;
		frontIdx = 6;
	}

	auto blitPz1l = [&](int recIdx, int frameOverride) {
		int frame = (frameOverride >= 0) ? frameOverride : _rules.r[recIdx].curr;
		if (frame >= 0 && (uint)frame < _pz1lFrames.size())
			blitFrame(out, _pz1lFrames[frame],
			          _rules.r[recIdx].x, _rules.r[recIdx].y);
	};

	blitPz1l(backIdx, -1);
	blitPz1l(4, -1);              // SUN
	blitPz1l(frontIdx, -1);

	// 5. Win highlight (FUN_00441828 lines 441a26-441ae1): when the win
	// condition holds AND (_frameCounter & 2) != 0, draw three extra
	// PZ1L blits to pulse the eclipse alignment:
	//   - record6 at its current frame (redrawn on top for z-order);
	//   - PZ1L frame 213 at record 4's position (SUN highlight sprite);
	//   - PZ1L frame 214 at record 5's position (MOON highlight sprite).
	if (_rules.isWin() && (_frameCounter & 2)) {
		blitPz1l(6, -1);
		blitPz1l(4, kHighlightFrameSun);
		blitPz1l(5, kHighlightFrameMoon);
	}

}

// Open WAV/<name>.APC, queue and play.  When `wait`, blocks until the
// sound finishes (used for the win sound which leads straight to puzzle
// exit).  Click sounds fire-and-forget.
void EclipsePuzzle::playSound(const char *name, bool wait) {
	Common::String fname = Common::String::format("%s.APC", name);
	Common::SeekableReadStream *apcFile =
	    _engine->openBigFileStream(kFileTypeSound, fname);
	if (!apcFile) {
		warning("EclipsePuzzle: cannot open WAV\\%s", fname.c_str());
		return;
	}
	Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
	if (!apc) { delete apcFile; return; }
	int64 remaining = apcFile->size() - apcFile->pos();
	if (remaining > 0) {
		byte *buf = new byte[remaining];
		apcFile->read(buf, (uint32)remaining);
		apc->queuePacket(new Common::MemoryReadStream(buf, remaining,
		                                              DisposeAfterUse::YES));
	}
	apc->finish();
	delete apcFile;

	Audio::SoundHandle handle;
	// Pan 0x40, volume 0x6e (FUN_0043d150 in FUN_0044154c's set-up).
	_engine->_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, apc, -1,
	                            CryOmni3DEngine_Atlantis::kPuzzleSampleVolume, 0);
	_handles.push_back(handle);
	if (wait) {
		while (!_engine->shouldAbort() &&
		       _engine->_mixer->isSoundHandleActive(handle)) {
			_engine->pollEvents();
			g_system->delayMillis(15);
		}
	}
}

void EclipsePuzzle::stopSounds() {
	for (uint i = 0; i < _handles.size(); i++)
		_engine->_mixer->stopHandle(_handles[i]);
	_handles.clear();
}

int EclipsePuzzle::run() {
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface working(kScreenW, kScreenH, fmt);

	g_system->lockMouse(false);
	_engine->setMousePos(Common::Point(kScreenW / 2, kScreenH / 2));
	_engine->showMouse(false);   // FUN_00441b2c draws the cursor into the frame

	// FUN_0044154c's loop: the click, the exit test, a frame, then the win
	// test.  It reads no key.  0x007bebf0, the highlight counter, is 0
	// (FUN_0043a1e0 clears it) and moves only in the win animation.
	_frameCounter = 0;
	if (g_eclipseMemory.valid) {
		_rules.r[0].curr = g_eclipseMemory.curr0;
		_rules.r[1].curr = g_eclipseMemory.curr1;
	}
	int  result = kFinPuzzleAbort;
	bool prevButton = (_engine->getCurrentMouseButton() != 0);
	auto present = [&](bool cursor) {
		// FUN_00441b2c (0x00441b2c..0x00441c5d), after the render: over the
		// two exit zones CURSEURS (0x006cd8b8) frame 11 at the mouse into
		// the work buffer, elsewhere FUN_0042d558 (frame 6).
		if (cursor) {
			const Common::Point m = _engine->getMousePos();
			if (const SprSprite *spr = _engine->cursorSprite(EclipseRules::overExit(m.x, m.y) ? 11 : 6))
				drawSprSprite(*working.surfacePtr(), *spr, m.x, m.y);
		}
		g_system->copyRectToScreen(working.getPixels(), working.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();
	};

	while (!_engine->shouldAbort()) {
		_engine->pollEvents();
		_engine->getNextKey();
		bool nowButton = (_engine->getCurrentMouseButton() == 1);
		bool leave = false;
		if (nowButton && !prevButton) {
			Common::Point m = _engine->getMousePos();
			const EclipseRules::Click c = _rules.click(m.x, m.y);
			if (c == EclipseRules::kClickExit) {
				leave = true;
			} else if (c == EclipseRules::kClickLever) {
				// Both branches of both levers play LION1 (FUN_00441cd8).
				playSound("LION1", false);
			}
		}
		prevButton = nowButton;
		if (leave) {
			stopSounds();                 // FUN_0043d0e0
			result = kFinPuzzleAbort;
			break;
		}

		renderFrame(working);
		present(true);
		g_system->delayMillis(15);

		if (_rules.isWin()) {
			// FUN_00441724: LION3, then 80 frames with the counter counting
			// from 0 (the highlight pulses on bit 1); then FUN_0043d0e0
			// stops every playing sample, LION3 included.
			playSound("LION3", false);
			// FUN_00441724 calls neither FUN_00441b2c nor FUN_0042d558;
			// what its FUN_0041afd0 shows of a cursor is not established,
			// so the port keeps the system arrow there as before.
			_engine->setArrowCursor();
			_engine->showMouse(true);
			_frameCounter = 0;
			for (int f = 0; f < kWinAnimFrames && !_engine->shouldAbort(); f++) {
				_engine->pollEvents();
				renderFrame(working);
				present(false);
				_frameCounter++;
				// [UNKNOWN] the animation has no timer of its own.
				g_system->delayMillis(40);
			}
			stopSounds();
			result = kFinPuzzleWin;
			break;
		}
	}

	debugC(1, kDebugScript, "EclipsePuzzle::run: levers L1=(r2=%d,r5=%d,r6=%d) L2=(r3=%d,r4=%d) result=%d",
	      _rules.r[2].curr, _rules.r[5].curr, _rules.r[6].curr,
	      _rules.r[3].curr, _rules.r[4].curr, result);
	_engine->showMouse(true);
	g_eclipseMemory.valid = true;
	g_eclipseMemory.curr0 = _rules.r[0].curr;
	g_eclipseMemory.curr1 = _rules.r[1].curr;
	return result;
}

} // namespace Atlantis
} // namespace CryOmni3D
