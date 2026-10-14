// Verifier "scripts": the per-perso subject table (task 3).
//
// Original: atlantis-cd-en-714240 FUN_0042a100 handing EnableSuj to
// FUN_00425f6c and DisableSuj to FUN_0042606c, emulated unmodified by
// atlantis-re/tools/emu/scripts_suj.py: the synthetic sequences below and,
// per shipped CON file, every EnableSuj / DisableSuj line replayed in file
// order (golden scripts_suj.txt).
//
// Port: ConSubjects (con_eval.h), called directly.

#ifndef TEST_ATLANTIS_SCRIPTS_CON_SUBJECTS_H
#define TEST_ATLANTIS_SCRIPTS_CON_SUBJECTS_H

#include "atlantis_test_helper.h"
#include "scripts_con_common.h"
#include "../../../system/null_osystem.h"

#include "cryomni3d/atlantis/con_eval.h"

namespace ScriptsSubjects {

using CryOmni3D::Atlantis::ConSubjects;

// "perso:id,id;..." for the non-empty records, ascending perso.
inline Common::String canon(const ConSubjects &t) {
	Common::String s;
	for (uint r = 0; r < ConSubjects::kRecords; r++) {
		const Common::Array<uint8> &l = t.subjects(0x200 + (int)r);
		if (l.empty())
			continue;
		if (!s.empty())
			s += ";";
		s += Common::String::format("%u:", 0x200 + r);
		for (uint k = 0; k < l.size(); k++)
			s += Common::String::format(k ? ",%u" : "%u", l[k]);
	}
	return s;
}

// One /set line as the harness runs it (the line, then "\r\n/1\r\n"):
// FUN_0042a100's key up to one of "=<>!#,", the value at the first digit
// among the 16 bytes from there.  The key is matched by prefix
// (FUN_00429d20), so "Enable" names ENABLESUJ.
inline void apply(ConSubjects &t, const Common::String &line) {
	Common::String text = line + "\r\n/1\r\n";
	Common::Array<char> buf(text.c_str(), text.size());
	uint32 p = 0;
	while (p < buf.size() && buf[p] != '(')
		p++;
	p++;
	Common::String key;
	while (p < buf.size() && !strchr("=<>!#,", buf[p]))
		key += buf[p++];
	key.toLowercase();
	uint32 v = p;
	for (uint i = 0; i < 16; i++)
		if (buf[p + i] >= '0' && buf[p + i] <= '9') {
			v = p + i;
			break;
		}
	if (key.hasPrefix("enable"))
		t.enable(buf, v);
	else if (key.hasPrefix("disable"))
		t.disable(buf, v);
}

struct SujCase {
	const char *name;
	const char *lines[3];
	const char *steps[3];   // the table after each line; "RUNAWAY": the
	                        // original reads past the line (not compared)
};

// tools/emu/scripts_suj.py
static const SujCase kSujCases[] = {
	{ "enable_dup_order", { "/set(enableSuj,600=12,5,1,2)", "/set(enableSuj,600=5,7)" }, { "600:12,5,1,2", "600:12,5,1,2,7" } },
	{ "disable_shift", { "/set(enableSuj,600=12,5,1,2)", "/set(disableSuj,600=5)", "/set(disableSuj,600=9)" }, { "600:12,5,1,2", "600:12,1,2", "600:12,1,2" } },
	{ "disable_list_dup", { "/set(enableSuj,587=1,2,26,36)", "/set(DisableSuj,587=1,2,26,36,1,38)" }, { "587:1,2,26,36", "" } },
	{ "zero_subject", { "/set(enableSuj,600=0,3)", "/set(disableSuj,600=0)" }, { "600:0,3", "600:3" } },
	{ "byte_subject", { "/set(enableSuj,600=300)" }, { "RUNAWAY" } },
	{ "byte_subject_256", { "/set(enableSuj,600=256,4)" }, { "600:0,6,4" } },
	{ "three_digit_subject", { "/set(enableSuj,600=123,7)" }, { "600:123,7" } },
	{ "perso_low_byte", { "/set(enableSuj,767=1)", "/set(disableSuj,767=1)", "/set(enableSuj,512=4)" }, { "767:1", "", "512:4" } },
	{ "sixteen", { "/set(enableSuj,700=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)", "/set(disableSuj,700=1)" }, { "700:1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17", "700:2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17" } },
	{ "prefix_key", { "/set(Enable,601=3)" }, { "601:3" } },
	{ "space_after_comma", { "/set(enableSuj,600=1, 2)" }, { "RUNAWAY" } },
};

} // End of namespace ScriptsSubjects

class ScriptsConSubjectsTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_subject_sequences_match_original() {
		using namespace ScriptsSubjects;
		for (uint c = 0; c < ARRAYSIZE(kSujCases); c++) {
			const SujCase &sc = kSujCases[c];
			ConSubjects t;
			for (uint i = 0; i < 3 && sc.lines[i]; i++) {
				apply(t, sc.lines[i]);
				if (!strcmp(sc.steps[i], "RUNAWAY"))
					break;
				TSM_ASSERT_EQUALS(Common::String::format("%s step %u", sc.name, i).c_str(),
				                  canon(t), Common::String(sc.steps[i]));
			}
		}
	}

	// Lookups outside 0x200..0x2ff find nothing; an EnableSuj for such a
	// perso (the original writes outside the table) changes nothing.
	void test_subject_lookup_outside_table() {
		CryOmni3D::Atlantis::ConSubjects t;
		ScriptsSubjects::apply(t, "/set(enableSuj,999=1)");
		ScriptsSubjects::apply(t, "/set(enableSuj,600=4)");
		TS_ASSERT_EQUALS(ScriptsSubjects::canon(t), Common::String("600:4"));
		TS_ASSERT(t.subjects(0x1ff).empty());
		TS_ASSERT(t.subjects(0x300).empty());
		TS_ASSERT(t.has(600, 4));
		TS_ASSERT(!t.has(600, 5));
		t.clear();
		TS_ASSERT(t.subjects(600).empty());
	}

	// Every EnableSuj / DisableSuj line of every shipped CON file, replayed
	// in file order from an empty table: md5 of the table after each line.
	void test_data_subject_commands_match_original() {
		using namespace ScriptsSubjects;
		if (!AtlantisTest::haveData()) {
			TS_WARN("scripts_con_subjects: no game data configured, skipped");
			return;
		}
		AtlantisTest::Golden golden;
		TS_ASSERT(AtlantisTest::loadGolden("scripts_suj.txt", golden));
		ScriptsTest::Big big;
		Common::Array<Common::String> files = big.names("SCENAR\\", ".CON");
		uint withLines = 0, mismatches = 0;
		Common::String list;
		for (uint f = 0; f < files.size(); f++) {
			Common::Array<char> data;
			TS_ASSERT(big.get(files[f], data));
			ConSubjects t;
			Common::String digestIn;
			uint n = 0;
			for (uint32 i = 0; i + 5 < data.size(); i++) {
				if (scumm_strnicmp(&data[i], "/set(", 5))
					continue;
				const char *after = &data[i + 5];
				const uint32 left = data.size() - (i + 5);
				if (!((left >= 10 && !scumm_strnicmp(after, "enablesuj,", 10)) ||
				      (left >= 11 && !scumm_strnicmp(after, "disablesuj,", 11))))
					continue;
				Common::String line;
				for (uint32 k = i; k < data.size() && data[k] != '\r' && data[k] != '\n'; k++)
					line += data[k];
				apply(t, line);
				digestIn += canon(t) + "\n";
				n++;
			}
			if (!n)
				continue;
			withLines++;
			Common::String key = files[f].substr(7);
			Common::String md5 = AtlantisTest::md5Hex((const byte *)digestIn.c_str(), digestIn.size());
			if (!golden.contains(key) || golden[key] != md5) {
				mismatches++;
				list += " " + key;
			}
		}
		TS_ASSERT_EQUALS(withLines, golden.size());
		if (mismatches)
			TS_WARN(Common::String::format("scripts_con_subjects: %u files differ:", mismatches) + list);
		TS_ASSERT_EQUALS(mismatches, 0u);
	}
};

#endif
