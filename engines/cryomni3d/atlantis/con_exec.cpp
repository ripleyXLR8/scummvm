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
#include "common/memstream.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "audio/decoders/apc.h"
#include "audio/mixer.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/puzzle_eclipse.h"
#include "cryomni3d/atlantis/puzzle_ogregame.h"

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
	// Sprite show/hide and subject-enable state are per-chapter; reset them
	// with the variables.
	_hiddenPersos.clear();
	_sujEnabled.clear();

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

void CryOmni3DEngine_Atlantis::playPuzzle(int puzzleId) {
	// Dispatch to the per-puzzle handler.  Puzzles not yet implemented
	// fall back to the auto-win stub so their CHAPI script chains keep
	// progressing (most of them gate further story content behind
	// FinPuzzle=255, e.g. CHAPI012's /sel(FinPuzzle=255)&(Vue=N)&...).
	int finPuzzle = 255;
	switch (puzzleId) {
	case 0: {
		// Celestial alignment ("eclipse on Atlantis") at CHAPI012 vue 221.
		EclipsePuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	case 2: {
		// The rat-hunter's "ogre & hero" board pursuit at CHAPI018 place 290.
		OgreGamePuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	default:
		debugC(1, kDebugScript, "playPuzzle: no handler for puzzle %d, auto-win", puzzleId);
		break;
	}
	_gameVars["finpuzzle"] = finPuzzle;
	// Puzzle launches are often gated behind a click on a sentinel zone
	// (CHAPI012 vue 221 zone.action=1000 — "1000" is not a real place,
	// just a hotspot trigger for /sel(vue=221)&(flagtemp6=0)).  The /sel
	// itself doesn't /set(abortdepart=1), so the pending departure to
	// place 1000 would still fire after the puzzle returns — emitting a
	// "place 1000 not found" warning and stranding the player.  Cancel
	// any pending departure here so the puzzle's caller stays put.
	_nextPlaceId = uint(-1);

	// Re-run the arrival /sel scan so /sel sections that gate on
	// `FinPuzzle = 255` fire immediately on win.  CHAPI012 line 129
	// (`/sel(FinPuzzle=255)&(Vue=221)&(Flagtemp6=0)` -> /set(cinema=30)
	// + hide/show NPCs + Flagtemp6=1) is the canonical case: without
	// this re-scan the cinematic only triggers on the next natural
	// place transition.  The puzzle-launch /sel
	// (`/sel(vue=221)&(flagtemp6=0)` -> /set(Startpuzzle=0)) sits
	// in the same scan but the cinema gate fires first (earlier in
	// the CON file), sets Flagtemp6=1, and the puzzle-launch /sel's
	// `flagtemp6=0` cond then fails — no recursive puzzle relaunch.
	if (finPuzzle == 255 && _currentPlaceId != uint(-1))
		runArrivalSelSections((int)_currentPlaceId, (int)_currentPlaceId);
}

void CryOmni3DEngine_Atlantis::execConSetCommand(const Common::String &cmd) {
	const char *p = cmd.c_str();

	// enableSuj,persoId=suj1,suj2,... / disableSuj,persoId=sujId[,sujId...]
	// These carry the perso before '=' and a comma-separated subject list
	// after, so they're parsed on the raw string before the key/value split.
	if (scumm_strnicmp(p, "enablesuj,", 10) == 0) {
		p += 10;
		int perso = atoi(p);
		while (*p && *p != '=') p++;
		if (*p == '=') {
			p++;
			Common::Array<int> &sujs = sujsForPerso(perso);
			while (*p) {
				int suj = atoi(p);
				if (suj > 0) {
					bool found = false;
					for (int s : sujs) { if (s == suj) { found = true; break; } }
					if (!found) sujs.push_back(suj);
				}
				while (*p && *p != ',' && *p != ')') p++;
				if (*p == ',') p++; else break;
			}
		}
		return;
	}
	if (scumm_strnicmp(p, "disablesuj,", 11) == 0) {
		p += 11;
		int perso = atoi(p);
		while (*p && *p != '=') p++;
		if (*p == '=') {
			p++;
			Common::Array<int> &sujs = sujsForPerso(perso);
			while (*p) {
				int suj = atoi(p);
				if (suj > 0) {
					for (int i = (int)sujs.size() - 1; i >= 0; i--)
						if (sujs[i] == suj) { sujs.remove_at(i); break; }
				}
				while (*p && *p != ',' && *p != ')') p++;
				if (*p == ',') p++; else break;
			}
		}
		return;
	}

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

	// startmusik=N — select music track N (kMusicTrackNames) and loop it.
	if (key == "startmusik") {
		musicPlayTrack(value);
		return;
	}

	// stopmusik=N — clear the music selection and stop playback.
	if (key == "stopmusik") {
		musicStop();
		return;
	}

	// loadmusik=N / delmusik=N — the original pre-loads/frees the track's
	// sample memory; our tracks load on demand, so both are no-ops.
	// startmusik / stopmusik do the actual playback control.
	if (key == "loadmusik" || key == "delmusik")
		return;

	// cinema=N — play the CINEM<NNN>.HNM cinematic full-screen (skippable).
	if (key == "cinema") {
		Common::String hnm = Common::String::format("CINEM%03d.HNM", value);
		debugC(1, kDebugScript, "execConSetCommand: cinema=%d -> %s", value, hnm.c_str());
		playTransitionVideo(hnm);
		return;
	}

	// hideperso=N / disableperso=N — deactivate every sprite for perso N and
	// add it to the persistent hidden list (atlantis.exe FUN_00427bd0).
	if (key == "hideperso" || key == "disableperso") {
		int perso = value;
		if (perso > 0) {
			bool found = false;
			for (int h : _hiddenPersos) if (h == perso) { found = true; break; }
			if (!found)
				_hiddenPersos.push_back(perso);
			for (SpritePlaceEntry &e : _spritePlaceList)
				if (e.persoId == perso) e.active = false;
			recompositeSpriteLayer();
			rebuildNpcPlaceBounds();
			debugC(1, kDebugScript, "execConSetCommand: hide perso %d", perso);
		}
		return;
	}

	// showperso=N — reactivate every sprite for perso N and clear its hide.
	if (key == "showperso") {
		int perso = value;
		for (int i = (int)_hiddenPersos.size() - 1; i >= 0; i--)
			if (_hiddenPersos[i] == perso) _hiddenPersos.remove_at(i);
		for (SpritePlaceEntry &e : _spritePlaceList)
			if (e.persoId == perso) e.active = true;
		recompositeSpriteLayer();
		rebuildNpcPlaceBounds();
		debugC(1, kDebugScript, "execConSetCommand: show perso %d", perso);
		return;
	}

	// inventN=1 — add object N to the inventory; inventN=0 — remove it.
	// (Match objects by their world id, stamped onto _objects by setupObjects.)
	if (key.hasPrefix("invent") && key.size() > 6 && Common::isDigit(key[6])) {
		uint objId = (uint)atoi(key.c_str() + 6);
		if (value) {
			Object *obj = nullptr;
			for (Object &o : _objects)
				if (o.idOBJ() == objId) { obj = &o; break; }
			if (obj) {
				if (!_inventory.inInventoryByNameID(objId))
					_inventory.add(obj);
				// Remember the just-given object so it shows centre-bottom for
				// the rest of the dialog (atlantis.exe DAT_0049619c); the sprite
				// index is the object's slot in _objects (parallel to _objSprites).
				if (!_objects.empty())
					_dialogGivenObjSprite = (int)(obj - &_objects.front());
				debugC(1, kDebugScript, "execConSetCommand: added object %u to inventory", objId);
			} else {
				debugC(1, kDebugScript, "execConSetCommand: invent%u=1 — object not defined", objId);
			}
		} else {
			_inventory.removeByNameID(objId);
			Object *sel = _inventory.selectedObject();
			if (sel && sel->idOBJ() == objId)
				_inventory.deselectObject();
			debugC(1, kDebugScript, "execConSetCommand: removed object %u from inventory", objId);
		}
		return;
	}

	// objetenmain=N — force object N into the player's hand (N=0 clears it).
	if (key == "objetenmain") {
		if (value == 0) {
			_inventory.setSelectedObject(nullptr);
		} else {
			for (Object &o : _objects)
				if ((int)o.idOBJ() == value) { _inventory.setSelectedObject(&o); break; }
		}
		_gameVars["objetenmain"] = value;
		return;
	}

	// newsound=N — play the ambient/SFX track identified by APC table index N
	// (table extracted from atlantis.exe by loadAtlantisExeTables).  Always
	// stops any active newsound before starting a new one, so blocked-zone
	// feedback (e.g. CHAPI001 `/set(newsound=14)` at zones 150/168) doesn't
	// pile up.  Played as one-shot SFX on the SFX channel.
	if (key == "newsound") {
		int soundId = value;
		_gameVars["newsound"] = soundId;

		if (_mixer->isSoundHandleActive(_newSoundHandle))
			_mixer->stopHandle(_newSoundHandle);

		// soundId is a 0-based index into the APC table extracted from
		// atlantis.exe: empirically, CON `newsound=14` plays the 15th file in
		// our enumeration (Toctoc.apc), so do NOT subtract 1 here.
		if (soundId < 0 || (uint)soundId >= _newSoundApcNames.size()) {
			if (!_newSoundApcNames.empty())
				debugC(1, kDebugScript, "execConSetCommand: newsound=%d out of table range (0..%u)",
				      soundId, _newSoundApcNames.size() - 1);
			return;
		}

		const Common::String &apcName = _newSoundApcNames[soundId];
		Common::SeekableReadStream *apcFile =
		    openBigFileStream(kFileTypeSound, apcName);
		if (!apcFile) {
			debugC(1, kDebugScript, "execConSetCommand: newsound=%d '%s' not in BigFile",
			      soundId, apcName.c_str());
			return;
		}

		Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
		if (apc) {
			int64 remaining = apcFile->size() - apcFile->pos();
			if (remaining > 0) {
				byte *buf = new byte[remaining];
				apcFile->read(buf, (uint32)remaining);
				apc->queuePacket(new Common::MemoryReadStream(buf, remaining,
				                                              DisposeAfterUse::YES));
			}
			apc->finish();
			_mixer->playStream(Audio::Mixer::kSFXSoundType, &_newSoundHandle, apc);
			debugC(1, kDebugScript, "execConSetCommand: newsound=%d playing '%s'",
			      soundId, apcName.c_str());
		}
		delete apcFile;
		return;
	}

	// startpuzzle=N — launch minigame N.  N is an enum of ~15 distinct
	// puzzles (eclipse, sliding tile, hexa, serpent, crab, lion, throne,
	// meta, …) referenced across chapters 12-79.  On completion the puzzle
	// writes back FinPuzzle (255 = win; smaller values denote loss patterns,
	// e.g. CHAPI018's chess puzzle uses 1/2 for "poor queen" / "poor hero"
	// voice-line variants).
	if (key == "startpuzzle") {
		playPuzzle(value);
		return;
	}

	// stopsound=N — stop the currently playing newsound.  N is ignored: the
	// original engine only tracks one ambient channel and any stopsound
	// silences it.
	if (key == "stopsound") {
		_gameVars["newsound"] = 0;
		if (_mixer->isSoundHandleActive(_newSoundHandle))
			_mixer->stopHandle(_newSoundHandle);
		return;
	}

	// Every other command currently just records a game variable.  The
	// subsystem-specific commands (newsound, addguest, startpuzzle, …) are
	// interpreted as their subsystems land; until then they populate _gameVars
	// harmlessly and gate nothing.
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
		} else if (cc.key == "objetenmain") {
			// Live held-object id, not the stored game variable: a pickup or
			// the toolbar can change the selection without any /set running.
			Object *sel = _inventory.selectedObject();
			lhs = (sel && sel->idOBJ() != uint(-1)) ? (int)sel->idOBJ() : 0;
		} else if (cc.key.hasPrefix("invent") && cc.key.size() > 6
		           && Common::isDigit(cc.key[6])) {
			// invent<N> — 1 when object N is in the inventory, else 0.  The
			// original (atlantis.exe FUN_00429e54) tests the live inventory
			// here; it is not a stored game variable.
			uint objId = (uint)atoi(cc.key.c_str() + 6);
			lhs = _inventory.inInventoryByNameID(objId) ? 1 : 0;
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
			// Generic variable (covers arriveevue, the flags, initflag, …).
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

Common::Array<int> &CryOmni3DEngine_Atlantis::sujsForPerso(int persoId) {
	for (SujState &ss : _sujEnabled)
		if (ss.persoId == persoId) return ss.sujs;
	SujState ns;
	ns.persoId = persoId;
	_sujEnabled.push_back(ns);
	return _sujEnabled.back().sujs;
}

bool CryOmni3DEngine_Atlantis::isSujEnabled(int persoId, int sujId) const {
	for (const SujState &ss : _sujEnabled) {
		if (ss.persoId != persoId) continue;
		for (int s : ss.sujs)
			if (s == sujId) return true;
		return false;
	}
	return false;
}

void CryOmni3DEngine_Atlantis::interactNPC(int persoId) {
	debugC(1, kDebugScript, "interactNPC: persoId=%d", persoId);
	// Expose clicperso so evalSelSection can match (clicperso=N) conditions.
	_gameVars["clicperso"] = persoId;
	// New conversation: forget the previous cam and capture the click-time
	// facing for the per-line default angle.
	_lastDialCam = 0;
	_dialBaseAlpha = _omni3dMan.getAlpha();
	_dialBaseAlphaValid = true;

	// atlantis.exe FUN_00425828 plays exactly ONE /con section per click and
	// returns.  A perso with enabled subjects (count>0) opens the topic menu;
	// a perso with none plays the first condition-passing /con(clicperso=N)
	// directly (the count==0 branch).
	bool persoHasMenu = false;
	for (const SujState &ss : _sujEnabled)
		if (ss.persoId == persoId && !ss.sujs.empty()) { persoHasMenu = true; break; }

	Common::Array<ConSection *> options;
	Common::Array<int> seenSujs;
	for (ConSection &sec : _conScript.sections()) {
		if (!sec.isCon)
			continue;
		// A /con section is for this NPC if a clicperso condition names it;
		// sections whose only click constraint is a cliczone belong to the
		// zone scan, not the per-NPC one.
		bool hasClicPerso = false, forThisPerso = false, hasClicZone = false;
		for (const ConCondition &cc : sec.conditions) {
			if (cc.key == "clicperso") {
				hasClicPerso = true;
				if (cc.value == persoId) forThisPerso = true;
			} else if (cc.key == "cliczone") {
				hasClicZone = true;
			}
		}
		if (hasClicPerso && !forThisPerso)
			continue;
		if (!hasClicPerso && hasClicZone)
			continue;
		if (sec.played)
			continue;
		if (!evalSelSection(sec, 0, (int)_currentPlaceId))
			continue;

		// A sujId-0 section (scripted greeting / give-object) — or any section
		// of a perso with no menu — plays directly and preempts the menu.
		if (sec.sujId == 0 || !persoHasMenu) {
			options.clear();
			options.push_back(&sec);
			break;
		}

		// Subject section: dedupe by sujId (first id is the menu icon / key)
		// and require one of its subjects enabled.
		bool alreadySeen = false;
		for (int s : seenSujs) if (s == sec.sujId) { alreadySeen = true; break; }
		if (alreadySeen)
			continue;
		bool anyEnabled = false;
		for (int s : sec.sujIds)
			if (isSujEnabled(persoId, s)) { anyEnabled = true; break; }
		if (!anyEnabled && sec.sujIds.empty() && isSujEnabled(persoId, sec.sujId))
			anyEnabled = true;
		if (!anyEnabled)
			continue;

		options.push_back(&sec);
		seenSujs.push_back(sec.sujId);
	}

	if (options.empty()) {
		debugC(1, kDebugScript, "interactNPC: persoId=%d no options found", persoId);
	} else {
		int choice;
		if (options.size() == 1 && (options[0]->sujId == 0 || !persoHasMenu))
			choice = 0;   // automatic section / menu-less perso → play directly
		else
			choice = showSubjectMenu(options);   // let the player pick a topic
		if (choice >= 0 && !shouldAbort()) {
			ConSection *sec = options[(uint)choice];
			sec->played = true;
			playConSection(*sec);
		}
	}

	_gameVars["clicperso"] = 0;
	_dialBaseAlphaValid = false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
