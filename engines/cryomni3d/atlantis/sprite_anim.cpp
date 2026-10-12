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

#include "cryomni3d/atlantis/sprite_anim.h"

namespace CryOmni3D {
namespace Atlantis {

static void viewEntries(const Common::Array<SpritePlaceEntry> &entries, uint16 vue, Common::Array<uint> &idx) {
	idx.clear();
	for (uint i = 0; i < entries.size(); i++)
		if (entries[i].placeId == vue)
			idx.push_back(i);
}

bool animatePlaceSprites(Common::Array<SpritePlaceEntry> &entries, uint16 vue, uint current,
                         uint32 ticks, uint32 *lastTick, int &wsprFrame) {
	Common::Array<uint> idx;
	viewEntries(entries, vue, idx);
	bool changed = false;
	for (uint k = 0; k < idx.size() && k < kSpriteAnimSlots; k++) {
		SpritePlaceEntry &e = entries[idx[k]];
		if (!e.active || e.frameCount == 0)
			continue;
		if (e.type == 0 || e.type == 3 || e.type == 4)
			continue;
		wsprFrame = (current < idx.size()) ? entries[idx[current]].frame : 0;
		if (ticks - lastTick[k] > 13) {
			lastTick[k] = ticks;
			e.frame++;
			changed = true;
		}
		if (e.frame == e.frameCount) {
			e.frame = 0;
			wsprFrame = 0;
			if (e.type == 2)
				e.active = false;
			changed = true;
		}
	}
	return changed;
}

bool startPlaceSpriteAnim(Common::Array<SpritePlaceEntry> &entries, uint16 vue, int n, uint &current) {
	Common::Array<uint> idx;
	viewEntries(entries, vue, idx);
	if (n < 1 || (uint)n > idx.size())
		return false;
	SpritePlaceEntry &e = entries[idx[n - 1]];
	e.active = true;
	current = (uint)(n - 1);
	e.frame = 0;
	return true;
}

bool stopPlaceSpriteAnim(Common::Array<SpritePlaceEntry> &entries, uint16 vue, int n) {
	Common::Array<uint> idx;
	viewEntries(entries, vue, idx);
	if (n < 1 || (uint)n > idx.size())
		return false;
	SpritePlaceEntry &e = entries[idx[n - 1]];
	e.active = false;
	e.frame = 0;
	return true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
