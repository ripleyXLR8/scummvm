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

#ifndef CRYOMNI3D_ATLANTIS_SUBTITLE_LAYOUT_H
#define CRYOMNI3D_ATLANTIS_SUBTITLE_LAYOUT_H

#include "common/array.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Atlantis {

// atlantis.exe FUN_0042ffe8 on the subtitle buffer FUN_0042844c fills: a space
// (the byte after ']') then the text.  Under 101 bytes it is one line;
// otherwise each line ends before the last space within the next 100 bytes
// and the following line starts at that space, until the remaining length
// is 0.  A window with no space makes the original loop forever; the port
// cuts it at 100 bytes.
void splitSubtitle(const Common::String &text, Common::Array<Common::String> &lines);

// atlantis.exe FUN_00430178: line i of n is drawn at x = 5,
// y = 0x1ce - (n - i + 1) * 14 (white, black shadow at +1, +1).
static const int kSubtitleX = 5;
int subtitleLineY(uint i, uint n);

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
