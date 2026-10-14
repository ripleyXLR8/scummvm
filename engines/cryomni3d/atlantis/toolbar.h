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

#ifndef CRYOMNI3D_ATLANTIS_TOOLBAR_H
#define CRYOMNI3D_ATLANTIS_TOOLBAR_H

#include "common/array.h"
#include "common/rect.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/objects.h"

namespace CryOmni3D {

namespace Atlantis {

class CryOmni3DEngine_Atlantis;

// The inventory bar: a 9-slot strip (OBJETS0.SPR background) that slides up
// from the bottom of the screen.  Items are dragged out to be carried; the
// SPRMENU decorative overlays and the in-game menu land with the menus layer.
class Atlantis_Toolbar {
public:
	Atlantis_Toolbar();
	~Atlantis_Toolbar();

	void init(Inventory *inventory, CryOmni3DEngine_Atlantis *engine);

	// The bar surface covers the bottom 82 rows; atlantis.exe draws the
	// OBJETS0 background with its hotspot at y = 443 (FUN_0042c8ac @
	// 0x0042c96e), so the menu sprite's ty is kBarHotspotY - kBarSurfaceTop.
	static const int kBarSurfaceTop = 480 - 82;
	static const int kBarHotspotY = 443;
	// Top row, in the bar surface, of a menu sprite at (tx, ty).
	static int menuSpriteTop(int ty, int16 yoff, uint position) { return (ty - yoff) + (int)position; }
	void addMenuSprite(uint16 w, uint16 h, int16 xoff, int16 yoff,
	                   int tx, int ty, const uint16 *pix, const uint8 *blend);

	bool displayToolbar(const Graphics::Surface *original);
	// atlantis.exe FUN_004278a0 after a pick-up: FUN_0042c2c4 slides the bar
	// in, FUN_0042cc98 holds the new object (obj, already in the inventory)
	// until a left or right click, FUN_0042c59c slides the bar out.  True for
	// a right click (the object stays in hand).
	bool displayPickup(const Graphics::Surface *original, Object *obj);
	// One frame of the bar loops' engine work (barFrameTick); true when the
	// bar must close.  Redraws the view and the bar after a timer section.
	bool barTick(const Graphics::Surface *original);
	// One frame of a bar slide: the view, the guest portraits, the bar at
	// _position (82 = not drawn), on the whole screen.
	void slideFrame(const Graphics::Surface *original);
	// FUN_0042c2c4 / FUN_0042c59c; true when a timer closed the bar.
	bool slideIn(const Graphics::Surface *original);
	bool slideOut(const Graphics::Surface *original);
	void inventoryChanged(uint newPosition);
	// The guest whose portrait was clicked in the open bar (0: none); the
	// caller talks to it (atlantis.exe FUN_0042c8ac @ 0x0042cbfe).
	int32 guestClicked() const { return _guestClicked; }

private:
	typedef uint(Atlantis_Toolbar::*ZoneCallback)(uint dragStatus);

	struct Zone {
		Common::Rect rect;
		ZoneCallback callback;
		bool secondary;  // true = inactive / grayed
		bool hidden;
	};

	Common::Array<Zone> _zones;
	Inventory *_inventory;
	CryOmni3DEngine_Atlantis *_engine;

	Graphics::PixelFormat _format;

	int32 _guestClicked = 0;
	uint _inventoryOffset;
	uint _inventoryMaxOffset;
	uint _inventoryHovered;
	uint _inventorySelected;
	bool _inventoryButtonDragging;

	uint _position;

	struct MenuSprData {
		Common::Array<uint16> pixels;
		Common::Array<uint8>  blend;   // per-pixel blend factor; kSprNoBlend = opaque
		uint16 w, h;
		int16  xoff, yoff;
		int    tx, ty;
	};
	Common::Array<MenuSprData> _menuSprData;

	Graphics::Surface _bgSurface;
	Graphics::ManagedSurface _destSurface;

	enum ZoneIdx {
		kZoneInventory0 = 0  // inventory slots 0-8 occupy indices 0-8
	};

	// 9 visible inventory slots, one per star-of-David ornament in OBJETS0.SPR.
	static const uint kInventorySlots = 9;

	void setupZones();
	void addZone(Common::Rect rect, ZoneCallback callback);
	void updateZones();
	Common::Array<Zone>::const_iterator hitTestZones(const Common::Point &mousePos) const;
	uint captureEvent(const Common::Point &mousePos, uint dragStatus);
	void drawBackground(const Graphics::Surface *original);
	void drawToolbar(const Graphics::Surface *original);
	// Runs the open toolbar's event loop.  Returns true when it ended because
	// an inventory item was taken (the caller then skips the slide-down so the
	// carried object reaches navigation immediately).
	bool handleToolbarEvents(const Graphics::Surface *original);
	// The record count 0x006cd7bc: one past the last record in the list.
	uint recordCount() const;

	// FUN_0042c098 @ 0x0042c0ff: the record a left click takes, first + cell
	// with cell = x / 70, when cell + 1 <= first + 9 and y >= 409; else -1.
	static int clickedRecord(int x, int y, uint first) {
		const int cell = x / 70;
		if (cell + 1 <= (int)first + 9 && y >= 409)
			return cell + (int)first;
		return -1;
	}
	// FUN_0042c8ac @ 0x0042cb83: +1 for x > 630 with y > 400, more than 9
	// records and first < count - 7; -1 for x < 5 with y > 400 and
	// first > 0; else 0.
	static int scrollStep(int x, int y, uint count, uint first) {
		if (y <= 400)
			return 0;
		if (x > 630 && count > 9 && first + 7 < count)
			return 1;
		if (x < 5 && first > 0)
			return -1;
		return 0;
	}

	template<uint N>
	uint callbackInventory(uint dragStatus) { return callbackInventory(N, dragStatus); }
	uint callbackInventory(uint invId, uint dragStatus);
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_TOOLBAR_H
