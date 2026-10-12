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

#include "common/system.h"

#include "cryomni3d/cryomni3d.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"
#include "cryomni3d/atlantis/toolbar.h"

namespace CryOmni3D {
namespace Atlantis {

Atlantis_Toolbar::Atlantis_Toolbar() :
	_inventory(nullptr), _engine(nullptr),
	_inventoryOffset(0), _inventoryMaxOffset(0),
	_inventoryHovered(uint(-1)), _inventorySelected(uint(-1)),
	_inventoryButtonDragging(false),
	_position(82) {
}

Atlantis_Toolbar::~Atlantis_Toolbar() {
	_bgSurface.free();
	_destSurface.free();
}

void Atlantis_Toolbar::init(Inventory *inventory, CryOmni3DEngine_Atlantis *engine) {
	_inventory = inventory;
	_engine    = engine;

	_format = g_system->getScreenFormat();
	// 82 rows: the height of OBJETS0.SPR, the inventory-bar background.  The
	// bar slides up from y=screen.h to y=screen.h-82.
	_bgSurface.create(640, 82, _format);
	_destSurface.create(640, 82, _format);

	// 9 inventory slot rectangles.  Slot centres are from atlantis.exe (toolbar
	// setup FUN_0042c2c4): the slot-X table at 0x00611710 is x = i*0x46 + 0x2a
	// = i*70 + 42; the slot-Y table settles at 443 (screen) = 45 in the
	// 398-origin toolbar surface.
	static const int kSlotCenters[kInventorySlots] = {
		42, 112, 182, 252, 322, 392, 462, 532, 602
	};
	const int kSlotHalfW = 25;
	const int kSlotTop   = 25;
	const int kSlotBot   = 65;
	ZoneCallback kSlotCbs[kInventorySlots] = {
		&Atlantis_Toolbar::callbackInventory<0>,
		&Atlantis_Toolbar::callbackInventory<1>,
		&Atlantis_Toolbar::callbackInventory<2>,
		&Atlantis_Toolbar::callbackInventory<3>,
		&Atlantis_Toolbar::callbackInventory<4>,
		&Atlantis_Toolbar::callbackInventory<5>,
		&Atlantis_Toolbar::callbackInventory<6>,
		&Atlantis_Toolbar::callbackInventory<7>,
		&Atlantis_Toolbar::callbackInventory<8>,
	};
	for (uint i = 0; i < kInventorySlots; i++)
		addZone(Common::Rect(kSlotCenters[i] - kSlotHalfW, kSlotTop,
		                     kSlotCenters[i] + kSlotHalfW, kSlotBot),
		        kSlotCbs[i]);
}

void Atlantis_Toolbar::addZone(Common::Rect rect, ZoneCallback callback) {
	Zone zone = { rect, callback, true, false };
	_zones.push_back(zone);
}

void Atlantis_Toolbar::inventoryChanged(uint newPosition) {
	if (newPosition != uint(-1) && newPosition > _inventoryOffset)
		_inventoryOffset = newPosition - (kInventorySlots - 1);
	updateZones();
}

void Atlantis_Toolbar::updateZones() {
	// Max scroll offset (how far we can page right).
	_inventoryMaxOffset = 0;
	for (uint i = kInventorySlots; i < _inventory->size(); i++) {
		if ((*_inventory)[i] != nullptr)
			_inventoryMaxOffset = i - (kInventorySlots - 1);
	}
	if (_inventoryOffset > _inventoryMaxOffset)
		_inventoryOffset = _inventoryMaxOffset;
}

Common::Array<Atlantis_Toolbar::Zone>::const_iterator Atlantis_Toolbar::hitTestZones(
	const Common::Point &mousePos) const {
	// atlantis.exe FUN_0042c098 hit-tests the bar as 9 full-width 70-px columns
	// (slot = mouseX / 70), valid while the cursor is inside the bar
	// (toolbar-local y >= 11).  The Zone rects only position the icons.
	if (mousePos.y < 11 || mousePos.y >= (int)_destSurface.h)
		return _zones.end();
	int column = mousePos.x / 70;
	if (column < 0 || column >= (int)_zones.size())
		return _zones.end();
	Common::Array<Zone>::const_iterator it = _zones.begin() + column;
	if (it->hidden || !it->callback)
		return _zones.end();
	return it;
}

uint Atlantis_Toolbar::captureEvent(const Common::Point &mousePos, uint dragStatus) {
	Common::Array<Zone>::const_iterator it = hitTestZones(mousePos);
	if (it != _zones.end())
		return (this->*(it->callback))(dragStatus);
	return 0;
}

void Atlantis_Toolbar::addMenuSprite(uint16 w, uint16 h, int16 xoff, int16 yoff,
                                      uint16 transpKey, int tx, int ty, const uint16 *pix,
                                      const uint8 *blend) {
	MenuSprData s;
	s.w = w; s.h = h;
	s.xoff = xoff; s.yoff = yoff;
	s.tx = tx; s.ty = ty;
	s.transpKey = transpKey;
	s.pixels.assign(pix, pix + (uint)w * h);
	s.blend.assign(blend, blend + (uint)w * h);
	_menuSprData.push_back(s);
}

void Atlantis_Toolbar::drawBackground(const Graphics::Surface *original) {
	if (!original)
		return;
	// Capture the bottom 82 rows of the current scene unmodified — Atlantis
	// paints no darkened panel; the SPRMENU/OBJETS0 sprites composited on top
	// are the entire visible toolbar, with the scene showing through.
	const int srcY = original->h - _bgSurface.h;
	if (srcY < 0)
		return;
	const byte *src = (const byte *)original->getBasePtr(0, srcY);
	byte *dst = (byte *)_bgSurface.getPixels();
	const uint rowBytes = _bgSurface.w * _format.bytesPerPixel;
	for (int y = 0; y < _bgSurface.h; y++)
		memcpy(dst + y * _bgSurface.pitch, src + y * original->pitch, rowBytes);
}

void Atlantis_Toolbar::drawToolbar(const Graphics::Surface *original) {
	if (_position > 82)
		_position = 82;

	if (_position != 0) {
		// Not fully visible: copy the top _position rows from the scene.
		if (original) {
			int srcY = original->h - _destSurface.h;
			Common::Rect srcRct(0, srcY, 640, srcY + _position);
			_destSurface.copyRectToSurface(*original, 0, 0, srcRct);
		}
	}

	if (_position == (uint)_destSurface.h)
		return;  // fully hidden

	// Copy the scene strip into the visible region.
	{
		Common::Rect visRct(0, _position, 640, _destSurface.h);
		_destSurface.copyRectToSurface(_bgSurface, 0, _position, visRct);
	}

	// Draw the bar background (and any other menu sprites) over it.
	for (const MenuSprData &spr : _menuSprData) {
		int baseX = spr.tx - spr.xoff;
		int baseY = (spr.ty - spr.yoff) + _position;
		for (int y = 0; y < (int)spr.h; y++) {
			int dy = baseY + y;
			if (dy < 0 || dy >= _destSurface.h)
				continue;
			for (int x = 0; x < (int)spr.w; x++) {
				int dx = baseX + x;
				if (dx < 0 || dx >= _destSurface.w)
					continue;
				uint sidx = y * spr.w + x;
				uint16 pix = spr.pixels[sidx];
				if (pix == spr.transpKey)
					continue;
				uint16 *dp = (uint16 *)_destSurface.getBasePtr(dx, dy);
				if (spr.blend[sidx] != kSprNoBlend)
					pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
				*dp = pix;
			}
		}
	}

	// Draw inventory item icons on top of the slot ornaments.  An empty slot
	// just leaves the ornament visible; the item currently in hand is skipped.
	for (uint i = 0; i < kInventorySlots; i++) {
		uint invIdx = i + _inventoryOffset;
		Object *obj = (invIdx < _inventory->size()) ? (*_inventory)[invIdx] : nullptr;
		if (obj == nullptr)
			continue;
		if (obj == _inventory->selectedObject())
			continue;

		// Slot rect in toolbar-local coords, offset by the slide position.  Not
		// clipped to the surface here — drawInventoryIcon() clips, so the icon
		// doesn't shift/trim mid-slide.
		Common::Rect r = _zones[kZoneInventory0 + i].rect;
		r.translate(0, _position);
		if (r.bottom <= 0 || r.top >= (int)_destSurface.h)
			continue;
		_engine->drawInventoryIcon(_destSurface, obj, r);
	}
}

bool Atlantis_Toolbar::displayToolbar(const Graphics::Surface *original) {
	// The toolbar uses the plain arrow cursor (the base sprite-cursor system
	// is unused by Atlantis).
	_engine->setArrowCursor();

	drawBackground(original);

	_inventorySelected = uint(-1);
	_inventoryHovered  = uint(-1);
	_guestClicked = 0;
	updateZones();

	// Slide the bar up.
	for (_position = _destSurface.h; _position > 0 && !_engine->shouldAbort(); _position--) {
		drawToolbar(original);
		g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0,
		                           original ? original->h - _destSurface.h : (480 - 82),
		                           _destSurface.w, _destSurface.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		_engine->pollEvents();
	}
	if (_engine->shouldAbort())
		return false;

	_engine->clearKeys();
	_engine->waitMouseRelease();

	bool itemTaken = handleToolbarEvents(original);
	if (_engine->shouldAbort())
		return false;

	// Slide down — skipped when an item was just taken, so the carried object
	// reaches the centred crosshair at once.
	if (!itemTaken) {
		for (_position = 0; _position <= (uint)_destSurface.h && !_engine->shouldAbort(); _position++) {
			drawToolbar(original);
			g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0,
			                           original ? original->h - _destSurface.h : (480 - 82),
			                           _destSurface.w, _destSurface.h);
			g_system->updateScreen();
			g_system->delayMillis(10);
			_engine->pollEvents();
		}
	}

	return false;
}

bool Atlantis_Toolbar::handleToolbarEvents(const Graphics::Surface *original) {
	_inventoryHovered  = uint(-1);
	_inventorySelected = uint(-1);
	_inventoryButtonDragging = false;
	// The in-hand object is deliberately NOT cleared: reopening the bar keeps
	// what you carry; it is put back only by an explicit action.

	updateZones();
	drawToolbar(original);
	g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0,
	                           original ? original->h - _destSurface.h : (480 - 82),
	                           _destSurface.w, _destSurface.h);
	g_system->updateScreen();

	// Toolbar covers screen.h-82 .. screen.h, plus a 32px hover buffer above.
	const int kInsideY = 480 - (int)_destSurface.h - 32;
	bool mouseInsideToolbar = (_engine->getMousePos().y > kInsideY);
	bool exitToolbar = false;
	bool itemTaken = false;
	bool rightReleasedSinceOpen = false;

	const uint32 kScrollIntervalMs = 110;
	uint32 nextScrollMs = 0;

	while (!exitToolbar) {
		_engine->pollEvents();
		if (_engine->shouldAbort()) {
			exitToolbar = true;
			break;
		}

		// A right-click is acted on only after the button has been released
		// once, so the click that OPENED the bar isn't consumed as the close.
		if (_engine->getCurrentMouseButton() != 2)
			rightReleasedSinceOpen = true;

		if (_engine->checkKeysPressed(2, Common::KEYCODE_ESCAPE, Common::KEYCODE_SPACE)) {
			exitToolbar = true;
			break;
		}
		// Right-click: drop any carried item back, then close.
		if (_engine->getCurrentMouseButton() == 2 && rightReleasedSinceOpen) {
			_engine->waitMouseRelease();
			if (_inventory->selectedObject() != nullptr) {
				_inventory->setSelectedObject(nullptr);
				_engine->setArrowCursor();
				updateZones();
			}
			exitToolbar = true;
			break;
		}

		Common::Point mousePosInToolbar = _engine->getMousePos();
		mousePosInToolbar -= Common::Point(0, 480 - _destSurface.h);

		DragStatus drag = _engine->getDragStatus();
		// A click on a guest portrait (other than 592) closes the bar for a
		// conversation with that guest (atlantis.exe FUN_0042c8ac @ 0x0042cbfe,
		// FUN_0042bdb8; the portrait test comes before the item click).
		if (drag == kDragStatus_Pressed) {
			const Common::Point ms = _engine->getMousePos();
			int32 g = _engine->guestAt(ms.x, ms.y);
			if (g != 0 && g != 592) {
				_engine->waitMouseRelease();
				_guestClicked = g;
				exitToolbar = true;
				break;
			}
		}
		bool redrawToolbar = false;
		bool tookItem = false;
		if (captureEvent(mousePosInToolbar, drag)) {
			updateZones();
			redrawToolbar = true;
			if (drag == kDragStatus_Finished && _inventory->selectedObject() != nullptr)
				tookItem = true;
		} else if (drag == kDragStatus_Pressed) {
			// Left-click on an empty part of the bar: put any in-hand item back.
			_inventorySelected = uint(-1);
			_inventory->setSelectedObject(nullptr);
			_engine->setArrowCursor();
			updateZones();
			redrawToolbar = true;
		}

		// Taking an item closes the bar at once and recentres the cursor (now
		// carrying the item); the caller skips the slide-down.
		if (tookItem) {
			_engine->setMousePos(Common::Point(g_system->getWidth() / 2, 240));
			itemTaken = true;
			exitToolbar = true;
			break;
		}

		// Edge-hover scrolling (atlantis.exe FUN_0042c8ac), throttled.
		{
			Common::Point ms = _engine->getMousePos();
			if (ms.y > kInsideY && g_system->getMillis() >= nextScrollMs) {
				if (ms.x > 630 && _inventoryOffset < _inventoryMaxOffset) {
					_inventoryOffset++;
					nextScrollMs = g_system->getMillis() + kScrollIntervalMs;
					redrawToolbar = true;
				} else if (ms.x < 5 && _inventoryOffset > 0) {
					_inventoryOffset--;
					nextScrollMs = g_system->getMillis() + kScrollIntervalMs;
					redrawToolbar = true;
				}
			}
		}

		// Exit when the mouse leaves the toolbar area.
		if (!mouseInsideToolbar) {
			mouseInsideToolbar = (_engine->getMousePos().y > kInsideY);
		} else if (_engine->getMousePos().y <= kInsideY) {
			exitToolbar = true;
			break;
		}

		// Hover inventory items.
		if (_inventory->selectedObject() == nullptr) {
			Common::Array<Zone>::const_iterator zoneIt = hitTestZones(mousePosInToolbar);
			uint zoneId = (uint)(zoneIt - _zones.begin());
			uint invIdx = zoneId + _inventoryOffset;
			bool shouldHover = (zoneId < kInventorySlots &&
			                    invIdx < _inventory->size() &&
			                    (*_inventory)[invIdx] != nullptr);
			if (shouldHover) {
				if (_inventoryHovered != invIdx && (*_inventory)[invIdx]->valid()) {
					_inventoryHovered = invIdx;
					redrawToolbar = true;
				}
			} else if (_inventoryHovered != uint(-1)) {
				_inventoryHovered  = uint(-1);
				_inventorySelected = uint(-1);
				updateZones();
				_engine->setArrowCursor();
				redrawToolbar = true;
			}
			_inventoryButtonDragging = false;
		}

		if (redrawToolbar) {
			drawToolbar(original);
			g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0,
			                           original ? original->h - _destSurface.h : (480 - 82),
			                           _destSurface.w, _destSurface.h);
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	return itemTaken;
}

uint Atlantis_Toolbar::callbackInventory(uint invId, uint dragStatus) {
	invId += _inventoryOffset;
	Object *obj = (invId < _inventory->size()) ? (*_inventory)[invId] : nullptr;
	if (obj == nullptr || !obj->valid())
		return 0;

	switch (dragStatus) {
	case kDragStatus_Pressed:
		_inventorySelected = invId;
		_inventoryButtonDragging = true;
		return 1;
	case kDragStatus_Dragging:
		if (_inventorySelected != invId) {
			_inventorySelected = invId;
			_inventoryButtonDragging = true;
		}
		return 1;
	case kDragStatus_Finished:
		if (obj == _inventory->selectedObject()) {
			// Clicking the slot of the item already in hand puts it back.
			_inventory->setSelectedObject(nullptr);
			_engine->setArrowCursor();
			_inventorySelected = uint(-1);
			return 1;
		}
		_inventory->setSelectedObject(obj);
		_engine->setItemCursor(obj);   // the taken item rides the cursor
		_inventorySelected = invId;
		return 1;
	default:
		return 0;
	}
}

} // namespace Atlantis
} // namespace CryOmni3D
