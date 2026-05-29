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

// CON scenario-script execution.  This grows over several commits: it starts
// here with per-chapter script loading and the game-variable model; section
// evaluation, the /set command interpreter and NPC/dialog dispatch are layered
// on as their subsystems land.

#include "common/debug.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

bool CryOmni3DEngine_Atlantis::isPersistentGameVar(const Common::String &key) {
	// Only these 7 CON variables survive a chapter change; the original
	// engine's FUN_00427634 zeroes every other slot on `chapter=`.
	static const char *const kPersistent[] = {
		"chapter", "gardecel", "newsound", "flagchp8",
		"bouclecreon", "codetronevu", "nbstatues"
	};
	for (const char *p : kPersistent)
		if (key.equalsIgnoreCase(p))
			return true;
	return false;
}

void CryOmni3DEngine_Atlantis::loadConScript(int chapter) {
	// Stamp the wall-clock so the `chaptertime` condition (seconds since the
	// active CHAPI*.CON loaded) can be evaluated dynamically.
	_chapterStartMs = g_system->getMillis();
	_gameVars["chaptertime"] = 0;

	_conScript.reset();
	// A chapter change keeps only the 7 persistent CON variables; the
	// transient ones reset to 0.
	{
		Common::HashMap<Common::String, int> persist;
		for (const auto &kv : _gameVars)
			if (isPersistentGameVar(kv._key))
				persist[kv._key] = kv._value;
		_gameVars = persist;
	}

	Common::String name = Common::String::format("CHAPI%03d.CON", chapter);
	Common::SeekableReadStream *s = openBigFileStream(kFileTypeScript, name);
	if (!s) {
		debugC(1, kDebugScript, "loadConScript: no CON file '%s'", name.c_str());
		return;
	}

	uint32 sz = (uint32)s->size();
	byte *buf = new byte[sz + 1];
	s->read(buf, sz);
	buf[sz] = '\0';
	delete s;

	_conScript.parse((const char *)buf, sz);
	delete[] buf;

	debugC(1, kDebugScript, "loadConScript: '%s' -> %u sections",
	      name.c_str(), _conScript.sections().size());
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
