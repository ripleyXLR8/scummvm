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
#ifndef CRYOMNI3D_ATLANTIS_VIDEO_SKIP_H
#define CRYOMNI3D_ATLANTIS_VIDEO_SKIP_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// What ends a video in atlantis.exe: the frame loop FUN_0041bc74 (every
// video: a move's transition, a CON Cinema=, the start-up logos) goes on
// while 0x00496124 != 0 and the click flag 0x0049611c == 0, and its input
// poll is FUN_0041a298, one per frame:
//  - a left-button press sets 0x0049611c (and nothing else is read);
//  - a right-button press toggles the bar request 0x004960c4 unless a CON
//    section plays from a click (0x00496170) or the menu is requested
//    (0x00496138);
//  - Escape with a talking head (0x00496188) sets 0x00496134 and
//    0x0049611c; else, with bow mode 0x00496164, the bar request and
//    OBJETENMAIN all 0, 0x00496124 = 0 and 0x00496138 = 1;
//  - Space, Enter and the other keys are not read.
// After the loop, in cinema mode (0x0049618c: Cinema= and the start-up
// videos) a click or a menu request fades the sound out (0x00495eb0 = 2);
// then the flags are cleared (0x00496138, 0x004960c4, 0x0049611c = 0,
// 0x00496124 = 1).  When the poll ends the loop, the next frame is still
// presented (0x0041be2e).
// See atlantis-re: functions/video-FUN_0041bc74.md,
// experiments/2026-10-16-06-video-skip-input.md.
struct VideoSkipState {
	bool bowMode = false;      // 0x00496164
	bool barRequest = false;   // 0x004960c4
	bool objectInHand = false; // OBJETENMAIN != 0
	bool talkingHead = false;  // 0x00496188
	bool sectionPlay = false;  // 0x00496170
	bool menuRequest = false;  // 0x00496138
	bool click = false;        // 0x0049611c
	bool keepRunning = true;   // 0x00496124
};

// One FUN_0041a298 poll inside FUN_0041bc74; true when the video ends.
inline bool videoSkipPoll(VideoSkipState &s, bool leftPress, bool rightPress, bool escapePress) {
	if (leftPress) {
		s.click = true;
		return true;
	}
	if (rightPress && !s.sectionPlay && !s.menuRequest)
		s.barRequest = !s.barRequest;
	if (escapePress) {
		if (s.talkingHead) {
			s.click = true;
		} else if (!s.bowMode && !s.barRequest && !s.objectInHand) {
			s.keepRunning = false;
			s.menuRequest = true;
		}
	}
	return s.click || !s.keepRunning;
}

// After the loop: whether the sound fades out (cinema mode only).
inline bool videoSkipFades(const VideoSkipState &s, bool cinemaMode) {
	return cinemaMode && (s.click || s.menuRequest);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
