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

	execConInitCommands();
	debugC(1, kDebugScript, "loadConScript: '%s' -> %u sections",
	      name.c_str(), _conScript.sections().size());
}

void CryOmni3DEngine_Atlantis::execConInitCommands() {
	for (const Common::String &cmd : _conScript.initCmds())
		execConSetCommand(cmd);
}

int CryOmni3DEngine_Atlantis::resolveSpeakerPersoId(const Common::String &speaker) const {
	if (speaker.empty())
		return 0;
	if (_heroPersoId > 0 && scumm_strnicmp(speaker.c_str(), "seth", 4) == 0)
		return _heroPersoId;

	struct SpeakerEntry { const char *prefix; uint len; int persoId; };
	static const SpeakerEntry kSpeakerTable[] = {
		{ "Agat",      4, 520 }, { "Lasc",      4, 521 }, { "Melj",      4, 522 },
		{ "Sorc",      4, 567 }, { "Gard",      4, 604 }, { "Sold",      4, 604 },
		{ "Pech",      4, 524 }, { "Acty",      4, 524 }, { "Creo",      4, 525 },
		{ "Chas",      4, 540 }, { "Passant9",  8, 602 }, { "Carv",      4, 602 },
		{ "Passant6",  8, 601 }, { "Passante7", 9, 606 }, { "Hect",      4, 543 },
		{ "Cuis",      4, 551 }, { "Rein",      4, 560 }, { "Reinem",    6, 561 },
		{ "Reinep",    6, 562 }, { "Garc",      4, 526 }, { "Servan",    6, 609 },
		{ "Servag",    6, 582 }, { "Chie",      4, 650 }, { "Anna",      4, 663 },
		{ "Escl",      4, 597 }, { "Enne",      4, 667 }, { "Hona",      4, 682 },
		{ "Sama",      4, 683 }, { "Naka",      4, 684 }, { "Lona",      4, 576 },
		{ "JPil",      4, 571 }, { "Gimb",      4, 563 },
	};
	for (const SpeakerEntry &e : kSpeakerTable) {
		if (scumm_strnicmp(speaker.c_str(), e.prefix, e.len) == 0)
			return e.persoId;
	}
	return 0;
}

void CryOmni3DEngine_Atlantis::execConSetCommand(const Common::String &cmd) {
	const char *p = cmd.c_str();
	const char *eq = strchr(p, '=');
	if (!eq)
		return;
	Common::String key(p, (uint)(eq - p));
	key.toLowercase();
	int value = atoi(eq + 1);

	if (key == "vue") {
		// Schedule a place change (gameStep enters it next iteration).
		_nextPlaceId = (uint)value;
		return;
	}

	// Every other command currently just records a game variable.  The
	// subsystem-specific commands (showperso/hideperso, newsound, addguest,
	// startpuzzle, enablesuj, …) are interpreted as their subsystems land;
	// until then they populate _gameVars harmlessly and gate nothing.
	_gameVars[key] = value;
}

bool CryOmni3DEngine_Atlantis::evalSelSection(const ConSection &sec,
                                               int departZone, int currentVue) const {
	// One random roll per section so multiple `random` clauses in the same
	// conjunction see a consistent value.
	const int rolledRandom = (int)_conRng.getRandomNumber(255);

	if (sec.conditions.empty())
		return true;

	// Left-to-right short-circuit eval (atlantis.exe FUN_004298ac):
	//   '&' + fail -> EXIT FAIL ; '|' + pass -> EXIT SUCCESS ; end -> last result.
	for (const ConCondition &cc : sec.conditions) {
		int lhs;
		if (cc.key == "departvue") {
			lhs = departZone;
		} else if (cc.key == "vue") {
			lhs = currentVue;
		} else if (cc.key == "chaptertime") {
			lhs = _chapterStartMs
			      ? (int)((g_system->getMillis() - _chapterStartMs) / 1000)
			      : 0;
		} else if (cc.key == "nbvisite") {
			lhs = (currentVue >= 0 && (uint)currentVue < _placeVisits.size())
			      ? (int)_placeVisits[currentVue] : 0;
		} else if (cc.key == "random") {
			lhs = rolledRandom;
		} else {
			// Generic variable (covers arriveevue, the flags, initflag, …);
			// objetenmain / invent<N> resolve to 0 until the inventory lands.
			Common::HashMap<Common::String, int>::const_iterator it = _gameVars.find(cc.key);
			lhs = (it != _gameVars.end()) ? it->_value : 0;
		}

		bool ok;
		switch (cc.op) {
		case '=': ok = (lhs == cc.value); break;
		case '<': ok = (lhs <  cc.value); break;
		case '>': ok = (lhs >  cc.value); break;
		case '!': ok = (lhs != cc.value); break;
		default:  ok = false;             break;
		}

		if (cc.nextSep == '&') {
			if (!ok) return false;
		} else if (cc.nextSep == '|') {
			if (ok) return true;
		} else {
			return ok;
		}
	}
	return true;
}

void CryOmni3DEngine_Atlantis::runArrivalSelSections(int arrivedVue, int fromVue) {
	// arriveevue = the place we arrived FROM (so both the marker form
	// (arriveevue>0)&(vue=N) and the specific (arriveevue=FROM)&(vue=TO) work).
	_gameVars["arriveevue"] = (fromVue > 0) ? fromVue : arrivedVue;

	// Single /sel pool, file order, first match fires (atlantis.exe
	// FUN_004280f8).  This is also what runs each chapter's intro section
	// (/sel(initflag=0), the first /sel after /FININIT).
	for (ConSection &sec : _conScript.sections()) {
		if (sec.isCon || sec.isTimer || sec.played)
			continue;
		if (!evalSelSection(sec, 0, arrivedVue))
			continue;
		debugC(1, kDebugScript, "runArrivalSelSections: vue=%d firing section %d",
		      arrivedVue, sec.id);
		sec.played = true;
		playConSection(sec);
		break;   // first match only
	}

	_gameVars["arriveevue"] = 0;
}

void CryOmni3DEngine_Atlantis::runDepartureSelSections(int departZone) {
	// Same single-fire /sel scan; departure sections gate on (departvue=N) or
	// (departvue>0), arrival-only sections self-exclude (arriveevue is 0 here).
	for (ConSection &sec : _conScript.sections()) {
		if (sec.isCon || sec.isTimer)
			continue;
		if (!evalSelSection(sec, departZone, (int)_currentPlaceId))
			continue;
		debugC(1, kDebugScript, "runDepartureSelSections: zone=%d firing section %d",
		      departZone, sec.id);
		playConSection(sec);
		break;
	}
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
