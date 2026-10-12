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

#ifndef CRYOMNI3D_ATLANTIS_CON_EVAL_H
#define CRYOMNI3D_ATLANTIS_CON_EVAL_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/atlantis/con_script.h"

namespace CryOmni3D {
namespace Atlantis {

// Evaluates the condition list of a CON section (/sel, /con, /tim) against
// the stored game variables and the inventory.  Keys whose value comes from
// live engine state (vue, departvue, ...) are supplied by liveValue(), which
// the engine overrides.  See atlantis-re: functions/scripts-FUN_004298ac.md.
class ConEvaluator {
public:
	ConEvaluator(const Common::HashMap<Common::String, int> &vars, const Inventory &inventory) :
		_vars(vars), _inventory(inventory) {}
	virtual ~ConEvaluator() {}

	// True when the conditions hold; an empty list holds.
	bool evaluate(const Common::Array<ConCondition> &conditions) const;

	// Value of the left-hand side of a condition.
	int value(const Common::String &key) const;

protected:
	// Return true and set out for a key read from live engine state.
	virtual bool liveValue(const Common::String &, int &) const { return false; }

private:
	const Common::HashMap<Common::String, int> &_vars;
	const Inventory &_inventory;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
