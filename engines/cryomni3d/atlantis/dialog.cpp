/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// In-game conversation playback.  This grows over several commits: it starts
// with the per-character dialog resource table (INITDIAL.TXT) and the dialog
// camera picker; per-line UBB talking-head playback, the subject (topic) menu
// and the F3DC mouth pipeline are layered on as their subsystems land.

#include "common/array.h"
#include "common/debug.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/str.h"
#include "common/textconsole.h"

#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

// Build the per-character dialog resource table from DIALOG\INITDIAL.TXT.  Each
// row is whitespace-separated:
//   persoId unk anim3DC anim3DA animM s3d cam2 cam3 cam4 cam5 [spf2..spf5]
// "none" tokens are the original engine's "unavailable" sentinel and map to
// empty strings here.
void CryOmni3DEngine_Atlantis::loadDialInitData() {
	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeDialog, "INITDIAL.TXT"));
	if (!s) {
		warning("loadDialInitData: cannot open DIALOG\\INITDIAL.TXT");
		return;
	}

	uint32 sz = (uint32)s->size();
	Common::Array<char> buf(sz + 1);
	s->read(buf.data(), sz);
	buf[sz] = '\0';

	const char *p = buf.data();
	while (*p) {
		while (*p && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')) p++;
		if (!*p) break;

		Common::Array<Common::String> tok;
		while (*p && *p != '\r' && *p != '\n') {
			while (*p && (*p == ' ' || *p == '\t')) p++;
			if (!*p || *p == '\r' || *p == '\n') break;
			const char *te = p;
			while (*te && *te != ' ' && *te != '\t' && *te != '\r' && *te != '\n') te++;
			tok.push_back(Common::String(p, (uint32)(te - p)));
			p = te;
		}

		if (tok.empty() || tok[0].equalsIgnoreCase("FIN")) break;
		if (tok.size() < 10) continue;

		int persoId = atoi(tok[0].c_str());
		if (persoId <= 0) continue;

		// The player character ("hero") is detected by the s3d field.
		if (tok[5].equalsIgnoreCase("hero"))
			_heroPersoId = persoId;

		DialInitEntry entry;
		for (int i = 0; i < 4; i++)
			entry.camUBB[i] = tok[6 + i];      // cam2..cam5 basenames

		// Column 4 = animM basename (mouth vertex animation, DIALOG\PERSO\<base>.3DA).
		if (!tok[4].equalsIgnoreCase("none"))
			entry.animMBase = tok[4];
		// Column 5 = s3d basename (scene-graph mesh, DIALOG\PERSO\<base>.S3D).
		if (!tok[5].equalsIgnoreCase("none"))
			entry.s3dBase = tok[5];
		// Column 2 = anim3DC basename (body mesh, DIALOG\PERSO\<base>.3DC).
		if (!tok[2].equalsIgnoreCase("none"))
			entry.anim3DCBase = tok[2];
		// Columns 10..13 = per-cam SPF overlay basenames (optional).
		for (int i = 0; i < 4; i++) {
			if (10 + i < (int)tok.size() && !tok[10 + i].equalsIgnoreCase("none"))
				entry.camSPF[i] = tok[10 + i];
		}

		_dialInitData[persoId] = entry;
	}

	debugC(1, kDebugScript, "loadDialInitData: %u entries from INITDIAL.TXT, heroPersoId=%d",
	      _dialInitData.size(), _heroPersoId);
}

// Per-line camera picker.  Re-implements the random-cam loop from atlantis.exe
// FUN_0042844c (lines 22460-22470):
//
//   while (cam == prevCam || INITDIAL[persoId].camUBB[cam-2] is "none")
//       cam = (rand() & 3) + 2;          // 2..5
//
// An explicit `camN` from a `[Speaker, X, Y, camN]` directive is honoured when
// it is valid and available; otherwise we fall through to the random pick.
// Returns 2..5.
int CryOmni3DEngine_Atlantis::pickDialCam(int persoId, int explicitCam) {
	bool avail[4] = { false, false, false, false };
	int availCount = 0;
	Common::HashMap<int, DialInitEntry>::const_iterator it = _dialInitData.find(persoId);
	if (it != _dialInitData.end()) {
		for (int i = 0; i < 4; i++) {
			const Common::String &s = it->_value.camUBB[i];
			if (!s.empty() && !s.equalsIgnoreCase("none")) {
				avail[i] = true;
				availCount++;
			}
		}
	}

	// Explicit cam: honour it if valid + available (or if we have no data at all).
	if (explicitCam >= 2 && explicitCam <= 5) {
		if (availCount == 0 || avail[explicitCam - 2])
			return explicitCam;
	}

	// No INITDIAL data → deterministic cam2 close-up.
	if (availCount == 0)
		return 2;

	// Random pick — same `(rand & 3) + 2` shape, rejecting the previous cam
	// and unavailable cams.
	int cam = 2;
	for (int tries = 0; tries < 16; tries++) {
		cam = (int)_dialRng.getRandomNumber(3) + 2;   // 2..5
		if (cam == _lastDialCam && availCount > 1)
			continue;
		if (avail[cam - 2])
			return cam;
	}
	// Fallback: first available cam, preferring one other than the last.
	for (int c = 2; c <= 5; c++)
		if (avail[c - 2] && c != _lastDialCam)
			return c;
	for (int c = 2; c <= 5; c++)
		if (avail[c - 2])
			return c;
	return 2;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
