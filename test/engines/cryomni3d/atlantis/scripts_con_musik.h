// Verifier "scripts": LoadMusik / DelMusik (task 22).
//
// Original: atlantis-cd-en-714240 FUN_0042a100 -> FUN_0041f1d0 (LoadMusik) and
// FUN_0041faa0 (DelMusik), emulated unmodified by
// atlantis-re/tools/emu/scripts_musik.py: the presence test FUN_0041f9f8 is
// answered from a set, the copy FUN_0041f380, the menu and DeleteFileA are
// recorded.  Port: conMusicList and conMusicApply (con_eval.h).

#ifndef TEST_ATLANTIS_SCRIPTS_CON_MUSIK_H
#define TEST_ATLANTIS_SCRIPTS_CON_MUSIK_H

#include "atlantis_test_helper.h"

#include "cryomni3d/atlantis/con_eval.h"

namespace ScriptsMusik {

struct MusikCase {
	const char *line;
	int current;          // 0x00772915: playing track (index + 1), 0 none
	const char *before;   // track indexes in the folder before
	const char *events;   // the original's calls, in order
	const char *after;    // track indexes in the folder after
};

// tools/emu/scripts_musik.py
static const MusikCase kMusikCases[] = {
	{ "/set(loadmusik=14)", 0, "", "test 13;menu 11;present;copy 13", "13" },
	{ "/set(loadmusik=14)", 0, "13", "test 13", "13" },
	{ "/set(loadmusik=14)", 14, "", "test 13", "" },
	{ "/set(loadmusik=1,2,3)", 0, "1", "test 0;menu 11;present;copy 0;test 1;test 2;copy 2", "0,1,2" },
	{ "/set(loadmusik=9,10,11)", 0, "", "test 8;menu 11;present;copy 8;test 9;copy 9", "8,9" },
	{ "/set(loadmusik=10,11)", 0, "", "test 9;menu 11;present;copy 9", "9" },
	{ "/set(loadmusik=20,21)", 0, "", "test 19;menu 11;present;copy 19;test 20;copy 20", "19,20" },
	{ "/set(delmusik=1,2,3,4,5,6,20)", 0, "0,1,2,3,4,5,13,19", "delete 0;delete 1;delete 2;delete 3;delete 4;delete 5;delete 19", "13" },
	{ "/set(delmusik=14)", 14, "13", "", "13" },
	{ "/set(delmusik=10,11)", 0, "9,10", "delete 9", "10" },
	{ "/set(loadmusik=26,9,10,22,31,3,4,5,6)", 0, "", "test 25;menu 11;present;copy 25;test 8;copy 8;test 9;copy 9", "8,9,25" },
	{ "/set(delmusik=1,2,3,4,5,6,7,8,11,13,14,15)", 0, "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", "delete 0;delete 1;delete 2;delete 3;delete 4;delete 5;delete 6;delete 7;delete 10;delete 12;delete 13;delete 14", "8,9,11,15" },
	{ "/set(loadmusik=12,21,16,17,18,19,25,26,27,30)", 0, "", "test 11;menu 11;present;copy 11;test 20;copy 20;test 15;copy 15;test 16;copy 16;test 17;copy 17;test 18;copy 18;test 24;copy 24;test 25;copy 25;test 26;copy 26;test 29;copy 29", "11,15,16,17,18,20,24,25,26,29" },
	{ "/set(loadmusik=33)", 0, "", "", "" },
	{ "/set(delmusik=33,2)", 0, "1,31", "delete None;delete 1", "31" },
};

inline uint32 bitsOf(const char *s) {
	uint32 b = 0;
	while (*s) {
		const int v = atoi(s);
		if (v < 32)
			b |= 1u << v;
		while (*s && *s != ',')
			s++;
		if (*s == ',')
			s++;
	}
	return b;
}

} // End of namespace ScriptsMusik

class ScriptsConMusikTestSuite : public CxxTest::TestSuite {
public:
	void test_musik_lists_match_original() {
		using namespace ScriptsMusik;
		for (uint c = 0; c < ARRAYSIZE(kMusikCases); c++) {
			const MusikCase &mc = kMusikCases[c];
			Common::String text = Common::String(mc.line) + "\r\n/1\r\n";
			Common::Array<char> buf(text.c_str(), text.size());
			uint32 p = 0;
			while (buf[p] != '=')
				p++;
			const bool load = Common::String(mc.line).contains("loadmusik");
			uint32 onDisk = bitsOf(mc.before);
			Common::Array<uint8> copies;
			CryOmni3D::Atlantis::conMusicApply(load, CryOmni3D::Atlantis::conMusicList(buf, p + 1),
			                                   mc.current, onDisk, copies);
			// the original's copies, in order, from its events
			Common::String want, got;
			const Common::String ev(mc.events);
			for (uint i = 0; i + 5 <= ev.size(); i++)
				if (ev.substr(i, 5) == "copy ") {
					if (!want.empty())
						want += ",";
					want += Common::String::format("%d", atoi(ev.c_str() + i + 5));
				}
			for (uint i = 0; i < copies.size(); i++)
				got += Common::String::format(i ? ",%u" : "%u", copies[i]);
			TSM_ASSERT_EQUALS(mc.line, got, want);
			TSM_ASSERT_EQUALS(mc.line, onDisk, bitsOf(mc.after));
		}
	}
};

#endif
