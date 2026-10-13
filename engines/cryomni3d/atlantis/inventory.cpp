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

#include "cryomni3d/atlantis/inventory.h"

namespace CryOmni3D {
namespace Atlantis {

void readInventoryIconTable(Common::SeekableReadStream &exe, Common::Array<uint> &ids) {
	ids.clear();
	for (uint i = 0; i < 64; i++) {
		int32 id = exe.readSint32LE();
		if (exe.eos() || id == -1)
			break;
		ids.push_back((uint)id);
	}
}

void buildInventoryCatalogue(const Sprites &sprites, const Common::Array<uint> &ids,
                             Objects &objects) {
	objects.clear();
	objects.reserve(ids.size());
	for (uint i = 0; i < ids.size(); i++)
		objects.push_back(Object(sprites, i, ids[i]));
}

Object *findInventoryObject(Objects &objects, uint objId) {
	Object *found = nullptr;
	for (Object &o : objects)
		if (o.idOBJ() == objId)
			found = &o;
	return found;
}

void inventoryAppend(Inventory &inventory, Object *obj) {
	// The used slots are at the front, so the first empty slot is the end of
	// the list.  Inventory::add errors out when every slot is used.
	inventory.add(obj);
}

void inventoryRemove(Inventory &inventory, uint objId) {
	for (uint i = 0; i < inventory.size(); i++) {
		if (inventory[i] && inventory[i]->idOBJ() == objId) {
			inventory.remove_at(i);
			inventory.push_back(nullptr);
			// Clears the (already empty) last slot and notifies the change.
			inventory.remove(inventory.size() - 1);
			return;
		}
	}
}

void inventoryCompact(Inventory &inventory) {
	uint used = 0;
	for (uint i = 0; i < inventory.size(); i++)
		if (inventory[i])
			inventory[used++] = inventory[i];
	for (uint i = used; i < inventory.size(); i++)
		inventory[i] = nullptr;
}

void inventoryReset(Inventory &inventory, Objects &objects) {
	inventory.clear();
	Object *start = findInventoryObject(objects, kAtlantisStartObject);
	if (start)
		inventoryAppend(inventory, start);
}

void AtlantisGuests::clear() {
	memset(id, 0, sizeof(id));
	memset(sprite, 0, sizeof(sprite));
	count = 0;
}

void AtlantisGuests::add(int32 guestId, const Common::Array<int32> &table) {
	if (count >= kSlots)
		return;   // past the record area of the original
	id[count] = guestId;
	for (uint i = 0; i < table.size(); i++) {
		if (table[i] == guestId) {
			sprite[count] = (byte)i;
			break;
		}
	}
	count++;
}

int32 AtlantisGuests::at(int x, int y) const {
	if (count == 0)
		return 0;
	const int x0 = 540 - 100 * ((int)count - 1);
	const int y0 = portraitY;
	if (x < x0 || x > x0 + (int)count * 85 || y < y0 || y > y0 + 100)
		return 0;
	const int cell = ((y - y0) / 100) * (int)count + (x - x0) / 85;
	const int k = (int)count - 1 - cell;
	return (k >= 0) ? id[k] : 0;
}

void AtlantisGuests::removeAt(uint i) {
	for (uint k = i; k < count && k + 1 < kSlots; k++) {
		id[k] = id[k + 1];
		sprite[k] = sprite[k + 1];
	}
	count--;
}

void AtlantisGuests::remove(int32 guestId) {
	for (uint i = 0; i < count; i++)
		if (id[i] == guestId)
			removeAt(i);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
