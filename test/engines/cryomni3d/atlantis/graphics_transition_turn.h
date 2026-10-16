#include "atlantis_test_helper.h"
#include "../../../system/null_osystem.h"

#include "common/array.h"

#include "cryomni3d/atlantis/omni3d.h"
#include "cryomni3d/atlantis/transition_turn.h"

// graphics audit — the turn before a transition video.
//
// Expected values come from the ORIGINAL FUN_004212cc of
// atlantis-cd-en-714240 run unmodified under Unicorn (drawing stubbed, the
// angle and FOV calls FUN_00451915 / FUN_0045179a recorded) for 120 cases:
// atlantis-re/tools/emu/graphics_transition_turn.py ->
// evidence/golden/graphics_transition_turn.txt.
//
// Port code exercised directly: TransitionTurn (transition_turn.h), which
// CryOmni3DEngine_Atlantis::executeTransition runs, and
// Omni3DManagerAtlantis::exePi.
//
// The golden's end columns are ALPHA BETA SAVED_A SAVED_B ZOOM of the
// original after FUN_004212cc returns.  Only the frames (md5) and the reached
// angles (SAVED_A / SAVED_B = endAlpha / endBeta of TransitionTurn::run) are
// produced by the port code under test.  The final FUN_0045179a(75.187, 50)
// entry appended to the frame list, the post-jump ALPHA / BETA written as the
// test's own da / db and the ZOOM 0 column are the original's end actions,
// which the port does outside TransitionTurn::run and which this test does
// not check: the FOV handling and the ZOOM clear in
// CryOmni3DEngine_Atlantis::turnToTransition (place.cpp, which restores the
// warp's HFov instead of holding 75.187 through the video) and the jump to
// dstAlpha / dstBeta in its callers (place.cpp, after playTransitionVideo).

namespace TransitionTurnTest {

inline Common::String hexOf(float v) {
	uint32 u;
	memcpy(&u, &v, 4);
	return Common::String::format("%02x%02x%02x%02x", u & 0xff, (u >> 8) & 0xff, (u >> 16) & 0xff, u >> 24);
}

} // End of namespace TransitionTurnTest

class GraphicsTransitionTurnTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_vs_original() {
		using namespace TransitionTurnTest;
		using CryOmni3D::Atlantis::TransitionTurn;
		if (AtlantisTest::goldenDir().empty()) {
			TS_WARN("graphics_transition_turn: no golden directory configured, skipped");
			return;
		}
		Common::Array<Common::String> golden;
		TS_ASSERT(AtlantisTest::loadGoldenLines("graphics_transition_turn.txt", golden));
		const float pi = CryOmni3D::Atlantis::Omni3DManagerAtlantis::exePi();

		struct V { double v; const char *s; };
		const V alphas[5] = { { 0.0, "0.0" }, { 1.0, "1.0" }, { 3.0, "3.0" }, { 5.5, "5.5" }, { 6.25, "6.25" } };
		const V betas[3] = { { -0.3, "-0.3" }, { 0.0, "0.0" }, { 0.25, "0.25" } };
		const V srcs[4][2] = { { { 0.5, "0.5" }, { 0.0, "0.0" } }, { { 3.14, "3.14" }, { -0.2, "-0.2" } },
		                       { { 6.0, "6.0" }, { 0.3, "0.3" } }, { { 1.0, "1.0" }, { 0.0, "0.0" } } };
		Common::Array<Common::String> out, full;
		int n = 0;
		for (int z = 0; z < 2; z++)
			for (int ai = 0; ai < 5; ai++)
				for (int bi = 0; bi < 3; bi++)
					for (int si = 0; si < 4; si++) {
						const float sa = (float)srcs[si][0].v, sb = (float)srcs[si][1].v;
						const float da = (float)(srcs[si][0].v + 1.0), db = (float)-srcs[si][1].v;
						Common::Array<TransitionTurn::Frame> frames;
						float endA, endB;
						TransitionTurn::run(pi, (float)alphas[ai].v, (float)betas[bi].v, sa, sb, z != 0, frames, endA, endB);
						Common::String lst;
						for (uint i = 0; i < frames.size(); i++) {
							if (frames[i].fovSet)
								lst += (lst.empty() ? "f" : " f") + hexOf(frames[i].fov) + hexOf(50.0f);
							lst += (lst.empty() ? "a" : " a") + hexOf(frames[i].alpha) + hexOf(frames[i].beta);
						}
						lst += " f" + hexOf(75.187f) + hexOf(50.0f);
						const Common::String head = Common::String::format("zoom %d start %s %s src %s %s", z,
						    alphas[ai].s, betas[bi].s, srcs[si][0].s, srcs[si][1].s);
						const Common::String line = head + Common::String::format(" -> frames %u end ", frames.size()) +
						    hexOf(da) + " " + hexOf(db) + " " + hexOf(endA) + " " + hexOf(endB) + " 0 md5 " +
						    AtlantisTest::md5Hex((const byte *)lst.c_str(), lst.size());
						out.push_back(line);
						if (n < 3 || (z == 1 && n % 60 == 0))
							full.push_back("frames of [" + head + "]: " + lst);
						n++;
					}
		out.push_back(Common::String::format("cases %d", n));
		for (uint i = 0; i < full.size(); i++)
			out.push_back(full[i]);

		Common::Array<Common::String> want;
		for (uint i = 0; i < golden.size(); i++)
			if (golden[i][0] != '#')
				want.push_back(golden[i]);
		TS_ASSERT_EQUALS(out.size(), want.size());
		for (uint i = 0; i < out.size() && i < want.size(); i++)
			TS_ASSERT_EQUALS(out[i], want[i]);
	}
};
