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

#ifndef CRYOMNI3D_ATLANTIS_GAMEOVER_H
#define CRYOMNI3D_ATLANTIS_GAMEOVER_H

#include "common/str.h"

#include "cryomni3d/atlantis/video_skip.h"

namespace CryOmni3D {
namespace Atlantis {

// The game-over screen of atlantis.exe, FUN_00426590 (GereGameOver), called
// by the CON executor FUN_0042844c when GAMEOVER is non-zero after a line
// (0x00428c05), before the end of the walk (ClicPerso and angle restores,
// music raised: 0x00428c92..0x00428d8b).
//  1. NEWSOUND != 0: the NewSound sample fades out (0x00495eb0 = 2, its
//     volume from the effects volume, 0x00426631..0x00426647).
//  2. images\gover + ("0" when < 10) + GAMEOVER + .tga, full screen.
//  3. When the next line starts with "[Narra": its voice
//     wav\ata<section, 4 digits><letter>.apc and its text from the byte
//     after "[Narratrice]" (12 bytes) to the CR, drawn by FUN_00430178,
//     which returns at once when SUBTITLES is 0 (0x0043019a): the caller's
//     "SUBTITLES == 1 or counter 0x00496098 < 2" only adds a call that draws
//     nothing.  Then the wait (gameOverWaitEnds).
//  4. The screen is cleared; the place loop then reloads the player's
//     checkpoint (main loop 0x0041975d -> FUN_0042de5c).
// Emulated whole on the 144 game overs of the game: atlantis-re
// tools/emu/scripts_gameover.py, experiments/2026-10-16-09-game-over.md.

// Step 2's image (IMAGES\ member, upper case).
inline Common::String gameOverImageName(int n) {
	return Common::String::format("GOVER%02d.TGA", n);
}

// Step 3: the narrator's voice (WAV\ member, upper case).
inline Common::String gameOverVoiceName(int section, char letter) {
	return Common::String::format("ATA%04d%c.APC", section, letter);
}

// Step 3: the subtitle is drawn when SUBTITLES is set, whatever the click
// counter 0x00496098.
inline bool gameOverSubtitleDrawn(bool subtitles) {
	return subtitles;
}

// Step 1.
inline bool gameOverFadesNewSound(int newSound) {
	return newSound != 0;
}

// Step 3's wait, one pass.  With the voice (0x00426b0c..0x00426b49): the
// loop reads the sample status, pumps the sounds, polls the input
// (FUN_0041a298, videoSkipPoll) and ends when the sample is done or a left
// press set 0x0049611c or Escape cleared 0x00496124 (bow mode, bar request
// and OBJETENMAIN all 0).  Without it (0x00426ae0..0x00426b05): only the
// left press, or 5000 ms since the text was drawn; the poll comes after
// the test.  Pass the input of this pass; true when the wait ends.
inline bool gameOverWaitEnds(VideoSkipState &s, bool withVoice, bool voiceDone, uint32 elapsedMs,
                             bool leftPress, bool rightPress, bool escapePress) {
	if (withVoice) {
		videoSkipPoll(s, leftPress, rightPress, escapePress);
		return voiceDone || s.click || !s.keepRunning;
	}
	if (s.click || elapsedMs >= 5000)
		return true;
	videoSkipPoll(s, leftPress, rightPress, escapePress);
	return false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
