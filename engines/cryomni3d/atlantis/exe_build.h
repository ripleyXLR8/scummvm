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

#ifndef CRYOMNI3D_ATLANTIS_EXE_BUILD_H
#define CRYOMNI3D_ATLANTIS_EXE_BUILD_H

#include "common/scummsys.h"

namespace CryOmni3D {
namespace Atlantis {

// What the port needs to know about one build of atlantis.exe: where the
// tables it reads sit, and the code differences between builds that change
// behaviour.  Behaviour is established on the reference build
// (atlantis-cd-en-714240); another build is described by its differences
// (atlantis-re binaries/, decision 0017).
struct AtlantisExeBuild {
	const char *name;        // atlantis-re binaries/ name
	uint32 size;             // file size of atlantis.exe
	uint32 inventoryVA;      // FUN_0042b328's object table
	uint32 guestVA;          // FUN_0042b480's guest sprite table
	uint32 creditWidthsVA;   // credits font advance table
	// FUN_00422fcc @ 0x0042313d: ids 0x219 / 0x21a get the navigate cursor.
	bool cursorIdRule;
	// FUN_0042844c @ 0x0042892e: perso 0x230 (Rhea) always on camera 2.
	bool rheaCamera2;
	// FUN_00426590: a second death in the same section plays the next
	// narrator line (per-section counter 0x6cccac; the 2001 build resets it
	// before the test).
	bool altGameOverNarration;
};

// atlantis-re experiments 2026-10-17-06 / -07 / -08: the GOG executable
// (the same code for its five languages) is a 1999 build; its three tables
// are 4 bytes after the reference's; it lacks the 2001 cursor and camera
// rules and keeps the alternative game-over narrations.
static const AtlantisExeBuild kAtlantisExeBuilds[] = {
	{ "atlantis-cd-en-714240", 714240, 0x004967b0, 0x00496884, 0x004966fc, true, true, false },
	{ "atlantis-gog-714752", 714752, 0x004967b4, 0x00496888, 0x00496700, false, false, true },
};

// The build of an executable of `size` bytes; the reference when unknown.
inline const AtlantisExeBuild &atlantisExeBuild(uint32 size) {
	for (uint i = 0; i < ARRAYSIZE(kAtlantisExeBuilds); i++)
		if (kAtlantisExeBuilds[i].size == size)
			return kAtlantisExeBuilds[i];
	return kAtlantisExeBuilds[0];
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
