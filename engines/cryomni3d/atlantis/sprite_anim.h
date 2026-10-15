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

#ifndef CRYOMNI3D_ATLANTIS_SPRITE_ANIM_H
#define CRYOMNI3D_ATLANTIS_SPRITE_ANIM_H

#include "common/array.h"

#include "cryomni3d/atlantis/npc_click.h"

namespace CryOmni3D {
namespace Atlantis {

// The 200 Hz tick counter of atlantis.exe (0x006ce118, AIL timer set up at
// 0x004310ac): one tick every 5 ms.
static const uint32 kSpriteTickMs = 5;

// Per-entry tick of the last frame step (0x006cc578), indexed by the entry's
// index in its view; shared by all views and never reset.
static const uint kSpriteAnimSlots = 256;

// atlantis.exe FUN_00425304, animation part, for the entries of view vue in
// list order: an active entry with SPW data whose type is not 0, 3 or 4 sets
// wsprFrame to the frame of the view's current animation entry (index
// current), then steps its frame when ticks - lastTick[i] > 13; when its frame
// equals its frame count it returns to 0, wsprFrame becomes 0 and a type 2
// entry turns inactive.  True when a frame or an active flag changed.
bool animatePlaceSprites(Common::Array<SpritePlaceEntry> &entries, uint16 vue, uint current,
                         uint32 ticks, uint32 *lastTick, int &wsprFrame, bool hideNpcs = false);

// FUN_00428da8: NEWWSPRANIM=n (1-based) activates the view's entry n - 1,
// makes it the current animation entry and resets its frame; STOPWSPRANIM=n
// deactivates entry n - 1 and resets its frame.  False when there is no such
// entry.
bool startPlaceSpriteAnim(Common::Array<SpritePlaceEntry> &entries, uint16 vue, int n, uint &current);
bool stopPlaceSpriteAnim(Common::Array<SpritePlaceEntry> &entries, uint16 vue, int n);

// The active flag (+0x04) of the place-sprite table 0x00590510.  Its only
// writers (operand scan of atlantis.exe, atlantis-re
// tools/emu/scripts_sprite_active.py --xrefs): the list load FUN_00424a20
// (0x00424f7a) with FUN_00427d30 for the hidden ids (0x00427d93), HidePerso
// FUN_00427bd0 (0x00427c57), ShowPerso FUN_00427eb0 (0x00427f30),
// NEWWSPRANIM / STOPWSPRANIM FUN_00428da8 (0x00428e07, 0x00428ecc) and the
// end of a type-2 cycle FUN_00425304 (0x004255d7).  Entering a place
// (FUN_0042431c) writes the frame, the frame count and the SPW buffers only,
// so a flag set by a script stays until the next list load (a WAM load).
// See atlantis-re: experiments/2026-10-15-95-sprite-active-flag.md.

// FUN_00427d30, run by the list load for each row: a row whose id is in the
// hidden-perso list starts inactive.
void spriteListApplyHidden(Common::Array<SpritePlaceEntry> &entries, const Common::Array<int> &hidden);

// FUN_00427eb0 (active) / FUN_00427bd0 (inactive): every row of the perso,
// in every view.
void spriteListSetPerso(Common::Array<SpritePlaceEntry> &entries, int perso, bool active);

// FUN_0042431c for view vue: frame 0 (the caller sets the frame count from
// the SPW header); the active flag is left as it is.
void spriteListEnterPlace(Common::Array<SpritePlaceEntry> &entries, uint16 vue);

// atlantis.exe FUN_0041bfa0 @ 0x0041c61d..0x0041c6a0, once per warp frame
// after the frame is drawn: a left click in bow mode is consumed and, when
// AngleWarpY < 20, sets TirFleche to 1; then, while TirFleche is non-zero,
// the arrow frame (0x0049619d) steps by one and at 15 returns to 0 with
// TirFleche.  True when the click was consumed.
bool bowFrameStep(bool bowMode, bool click, int angleWarpY, int &tirFleche, uint &arrowFrame);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
