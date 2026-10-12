#include "atlantis_test_helper.h"

#include "cryomni3d/atlantis/con_script.h"

// Smoke test of the Atlantis test wiring: the CON parser links and parses.
class AtlantisConScriptSmokeTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_minimal() {
		const char text[] = "/sel(vue=1)\r\n/set(flag=1)\r\n";
		CryOmni3D::Atlantis::ConScript script;
		script.parse(text, sizeof(text) - 1);
		TS_ASSERT(true);
	}
};
