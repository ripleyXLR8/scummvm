#include "atlantis_test_helper.h"

#include "cryomni3d/atlantis/con_script.h"

// Smoke test of the Atlantis test wiring: the CON parser links and parsing a
// two-line script does not crash; parsing is checked in scripts_con_parse.h.
class AtlantisConScriptSmokeTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_minimal() {
		const char text[] = "/sel(vue=1)\r\n/set(flag=1)\r\n";
		CryOmni3D::Atlantis::ConScript script;
		script.parse(text, sizeof(text) - 1);
		// The text is kept as loaded (the evaluator reads it).
		TS_ASSERT_EQUALS(script.text().size(), sizeof(text) - 1);
		TS_ASSERT_EQUALS(memcmp(script.text().begin(), text, sizeof(text) - 1), 0);
	}
};
