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

#ifndef CRYOMNI3D_ATLANTIS_OMNI3D_H
#define CRYOMNI3D_ATLANTIS_OMNI3D_H

#include "cryomni3d/omni3d.h"

namespace CryOmni3D {
namespace Atlantis {

// The warp field of view: atlantis.exe pushes the float 0x42964625
// (75.137 deg) to FUN_0045179a at 0x0044a701, 0x0043044c, 0x00428abb,
// 0x00428b48 and 0x00428cbd.
static const float kWarpFovDeg = 75.137f;

// The projection of atlantis.exe: FUN_0045179a (tables), FUN_00451915 (grid)
// and FUN_00451d8b (tile steps).  The game runs the x87 at 53-bit precision
// (control word 0x127f, set at 0x004719a5) and stores most intermediates as
// floats; the grid is computed in double with a float store at each place
// the original has one, so the coordinates match the original's exactly.
class Omni3DManagerAtlantis : public Omni3DManager {
public:
	Omni3DManagerAtlantis() : _piF(0), _twoPiF(0), _vfovF(0), _b5c(0), _step(0), _panScale(0) {
		setRowStepShifts(4, 9);
	}

	void setHFov(double hfov) override;

protected:
	void updateImageCoords() override;

private:
	float _piF, _twoPiF, _vfovF, _b5c, _step, _panScale;
	float _rowAngle[31];       // 0x00bca9fc
	float _rowHyp[31];         // 0x00bcaacc
	float _colOpp[21];         // 0x00bcaa78
	float _rowColScale[31 * 21]; // 0x00bcab68
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
