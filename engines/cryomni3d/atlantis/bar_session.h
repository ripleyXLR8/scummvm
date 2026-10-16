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

#ifndef CRYOMNI3D_ATLANTIS_BAR_SESSION_H
#define CRYOMNI3D_ATLANTIS_BAR_SESSION_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The bar request 0x004960c4 and the right-press flag 0x00496120 of
// atlantis.exe while the inventory bar is up.  The bar loop FUN_0042c8ac sets
// the request on entry (0x0042c8c4), runs the CON timers every frame
// (0x0042cb2c) and leaves when it reads 0 (0x0042cb76); FUN_0042c098 then
// skips the item click (it needs the request at 1) and empties the hand when
// the click flag 0x0049611c or 0x00496120 is set (0x0042c1c9).  A timer
// section's walk ends with 0x0049611c = 0 (FUN_0042844c @ 0x00428d77), so
// after a section only 0x00496120 can empty it.
// See atlantis-re: experiments/2026-10-16-16-bar-timer-close.md.
struct BarSession {
	bool request = false;      // 0x004960c4
	bool rightPress = false;   // 0x00496120
};

namespace BarSessionRules {

// FUN_0042c8ac @ 0x0042c8c4.
inline void opened(BarSession &s) {
	s.request = true;
}

// The flag resets of a video end (FUN_0041bc74 @ 0x0041bf5b..0x0041bf8d), a
// Cinema= (FUN_0042a100 @ 0x0042a907..0x0042a939), a StartMusik= that closes
// the open stream for another track (@ 0x0042add3..0x0042ae05), a StopMusik=
// with a stream open (@ 0x0042af26..0x0042af58), the end of a bar session
// (FUN_0042c098 step 6) and the warp loop's entry (FUN_0041bfa0).
inline void flagsReset(BarSession &s) {
	s.request = false;
	s.rightPress = false;
}

// The end of a voice-only line that loaded its voice (FUN_00430258 @
// 0x004305cb, FUN_004305e8 @ 0x00430867, FUN_00430884 @ 0x00430bd0); a
// missing voice returns before it.
inline void voiceLineEnded(BarSession &s) {
	s.request = false;
}

// A right-button press seen by FUN_0041a298 (@ 0x0041a2f2..0x0041a337):
// 0x00496120 = 1, and the request toggles unless a clicked CON section plays
// (0x00496170) or the menu is requested (0x00496138); 0x0049615c, also
// tested, is never written.
inline void rightPressed(BarSession &s, bool sectionPlay, bool menuRequest) {
	s.rightPress = true;
	if (!sectionPlay && !menuRequest)
		s.request = !s.request;
}

// The warp loop FUN_0041bfa0: its timers (step 6, @ 0x0041c369) run before
// its own poll and its bar test (step 17, @ 0x0041c6d2), so a request left by
// a timer section (a right press during its talking-head lines) opens the
// bar in the same frame; the next frame starts by clearing the flags
// (0x0041c04a..0x0041c07c).  The port tests the bar at the top of its frame:
// this returns the request carried over and clears the flags.
inline bool warpFrameStart(BarSession &s) {
	const bool pending = s.request;
	flagsReset(s);
	return pending;
}

// After the timers of a bar frame: true when the loop leaves (0x0042cb76).
inline bool timersEndBar(const BarSession &s) {
	return !s.request;
}

// FUN_0042c098 @ 0x0042c1c9 after such an exit (no item click: the request
// is 0; the click flag is 0 after the walk).
inline bool exitEmptiesHand(const BarSession &s) {
	return s.rightPress;
}

} // End of namespace BarSessionRules

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
