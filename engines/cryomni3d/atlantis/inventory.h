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

#ifndef CRYOMNI3D_ATLANTIS_INVENTORY_H
#define CRYOMNI3D_ATLANTIS_INVENTORY_H

#include "common/array.h"
#include "common/stream.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/sprites.h"

namespace CryOmni3D {
namespace Atlantis {

// The inventory model of atlantis.exe.  See atlantis-re:
// functions/graphics-FUN_0042b328.md.
//
// The original keeps a compact list of 9-byte records {sprite, object id,
// present} at 0x006ce138, with the count at 0x006cd7bc.  The port keeps the
// same order in a CryOmni3D::Inventory: the used slots are 0..count-1 and every
// slot after them is empty.

// Object id that FUN_0042c054 puts in record 0 at start-up (OBJETS1 sprite 0).
static const uint kAtlantisStartObject = 257;

// Read the inventory-icon table at 0x004967b0 from a stream positioned on it:
// int32 object ids up to the -1 terminator, the index being the OBJETS1.SPR
// sprite.  The 0 entries (39, 46 and 47 in the CD build) are kept as
// placeholders so the indices stay aligned.  Reading also stops after 64
// entries or at the end of the stream.
void readInventoryIconTable(Common::SeekableReadStream &exe, Common::Array<uint> &ids);

// Build one valid object per table entry: objects[i] has id ids[i] and icon
// (idCA) i.  sprites must not have a sprite map, so idCA is the plain index.
void buildInventoryCatalogue(const Sprites &sprites, const Common::Array<uint> &ids,
                             Objects &objects);

// The catalogue object FUN_0042b328 uses for an id: the LAST table entry with
// that id.  Returns nullptr when no entry matches.
Object *findInventoryObject(Objects &objects, uint objId);

// FUN_0042b328 with v != 0: append a record, without a duplicate check.
void inventoryAppend(Inventory &inventory, Object *obj);

// FUN_0042b3ec: remove the first record with this id and shift the tail down.
// Does nothing when the id is not carried.  Does not touch the held object.
void inventoryRemove(Inventory &inventory, uint objId);

// Move the used slots to the front, keeping their order.
void inventoryCompact(Inventory &inventory);

// FUN_0042c054: one record, object 257.
void inventoryReset(Inventory &inventory, Objects &objects);

// The characters following Seth ("guests"), atlantis.exe: 5-byte records
// {int32 id, byte sprite} at 0x006cd7fc, count byte 0x0077290e.  Fixed slots,
// so that a slot reused by AddGuest keeps the sprite byte it held when the id
// is not in the sprite table, as in the original.  See atlantis-re:
// functions/scripts-guests.md.
struct AtlantisGuests {
	static const uint kSlots = 12;
	static const byte kHidden = 0xff;   // sprite of a record not drawn
	int32 id[kSlots];
	byte sprite[kSlots];
	uint count;

	// 0x00496880: the portraits' y, 350 in the exe image; the bar slide-in
	// lowers it by 7 a frame and the slide-out raises it back (FUN_0042c2c4,
	// FUN_0042c59c).  Not reset with the list.
	int32 portraitY;
	AtlantisGuests() : portraitY(350) { clear(); }
	void clear();
	// FUN_0042b480 (list part): append {id, index of id in table}.
	void add(int32 guestId, const Common::Array<int32> &table);
	// Remove record i and shift the following ones down.
	void removeAt(uint i);
	// FUN_0042b6c0 (list part): remove the records with this id; after a
	// removal the scan goes on at the next index, so it skips the record
	// that moved into the freed slot.
	void remove(int32 guestId);
	// FUN_0042bdb8: the guest whose portrait cell holds (x, y), 0 if none.
	// Grid FUN_004211f0 at x0 = 540 - 100 * (count - 1), y0 = portraitY, cells
	// 85 x 100, count columns, one row, edges inclusive; the record is
	// count - 1 - cell.  On the right and bottom edge pixels the original
	// reads the memory before the list; 0 here.
	int32 at(int x, int y) const;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
