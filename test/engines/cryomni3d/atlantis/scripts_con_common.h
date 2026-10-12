#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_CON_COMMON_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_SCRIPTS_CON_COMMON_H

// Verifier "scripts": helpers shared by scripts_con_parse.h and
// scripts_con_eval.h (no test suite in this file).
//
//  - ScriptsTest::Big   port-independent reader of BIGCD1..4.BIG (same layout
//    as atlantis-re/tools/emu/scripts_big.py; first archive wins).
//  - canonicalDump()    canonical text of a ConScript parse, the same format
//    as atlantis-re/tools/emu/scripts_con.py writes from the ORIGINAL
//    interpreter (atlantis-cd-en-714240 @ 0x004280f8/0x004281f8/0x0042bef4/
//    0x0042bfa4 entry scanners, 0x0042844c section executor, 0x00425eb0 /INIT).
//  - portEval()         runs the REAL condition evaluator, ConEvaluator
//    (con_eval.cpp), on one condition line laid out as the emulation harness
//    lays it out (line + "\r\n"), with the VARIAS.CON names, a variable map
//    and an inventory built with the real inventory unit (inventory.cpp).
//    The engine's evalSelSection uses the same class; only the keys it reads
//    from live engine state (departvue, vue, objetenmain, chaptertime,
//    nbvisite, random) come from an engine subclass, and the test gives them
//    through the variable map instead.
//  - the deterministic variable-state generator mirrored from
//    atlantis-re/tools/emu/scripts_eval.py.

#include "atlantis_test_helper.h"

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"
#include "common/util.h"

#include "common/func.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/sprites.h"
#include "cryomni3d/atlantis/con_eval.h"
#include "cryomni3d/atlantis/con_script.h"
#include "cryomni3d/atlantis/inventory.h"

namespace ScriptsTest {

using CryOmni3D::Atlantis::ConCondition;
using CryOmni3D::Atlantis::ConScript;
using CryOmni3D::Atlantis::ConSection;

struct BigEntry {
	int cd;
	uint32 offset;
	uint32 size;
};

class Big {
public:
	Big() {
		for (int cd = 1; cd <= 4; cd++) {
			Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/')
			                    .appendComponent(Common::String::format("BIGCD%d.BIG", cd)));
			Common::File f;
			if (!f.open(node))
				continue;
			f.seek(16);
			uint32 n = f.readUint32LE();
			f.seek(24);
			uint32 base = f.readUint32LE();
			f.seek(32);
			uint32 seq = 0;
			for (uint32 i = 0; i < n; i++) {
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
				if (!_entries.contains(name)) {
					BigEntry e = { cd, base + seq, rec[2] };
					_entries[name] = e;
					_names.push_back(name);
				}
				seq += rec[2];
			}
		}
	}

	bool empty() const { return _entries.empty(); }
	bool has(const Common::String &name) const { return _entries.contains(name); }

	Common::Array<Common::String> names(const char *prefix, const char *suffix) const {
		Common::Array<Common::String> out;
		for (uint i = 0; i < _names.size(); i++)
			if (_names[i].hasPrefix(prefix) && _names[i].hasSuffix(suffix))
				out.push_back(_names[i]);
		Common::sort(out.begin(), out.end());
		return out;
	}

	bool get(const Common::String &name, Common::Array<char> &out) const {
		if (!_entries.contains(name))
			return false;
		const BigEntry &e = _entries[name];
		Common::FSNode node(Common::Path(AtlantisTest::dataDir(), '/')
		                    .appendComponent(Common::String::format("BIGCD%d.BIG", e.cd)));
		Common::File f;
		if (!f.open(node))
			return false;
		f.seek(e.offset);
		out.resize(e.size);
		if (e.size)
			f.read(out.begin(), e.size);
		return true;
	}

private:
	Common::HashMap<Common::String, BigEntry> _entries;
	Common::Array<Common::String> _names;
};

// ---------------------------------------------------------------------------
// Canonical dump of a port parse (format of scripts_con.py StructDumper.dump).
// ---------------------------------------------------------------------------

inline Common::String canonicalDump(const ConScript &script) {
	Common::String out;
	for (uint i = 0; i < script.initCmds().size(); i++)
		out += "I " + script.initCmds()[i] + "\n";
	const Common::Array<ConSection> &secs = script.sections();
	for (uint s = 0; s < secs.size(); s++) {
		const ConSection &sec = secs[s];
		Common::String kind;
		// The harness calls an entry "con*" when the /con scanner still finds
		// it with every play counter at 0xff.
		if (!sec.isEntry)
			kind = "none";
		else if (sec.isTimer)
			kind = Common::String::format("tim%d", sec.timerNum);
		else if (sec.isCon)
			kind = (sec.repeatable || sec.conLimit > 0xff) ? "con*" : "con";
		else
			kind = "sel";
		out += Common::String::format("S %d %s\n", sec.id, kind.c_str());
		for (uint k = 0; k < sec.items.size(); k++) {
			const CryOmni3D::Atlantis::ConSectionItem &it = sec.items[k];
			if (it.isSet)
				out += "SET " + it.setCmd + "\n";
			else
				out += "DLG " + it.line.speaker + "|" + it.line.params + "|" + it.line.text + "\n";
		}
		if (sec.gotoId)
			out += Common::String::format("GO %d\n", sec.gotoId);
	}
	return out;
}

// ---------------------------------------------------------------------------
// Variable states (mirror of scripts_eval.py).
// ---------------------------------------------------------------------------

struct VarState {
	Common::HashMap<Common::String, int> vars;   // lowercase VARIAS name -> value
	Common::Array<int> inventory;                // object ids present (val = 1)
};

inline uint32 lcg(uint32 x) { return x * 1103515245u + 12345u; }
inline uint32 rnd(uint32 x) { return (x >> 16) & 0x7fff; }

// Raw condition lines: '/' + sel|con|tim (any case) at line start.
inline void condLines(const Common::Array<char> &data, Common::Array<Common::String> &out) {
	uint i = 0;
	while (i < data.size()) {
		uint st = i;
		while (i < data.size() && data[i] != '\r' && data[i] != '\n')
			i++;
		Common::String line(data.begin() + st, i - st);
		if (i < data.size() && data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n')
			i += 2;
		else if (i < data.size())
			i++;
		if (line.size() >= 4 && line[0] == '/') {
			Common::String d = line.substr(1, 3);
			d.toLowercase();
			if (d == "sel" || d == "con" || d == "tim")
				out.push_back(line);
		}
	}
}

inline void digitRuns(const Common::String &s, Common::Array<int> &out, Common::Array<uint> *pos = nullptr) {
	for (uint i = 0; i < s.size();) {
		if (Common::isDigit(s[i])) {
			uint st = i;
			int n = 0;
			while (i < s.size() && Common::isDigit(s[i]))
				n = n * 10 + (s[i++] - '0');
			out.push_back(n);
			if (pos)
				pos->push_back(st);
		} else {
			i++;
		}
	}
}

inline void makeState(uint lineIdx, uint k, const Common::Array<Common::String> &varNames,
                      const Common::String &line, VarState &st) {
	Common::Array<int> runs;
	Common::Array<uint> pos;
	digitRuns(line, runs, &pos);
	Common::Array<int> cands;
	cands.push_back(0);
	for (uint i = 0; i < runs.size(); i++) {
		int v3[3] = { MAX(runs[i] - 1, 0), runs[i], runs[i] + 1 };
		for (int j = 0; j < 3; j++) {
			bool seen = false;
			for (uint c = 0; c < cands.size(); c++)
				if (cands[c] == v3[j]) { seen = true; break; }
			if (!seen)
				cands.push_back(v3[j]);
		}
	}
	Common::Array<int> inv;
	Common::String low = line;
	low.toLowercase();
	for (uint i = 0; i < runs.size(); i++) {
		if (pos[i] >= 6 && low.substr(pos[i] - 6, 6) == "invent") {
			bool seen = false;
			for (uint c = 0; c < inv.size(); c++)
				if (inv[c] == runs[i]) { seen = true; break; }
			if (!seen)
				inv.push_back(runs[i]);
		}
	}
	uint32 x = (uint32)(lineIdx + 1) * 7919u + (uint32)k * 104729u;
	st.vars.clear();
	st.inventory.clear();
	for (uint v = 0; v < varNames.size(); v++) {
		x = lcg(x);
		st.vars[varNames[v]] = cands[rnd(x) % cands.size()];
	}
	for (uint i = 0; i < inv.size(); i++) {
		x = lcg(x);
		if (rnd(x) & 1)
			st.inventory.push_back(inv[i]);
	}
}

// Conditions of one raw condition line, parsed by the REAL port parser.
inline void portParseLine(const Common::String &line, Common::Array<ConCondition> &out) {
	Common::String text = "/1\r\n" + line + "\r\n";
	ConScript script;
	script.parse(text.c_str(), text.size());
	out.clear();
	if (!script.sections().empty())
		out = script.sections()[0].conditions;
}

struct NullInventoryCb {
	void cb(uint) {}
};

// VARIAS.CON from the game data, loaded once.
inline const CryOmni3D::Atlantis::ConVariableNames &variasNames() {
	static CryOmni3D::Atlantis::ConVariableNames names;
	if (names.empty()) {
		Big big;
		Common::Array<char> data;
		if (big.get("SCENAR\\VARIAS.CON", data))
			names.load(data.begin(), data.size());
	}
	return names;
}

// The port's condition evaluator on a test state: variables from st.vars,
// inventory records for st.inventory (one per id, in order).
inline bool portEval(const Common::String &line, const VarState &st) {
	static const CryOmni3D::Sprites sprites;
	static NullInventoryCb nullCb;
	Common::Array<uint> ids;
	for (uint i = 0; i < st.inventory.size(); i++)
		ids.push_back((uint)st.inventory[i]);
	CryOmni3D::Objects objects;
	CryOmni3D::Atlantis::buildInventoryCatalogue(sprites, ids, objects);
	CryOmni3D::Inventory inventory;
	inventory.init(MAX<uint>(ids.size(), 1),
	               new Common::Functor1Mem<uint, void, NullInventoryCb>(&nullCb, &NullInventoryCb::cb));
	for (uint i = 0; i < objects.size(); i++)
		CryOmni3D::Atlantis::inventoryAppend(inventory, &objects[i]);
	Common::String text = line + "\r\n";
	Common::Array<char> buf(text.c_str(), text.size());
	CryOmni3D::Atlantis::ConEvaluator eval(variasNames(), st.vars, inventory);
	return eval.evaluate(buf, 0);
}

} // End of namespace ScriptsTest

#endif
