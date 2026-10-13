// Audit "graphics", element 2: inventory model and inventory toolbar.
//
// ORIGINAL (atlantis-cd-en-714240):
//   FUN_0042c054  start-up reset: 1 record {sprite 0, object 257}
//   FUN_0042b328  "/set(INVENTnnn=v)": append {sprite, nnn} (no duplicate
//                 check; sprite = last index i with table[i]==nnn, table
//                 0x004967b0 scanned up to the -1 terminator); v==0 -> FUN_0042b3ec
//   FUN_0042b3ec  remove the first record with that id and shift the tail down
//   FUN_0042c2c4 / FUN_0042c8ac  bar drawing: OBJETS0 sprite 0 at hotspot
//                 (0, 443), icon of record first+i at hotspot (i*70+42, 443)
//   FUN_0042c098  click: record = x/70 + first, valid when x/70 + 1 <= end
//                 and y >= 409; takes the record's object
// The record lists below were produced by running FUN_0042c054/FUN_0042b328/
// FUN_0042b3ec under Unicorn (atlantis-re/tools/emu/graphics_inventory.py).
// The geometry/hit-test rules are read from the disassembly (level B).
//
// PORT code exercised directly (linked): CryOmni3D::Inventory (objects.cpp),
// the Atlantis inventory unit (inventory.cpp: readInventoryIconTable,
// buildInventoryCatalogue, findInventoryObject, inventoryAppend,
// inventoryRemove, inventoryReset), Atlantis_Toolbar::hitTestZones /
// callbackInventory / setupZones / menuSpriteTop (toolbar.cpp); init()
// itself cannot run (see initToolbarNoGfx).
// portInvent() repeats the "inventN" branch of execConSetCommand
// (con_exec.cpp), which only calls the inventory unit.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"
#include "common/func.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/objects.h"
#include "cryomni3d/sprites.h"
#include "cryomni3d/atlantis/inventory.h"

#define private public
#include "cryomni3d/atlantis/toolbar.h"
#undef private

namespace GraphicsToolbarTest {

struct NullCb {
	void cb(uint) {}
};

// setupObjects() (exetables.cpp) for a list of ids: the real catalogue
// builder, with an unmapped Sprites like the engine's _sprites.
inline void portSetupObjects(CryOmni3D::Objects &objects, const Common::Array<uint> &ids) {
	static const CryOmni3D::Sprites sprites;
	CryOmni3D::Atlantis::buildInventoryCatalogue(sprites, ids, objects);
}

// The inventN branch of execConSetCommand (con_exec.cpp).
inline void portInvent(CryOmni3D::Objects &objects, CryOmni3D::Inventory &inv, uint objId, int value) {
	if (value) {
		CryOmni3D::Object *obj = CryOmni3D::Atlantis::findInventoryObject(objects, objId);
		if (obj)
			CryOmni3D::Atlantis::inventoryAppend(inv, obj);
	} else {
		CryOmni3D::Atlantis::inventoryRemove(inv, objId);
		CryOmni3D::Object *sel = inv.selectedObject();
		if (sel && sel->idOBJ() == objId)
			inv.deselectObject();
	}
}

// Ids carried, in toolbar order (holes skipped, as drawToolbar skips them).
inline Common::String order(const CryOmni3D::Inventory &inv) {
	Common::String s;
	for (uint i = 0; i < inv.size(); i++)
		if (inv[i]) {
			if (!s.empty())
				s += " ";
			s += Common::String::format("%u", inv[i]->idOBJ());
		}
	return s;
}

// Atlantis_Toolbar::init() calls g_system->getScreenFormat(), which the
// test null OSystem cannot serve (no graphics manager in test mode).  This
// sets the members init() sets and calls the real setupZones().
inline void initToolbarNoGfx(CryOmni3D::Atlantis::Atlantis_Toolbar &tb, CryOmni3D::Inventory *inv) {
	tb._inventory = inv;
	tb._engine = nullptr;
	tb._destSurface.create(640, 82, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	tb.setupZones();
}

} // End of namespace GraphicsToolbarTest

class GraphicsToolbarTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void runScenario(const char *name, const uint (*ops)[2], uint nops, const char *origOrder) {
		using namespace GraphicsToolbarTest;
		Common::Array<uint> ids;
		for (uint i = 0; i < nops; i++) {
			bool seen = false;
			for (uint k = 0; k < ids.size(); k++)
				seen |= ids[k] == ops[i][0];
			if (!seen)
				ids.push_back(ops[i][0]);
		}
		CryOmni3D::Objects objects;
		portSetupObjects(objects, ids);
		CryOmni3D::Inventory inv;
		NullCb nc;
		inv.init(20, new Common::Functor1Mem<uint, void, NullCb>(&nc, &NullCb::cb));
		for (uint i = 0; i < nops; i++)
			portInvent(objects, inv, ops[i][0], ops[i][1]);
		Common::String got = order(inv);
		if (got != origOrder)
			TS_WARN(Common::String::format("inventory %s: port [%s] original [%s]", name, got.c_str(), origOrder).c_str());
		TS_ASSERT_EQUALS(got, Common::String(origOrder));
	}

	// Original record lists (graphics_inventory.py, started from an empty list).
	void test_inventory_order_after_remove() {
		static const uint ops[][2] = {{300, 1}, {301, 1}, {302, 1}, {301, 0}, {304, 1}};
		runScenario("order_after_remove", ops, ARRAYSIZE(ops), "300 302 304");
	}
	void test_inventory_duplicate_add() {
		static const uint ops[][2] = {{300, 1}, {300, 1}};
		runScenario("duplicate_add", ops, ARRAYSIZE(ops), "300 300");
	}
	void test_inventory_remove_one_of_duplicates() {
		static const uint ops[][2] = {{300, 1}, {301, 1}, {300, 1}, {300, 0}};
		runScenario("remove_one_of_duplicates", ops, ARRAYSIZE(ops), "301 300");
	}

	// Start-up inventory: FUN_0042c054 (called at 0x00419605) leaves object
	// 257 (OBJETS1 sprite 0) in record 0; no CON script gives or removes 257.
	// The port's new game calls inventoryReset (place.cpp, chapter 1).
	void test_inventory_startup_record() {
		static const char *const kOriginalStartup = "257";
		Common::Array<uint> ids;
		ids.push_back(257);
		ids.push_back(258);
		CryOmni3D::Objects objects;
		GraphicsToolbarTest::portSetupObjects(objects, ids);
		CryOmni3D::Inventory inv;
		GraphicsToolbarTest::NullCb nc;
		inv.init(20, new Common::Functor1Mem<uint, void, GraphicsToolbarTest::NullCb>(&nc, &GraphicsToolbarTest::NullCb::cb));
		GraphicsToolbarTest::portInvent(objects, inv, 258, 1);   // a previous game
		CryOmni3D::Atlantis::inventoryReset(inv, objects);
		TS_ASSERT_EQUALS(GraphicsToolbarTest::order(inv), Common::String(kOriginalStartup));
		TS_ASSERT(inv[0] && inv[0]->idCA() == 0u);   // record 0 = {sprite 0, 257}
	}

	// Object id -> OBJETS1 sprite index: original FUN_0042b328 over the table
	// at 0x004967b0 up to -1 (golden graphics_inventory.txt) versus the port's
	// table reader, catalogue and lookup (inventory.cpp).
	void test_inventory_icon_table() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_toolbar: no atlantis_test.cfg, data test skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("graphics_inventory.txt", golden));
		Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("atlantis.exe"));
		Common::File exe;
		TS_ASSERT(exe.open(node));
		if (!exe.isOpen())
			return;
		Common::Array<uint> ids;
		exe.seek((0x004967b0 - 0x400000 - 0x90000) + 0x71200);
		CryOmni3D::Atlantis::readInventoryIconTable(exe, ids);
		CryOmni3D::Objects objects;
		GraphicsToolbarTest::portSetupObjects(objects, ids);
		uint missing = 0, wrong = 0;
		for (AtlantisTest::Golden::const_iterator it = golden.begin(); it != golden.end(); ++it) {
			uint id = atoi(it->_key.c_str());
			const CryOmni3D::Object *obj = CryOmni3D::Atlantis::findInventoryObject(objects, id);
			int portIdx = obj ? (int)obj->idCA() : -1;
			int origIdx = atoi(it->_value.c_str());
			if (portIdx < 0) {
				missing++;
				TS_WARN(Common::String::format("object %u (original sprite %d) is not in the port's table", id, origIdx).c_str());
			} else if (portIdx != origIdx) {
				wrong++;
				TS_WARN(Common::String::format("object %u: port sprite %d, original %d", id, portIdx, origIdx).c_str());
			}
		}
		TS_WARN(Common::String::format("inventory table: port %u ids, original %u ids; %u missing, %u wrong",
		                               ids.size(), golden.size(), missing, wrong).c_str());
		TS_ASSERT_EQUALS(missing, 0u);
		TS_ASSERT_EQUALS(wrong, 0u);
	}

	// Guests: the list after AddGuest / SubGuest sequences as the ORIGINAL
	// FUN_0042b480 / FUN_0042b6c0 leave it (atlantis-re
	// tools/emu/scripts_guests.py, drawing stubbed), "id:sprite" per record.
	// Port: AtlantisGuests (inventory.cpp) with the exe table 0x00496884.
	void test_guest_list() {
		static const int32 kTable[] = { 562, 561, 587, 597, 543, 681, 682, 592, 523 };
		Common::Array<int32> table(kTable, ARRAYSIZE(kTable));
		static const struct { const char *ops; const char *list; } kOrig[] = {
			{ "+587", "587:2" },
			{ "+587 +562", "587:2 562:0" },
			{ "+587 +562 +523", "587:2 562:0 523:8" },
			{ "+587 +562 +523 -562", "587:2 523:8" },
			{ "+587 -587", "" },
			{ "+587 +587", "587:2 587:2" },
			{ "+587 +587 -587", "587:2" },
			{ "+543 -600", "543:4" },
			{ "+597 +999", "597:3 999:0" },
		};
		for (uint i = 0; i < ARRAYSIZE(kOrig); i++) {
			CryOmni3D::Atlantis::AtlantisGuests g;
			for (const char *p = kOrig[i].ops; *p; ) {
				char op = *p++;
				int32 id = atoi(p);
				while (*p && *p != ' ') p++;
				while (*p == ' ') p++;
				if (op == '+')
					g.add(id, table);
				else
					g.remove(id);
			}
			Common::String got;
			for (uint k = 0; k < g.count; k++) {
				if (!got.empty())
					got += " ";
				got += Common::String::format("%d:%u", g.id[k], g.sprite[k]);
			}
			TSM_ASSERT_EQUALS(kOrig[i].ops, got, Common::String(kOrig[i].list));
		}
	}

	// Guest portrait hit-test: the id the ORIGINAL FUN_0042bdb8 returns
	// (atlantis-re tools/emu/scripts_guest_hit.py), for 1, 2 and 3 guests.
	// The two points where the original reads before the list (x = x0 +
	// count * 85, y = 450) are left out.  Port: AtlantisGuests::at.
	void test_guest_hit() {
		static const int32 kTable[] = { 562, 561, 587, 597, 543, 681, 682, 592, 523 };
		Common::Array<int32> table(kTable, ARRAYSIZE(kTable));
		static const int32 kIds[] = { 587, 562, 523 };
		static const struct { uint n; int x, y; int32 id; } kOrig[] = {
			{ 1, 440, 350, 0 }, { 1, 440, 449, 0 }, { 1, 439, 400, 0 }, { 1, 539, 400, 0 },
			{ 1, 540, 400, 587 }, { 1, 610, 400, 587 }, { 1, 624, 400, 587 }, { 1, 626, 400, 0 },
			{ 1, 500, 349, 0 },
			{ 2, 440, 350, 562 }, { 2, 440, 449, 562 }, { 2, 439, 400, 0 }, { 2, 525, 400, 587 },
			{ 2, 539, 400, 587 }, { 2, 540, 400, 587 }, { 2, 609, 400, 587 }, { 2, 611, 400, 0 },
			{ 2, 340, 400, 0 }, { 2, 500, 349, 0 },
			{ 3, 440, 350, 562 }, { 3, 440, 449, 562 }, { 3, 439, 400, 562 }, { 3, 525, 400, 587 },
			{ 3, 540, 400, 587 }, { 3, 610, 400, 0 }, { 3, 340, 400, 523 }, { 3, 339, 400, 0 },
			{ 3, 239, 400, 0 }, { 3, 500, 349, 0 },
		};
		for (uint i = 0; i < ARRAYSIZE(kOrig); i++) {
			CryOmni3D::Atlantis::AtlantisGuests g;
			for (uint k = 0; k < kOrig[i].n; k++)
				g.add(kIds[k], table);
			TSM_ASSERT_EQUALS(Common::String::format("%u guests at (%d,%d)", kOrig[i].n, kOrig[i].x, kOrig[i].y).c_str(),
			                  g.at(kOrig[i].x, kOrig[i].y), kOrig[i].id);
		}
	}

	// Icon hotspots: original (i*70+42, 443); port = centre of the toolbar's
	// zone rect (rects from the faithful copy of init()), origin y = 480-82.
	void test_icon_hotspots() {
		CryOmni3D::Atlantis::Atlantis_Toolbar tb;
		GraphicsToolbarTest::initToolbarNoGfx(tb, nullptr);
		TS_ASSERT_EQUALS(tb._zones.size(), 10u);   // 9 slots + the column at x >= 630
		for (uint i = 0; i < 9; i++) {
			const Common::Rect &r = tb._zones[i].rect;
			TS_ASSERT_EQUALS((r.left + r.right) / 2, (int)(i * 70 + 42));
			TS_ASSERT_EQUALS((r.top + r.bottom) / 2 + (480 - 82), 443);
		}
	}

	// Bar background: original OBJETS0 sprite 0 drawn with hotspot (0, 443),
	// i.e. top = 443 - yoff; port: addMenuSprite(tx=0, ty=yoff) and drawToolbar
	// baseY = (ty - yoff) + _position = 0 -> screen 398 when open.
	void test_bar_background_position() {
		if (!AtlantisTest::haveData()) {
			TS_WARN("graphics_toolbar: no atlantis_test.cfg, data test skipped");
			return;
		}
		// OBJETS0.SPR header from the archive (port-independent reader).
		Common::SeekableReadStream *s = nullptr;
		{
			for (int cd = 1; cd <= 4 && !s; cd++) {
				Common::FSNode n(Common::Path(AtlantisTest::dataDir(), '/')
				                 .appendComponent(Common::String::format("BIGCD%d.BIG", cd)));
				Common::File f;
				if (!f.open(n))
					continue;
				f.seek(16);
				uint32 cnt = f.readUint32LE();
				f.seek(24);
				uint32 base = f.readUint32LE();
				f.seek(32);
				uint32 seq = 0;
				for (uint32 i = 0; i < cnt; i++) {
					uint32 rec[8];
					for (int k = 0; k < 8; k++)
						rec[k] = f.readUint32LE();
					Common::String name;
					for (uint32 k = 0; k < rec[0]; k++) {
						char c = (char)f.readByte();
						if (c)
							name += c;
					}
					name.toUppercase();
					if (name == "SPRITE\\2D\\OBJETS0.SPR") {
						int32 here = f.pos();
						f.seek(base + seq + 2 + 512 + 2 + 4);
						byte *b = (byte *)malloc(8);
						f.read(b, 8);
						s = new Common::MemoryReadStream(b, 8, DisposeAfterUse::YES);
						f.seek(here);
						break;
					}
					seq += rec[2];
				}
			}
		}
		TS_ASSERT(s);
		if (!s)
			return;
		/* w = */ s->readUint16LE();
		/* h = */ s->readUint16LE();
		int16 xoff = s->readSint16LE();
		int16 yoff = s->readSint16LE();
		delete s;
		typedef CryOmni3D::Atlantis::Atlantis_Toolbar T;
		int origLeft = 0 - xoff, origTop = 443 - yoff;
		// engine.cpp adds the sprite at (0, kBarHotspotY - kBarSurfaceTop);
		// drawToolbar places it with menuSpriteTop (bar open: position 0).
		int portLeft = 0 - xoff;
		int portTop = T::kBarSurfaceTop + T::menuSpriteTop(T::kBarHotspotY - T::kBarSurfaceTop, yoff, 0);
		TS_ASSERT_EQUALS(portLeft, origLeft);
		TS_ASSERT_EQUALS(portTop, origTop);
	}

	// Click hit-test over the whole bar.  Original FUN_0042c098 model:
	// record = x/70 + first when x/70 + 1 <= end and y >= 409.  Port: real
	// hitTestZones on toolbar-local coordinates, record = zone + offset.
	void test_click_hit_test() {
		CryOmni3D::Atlantis::Atlantis_Toolbar tb;
		GraphicsToolbarTest::initToolbarNoGfx(tb, nullptr);
		static const int firsts[] = {0, 2};
		uint diffs = 0, firstDiffX = 0;
		for (uint fi = 0; fi < ARRAYSIZE(firsts); fi++) {
			int first = firsts[fi], end = first + 9;
			for (int y = 380; y < 480; y++)
				for (int x = 0; x < 640; x++) {
					int orig = (x / 70 + 1 <= end && y >= 409) ? x / 70 + first : -1;
					tb._inventoryOffset = first;
					Common::Array<CryOmni3D::Atlantis::Atlantis_Toolbar::Zone>::const_iterator it =
					    tb.hitTestZones(Common::Point(x, y - (480 - 82)));
					int port = (it == tb._zones.end()) ? -1 : (int)(it - tb._zones.begin()) + first;
					if (orig != port) {
						if (!diffs)
							firstDiffX = x;
						diffs++;
					}
				}
		}
		if (diffs)
			TS_WARN(Common::String::format("toolbar click: %u points differ (first at x=%u)", diffs, firstDiffX).c_str());
		TS_ASSERT_EQUALS(diffs, 0u);
	}

	// Taking an item: the original click takes the record's object
	// (FUN_0042c098 writes it to the held-object slot 0x611210[...]).  The
	// port's callbackInventory refuses objects whose valid() is false, so
	// setupObjects must build valid objects.
	void test_take_item() {
		using namespace GraphicsToolbarTest;
		Common::Array<uint> ids;
		ids.push_back(300);
		CryOmni3D::Objects objects;
		portSetupObjects(objects, ids);
		CryOmni3D::Inventory inv;
		NullCb nc;
		inv.init(20, new Common::Functor1Mem<uint, void, NullCb>(&nc, &NullCb::cb));
		inv.add(&objects[0]);
		CryOmni3D::Atlantis::Atlantis_Toolbar tb;
		initToolbarNoGfx(tb, &inv);
		// Pressed on slot 0: no engine call on this path.
		TS_ASSERT_EQUALS(tb.callbackInventory(0, CryOmni3D::kDragStatus_Pressed), 1u);
	}
};
