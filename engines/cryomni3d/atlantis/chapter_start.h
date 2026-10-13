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

#ifndef CRYOMNI3D_ATLANTIS_CHAPTER_START_H
#define CRYOMNI3D_ATLANTIS_CHAPTER_START_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// The place each WAM chapter starts in (placeId) and the initial camera angles.
struct LevelInitialState {
	uint placeId;
	float alpha;
	float beta;
};

// The first place record of ATLAN1..5.WAM, the place FUN_00421ebc /
// FUN_00421b4c show after a WAM change until a vue is set (atlantis-re
// evidence/golden/archive_chapter.txt).  A new game starts chapter 1 at vue
// 1 (DAT_0049692c); the camera angles 0x00bcb59c / 0x00bcb598 keep their
// .bss value, 0.
static const LevelInitialState kChapterInitialStates[5] = {
	{   1, 0.f, 0.f },  // ATLAN1.WAM
	{  22, 0.f, 0.f },  // ATLAN2.WAM
	{  58, 0.f, 0.f },  // ATLAN3.WAM
	{  70, 0.f, 0.f },  // ATLAN4.WAM
	{ 123, 0.f, 0.f },  // ATLAN5.WAM
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
