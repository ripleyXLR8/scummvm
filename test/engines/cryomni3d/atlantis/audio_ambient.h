// Verifier "audio": the places' ambient sounds (task 20).
//
// Original: atlantis-cd-en-714240 FUN_00432178 / FUN_00450ed8 (per frame,
// emulated unmodified by atlantis-re/tools/emu/audio_ambient.py, golden
// audio_ambient_gain.txt) and FUN_00431a1c / FUN_004320bc / FUN_00431e98
// (read in the disassembly: functions/audio-FUN_00431a1c.md,
// audio-FUN_00432178.md; the state tests below restate those rules).
//
// Port: AmbientSounds and ambientGains (ambient_sounds.h), called directly.

#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/file.h"
#include "common/fs.h"

#include "cryomni3d/atlantis/ambient_output.h"
#include "cryomni3d/atlantis/ambient_sounds.h"
#include "cryomni3d/atlantis/miles_samples.h"

namespace AudioAmbient {

using namespace CryOmni3D::Atlantis;

// A recording sample output with a settable status per slot.
class FakeOut : public AmbientSampleOutput {
public:
	int st[AmbientSounds::kSlots];
	Common::Array<Common::String> log;
	FakeOut() { for (uint i = 0; i < AmbientSounds::kSlots; i++) st[i] = kStopped; }
	int status(uint slot) override { return st[slot]; }
	void start(uint slot, uint object, uint variant, bool loopForever) override {
		log.push_back(Common::String::format("start %u %u %u %d", slot, object, variant, loopForever ? 1 : 0));
		st[slot] = kPlaying;
	}
	void stop(uint slot) override { log.push_back(Common::String::format("stop %u", slot)); }
	void setPan(uint slot, uint8 pan) override { log.push_back(Common::String::format("pan %u %u", slot, pan)); }
	void setVolume(uint slot, uint8 v) override { log.push_back(Common::String::format("vol %u %u", slot, v)); }
	void release(uint object) override { log.push_back(Common::String::format("release %u", object)); }
	bool has(const Common::String &s) const {
		for (uint i = 0; i < log.size(); i++)
			if (log[i] == s)
				return true;
		return false;
	}
};

struct Clock {
	Common::Array<uint32> values;
	uint pos = 0;
	static uint32 fn(void *ctx) {
		Clock *c = (Clock *)ctx;
		return c->pos < c->values.size() ? c->values[c->pos++] : 0;
	}
};

static AtlantisAmbientObject object(uint8 id, uint8 w0, uint8 w1, uint8 w2, uint8 w3, int variants) {
	AtlantisAmbientObject o;
	o.id = id;
	o.pos[0] = 100.0f; o.pos[1] = 0.0f; o.pos[2] = 0.0f;
	o.loudness = 500.0f;
	o.weights[0] = w0; o.weights[1] = w1; o.weights[2] = w2; o.weights[3] = w3;
	for (int k = 0; k < variants; k++)
		o.variants[k] = Common::String::format("v%d.apc", k);
	return o;
}

static AtlantisPlace place(uint16 id, const uint8 *ids, uint n) {
	AtlantisPlace p;
	p.placeId = id;
	for (uint i = 0; i < n; i++) {
		AtlantisAmbientRef r;
		r.objectId = ids[i];
		r.t = 0.0f; r.m = 200.0f; r.c = 0.0f;
		p.ambientRefs.push_back(r);
	}
	return p;
}

// A sample output with the Miles statuses (miles_samples.h); `active` is
// whether the slot's sound is still heard (the mixer's handle).
class MilesOut : public AmbientSampleOutput {
public:
	MilesSampleStatus miles;
	bool active[AmbientSounds::kSlots];
	MilesOut() { for (uint i = 0; i < AmbientSounds::kSlots; i++) active[i] = false; }
	int status(uint slot) override { return miles.status(slot, active[slot]); }
	void start(uint slot, uint, uint, bool) override { miles.started(slot); active[slot] = true; }
	void stop(uint slot) override { miles.stopped(slot, active[slot]); active[slot] = false; }
	void setPan(uint, uint8) override {}
	void setVolume(uint, uint8) override {}
	void release(uint) override {}
	void statuses(int *st) { for (uint i = 0; i < AmbientSounds::kSlots; i++) st[i] = status(i); }
};

} // End of namespace AudioAmbient

class AudioAmbientTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// FUN_00450ed8 + FUN_00432178's pan / volume against the emulated original
	// (3006 cases, ear angle 0.65).
	void test_gains_vs_original() {
		using namespace AudioAmbient;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("golden directory not configured: ambient gains test skipped");
			return;
		}
		Common::File f;
		if (!f.open(Common::FSNode(Common::Path(AtlantisTest::goldenDir(), '/').appendComponent("audio_ambient_gain.txt")))) {
			TS_WARN("audio_ambient_gain.txt missing: ambient gains test skipped");
			return;
		}
		uint cases = 0, bad = 0;
		while (!f.eos()) {
			Common::String line = f.readLine();
			if (line.empty() || line[0] == '#')
				continue;
			// "((x, y, z), (cx, cy, cz), (t, m, c), gain, alpha, beta) a b c26 pan vol"
			Common::Array<double> v;
			Common::String num;
			for (uint k = 0; k <= line.size(); k++) {
				const char ch = k < line.size() ? line[k] : ' ';
				if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == 'e' || ch == '+') {
					num += ch;
				} else {
					if (!num.empty())
						v.push_back(atof(num.c_str()));
					num.clear();
				}
			}
			if (v.size() != 17)
				continue;
			cases++;
			const float pos[3] = { (float)v[0], (float)v[1], (float)v[2] };
			AtlantisCentrePos cam = { (float)v[3], (float)v[4], (float)v[5] };
			AtlantisAmbientRef ref;
			ref.objectId = 1;
			ref.t = (float)v[6]; ref.m = (float)v[7]; ref.c = (float)v[8];
			const AmbientGains g = ambientGains(pos, (float)v[9], cam, ref, (float)v[10], (float)v[11], 0.65f);
			if (g.right != (int32)v[12] || g.left != (int32)v[13] || g.out26 != (int32)v[14] ||
			        g.pan != (uint8)v[15] || g.volume != (uint8)v[16]) {
				if (bad < 5)
					TS_WARN(("ambient gains differ: " + line + Common::String::format(" / port %d %d %d %u %u",
					         g.right, g.left, g.out26, g.pan, g.volume)).c_str());
				bad++;
			}
		}
		TS_ASSERT_EQUALS(cases, 3006u);
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// FUN_004320bc: draw = clock & 0x7f, redrawn above 100; the first variant
	// whose running sum exceeds it; -1 past the last; a weight above 100
	// counts as weight - 100 and sets the loop flag when scanned.
	void test_pick_variant() {
		using namespace AudioAmbient;
		AmbientSounds a;
		Clock c;
		a.setClock(&Clock::fn, &c);
		const AtlantisAmbientObject o = object(1, 25, 25, 25, 25, 4);
		c.values = { 0, 24, 25, 99, 100, 128 + 50, 101, 127, 30 };
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 0);    // 0
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 0);    // 24 < 25
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 1);    // 25
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 3);    // 99
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), -1);   // 100: no sum exceeds it
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 2);    // & 0x7f -> 50
		TS_ASSERT_EQUALS(a.pickVariant(o, 4), 1);    // 101, 127 redrawn, then 30
		TS_ASSERT(!a._loopFlag);
		const AtlantisAmbientObject loop = object(1, 199, 0, 0, 0, 1);
		c.values = { 100 };
		c.pos = 0;
		TS_ASSERT_EQUALS(a.pickVariant(loop, 1), -1);   // 99 is not above 100
		TS_ASSERT(a._loopFlag);                          // set even for a pause
	}

	// FUN_00431a1c / FUN_00431f9c: records by ref index, the delay, the
	// pointer-identity "already loaded" test, the remove pass shifting the
	// records but not the handles; FUN_00431e98.
	void test_enter_place_and_stop() {
		using namespace AudioAmbient;
		AmbientSounds a;
		Clock c;
		a.setClock(&Clock::fn, &c);
		FakeOut out;
		Common::Array<AtlantisAmbientObject> objs;
		objs.push_back(object(1, 25, 25, 25, 25, 4));   // delay (100 - 100) / 4 = 0
		objs.push_back(object(2, 20, 20, 20, 20, 4));   // delay 20 / 4 = 5
		const uint8 both[] = { 1, 2 };
		TS_ASSERT(a.enterPlace(out, place(132, both, 2), objs));
		TS_ASSERT_EQUALS(a.count(), 2u);
		TS_ASSERT(a.active());
		TS_ASSERT_EQUALS(a._records[0].object, 0);
		TS_ASSERT_EQUALS(a._records[1].object, 1);
		TS_ASSERT_EQUALS(a._records[0].delay, 0u);
		TS_ASSERT_EQUALS(a._records[1].delay, 5u);
		TS_ASSERT_EQUALS(a._records[1].count, 4);
		// the same objects in the other order: nothing reloaded, records kept
		const uint8 swapped[] = { 2, 1 };
		out.log.clear();
		TS_ASSERT(a.enterPlace(out, place(133, swapped, 2), objs));
		TS_ASSERT_EQUALS(out.log.size(), 0u);
		TS_ASSERT_EQUALS(a._records[0].object, 0);
		// only object 2 left: record 0 removed, record 1 shifted to 0
		const uint8 two[] = { 2 };
		out.log.clear();
		TS_ASSERT(a.enterPlace(out, place(134, two, 1), objs));
		TS_ASSERT_EQUALS(a.count(), 1u);
		TS_ASSERT_EQUALS(a._records[0].object, 1);
		TS_ASSERT(out.has("release 0"));
		TS_ASSERT(out.has("pan 0 64"));
		// no references: FUN_00431e98
		out.log.clear();
		TS_ASSERT(a.enterPlace(out, place(1, nullptr, 0), objs));
		TS_ASSERT_EQUALS(a.count(), 0u);
		TS_ASSERT(!a.active());
		TS_ASSERT(out.has("release 1"));
	}

	// FUN_00432178: a stopped sample starts a drawn variant and gets its pan
	// and volume; a pause arms CHAPTERTIME and ends when the difference
	// exceeds the delay; the loop flag makes the next start loop forever.
	void test_frame() {
		using namespace AudioAmbient;
		AmbientSounds a;
		Clock c;
		a.setClock(&Clock::fn, &c);
		FakeOut out;
		Common::Array<AtlantisAmbientObject> objs;
		objs.push_back(object(1, 20, 20, 20, 20, 4));   // delay 5
		const uint8 one[] = { 1 };
		const AtlantisPlace p = place(135, one, 1);
		c.values = { 0 };                                // the load's discarded draw
		TS_ASSERT(a.enterPlace(out, p, objs));
		AtlantisCentrePos cam = { 0.0f, 0.0f, 0.0f };
		// draw 90: no variant (sum 80) -> a pause from CHAPTERTIME 10
		c.values = { 90 }; c.pos = 0;
		out.log.clear();
		a.frame(out, p, objs, cam, 0.0f, 0.0f, 10);
		TS_ASSERT_EQUALS(a._records[0].start, 10u);
		TS_ASSERT(!out.has("pan 0 63"));
		// CHAPTERTIME 15: 15 - 10 = 5 is not above the delay 5
		c.values = { 30 }; c.pos = 0;
		out.log.clear();
		a.frame(out, p, objs, cam, 0.0f, 0.0f, 15);
		TS_ASSERT_EQUALS(a._records[0].start, 10u);
		TS_ASSERT_EQUALS(out.log.size(), 0u);
		// CHAPTERTIME 16: redraws until a variant (90 none, then 30 -> 1)
		c.values = { 90, 30 }; c.pos = 0;
		a.frame(out, p, objs, cam, 0.0f, 0.0f, 16);
		TS_ASSERT_EQUALS(a._records[0].start, 0u);
		TS_ASSERT(out.has("start 0 0 1 0"));
		// playing: only the positional update
		out.log.clear();
		a.frame(out, p, objs, cam, 0.0f, 0.0f, 17);
		TS_ASSERT_EQUALS(out.log.size(), 2u);
		// the loop flag: a stale flag makes the next start loop forever
		a._loopFlag = true;
		out.st[0] = AmbientSampleOutput::kDone;
		c.values = { 0 }; c.pos = 0;
		out.log.clear();
		a.frame(out, p, objs, cam, 0.0f, 0.0f, 18);
		TS_ASSERT(out.has("start 0 0 0 1"));
		TS_ASSERT(!a._loopFlag);
	}

	// FUN_0043259c / FUN_00432758 (atlantis-re functions/audio-FUN_00432178.md,
	// STATIC): steps = (gain at the destination - last value) / frames;
	// each video frame adds them and sets the truncated values; a finished
	// sample (status 2 only) with more than one variant restarts, keeping a
	// finite loop count.
	void test_video_glide() {
		using namespace AudioAmbient;
		AmbientSounds a;
		Clock c;
		a.setClock(&Clock::fn, &c);
		FakeOut out;
		Common::Array<AtlantisAmbientObject> objs;
		objs.push_back(object(1, 50, 50, 0, 0, 2));
		const uint8 one[] = { 1 };
		const AtlantisPlace p = place(135, one, 1);
		c.values = { 0 };
		TS_ASSERT(a.enterPlace(out, p, objs));
		out.st[0] = AmbientSampleOutput::kPlaying;
		a._curPan[0] = 10.0f;
		a._curVol[0] = 0.0f;
		const AtlantisCentrePos dst = { 0.0f, 300.0f, 0.0f };
		const float pos[3] = { -objs[0].pos[1], objs[0].pos[0], -objs[0].pos[2] };
		const AmbientGains g = ambientGains(pos, objs[0].loudness, dst, p.ambientRefs[0], 0.5f, 0.0f);
		a.prepareGlide(p, objs, dst, 0.5f, 0.0f, 4);
		TS_ASSERT_EQUALS(a._stepPan[0], (float)(((double)g.pan - 10.0) / 4.0));
		TS_ASSERT_EQUALS(a._stepVol[0], (float)((double)g.volume / 4.0));
		for (int k = 1; k <= 4; k++) {
			out.log.clear();
			a.videoFrame(out, p, objs, 100);
			TS_ASSERT_EQUALS(out.log.size(), 2u);
		}
		TS_ASSERT(out.has(Common::String::format("pan 0 %d", (int)a._curPan[0])));
		TS_ASSERT_DELTA(a._curPan[0], (float)g.pan, 1e-4f);
		TS_ASSERT_DELTA(a._curVol[0], (float)g.volume, 1e-4f);
		// status 8 (stopped) does not restart here
		out.st[0] = AmbientSampleOutput::kStopped;
		out.log.clear();
		a.videoFrame(out, p, objs, 100);
		TS_ASSERT_EQUALS(out.log.size(), 2u);
		// status 2 restarts a drawn variant with a finite loop count
		out.st[0] = AmbientSampleOutput::kDone;
		a._loopFlag = true;
		c.values = { 60 }; c.pos = 0;
		out.log.clear();
		a.videoFrame(out, p, objs, 100);
		TS_ASSERT(out.has("start 0 0 1 0"));
		TS_ASSERT(a._loopFlag);   // FUN_00432758 neither reads nor clears it
	}

	// MSS32.DLL DirectSound pan (0x2000dd50, helper 0x2000dcd0; atlantis-re
	// experiments/2026-10-15-61-mss32-status-pan.md): SetPan of
	// trunc(2000 log10(p / 63)) (p < 63), its mirror with 127 - p (p > 64),
	// 0 for 63 / 64, -10000 for 0.  Expected balances: 127 x the far
	// channel's amplitude 10^(cB / 2000), to the nearest step, computed
	// outside the port (Python, math.trunc / log10).
	// The six Miles samples shared by the ambient sounds and the puzzles'
	// players, against experiments/2026-10-15-71 (DYN, Wine) and the
	// players' disassembly (FUN_0043ca28, FUN_0043cd60, FUN_0043ce80).
	void test_miles_sample_pools() {
		using namespace AudioAmbient;
		typedef MilesSampleStatus M;
		// Statuses: 2 at start-up; 4 while playing, 2 once done; a stop
		// makes a playing sample 8 and leaves 2 or 8 as they are.
		MilesOut out;
		int st[AmbientSounds::kSlots];
		out.statuses(st);
		for (uint i = 0; i < AmbientSounds::kSlots; i++)
			TS_ASSERT_EQUALS(st[i], (int)M::kDone);
		out.start(0, 0, 0, false);
		TS_ASSERT_EQUALS(out.status(0), (int)M::kPlaying);
		out.active[0] = false;                  // the sound ended
		TS_ASSERT_EQUALS(out.status(0), (int)M::kDone);
		out.stop(0);
		TS_ASSERT_EQUALS(out.status(0), (int)M::kDone);
		out.start(0, 0, 0, false);
		out.stop(0);
		TS_ASSERT_EQUALS(out.status(0), (int)M::kStopped);
		out.stop(0);
		TS_ASSERT_EQUALS(out.status(0), (int)M::kStopped);
		out.start(0, 0, 0, false);              // only a start changes 8
		TS_ASSERT_EQUALS(out.status(0), (int)M::kPlaying);

		// Muria from MURIA.WAM place 1 (two ambient sounds on samples 0, 1),
		// run 175609: both playing at the puzzle entry, FUN_00431e98 leaves
		// them at 8 and every short effect is dropped.
		AmbientSounds a;
		Clock c;
		a.setClock(&Clock::fn, &c);
		MilesOut amb;
		Common::Array<AtlantisAmbientObject> objs;
		objs.push_back(object(1, 25, 25, 25, 25, 4));
		objs.push_back(object(2, 25, 25, 25, 25, 4));
		const uint8 both[] = { 1, 2 };
		TS_ASSERT(a.enterPlace(amb, place(1, both, 2), objs));
		amb.start(0, 0, 0, false);
		amb.start(1, 1, 0, false);
		a.stopAll(amb);
		amb.statuses(st);
		TS_ASSERT_EQUALS(st[0], (int)M::kStopped);
		TS_ASSERT_EQUALS(st[1], (int)M::kStopped);
		TS_ASSERT_EQUALS(M::slotCa28(st, 6464), -1);    // MURIA2
		TS_ASSERT_EQUALS(M::slotCa28(st, 12036), -1);   // MURIA5
		// Run 175227, second puzzle: only sample 0 was playing; sample 1 is used.
		TS_ASSERT(a.enterPlace(amb, place(1, both, 2), objs));
		amb.start(0, 0, 0, false);
		amb.start(1, 1, 0, false);
		amb.active[1] = false;                  // sample 1 had finished
		a.stopAll(amb);
		amb.statuses(st);
		TS_ASSERT_EQUALS(st[0], (int)M::kStopped);
		TS_ASSERT_EQUALS(st[1], (int)M::kDone);
		TS_ASSERT_EQUALS(M::slotCa28(st, 6464), 1);
		// The ambient player restarts 8 at the place (FUN_00432178: 2 or 8).
		TS_ASSERT(a.enterPlace(amb, place(1, both, 2), objs));
		a.frame(amb, place(1, both, 2), objs, AtlantisCentrePos(), 0.0f, 0.0f, 0);
		TS_ASSERT_EQUALS(amb.status(0), (int)M::kPlaying);
		TS_ASSERT_EQUALS(amb.status(1), (int)M::kPlaying);

		// The hens at place 52 (no ambient sound), run 173556: POULE2 still
		// playing on sample 2 at the exit, FUN_0043d0e0 makes it 8, and the
		// next run's long sounds (POULE1 22230, POULE2 41150 bytes) are dropped.
		MilesOut hens;
		hens.statuses(st);
		TS_ASSERT_EQUALS(M::slotCa28(st, 22230), 2);
		hens.start(2, 0, 0, false);
		for (uint i = 0; i < 4; i++)            // FUN_0043d0e0
			hens.stop(i);
		hens.statuses(st);
		TS_ASSERT_EQUALS(st[2], (int)M::kStopped);
		TS_ASSERT_EQUALS(st[0], (int)M::kDone);
		TS_ASSERT_EQUALS(M::slotCa28(st, 22230), -1);
		TS_ASSERT_EQUALS(M::slotCa28(st, 41150), -1);
		TS_ASSERT_EQUALS(M::slotCa28(st, 19999), 0);   // under 0x4e20: samples 0 / 1

		// FUN_0043cd60: refuses over 120000 bytes, else samples 0..2 in status 2.
		const int s822[6] = { 8, 2, 2, 2, 2, 2 };
		TS_ASSERT_EQUALS(M::slotCd60(s822, 120001), -1);
		TS_ASSERT_EQUALS(M::slotCd60(s822, 120000), 1);
		const int s884[6] = { 8, 8, 4, 2, 2, 2 };
		TS_ASSERT_EQUALS(M::slotCd60(s884, 100), -1);
		// FUN_0043ce80: sample 0 when 2 or 8, never another.
		TS_ASSERT_EQUALS(M::slotCe80(s884, 100), 0);
		const int s42[6] = { 4, 2, 2, 2, 2, 2 };
		TS_ASSERT_EQUALS(M::slotCe80(s42, 100), -1);
		TS_ASSERT_EQUALS(M::slotCa28(s42, 100), 1);
		TS_ASSERT_EQUALS(M::slotCa28(s42, 0), -1);     // an empty record
	}

	void test_miles_pan_balance() {
		static const int8 kWant[128] = {
			-127, -125, -123, -121, -119, -117, -115, -113, -111, -109, -107, -105, -103, -101, -99, -97,
			-95, -93, -91, -89, -87, -85, -83, -81, -79, -77, -75, -73, -71, -68, -66, -64,
			-62, -60, -58, -56, -54, -52, -50, -48, -46, -44, -42, -40, -38, -36, -34, -32,
			-30, -28, -26, -24, -22, -20, -18, -16, -14, -12, -10, -8, -6, -4, -2, 0,
			0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
			32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
			64, 66, 68, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91, 93, 95,
			97, 99, 101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125, 127
		};
		for (int p = 0; p < 128; p++)
			TS_ASSERT_EQUALS((int)CryOmni3D::Atlantis::milesPanBalance((uint8)p), (int)kWant[p]);
		// Pan 64 is the centre in Miles (both channels full); the old
		// 2p - 127 mapping gave balance 1 there.
		TS_ASSERT_EQUALS((int)CryOmni3D::Atlantis::milesPanBalance(64), 0);
	}
};
