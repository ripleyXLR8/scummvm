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

#include "common/scummsys.h"

#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/warp_momentum.h"

namespace CryOmni3D {
namespace Atlantis {

// FUN_0045179a(hfov, 50).  Every caller passes 50 deg as the second argument.
float Omni3DManagerAtlantis::exePi() {
	const double q = sqrt(2.) * 0.5;
	return (float)((M_PI_2 - atan2(q, sqrt(1. - q * q))) * 4.);
}

void Omni3DManagerAtlantis::setHFov(double hfov) {
	_hfov = hfov;

	const float hfovDeg = (float)(hfov * 180. / M_PI);
	const float vfovDeg = 50.f;

	// pi as acos(sqrt(2) / 2) * 4 (0x004517ab..0x004517c1), kept in the
	// register for the 155 deg term and stored as a float elsewhere.
	const double q = sqrt(2.) * 0.5;
	const double pi = (M_PI_2 - atan2(q, sqrt(1. - q * q))) * 4.;
	_piF = (float)pi;
	_twoPiF = (float)(pi * 2.);
	const float h = (float)((double)_piF * hfovDeg / 180.);
	_vfovF = (float)((double)_piF * vfovDeg / 180.);
	_b5c = (float)(384. / sin(pi * 155. / 180. * 0.5));
	_step = (float)(tan((double)h * 0.5) * 16. / 320.);
	_panScale = (float)(134217728. / (double)_twoPiF);

	uint k = 0;
	for (int i = 0; i < 31; i++) {
		const double t = (i - 15) * (double)_step;
		_rowAngle[i] = (float)atan2(t, 1.);
		const double tf = (float)t;
		const float c = (float)sqrt(tf * tf + 1.);
		_rowHyp[i] = c;
		for (int j = 0; j < 21; j++) {
			const float u = (float)((j - 20) * (double)_step);
			_colOpp[j] = u;
			const double den = sqrt((double)u * u + (double)c * c);
			_rowColScale[k++] = (float)((double)c * _b5c * 65536. / den);
		}
	}

	_dirty = true;
	_dirtyCoords = true;
}

// FUN_00451915(alpha, beta).  The clamped beta and wrapped alpha are the
// game's view angles (0x00bcb598, 0x00bcb59c), so they are kept.
void Omni3DManagerAtlantis::updateImageCoords() {
	if (!_dirtyCoords) {
		return;
	}

	float a = (float)_alpha;
	float b = (float)_beta;
	warpWrapAngles(a, b, _twoPiF, betaLimit());
	_alpha = a;
	_beta = b;

	const float base = (float)(134217728. - (double)a * _panScale);

	uint k = 0;
	for (uint i = 0; i < 31; i++) {
		const double v = (double)b + _rowAngle[i];
		const float s = (float)sin(v);
		const float r = (float)((double)_rowHyp[i] * cos(v));
		int *row = &_imageCoords[2 + 82 * i];

		for (uint j = 0; j < 20; j++) {
			const float d = (float)(atan2((double)_colOpp[j], (double)r) * _panScale);
			const int y = (int)(25165824. - (double)s * _rowColScale[k++]);
			row[2 * j + 0] = (int)((double)base + d);
			row[80 - 2 * j] = (int)((double)base - d);
			row[2 * j + 1] = y;
			row[81 - 2 * j] = y;
		}
		row[40] = (int)(134217728. - ((double)a - atan2((double)_colOpp[20], (double)r)) * _panScale);
		row[41] = (int)(25165824. - (double)s * _rowColScale[k++]);
	}

	_dirtyCoords = false;
	_dirty = true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
