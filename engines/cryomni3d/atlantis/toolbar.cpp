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

	setupZones();
}

void Atlantis_Toolbar::setupZones() {
	_zones.clear();
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
	// FUN_0042c098 takes record x / 70 + first while x / 70 + 1 <= first + 9
	// (0x00496924 / 0x00496928 move together), so once the bar is scrolled
	// the 10-px column at x >= 630 takes record first + 9.  It has no icon.
	addZone(Common::Rect(630, kSlotTop, 630, kSlotBot), &Atlantis_Toolbar::callbackInventory<kInventorySlots>);
}

void Atlantis_Toolbar::addZone(Common::Rect rect, ZoneCallback callback) {
	Zone zone = { rect, callback, true, false };
	_zones.push_back(zone);
}

void Atlantis_Toolbar::inventoryChanged(uint newPosition) {
	// The first shown record 0x00496924 does not follow the inventory: its
	// only writers are the edge scrolls of FUN_0042c8ac / FUN_0042cc98
	// (0x0042cbba, 0x0042cbe8, 0x0042cf55, 0x0042cf83) and FUN_0042de5c,
	// which sets it to 0 (0x0042e2ec).
	// See atlantis-re: experiments/2026-10-16-05-inventory-first-shown.md.
	(void)newPosition;
	updateZones();
}

void Atlantis_Toolbar::updateZones() {
	// How far the edge scroll can go is scrollStep's rule (first < count - 7),
	// checked when it scrolls; nothing clamps the first shown record here.
	_inventoryMaxOffset = 0;
	for (uint i = kInventorySlots; i < _inventory->size(); i++) {
		if ((*_inventory)[i] != nullptr)
			_inventoryMaxOffset = i - (kInventorySlots - 1);
	}
}

Common::Array<Atlantis_Toolbar::Zone>::const_iterator Atlantis_Toolbar::hitTestZones(
	const Common::Point &mousePos) const {
	// atlantis.exe FUN_0042c098 hit-tests the bar as 9 full-width 70-px columns
	// (slot = mouseX / 70), valid while the cursor is inside the bar
	// (toolbar-local y >= 11).  The Zone rects only position the icons.
	if (mousePos.y < 11 || mousePos.y >= (int)_destSurface.h)
		return _zones.end();
	int column = mousePos.x / 70;
	if (column < 0 || column >= (int)_zones.size() ||
	    column + 1 > (int)(_inventoryOffset + kInventorySlots))
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
                                      int tx, int ty, const uint16 *pix,
                                      const uint8 *blend) {
	MenuSprData s;
	s.w = w; s.h = h;
	s.xoff = xoff; s.yoff = yoff;
	s.tx = tx; s.ty = ty;
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
		int baseY = menuSpriteTop(spr.ty, spr.yoff, _position);
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
				if (spr.blend[sidx] == kSprTranspBlend)
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

	// Slide the bar up (FUN_0042c2c4); a timer that sets GameOver or Exit
	// ends it early (0x0042c55d).
	bool closed = slideIn(original);
	if (_engine->shouldAbort())
		return false;

	_engine->clearKeys();
	_engine->waitMouseRelease();

	bool itemTaken = closed ? false : handleToolbarEvents(original);
	if (_engine->shouldAbort())
		return false;

	// Slide down (FUN_0042c59c), after a take too (FUN_0042c098 calls it at
	// 0x0042c219 on every exit); a timer that sets GameOver or Exit ends it
	// early (0x0042c805).
	(void)itemTaken;
	slideOut(original);

	return false;
}

void Atlantis_Toolbar::slideFrame(const Graphics::Surface *original) {
	Graphics::ManagedSurface frame(640, 480, _format);
	if (original)
		frame.blitFrom(*original);
	_engine->drawGuests(frame);
	drawToolbar(&frame.rawSurface());
	if (_position < (uint)_destSurface.h)
		frame.blitFrom(_destSurface, Common::Point(0, frame.h - _destSurface.h));
	g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
	g_system->updateScreen();
	// [UNKNOWN] the slides have no timer of their own; the port's pace.
	g_system->delayMillis(10);
	_engine->pollEvents();
}

// FUN_0042c2c4: the slots start at y 499 and move up 7 a frame, then are
// drawn, until 443 (8 frames): the bar's offset from its up position is
// 49, 42, .. 0.  The guest portraits move up with it.
bool Atlantis_Toolbar::slideIn(const Graphics::Surface *original) {
	for (int slotY = 499 - 7; slotY >= 443 && !_engine->shouldAbort(); slotY -= 7) {
		_position = (uint)(slotY - 443);
		_engine->slideGuestPortraits(-7);
		slideFrame(original);
		if (barTick(original))
			return true;
	}
	_position = 0;
	return false;
}

// FUN_0042c59c: each frame the bar is drawn at the slots' y while it is
// below 0x1fa (506), then they move down 7; it stops when they reach 0x208
// (520): 11 frames, the last two without the bar.  The portraits go back
// down to 350.
bool Atlantis_Toolbar::slideOut(const Graphics::Surface *original) {
	for (int slotY = 443; slotY < 0x208 && !_engine->shouldAbort(); slotY += 7) {
		_position = slotY < 0x1fa ? (uint)(slotY - 443) : (uint)_destSurface.h;
		slideFrame(original);
		_engine->slideGuestPortraits(7);
		if (barTick(original))
			return true;
	}
	_position = _destSurface.h;
	return false;
}

void Atlantis_Toolbar::presentBarFrame(const Graphics::Surface *original) {
	_engine->animateSprites();   // FUN_00425304
	const Graphics::Surface *view = _engine->currentViewSurface();   // FUN_00451d8b
	if (!view || !original) {
		drawToolbar(original);
		const int barY = original ? original->h - _destSurface.h : (480 - 82);
		g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0, barY,
		                           _destSurface.w, _destSurface.h);
		return;
	}
	Graphics::ManagedSurface frame(view->w, view->h, view->format);
	frame.blitFrom(*view);
	drawToolbar(&frame.rawSurface());
	frame.blitFrom(_destSurface, Common::Point(0, frame.h - _destSurface.h));
	_engine->drawGuests(frame);   // FUN_0042bc58, after the bar
	g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
}

bool Atlantis_Toolbar::barTick(const Graphics::Surface *original) {
	bool played = false;
	bool close = _engine->barFrameTick(played);
	// A timer section drew over the screen: put the view and the bar back.
	if (played && original && !close) {
		g_system->copyRectToScreen(original->getPixels(), original->pitch, 0, 0, original->w, original->h);
		drawToolbar(original);
		g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0, original->h - _destSurface.h,
		                           _destSurface.w, _destSurface.h);
		g_system->updateScreen();
	}
	return close;
}

bool Atlantis_Toolbar::displayPickup(const Graphics::Surface *original, Object *obj) {
	drawBackground(original);
	_inventorySelected = uint(-1);
	_inventoryHovered  = uint(-1);
	_guestClicked = 0;
	// FUN_0042cc98: ObjetEnMain and the held icon are the new record's.
	_inventory->setSelectedObject(obj);
	_engine->setItemCursor(obj);
	updateZones();

	slideIn(original);

	// The loop ends on the press edge of either button (FUN_0041a298 sets
	// 0x0049611c on a VK_LBUTTON edge, 0x00496120 on a VK_RBUTTON edge); a
	// button still held from the pick-up click does not count.
	uint used = 0;
	for (Object *o : *_inventory)
		if (o) used++;
	uint prevButton = _engine->getCurrentMouseButton();
	bool right = false;
	const uint32 kScrollIntervalMs = 110;
	uint32 nextScrollMs = 0;
	while (!_engine->shouldAbort()) {
		_engine->pollEvents();
		uint button = _engine->getCurrentMouseButton();
		if (button != prevButton && button == 1)
			break;
		if (button != prevButton && button == 2) {
			right = true;
			break;
		}
		prevButton = button;
		// FUN_0042cc98 animates the place sprites (0x0042cd1c) but runs no
		// timers; the frame is drawn below, like the bar loop's.
		// FUN_0042cc98 edge scrolling: x > 0x276 or x < 5 with y > 400; it
		// scrolls right only while more than 9 records exist and the first
		// shown is below count - 7.  The port throttles it as in the bar loop.
		const Common::Point ms = _engine->getMousePos();
		if (ms.y > 400 && g_system->getMillis() >= nextScrollMs) {
			if (ms.x > 0x276 && used > 9 && _inventoryOffset + 7 < used) {
				_inventoryOffset++;
				nextScrollMs = g_system->getMillis() + kScrollIntervalMs;
			} else if (ms.x < 5 && _inventoryOffset > 0) {
				_inventoryOffset--;
				nextScrollMs = g_system->getMillis() + kScrollIntervalMs;
			}
		}
		presentBarFrame(original);
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	_engine->waitMouseRelease();
	if (!right) {
		_inventory->setSelectedObject(nullptr);
		_engine->setArrowCursor();
	}

	slideOut(original);
	return right;
}

// One opening of the bar after its slide-in: the bar loop FUN_0042c8ac, then
// the item click of FUN_0042c098.  The loop ends on a left-button edge
// (0x0049611c), a right-button edge (the 0x004960c4 toggle of FUN_0041a298)
// or GameOver / Exit set by a timer (0x0042cb67); it does not end when the
// pointer leaves the bar.  On every exit the guest portrait under the
// pointer, other than 592, starts a conversation (0x0042cbfe, FUN_0042bdb8).
// Then, unless GameOver is set (0x0042c0ed): a left click with the pointer
// on an item cell (cell = x / 70, cell + 1 <= first + 9, y >= 409) takes the
// record first + cell, or puts it back when it is the one in hand
// (0x0042c161, 0x0042c1a1); any other left or right click empties the hand
// (0x0042c1c9).  Returns true when an item was taken.
bool Atlantis_Toolbar::handleToolbarEvents(const Graphics::Surface *original) {
	_inventoryHovered  = uint(-1);
	_inventorySelected = uint(-1);
	_inventoryButtonDragging = false;

	updateZones();
	drawToolbar(original);
	const int barY = original ? original->h - _destSurface.h : (480 - 82);
	g_system->copyRectToScreen(_destSurface.getPixels(), _destSurface.pitch, 0, barY,
	                           _destSurface.w, _destSurface.h);
	g_system->updateScreen();

	// [UNKNOWN] pace: FUN_0042c8ac scrolls once per frame and the loop has
	// no timer (FUN_00432178 at its end manages the ambient sounds), so the
	// original's speed was the machine's; the port steps every 110 ms
	// (decisions/0013-unpaced-loops.md).
	const uint32 kScrollIntervalMs = 110;
	uint32 nextScrollMs = 0;

	uint prevButton = _engine->getCurrentMouseButton();
	bool leftClick = false, gameOver = false;
	while (!_engine->shouldAbort()) {
		_engine->pollEvents();
		const uint button = _engine->getCurrentMouseButton();
		if (button != prevButton && button == 1) {
			leftClick = true;
			break;
		}
		if (button != prevButton && button == 2)
			break;   // the right button empties the hand below
		prevButton = button;
		// FUN_0042c8ac: the timers each frame (0x0042cb2c); GameOver or Exit
		// closes the bar (0x0042cb67).
		if (barTick(original)) {
			gameOver = true;
			break;
		}
		// No key closes the bar: FUN_0041a298 never reads VK_SPACE, and its
		// VK_ESCAPE branch needs 0x00496188 == 1 or 0x004960c4 == 0, while
		// the bar loop keeps 0x004960c4 at 1.

		// Scroll (0x0042cb83): x > 630 with y > 400, more than 9 records and
		// first < count - 7; x < 5 with y > 400 and first > 0.
		const Common::Point ms = _engine->getMousePos();
		if (g_system->getMillis() >= nextScrollMs) {
			const int step = scrollStep(ms.x, ms.y, recordCount(), _inventoryOffset);
			if (step != 0) {
				_inventoryOffset += step;
				nextScrollMs = g_system->getMillis() + kScrollIntervalMs;
			}
		}

		// Every frame: the place sprites and the view are redrawn behind the
		// bar (FUN_0042c8ac step 2), not only when it scrolls.
		presentBarFrame(original);
		g_system->updateScreen();
		_engine->ambientFrame();   // FUN_00432178 ends each bar frame (FUN_0042c8ac step 5)
		g_system->delayMillis(10);
	}
	if (_engine->shouldAbort())
		return false;

	// The exit test of FUN_0042c8ac (0x0042cbfe): the guest portrait under
	// the pointer.  The conversation then runs from the caller after the
	// slide-out, as FUN_0042c59c comes before FUN_00425828.
	const Common::Point at = _engine->getMousePos();
	const int32 guest = _engine->guestAt(at.x, at.y);
	if (guest != 0 && guest != 592)
		_guestClicked = guest;
	_engine->waitMouseRelease();

	if (gameOver)
		return false;
	// The conversation clears 0x004960c4, so the item click is skipped; the
	// hand is left as it is [INFERRED: the dialog clears the click flags
	// 0x0049611c / 0x00496120 that would empty it].
	if (_guestClicked != 0)
		return false;

	const int rec = leftClick ? clickedRecord(at.x, at.y, _inventoryOffset) : -1;
	if (rec >= 0) {
		const uint r = (uint)rec;
		Object *obj = (r < _inventory->size()) ? (*_inventory)[r] : nullptr;
		Object *held = _inventory->selectedObject();
		// FUN_0042c098 compares ids (duplicates are allowed); a cell past the
		// last record reads id 0, which empties the hand.
		if (obj && obj->valid() && !(held && held->idOBJ() == obj->idOBJ())) {
			_inventory->setSelectedObject(obj);
			_engine->setItemCursor(obj);   // the taken item rides the cursor
			updateZones();
			return true;
		}
	}
	_inventory->setSelectedObject(nullptr);
	_engine->setArrowCursor();
	updateZones();
	return false;
}

uint Atlantis_Toolbar::recordCount() const {
	uint count = 0;
	for (uint i = 0; i < _inventory->size(); i++)
		if ((*_inventory)[i] != nullptr)
			count = i + 1;
	return count;
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
