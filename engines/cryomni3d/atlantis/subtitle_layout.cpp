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

#include "cryomni3d/atlantis/subtitle_layout.h"

namespace CryOmni3D {
namespace Atlantis {

void splitSubtitle(const Common::String &text, Common::Array<Common::String> &lines) {
	lines.clear();
	const Common::String b = " " + text;
	int remaining = (int)b.size();
	if (remaining < 0x65) {
		lines.push_back(b);
		return;
	}
	uint start = 0;
	uint p = 100;
	do {
		if (remaining < 0x65) {
			lines.push_back(b.substr(start));
			break;
		}
		// Back from p to the first space (a CR also stops it; the buffer
		// holds none).
		uint back = 0;
		while (back < 100 && b[p - back] != ' ')
			back++;
		if (back == 100)
			back = 0;   // no space: the original never ends here
		lines.push_back(b.substr(start, 100 - back));
		remaining -= (int)(100 - back);
		start = p - back;
		p = start + 100;
	} while (remaining > 0);
}

int subtitleLineY(uint i, uint n) {
	return 0x1ce - (int)(n - i + 1) * 14;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
