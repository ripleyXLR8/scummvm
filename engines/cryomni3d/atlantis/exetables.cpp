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

	Common::File exe;
	if (!exe.open("atlantis.exe")) {
		warning("loadAtlantisExeTables: cannot open atlantis.exe");
		return;
	}

	// Inventory-icon table at VA 0x004967b0: a zero-terminated int32 array
	// where index i is the sprite slot in SPRITE\2D\OBJETS1.SPR and the value
	// is the world object id that uses that sprite as its inventory icon.
	// Empirically 39 entries + a 0 terminator for the English build; the loop
	// stops at the first non-positive value or a generous upper bound so an
	// unexpected build can't run off the data section.
	const uint32 kInvTableVA = 0x004967b0;
	const uint32 kInvTableFileOff =
	    (kInvTableVA - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
	exe.seek(kInvTableFileOff);
	for (int i = 0; i < 64 && !exe.eos(); i++) {
		int32 id = exe.readSint32LE();
		if (id <= 0)
			break;
		_inventoryObjectIds.push_back((uint)id);
	}
	debugC(1, kDebugScript, "loadAtlantisExeTables: %u inventory object ids from atlantis.exe",
	      _inventoryObjectIds.size());

	// Credits-font char-width table at VA 0x004966fc: 256 raw bytes indexed by
	// char code, for proportional spacing of CREDBLAN.SPR / CREDBLEU.SPR (the
	// space value 0x20 = 13 is authentic).
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
	// icon drawing depends on.  rename() stamps the world object id onto each
	// slot; lookups below match on that id.
	_objects.clear();
	_objects.resize(_inventoryObjectIds.size());
	for (uint i = 0; i < _inventoryObjectIds.size(); i++)
		_objects[i].rename(_inventoryObjectIds[i]);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
