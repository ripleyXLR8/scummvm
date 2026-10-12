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

#include "common/util.h"

#include "cryomni3d/atlantis/con_eval.h"

namespace CryOmni3D {
namespace Atlantis {

int ConEvaluator::value(const Common::String &key) const {
	int live;
	if (liveValue(key, live))
		return live;
	if (key.hasPrefix("invent") && key.size() > 6 && Common::isDigit(key[6])) {
		// invent<N> — 1 when object N is in the inventory, else 0.  The
		// original (atlantis.exe FUN_00429e54) tests the live inventory
		// here; it is not a stored game variable.
		uint objId = (uint)atoi(key.c_str() + 6);
		return _inventory.inInventoryByNameID(objId) ? 1 : 0;
	}
	// Generic variable (covers arriveevue, the flags, initflag, …).
	Common::HashMap<Common::String, int>::const_iterator it = _vars.find(key);
	return (it != _vars.end()) ? it->_value : 0;
}

bool ConEvaluator::evaluate(const Common::Array<ConCondition> &conditions) const {
	if (conditions.empty())
		return true;

	// Left-to-right short-circuit eval (atlantis.exe FUN_004298ac):
	//   '&' + fail -> EXIT FAIL ; '|' + pass -> EXIT SUCCESS ; end -> last result.
	for (const ConCondition &cc : conditions) {
		int lhs = value(cc.key);

		bool ok;
		switch (cc.op) {
		case '=': ok = (lhs == cc.value); break;
		case '<': ok = (lhs <  cc.value); break;
		case '>': ok = (lhs >  cc.value); break;
		case '!': ok = (lhs != cc.value); break;
		default:  ok = false;             break;
		}

		if (cc.nextSep == '&') {
			if (!ok) return false;
		} else if (cc.nextSep == '|') {
			if (ok) return true;
		} else {
			return ok;
		}
	}
	return true;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
