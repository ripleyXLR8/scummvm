#ifndef TEST_ENGINES_CRYOMNI3D_ATLANTIS_HELPER_H
#define TEST_ENGINES_CRYOMNI3D_ATLANTIS_HELPER_H

// Helpers for the Atlantis: The Lost Tales tests.
//
// Two kinds of tests exist:
// - self-contained tests, whose expected values are embedded (produced by
//   running the original game code on synthetic input);
// - data tests, which need the original game files and reference digests
//   produced from the original executable. They are skipped unless a file
//   named atlantis_test.cfg in the current directory gives both paths:
//       data=/path/to/game        (directory with BIGCD1.BIG and atlantis.exe)
//       golden=/path/to/digests   (directory of reference digest files)

#include "common/file.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/md5.h"
#include "common/memstream.h"
#include "common/str.h"

#include "common/system.h"

#include "../../../system/null_osystem.h"

#include <cxxtest/TestSuite.h>

namespace AtlantisTest {

// Value of "key=" in ./atlantis_test.cfg, or an empty string.
inline Common::String configValue(const char *key) {
	// File access needs an OSystem; provide a null one if the suite has not.
	bool ownSystem = (g_system == nullptr);
	if (ownSystem)
		Common::install_null_g_system();
	Common::String result;
	{
		Common::FSNode node(Common::Path("atlantis_test.cfg"));
		Common::File f;
		if (node.exists() && f.open(node)) {
			Common::String prefix = Common::String(key) + "=";
			while (!f.eos()) {
				Common::String line = f.readLine();
				line.trim();
				if (line.hasPrefix(prefix)) {
					result = line.substr(prefix.size());
					break;
				}
			}
		}
	}
	if (ownSystem)
		Common::uninstall_null_g_system();
	return result;
}

inline Common::String dataDir() { return configValue("data"); }
inline Common::String goldenDir() { return configValue("golden"); }

inline bool haveData() { return !dataDir().empty() && !goldenDir().empty(); }

// Reference digests: one "key md5" pair per line, '#' starts a comment.
// Files are written by atlantis-re/tools/emu/* from the original executable.
typedef Common::HashMap<Common::String, Common::String> Golden;

inline bool loadGolden(const char *name, Golden &out) {
	Common::FSNode node(Common::Path(goldenDir(), '/').appendComponent(name));
	Common::File f;
	if (!f.open(node))
		return false;
	while (!f.eos()) {
		Common::String line = f.readLine();
		line.trim();
		if (line.empty() || line[0] == '#')
			continue;
		uint sp = line.findLastOf(' ');
		if (sp == Common::String::npos)
			continue;
		Common::String key = line.substr(0, sp);
		key.trim();
		out[key] = line.substr(sp + 1);
	}
	return true;
}

inline Common::String md5Hex(const byte *data, uint32 size) {
	Common::MemoryReadStream s(data, size);
	return Common::computeStreamMD5AsString(s);
}

} // End of namespace AtlantisTest

#endif
