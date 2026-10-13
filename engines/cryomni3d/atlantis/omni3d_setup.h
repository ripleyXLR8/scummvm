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

#ifndef CRYOMNI3D_ATLANTIS_OMNI3D_SETUP_H
#define CRYOMNI3D_ATLANTIS_OMNI3D_SETUP_H

#include "cryomni3d/omni3d.h"

namespace CryOmni3D {
namespace Atlantis {

// The warp field of view: atlantis.exe pushes the float 0x42964625
// (75.137 deg) to FUN_0045179a at 0x0044a701, 0x0043044c, 0x00428abb,
// 0x00428b48 and 0x00428cbd.
static const float kWarpFovDeg = 75.137f;

// atlantis.exe FUN_00451d8b interpolates each 16 x 16 tile with the row step
// changes shifted by 4 and 9 (Versailles: 10 and 15).
inline void configureOmni3D(Omni3DManager &m) {
	m.setRowStepShifts(4, 9);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
