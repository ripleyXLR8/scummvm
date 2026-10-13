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

// Runtime extraction of the static lookup tables baked into atlantis.exe's
// data section, plus the object catalogue that consumes them.  This grows as
// more tables are needed (the newsound APC name table and the credit-font
// width table land with the music and menu layers).

#include "common/debug.h"
#include "common/file.h"
#include "common/textconsole.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/inventory.h"

namespace CryOmni3D {
namespace Atlantis {

void CryOmni3DEngine_Atlantis::loadAtlantisExeTables() {
	// PE section parameters for atlantis.exe (imageBase 0x400000, DGROUP
	// section at vaddr 0x90000 / rawptr 0x71200) are stable for the
	// MD5-checked build we detect against.
	const uint32 kImageBase    = 0x400000;
	const uint32 kDgroupVaddr   = 0x90000;
	const uint32 kDgroupRawPtr  = 0x71200;

	_inventoryObjectIds.clear();
	_newSoundApcNames.clear();

	Common::File exe;
	if (!exe.open("atlantis.exe")) {
		warning("loadAtlantisExeTables: cannot open atlantis.exe");
		return;
	}

	// Named-resource pointer table at VA 0x004961e0 (file offset 0x773e0):
	// 17 WAM filenames followed by ~137 APC filenames, terminated by a "FIN"
	// sentinel.  The APC portion is what CON `/set(newsound=N)` indexes into
	// (0-based: newsound=14 plays the 15th APC, Toctoc.apc).
	{
		const uint32 kTableFileOff = 0x773e0;
		const int kSkipWams = 17;

		exe.seek(kTableFileOff + kSkipWams * 4);
		for (int i = 0; i < 200 && !exe.eos(); i++) {
			uint32 va = exe.readUint32LE();
			if (va < 0x4b0000 || va > 0x4d0000) break;  // off the end of the data
			uint32 fileOff = (va - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
			int64 saved = exe.pos();
			exe.seek(fileOff);
			Common::String s;
			while (!exe.eos()) {
				byte b = exe.readByte();
				if (b == 0) break;
				s += (char)b;
			}
			if (s.equalsIgnoreCase("FIN")) break;
			_newSoundApcNames.push_back(s);
			exe.seek(saved);
		}
		debugC(1, kDebugScript, "loadAtlantisExeTables: %u newsound APC names from atlantis.exe",
		      _newSoundApcNames.size());
	}

	// Inventory-icon table at VA 0x004967b0: index i is the sprite slot in
	// SPRITE\2D\OBJETS1.SPR, the value the object id that uses that sprite.
	// atlantis.exe FUN_0042b328 scans it up to the -1 terminator: 51 entries,
	// with 0 placeholders at 39, 46 and 47.  See atlantis-re:
	// functions/graphics-FUN_0042b328.md.
	const uint32 kInvTableVA = 0x004967b0;
	const uint32 kInvTableFileOff =
	    (kInvTableVA - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
	exe.seek(kInvTableFileOff);
	readInventoryIconTable(exe, _inventoryObjectIds);

	// Guest sprite table at VA 0x00496884: perso ids up to -1, the index being
	// the sprite of SUIVEUR.SPR (atlantis.exe FUN_0042b480).
	_guestSpriteIds.clear();
	exe.seek((0x00496884 - kImageBase - kDgroupVaddr) + kDgroupRawPtr);
	for (int i = 0; i < 64 && !exe.eos(); i++) {
		int32 id = exe.readSint32LE();
		if (id == -1)
			break;
		_guestSpriteIds.push_back(id);
	}
	debugC(1, kDebugScript, "loadAtlantisExeTables: %u inventory object ids from atlantis.exe",
	      _inventoryObjectIds.size());

	// Credits-font char-width table at VA 0x004966fc: 256 raw bytes indexed by
	// char code, for proportional spacing of CREDBLAN.SPR / CREDBLEU.SPR.  The
	// byte for 0x20 (13) is never read: a space advances 6 (creditCharAdvance).
	memset(_creditCharWidths, 0, sizeof(_creditCharWidths));
	const uint32 kCredWidthsVA = 0x004966fc;
	const uint32 kCredWidthsFileOff =
	    (kCredWidthsVA - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
	exe.seek(kCredWidthsFileOff);
	exe.read(_creditCharWidths, sizeof(_creditCharWidths));
	debugC(1, kDebugScript, "loadAtlantisExeTables: credit char-width table loaded");
}

void CryOmni3DEngine_Atlantis::setupObjects() {
	// Build the object catalogue in the canonical OBJETS1.SPR sprite order, so
	// _objects[i] is the icon at sprite slot i — the invariant the inventory
	// icon drawing depends on.  The objects are valid so the toolbar lets the
	// player take them.  _sprites has no sprite map: idCA() is the index.
	buildInventoryCatalogue(_sprites, _inventoryObjectIds, _objects);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
