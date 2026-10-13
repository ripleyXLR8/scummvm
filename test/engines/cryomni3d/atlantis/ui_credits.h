#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/ptr.h"

#include "cryomni3d/atlantis/bigfile.h"
#include "cryomni3d/atlantis/credits_layout.h"

// ui audit — the credits.
//
// Expected values come from the ORIGINAL FUN_0041a72c of
// atlantis-cd-en-714240 run unmodified under Unicorn on the shipped
// SPRLIST\CREDITS.TXT (glyph draws, pictures, presents and their ticks):
// atlantis-re/tools/emu/ui_credits.py -> evidence/golden/ui_credits.txt.
//
// Port code exercised directly: CreditsLayout (credits_layout.h), which
// CryOmni3DEngine_Atlantis::playQuitOutro plays.

class UiCreditsTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using CryOmni3D::Atlantis::CreditsEvent;
		using CryOmni3D::Atlantis::CreditsLayout;
		if (!AtlantisTest::haveData()) {
			TS_WARN("ui_credits: no game data configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_credits.txt", golden));

		// The width table 0x004966fc, read from the executable.
		Common::File exe;
		TS_ASSERT(exe.open(Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/').appendComponent("atlantis.exe"))));
		byte widths[256];
		exe.seek(0x004966fc - 0x400000 - 0x90000 + 0x71200);
		exe.read(widths, 256);

		SearchMan.addDirectory("ui_credits_data", Common::FSNode(Common::Path(AtlantisTest::dataDir(), '/')));
		CryOmni3D::Atlantis::BigFileArchive big;
		for (int cd = 1; cd <= 4; cd++)
			big.open(Common::Path(Common::String::format("BIGCD%d.BIG", cd)));
		SearchMan.remove("ui_credits_data");
		Common::ScopedPtr<Common::SeekableReadStream> s(big.createReadStreamForMember(Common::String("SPRLIST\\CREDITS.TXT")));
		TS_ASSERT(s);
		if (!s)
			return;
		Common::Array<byte> txt((uint)s->size());
		s->read(txt.data(), txt.size());

		Common::Array<CreditsEvent> ev;
		CreditsLayout::build(txt.data(), txt.size(), widths, ev);
		Common::Array<Common::String> out;
		out.push_back("returned 1");
		out.push_back(Common::String::format("events %u", ev.size()));
		for (uint i = 0; i < ev.size(); i++) {
			const CreditsEvent &e = ev[i];
			switch (e.kind) {
			case CreditsEvent::kImage: out.push_back(Common::String::format("image %d", e.image)); break;
			case CreditsEvent::kGlyph:
				out.push_back(Common::String::format("glyph %c %d %d %d", e.blue ? 'B' : 'W', e.x, e.y, e.frame));
				break;
			case CreditsEvent::kPresent: out.push_back(Common::String::format("present at tick %u", e.tick)); break;
			case CreditsEvent::kClear: out.push_back(Common::String::format("clear at tick %u", e.tick)); break;
			}
		}
		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		uint bad = 0;
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			if (out[i] != want[i] && bad++ < 5)
				TS_ASSERT_EQUALS(out[i], want[i]);
		TS_ASSERT_EQUALS(bad, 0u);
	}
};
