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

#ifndef CRYOMNI3D_ATLANTIS_PLACE_LOOP_VARS_H
#define CRYOMNI3D_ATLANTIS_PLACE_LOOP_VARS_H

#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Atlantis {

// The CON variables the place loop atlantis.exe FUN_00423ad8 writes around
// its scans.  DEPARTVUE and ABORTDEPART are ordinary stored variables: a
// script Vue= leaves DEPARTVUE = N for every later scan until a move ends,
// and an AbortDepart=1 set outside a departure scan cancels the next move.
// See atlantis-re: experiments/2026-10-16-14-departvue-after-vue.md.
namespace PlaceLoopVars {

typedef Common::HashMap<Common::String, int> Vars;

// FUN_00421cfc @ 0x00421db3: DEPARTVUE = the id of the place looked up, on a
// move (the destination, @ 0x00423dc5) and on a script Vue= (@ 0x0042a66d).
inline void placeLookedUp(Vars &vars, int placeId) {
	vars["departvue"] = placeId;
}

// FUN_00427548 @ 0x00427607, the arrival preparation run before the place
// loop's own arrival scan (its only call, @ 0x00423b63).
inline void arrivalPrepared(Vars &vars) {
	vars["abortdepart"] = 0;
}

// FUN_00427814 @ 0x00427843: FLAGVUE1 = 0 after the departure scan.
inline void departureScanned(Vars &vars) {
	vars["flagvue1"] = 0;
}

// FUN_00423ad8 @ 0x00423e2b..0x00423e7e, after the departure scan: an
// AbortDepart (from any scan since the last arrival preparation) ends the
// move and clears ARRIVEEVUE, ABORTDEPART and DEPARTVUE.
inline bool takeAbort(Vars &vars) {
	if (vars.getValOrDefault("abortdepart", 0) == 0)
		return false;
	vars["arriveevue"] = 0;
	vars["abortdepart"] = 0;
	vars["departvue"] = 0;
	return true;
}

// FUN_00423ad8 @ 0x00424089: DEPARTVUE = 0 once the move has entered the
// new place, before the next pass's arrival scan.
inline void moveEnded(Vars &vars) {
	vars["departvue"] = 0;
}

// FUN_00423ad8 @ 0x00423c70 / 0x00423c91: the zone-999 scan runs with
// DEPARTVUE = 999, then 0.
inline void zone999Begin(Vars &vars) {
	vars["departvue"] = 999;
}
inline void zone999End(Vars &vars) {
	vars["departvue"] = 0;
}

// A script Vue= sets ARRIVEEVUE (FUN_0042a100); the next pass then runs the
// arrival scan (@ 0x00423b48), except when the Vue= ran inside the place
// loop's own arrival scan, after which ARRIVEEVUE is cleared (@ 0x00423be7).
inline bool vueQueuesArrivalScan(bool inPlaceArrivalScan) {
	return !inPlaceArrivalScan;
}

} // End of namespace PlaceLoopVars

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
