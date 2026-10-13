#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/algorithm.h"
#include "common/array.h"
#include "common/md5.h"
#include "common/stream.h"

#include "cryomni3d/atlantis/hens_rules.h"

// ui audit, task 18 — puzzle 1 (StartPuzzle=1, CHAPI034): the hens on the levers.
//
// Expected values come from the ORIGINAL click handler FUN_00444a4c of
// atlantis-cd-en-714240, with the drop check FUN_004457bc, the weighing
// FUN_00444d80 and the lever propagation FUN_00445614, run unmodified under
// Unicorn (drawing stubbed):
// atlantis-re/tools/emu/ui_puzzle1_hens.py -> evidence/golden/ui_puzzle1_hens.txt.
//
// Port code exercised directly: HensRules (hens_rules.h), the rules
// HensPuzzle runs.

namespace UiHens {

typedef CryOmni3D::Atlantis::HensRules Rules;

// The harness's state: the hen carried, then the targets of hens 2..8.
inline Common::String stateRepr(const Rules &r) {
	Common::String s = Common::String::format("%d", r.carried);
	for (int n = 2; n < 9; n++)
		s += Common::String::format(" %d", r.hens[n].target);
	return s;
}

inline uint32 stateKey(const Rules &r) {
	uint32 k = (uint32)r.carried;
	for (int n = 2; n < 9; n++)
		k = k * 16 + (uint32)r.hens[n].target;
	return k;
}

inline Rules fromKey(uint32 k) {
	Rules r;
	for (int n = 8; n >= 2; n--) {
		r.hens[n].target = (int16)(k & 15);
		k >>= 4;
	}
	r.carried = (int16)k;
	return r;
}

// A set of state keys (Common::HashMap's pool does not take 700,000 entries).
class KeySet {
public:
	KeySet() { _slots.resize(1 << 21); for (uint i = 0; i < _slots.size(); i++) _slots[i] = 0xffffffff; }
	// True when the key was not in the set.
	bool insert(uint32 k) {
		uint32 i = (k * 2654435761u) >> 11;
		while (_slots[i] != 0xffffffff) {
			if (_slots[i] == k)
				return false;
			i = (i + 1) & (_slots.size() - 1);
		}
		_slots[i] = k;
		return true;
	}

private:
	Common::Array<uint32> _slots;
};

struct Keyed {
	Common::String repr;
	uint32 key;
	bool operator<(const Keyed &o) const { return repr < o.repr; }
};

struct Action {
	Common::String name;
	int x, y;
};

inline const char *soundName(Rules::Sound s) {
	static const char *const kNames[] = { "", "pick", "home", "drop", "none", "win", "lose" };
	return kNames[s];
}

inline Common::String joinLines(const Common::Array<Common::String> &lines) {
	Common::String joined;
	for (uint i = 0; i < lines.size(); i++) {
		if (i)
			joined += '\n';
		joined += lines[i];
	}
	return joined;
}

// One click from a state, as the harness prints it; every click starts
// from the start records with only the state changed.
inline Common::String transition(uint32 key, const Action &a, Rules &r, Rules::Sound snd[2]) {
	r = fromKey(key);
	const Common::String from = stateRepr(r);
	const int ret = r.click(a.x, a.y, snd);
	Common::String sounds = soundName(snd[0]);
	if (snd[1] != Rules::kSoundNone)
		sounds += Common::String(",") + soundName(snd[1]);
	return from + " " + a.name + " -> " + stateRepr(r) +
	       Common::String::format(" %d %d %d %d ", ret, r.leave ? 1 : 0, r.won, r.marked) + sounds;
}

// The harness's sorted listing, '\n'-joined, produced state by state for
// the MD5 (17 million lines do not fit in memory as strings).  A state's
// repr is never followed by a digit in its lines, so sorting the states
// by repr and then each state's lines sorts the whole listing.
class ListingStream : public Common::ReadStream {
public:
	ListingStream(const Common::Array<uint32> &keys, const Common::Array<Action> &actions)
		: _keys(keys), _actions(actions), _next(0), _pos(0), _first(true) {}
	bool eos() const override { return _pos >= _buf.size() && _next >= _keys.size(); }
	uint32 read(void *dataPtr, uint32 dataSize) override {
		byte *out = (byte *)dataPtr;
		uint32 done = 0;
		while (done < dataSize) {
			if (_pos >= _buf.size()) {
				if (_next >= _keys.size())
					break;
				fill(_keys[_next++]);
			}
			const uint32 n = MIN<uint32>(dataSize - done, _buf.size() - _pos);
			memcpy(out + done, _buf.c_str() + _pos, n);
			done += n;
			_pos += n;
		}
		return done;
	}

private:
	const Common::Array<uint32> &_keys;
	const Common::Array<Action> &_actions;
	uint _next;
	uint32 _pos;
	bool _first;
	Common::String _buf;

	void fill(uint32 key) {
		Common::Array<Common::String> lines;
		for (uint i = 0; i < _actions.size(); i++) {
			Rules r;
			Rules::Sound snd[2];
			lines.push_back(transition(key, _actions[i], r, snd));
		}
		Common::sort(lines.begin(), lines.end());
		_buf.clear();
		for (uint i = 0; i < lines.size(); i++) {
			if (!_first)
				_buf += '\n';
			_first = false;
			_buf += lines[i];
		}
		_pos = 0;
	}
};

} // End of namespace UiHens

class UiPuzzleHensTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	// Start records and every state reachable by the clicks, line by line
	// against the emulated original.
	void test_vs_original() {
		using namespace UiHens;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("ui_puzzle_hens: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("ui_puzzle1_hens.txt", golden));
		Common::Array<Common::String> out;

		const Rules start;
		out.push_back("start records (target, weight, x, y, tilt):");
		for (int n = 0; n < Rules::kNumHens; n++) {
			const Rules::Hen &h = start.hens[n];
			out.push_back(Common::String::format("%d (%d, %d, %d, %d, %d)", n, h.target, h.weight,
			                                     h.homeX, h.homeY, h.tilt));
		}
		Common::String tl = "targets";
		Common::Array<Action> actions;
		for (int t = 1; t < Rules::kNumTargets; t++) {
			Action a;
			a.name = Common::String::format("t%d", t);
			Rules::targetPos(t, a.x, a.y);
			tl += Common::String::format(" %d=(%d, %d)", t, a.x, a.y);
			actions.push_back(a);
		}
		out.push_back(tl);
		for (int n = 2; n < 9; n++) {
			Action a = { Common::String::format("home%d", n), start.hens[n].homeX, start.hens[n].homeY };
			actions.push_back(a);
		}
		const Action extra[3] = { { "else", 450, 150 }, { "b1", 356, 360 }, { "b2", 420, 360 } };
		for (int i = 0; i < 3; i++)
			actions.push_back(extra[i]);
		Common::String al = "actions";
		for (uint i = 0; i < actions.size(); i++)
			al += Common::String::format(" %s=(%d, %d)", actions[i].name.c_str(), actions[i].x, actions[i].y);
		out.push_back(al);

		// Breadth first over (carried, targets of hens 2..8).
		Common::Array<uint32> queue;
		KeySet seen;
		queue.push_back(stateKey(start));
		seen.insert(queue[0]);
		Common::Array<Common::String> weigh;
		uint transitions = 0;
		for (uint qi = 0; qi < queue.size(); qi++) {
			const uint32 cur = queue[qi];
			for (uint ai = 0; ai < actions.size(); ai++) {
				Rules r;
				Rules::Sound snd[2];
				const Common::String line = transition(cur, actions[ai], r, snd);
				transitions++;
				if ((actions[ai].name == "b1" || actions[ai].name == "b2") &&
				        (snd[0] == Rules::kSoundEven || snd[0] == Rules::kSoundWin || snd[0] == Rules::kSoundLose))
					weigh.push_back(line);
				const uint32 key = stateKey(r);
				if (seen.insert(key))
					queue.push_back(key);
			}
		}
		out.push_back(Common::String::format("states %u transitions %u", queue.size(), transitions));

		// The states in the order of their reprs.
		Common::Array<Keyed> reprs;
		for (uint i = 0; i < queue.size(); i++) {
			Keyed k;
			k.repr = stateRepr(fromKey(queue[i]));
			k.key = queue[i];
			reprs.push_back(k);
		}
		Common::sort(reprs.begin(), reprs.end());
		Common::Array<uint32> sorted;
		for (uint i = 0; i < reprs.size(); i++)
			sorted.push_back(reprs[i].key);
		reprs.clear();
		ListingStream listing(sorted, actions);
		out.push_back("transitions md5 " + Common::computeStreamMD5AsString(listing));

		Common::sort(weigh.begin(), weigh.end());
		uint lose = 0, even = 0, win = 0;
		for (uint i = 0; i < weigh.size(); i++) {
			if (weigh[i].hasSuffix(" lose"))
				lose++;
			else if (weigh[i].hasSuffix(" none"))
				even++;
			else if (weigh[i].hasSuffix(" win"))
				win++;
		}
		Common::String wl = "weighings";
		if (lose)
			wl += Common::String::format(" lose=%u", lose);
		if (even)
			wl += Common::String::format(" none=%u", even);
		if (win)
			wl += Common::String::format(" win=%u", win);
		out.push_back(wl);
		const Common::String wall = joinLines(weigh);
		out.push_back("weighings md5 " + AtlantisTest::md5Hex((const byte *)wall.c_str(), wall.size()));
		uint shown = 0;
		for (uint i = 0; i < weigh.size() && shown < 20; i++)
			if (weigh[i].hasSuffix(" win")) {
				out.push_back(weigh[i]);
				shown++;
			}

		// Compare with the golden, skipping its comment line.
		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}

	// The only solution (hen n on target n, second button) weighed four
	// times solves the puzzle; a corner click asks to leave.
	void test_solve_and_leave() {
		using namespace UiHens;
		Rules r;
		Rules::Sound snd[2];
		for (int n = 2; n < 9; n++)
			r.hens[n].target = n;
		for (int i = 0; i < 4; i++) {
			TS_ASSERT(!r.solved());
			r.click(420, 360, snd);
			TS_ASSERT_EQUALS(snd[0], Rules::kSoundWin);
		}
		TS_ASSERT(r.solved());
		TS_ASSERT_EQUALS(r.marks[3].x, 322);
		TS_ASSERT(!r.leave);
		r.click(10, 10, snd);
		TS_ASSERT(r.leave);
	}
};
