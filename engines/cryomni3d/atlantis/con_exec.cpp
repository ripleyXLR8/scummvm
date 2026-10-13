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
#include "common/ptr.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "audio/decoders/apc.h"
#include "audio/mixer.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/atlantis/con_eval.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/inventory.h"
#include "cryomni3d/atlantis/puzzle_eclipse.h"
#include "cryomni3d/atlantis/puzzle_ogregame.h"
#include "cryomni3d/atlantis/puzzle_dispatch.h"
#include "cryomni3d/atlantis/puzzle_hens.h"
#include "cryomni3d/atlantis/puzzle_pieces.h"
#include "cryomni3d/atlantis/puzzle_rat.h"
#include "cryomni3d/atlantis/puzzle_snakes.h"
#include "cryomni3d/atlantis/puzzle_throne.h"

namespace CryOmni3D {
namespace Atlantis {

bool CryOmni3DEngine_Atlantis::isPersistentGameVar(const Common::String &key) {
	// FUN_00427634 zeroes the CON variables from slot 7 on (0x0042767f..)
	// on `chapter=`: the first 7 of VARIAS.CON survive, and ZOOM, which it
	// reads before the clear and writes back (0x00427664..0x004276ca).
	static const char *const kPersistent[] = {
		"chapter", "gardecel", "newsound", "flagchp8",
		"bouclecreon", "codetronevu", "nbstatues", "zoom"
	};
	for (const char *p : kPersistent)
		if (key.equalsIgnoreCase(p))
			return true;
	return false;
}

void CryOmni3DEngine_Atlantis::loadConScript(int chapter) {
	// FUN_00427634 restarts ChapterTime before loading the file (0x004276d0).
	_conClocks.chapterStart = g_system->getMillis();

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
	// Subject-enable state is per-chapter; reset it with the variables.
	// The hidden-perso list PERSISTS across chapter switches — it is the
	// exe's persistent list at 0x611010, distinct from the per-chapter CON
	// variable array that FUN_00427634 zeroes.  A chapter's /INIT block
	// carries only the DELTAS (the persos it newly shows/hides), not a
	// complete re-statement: CHAPI001 hides ~42 persos, CHAPI002 re-lists
	// only ~18 of them.  Clearing here would make the ~24 left out of
	// CHAPI002's INIT pop back into view on every /set(chapter=N) — the
	// concrete case is perso 587 (Anaan): hidden by CHAPI001, never re-shown
	// until CHAPI056, but its SPW entry has startActive=true at vue 110 in
	// chapters 17-24.  A genuine new game still starts clean: changeChapter()
	// clears the list before the first loadConScript, and loadGame restores
	// the saved list.
	_sujEnabled.clear();

	if (_conVarNames.empty()) {
		Common::ScopedPtr<Common::SeekableReadStream> v(openBigFileStream(kFileTypeScript, "VARIAS.CON"));
		if (v) {
			Common::Array<char> data(v->size());
			if (data.size())
				v->read(data.begin(), data.size());
			_conVarNames.load(data.begin(), data.size());
		} else {
			warning("loadConScript: no VARIAS.CON");
		}
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
	// FUN_00427634 restarts Time2 after the /INIT block (0x0042779e).
	_conClocks.time2Start = g_system->getMillis();
	_conClocks.time2Offset = 0;
	debugC(1, kDebugScript, "loadConScript: '%s' -> %u sections",
	      name.c_str(), _conScript.sections().size());
}

void CryOmni3DEngine_Atlantis::execConInitCommands() {
	const bool wasRunning = _conInitRunning;
	_conInitRunning = true;
	for (uint i = 0; i < _conScript.initCmds().size(); i++)
		execConSetCommand(_conScript.initCmds()[i], _conScript.initCmdOffsets()[i]);
	_conInitRunning = wasRunning;
}

int CryOmni3DEngine_Atlantis::resolveSpeakerPersoId(const Common::String &speaker) const {
	// atlantis.exe FUN_00428f58: memcmp of the text after '[' against each
	// literal of the chain below, in code order (case-sensitive); every test
	// runs and a hit overwrites the previous result, so the LAST match wins
	// (e.g. "Seth" -> 513 then 514).  The literals contain no ',' or ']', so
	// comparing the speaker token is the same as comparing the raw text.
	// Chain listed by atlantis-re tools/emu/video_dialog_speakers.py --table.
	struct SpeakerEntry { const char *lit; uint len; int persoId; };
	static const SpeakerEntry kSpeakerChain[] = {
		{ "Agat", 4, 520 }, { "agat", 4, 520 }, { "Lasc", 4, 521 }, { "lasc", 4, 521 },
		{ "Melj", 4, 522 }, { "melj", 4, 522 }, { "Sorc", 4, 567 }, { "sorc", 4, 567 },
		{ "Gard", 4, 604 }, { "gard", 4, 604 }, { "Sold", 4, 604 }, { "Pech", 4, 524 },
		{ "pech", 4, 524 }, { "Acty", 4, 524 }, { "acty", 4, 524 }, { "Creo", 4, 525 },
		{ "creo", 4, 525 }, { "Chas", 4, 540 }, { "chas", 4, 540 }, { "Passant9", 8, 602 },
		{ "passant9", 8, 602 }, { "Carv", 4, 602 }, { "carv", 4, 602 }, { "Passant6", 8, 601 },
		{ "passant6", 8, 601 }, { "Passante7", 9, 606 }, { "passante7", 9, 606 }, { "Hect", 4, 543 },
		{ "hect", 4, 543 }, { "Cuis", 4, 551 }, { "cuis", 4, 551 }, { "Seth", 4, 513 },
		{ "seth", 4, 513 }, { "Seth", 4, 514 }, { "seth", 4, 514 }, { "Rein", 4, 560 },
		{ "rein", 4, 560 }, { "Reinem", 6, 561 }, { "reinem", 6, 561 }, { "Reinep", 6, 562 },
		{ "reinep", 6, 562 }, { "Garc", 4, 526 }, { "garc", 4, 526 }, { "Servan", 6, 609 },
		{ "servan", 6, 609 }, { "Servag", 6, 582 }, { "servag", 6, 582 }, { "Chie", 4, 650 },
		{ "chie", 4, 650 }, { "Anna", 4, 663 }, { "anna", 4, 663 }, { "Escl", 4, 597 },
		{ "escl", 4, 597 }, { "Enne", 4, 667 }, { "Enne", 4, 667 }, { "Hona", 4, 682 },
		{ "hona", 4, 682 }, { "Sama", 4, 683 }, { "sama", 4, 683 }, { "Naka", 4, 684 },
		{ "naka", 4, 684 }, { "Lona", 4, 576 }, { "lona", 4, 576 }, { "JPil", 4, 571 },
		{ "jpil", 4, 571 }, { "Gimb", 4, 563 }, { "gimb", 4, 563 },
	};
	int persoId = 0;
	for (const SpeakerEntry &e : kSpeakerChain) {
		if (speaker.size() >= e.len && memcmp(speaker.c_str(), e.lit, e.len) == 0)
			persoId = e.persoId;
	}
	return persoId;
}

void CryOmni3DEngine_Atlantis::playPuzzleSound(const char *name) {
	Common::String fname = Common::String::format("%s.APC", name);
	Common::SeekableReadStream *apcFile = openBigFileStream(kFileTypeSound, fname);
	if (!apcFile) {
		warning("playPuzzleSound: cannot open WAV\\%s", fname.c_str());
		return;
	}
	Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
	if (!apc) {
		delete apcFile;
		return;
	}
	int64 remaining = apcFile->size() - apcFile->pos();
	if (remaining > 0) {
		byte *buf = new byte[remaining];
		apcFile->read(buf, (uint32)remaining);
		apc->queuePacket(new Common::MemoryReadStream(buf, remaining, DisposeAfterUse::YES));
	}
	apc->finish();
	delete apcFile;
	Audio::SoundHandle handle;
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, apc);
}

void CryOmni3DEngine_Atlantis::playPuzzle(int puzzleId) {
	// Dispatch to the per-puzzle handler.  Puzzles not yet implemented
	// fall back to the auto-win stub so their CHAPI script chains keep
	// progressing (most of them gate further story content behind
	// FinPuzzle=255, e.g. CHAPI012's /sel(FinPuzzle=255)&(Vue=N)&...).
	int finPuzzle = 255;
	// FUN_0043a1e0's checks (puzzle_dispatch.h): StartPuzzle 15 empties the
	// hand; puzzles 0..5, 8, 9 and above 14 refuse an object in hand (1);
	// numbers above 16 have no puzzle (0).
	int held = _inventory.selectedObject() ? (int)_inventory.selectedObject()->idOBJ() : 0;
	const int check = PuzzleDispatch::check(puzzleId, held);
	if (held == 0 && _inventory.selectedObject() != nullptr) {
		_inventory.setSelectedObject(nullptr);
		_gameVars["objetenmain"] = 0;
	}
	if (check != PuzzleDispatch::kRun) {
		_gameVars["finpuzzle"] = check;
		return;
	}
	switch (puzzleId) {
	case 0: {
		// Celestial alignment ("eclipse on Atlantis") at CHAPI012 vue 221.
		EclipsePuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	case 1: {
		// The hens on the levers at CHAPI034 (FUN_004441e8).
		HensPuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	case 2: {
		// The rat-hunter's "ogre & hero" board pursuit at CHAPI018 place 290.
		OgreGamePuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	case 3: {
		// Six rotating pieces at CHAPI029 (FUN_004459c0).
		RotatingPiecesPuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	case 4: {
		// The rat's run at CHAPI041 (FUN_00438bf0).
		RatPuzzle puz(this);
		finPuzzle = puz.play();
		break;
	}
	case 5: {
		// The throne lock at CHAPI044 (FUN_0043fcb4).
		ThronePuzzle puz(this);
		finPuzzle = puz.play();
		break;
	}
	case 8: {
		// Four snakes in a maze at CHAPI034 (FUN_0043b0f4).
		SnakesPuzzle puz(this);
		finPuzzle = puz.run();
		break;
	}
	default:
		debugC(1, kDebugScript, "playPuzzle: no handler for puzzle %d, auto-win", puzzleId);
		break;
	}
	_gameVars["finpuzzle"] = finPuzzle;
}

void CryOmni3DEngine_Atlantis::runPuzzleZone(int puzzleId) {
	// atlantis.exe FUN_004240fc: FinPuzzle = the puzzle's result, the view is
	// reloaded, StartPuzzle = 0, one /sel scan, then FinPuzzle = 0.
	playPuzzle(puzzleId);
	if (shouldAbort())
		return;
	if (_currentPlace && loadCycloHNM(_currentPlace->cycloHnm)) {
		_omni3dMan.setSourceSurface(&_warpSurface);
		setupPalette(_mainPalette, 0, 256);
		compositeNPCSprites();
	}
	_gameVars["startpuzzle"] = 0;
	runSelScan();
	_gameVars["finpuzzle"] = 0;
}

void CryOmni3DEngine_Atlantis::runSelScan() {
	// FUN_004277c8: the first /sel entry whose condition holds runs.
	for (ConSection &sec : _conScript.sections()) {
		if (!sec.isEntry || sec.isCon || sec.isTimer)
			continue;
		if (!evalSelSection(sec, 0, (int)_currentPlaceId))
			continue;
		debugC(1, kDebugScript, "runSelScan: section %d", sec.id);
		playConSection(sec);
		break;
	}
}

void CryOmni3DEngine_Atlantis::execConInventCommand(uint objId, int value) {
	// atlantis.exe FUN_0042b328 appends a record without a duplicate check;
	// FUN_0042b3ec removes the first record with that id and shifts the rest
	// down.  See atlantis-re: functions/graphics-FUN_0042b328.md.
	if (value) {
		Object *obj = findInventoryObject(_objects, objId);
		if (obj) {
			inventoryAppend(_inventory, obj);
			// Remember the just-given object so it shows centre-bottom for
			// the rest of the dialog (atlantis.exe DAT_0049619c); the sprite
			// index is the object's slot in _objects (parallel to _objSprites).
			_dialogGivenObjSprite = (int)(obj - &_objects.front());
			debugC(1, kDebugScript, "execConSetCommand: added object %u to inventory", objId);
		} else {
			debugC(1, kDebugScript, "execConSetCommand: invent%u=1 — object not defined", objId);
		}
	} else {
		inventoryRemove(_inventory, objId);
		Object *sel = _inventory.selectedObject();
		if (sel && sel->idOBJ() == objId)
			_inventory.deselectObject();
		debugC(1, kDebugScript, "execConSetCommand: removed object %u from inventory", objId);
	}
}

void CryOmni3DEngine_Atlantis::execConSetCommand(const Common::String &cmd, int32 offset) {
	// Read the command as atlantis.exe FUN_0042a100 does: from the CON text,
	// so that a value search running past the line sees the same bytes.  See
	// atlantis-re: functions/scripts-FUN_0042a100.md.
	ConSetCommand set;
	if (offset >= 0) {
		parseConSet(_conVarNames, _conScript.text(), (uint32)offset, set);
	} else {
		Common::String line = "/set(" + cmd + ")\r\n";
		Common::Array<char> text(line.c_str(), line.size());
		parseConSet(_conVarNames, text, 0, set);
	}
	if (!set.valid)
		return;

	// ChangeWam=name — load "wam\\name" and make its first place the current
	// one (FUN_0042a100 @ 0x0042a1f8..0x0042a2b6).  Every ChangeWam of the game
	// is followed by a Vue= in the same section, which moves the player; until
	// then no place is entered (as after a sub-WAM swap, executeTransition).
	if (set.changeWam) {
		setupWAMByName(set.wamName);
		_currentPlace = nullptr;
		_currentPlaceId = _wam.getPlaces().empty() ? uint(-1) : _wam.getPlaces()[0].placeId;
		debugC(1, kDebugScript, "execConSetCommand: changewam '%s'", set.wamName.c_str());
		return;
	}

	// inventN=v — v != 0 adds object N, v == 0 removes it (FUN_0042b328).
	if (set.invent) {
		execConInventCommand((uint)set.inventId, set.inventValue);
		return;
	}

	const Common::String &key = set.name;
	const int value = set.value;
	// The raw text after the key, for the commands that read more than one
	// number (EnableSuj,perso=suj,suj...).
	const char *p = cmd.c_str();
	while (*p && !strchr("=<>!#,", *p))
		p++;

	// enableSuj,persoId=suj1,suj2,... / disableSuj,persoId=sujId[,sujId...]
	if (key == "enablesuj" && *p == ',') {
		p++;
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
	if (key == "disablesuj" && *p == ',') {
		p++;
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

	// Time1=N / Time2=N — restart the timer at N seconds; the variable itself
	// is written by updateConClocks (atlantis.exe FUN_0042a100 @ 0x0042a321,
	// 0x0042a392).
	if (key == "time1" || key == "time2") {
		restartConTimer(key == "time1" ? 1 : 2, value);
		return;
	}

	// Disable=N — count a play of section N, which disables /con N
	// (atlantis.exe FUN_0042a100 @ 0x0042a41a); the variable is not stored.
	if (key == "disable") {
		bumpConPlayCounter(value);
		return;
	}

	if (key == "vue") {
		enterPlaceFromScript((uint)value);
		return;
	}

	// abortdepart=1 — cancel the pending place departure.  Fired by the
	// departure /sel sections that lock the player into an area (the
	// catch-all pattern is /sel(departvue>0)&(vue>A)&(vue<B) -> a refusal
	// bark + /set(abortdepart=1)); gameStep sees _nextPlaceId cleared and
	// skips the transition.
	if (key == "abortdepart") {
		_nextPlaceId = uint(-1);
		return;
	}

	// chapter=N — switch the active CON script to chapter N.  This is the
	// story-progression command (e.g. CHAPI013's /014 section fires
	// /set(chapter=14)); it is independent of the WAM — area changes go
	// through changewam.  loadConScript keeps the 7 persistent variables,
	// resets the transient ones and runs the new chapter's /INIT block.
	if (key == "chapter") {
		_gameVars["chapter"] = value;
		if (value > 0 && (uint)value != _currentCONChapter) {
			debugC(1, kDebugScript, "execConSetCommand: switching CON script to chapter %d", value);
			_currentCONChapter = (uint)value;
			// The chapter checkpoint: FUN_00427634 writes it first
			// (0x0042765f), before loading the new CON, so it holds the
			// state where chapter= fired with the new chapter number.  Its
			// /INIT runner FUN_00425eb0 sets the loading flag 0x00496168,
			// which suppresses the write for a chapter= inside /INIT.
			if (!_conInitRunning)
				autosave();
			loadConScript(value);
			// Fire the new chapter's intro /sel sections — canonical example
			// is CHAPI014.CON section 10 (`/sel(initflag=0)` -> /set(Cinema=36)),
			// the throw-success cinematic that plays right after CHAPI013
			// fires /set(chapter=14).  Without this scan the chapter switches
			// silently and the player just lands at the same vue with no
			// cinematic.  Same-place arrival keeps (arriveevue>0) true.
			// FUN_00427634 sets ARRIVEEVUE to LastVue (0x0042777d), then
			// FUN_0042a100 runs the /sel scan FUN_004277c8 (0x0042a60e).
			if (_currentPlaceId != uint(-1))
				runArrivalSelSections((int)_currentPlaceId, (int)_lastVue);
		}
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
		playTransitionVideo(hnm, uint(-1), uint(-1), kCinemaFrameMs);
		return;
	}

	// AngleWarpX=N / AngleWarpY=N — turn the view to N degrees, then store
	// the variable (atlantis.exe FUN_0042a100 @ 0x0042a995, 0x0042a9cf).
	if (key == "anglewarpx")
		_omni3dMan.setAlpha(conAngleRadians(value));
	else if (key == "anglewarpy")
		_omni3dMan.setBeta(conAngleRadians(value));

	// Guests (atlantis.exe FUN_0042a100; atlantis-re:
	// functions/scripts-guests.md).  HideGuest/ShowGuest store no variable.
	if (key == "hideguest" || key == "showguest") {
		_guestsShown = (key == "showguest");
		return;
	}
	if (key == "addguest") {
		addGuest(value);
		_gameVars[key] = 0;
		return;
	}
	if (key == "subguest") {
		subGuest(value);
		_gameVars[key] = 0;
		return;
	}

	// hideperso=N — deactivate every sprite for perso N and
	// add it to the persistent hidden list (atlantis.exe FUN_00427bd0).
	if (key == "hideperso") {
		if (value > 0)
			hidePerso(value);
		return;
	}

	// showperso=N — reactivate every sprite for perso N and clear its hide.
	if (key == "showperso") {
		showPerso(value);
		return;
	}

	// IncPerso=N (N != 0): the Seth row count steps, and returns to 0 when N
	// is 99; the variable returns to 0 (FUN_0042a100 @ 0x0042b07a).
	if (key == "incperso") {
		if (value != 0) {
			_incPersoCount++;
			if (value == 99)
				_incPersoCount = 0;
		}
		_gameVars[key] = 0;
		return;
	}

	// TirFleche=N restarts the arrow frame (0x0042a98e), then the store.
	if (key == "tirfleche")
		_arrowFrame = 0;

	// ArcMagique=N (any value): the magic-bow animation, then the store
	// (FUN_0042a100 @ 0x0042aa09).
	if (key == "arcmagique") {
		playArcMagique();
		_gameVars[key] = value;
		return;
	}

	// atlantis.exe FUN_00428da8 (after each executor line): NEWWSPRANIM=N
	// starts the current view's entry N, STOPWSPRANIM=N stops it (and clears
	// WSPRFRAME); the variable returns to 0.
	if (key == "newwspranim" || key == "stopwspranim") {
		if (value != 0 && _currentPlaceId < _spriteAnimCurrent.size()) {
			bool ok = (key == "newwspranim")
			          ? startPlaceSpriteAnim(_spritePlaceList, (uint16)_currentPlaceId, value,
			                                 _spriteAnimCurrent[_currentPlaceId])
			          : stopPlaceSpriteAnim(_spritePlaceList, (uint16)_currentPlaceId, value);
			if (!ok)
				warning("execConSetCommand: %s=%d: no such sprite in view %u", key.c_str(), value, _currentPlaceId);
			if (key == "stopwspranim")
				_gameVars["wsprframe"] = 0;
			recompositeSpriteLayer();
		}
		_gameVars[key] = 0;
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
	// StartPuzzle=N only arms puzzle N: it runs when zone 1000 + N is clicked
	// (atlantis.exe FUN_004240fc); the variable is stored below.

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

namespace {

// The keys of a CON condition that the engine reads from its live state
// rather than from the stored game variables.
class EngineConEvaluator : public ConEvaluator {
public:
	EngineConEvaluator(const ConVariableNames &names, const Inventory &inventory,
	                   const Common::HashMap<Common::String, int> &vars,
	                   const Common::Array<byte> &placeVisits,
	                   int departZone, int currentVue) :
		ConEvaluator(names, vars, inventory), _inventory(inventory), _placeVisits(placeVisits),
		_departZone(departZone), _currentVue(currentVue) {}

protected:
	bool liveValue(const Common::String &key, int &out) const override {
		if (key == "departvue") {
			out = _departZone;
		} else if (key == "vue") {
			out = _currentVue;
		} else if (key == "objetenmain") {
			// Live held-object id, not the stored game variable: a pickup or
			// the toolbar can change the selection without any /set running.
			Object *sel = _inventory.selectedObject();
			out = (sel && sel->idOBJ() != uint(-1)) ? (int)sel->idOBJ() : 0;
		} else {
			return false;
		}
		return true;
	}

private:
	const Inventory &_inventory;
	const Common::Array<byte> &_placeVisits;
	int _departZone, _currentVue;
};

} // End of anonymous namespace

void CryOmni3DEngine_Atlantis::updateConClocks() {
	// atlantis.exe FUN_0041c75c, at the start of every warp frame.
	const uint32 now = g_system->getMillis();
	_gameVars["random"] = (int)(now & 0xff);
	_gameVars["chaptertime"] = _conClocks.chapterTime(now);
	_gameVars["time1"] = _conClocks.timerValue(1, now);
	_gameVars["time2"] = _conClocks.timerValue(2, now);
}

void CryOmni3DEngine_Atlantis::restartConTimer(int timer, int value) {
	_conClocks.restart(timer, value, g_system->getMillis());
}

void CryOmni3DEngine_Atlantis::rebaseConClocks() {
	// After the toolbar (FUN_0042c59c @ 0x0042c844) and the in-game menu
	// (FUN_0041c838 @ 0x0041cb84): the timers resume from their current
	// values, and ChapterTime starts again from 0.
	_conClocks.rebase(g_system->getMillis(), _gameVars.getValOrDefault("time1", 0),
	                  _gameVars.getValOrDefault("time2", 0));
}

bool CryOmni3DEngine_Atlantis::barFrameTick(bool &played) {
	animateSprites();
	played = runConTimers();
	return shouldAbort() || _gameVars.getValOrDefault("gameover", 0) != 0 ||
	       _gameVars.getValOrDefault("exit", 0) != 0;
}

bool CryOmni3DEngine_Atlantis::runConTimers() {
	bool played = false;
	for (int timer = 1; timer <= 2; timer++) {
		if (_gameVars.getValOrDefault(timer == 1 ? "testtime1" : "testtime2", 0) == 0)
			continue;
		for (ConSection &sec : _conScript.sections()) {
			if (!sec.isEntry || !sec.isTimer || sec.timerNum != timer)
				continue;
			if (!evalSelSection(sec, 0, (int)_currentPlaceId))
				continue;
			debugC(1, kDebugScript, "runConTimers: /tim%d section %d", timer, sec.id);
			// A timer run counts as a play (flag 0x00496174).
			bumpConPlayCounter(sec.id);
			g_system->lockMouse(false);   // leave the warp's mouse-look for the dialog
			playConSection(sec);
			played = true;
			break;
		}
		if (shouldAbort())
			break;
	}
	return played;
}

void CryOmni3DEngine_Atlantis::enterPlaceFromScript(uint placeId) {
	// atlantis.exe FUN_0042a100 @ 0x0042a630..0x0042a749: the sub-WAM swap
	// FUN_00422ad0 (current place, N), the place load FUN_00421b4c without a
	// transition video, ArriveeVue = the previous place, ClicPerso = 0.  The
	// rest of the section then plays in the new place; the place loop
	// FUN_00423ad8 runs the arrival /sel scan when it comes round
	// (ArriveeVue != 0 @ 0x00423b48).
	const uint from = _currentPlaceId;
	for (const AtlantisSubWAM &sub : _wam.getSubWAMs()) {
		if (sub.srcPlaceId == from && sub.dstPlaceId == placeId) {
			Common::String wamName = sub.wamName;   // setupWAMByName clears the list
			setupWAMByName(wamName);
			_currentPlace = nullptr;
			break;
		}
	}
	// LastVue = the place being left, unless loading (0x0042a6a7..0x0042a6b5).
	if (!_conInitRunning)
		_lastVue = from;
	_nextPlaceId = placeId;
	doPlaceChange();
	_gameVars["arriveevue"] = (int)from;
	_gameVars["clicperso"] = 0;
	_arrivalScanPending = true;
	_arrivalFromVue = from;
	debugC(1, kDebugScript, "enterPlaceFromScript: %u -> %u", from, placeId);
}

void CryOmni3DEngine_Atlantis::drawGuests(Graphics::ManagedSurface &dst, int movingSprite, int movingX) {
	// FUN_0042bc58: every record not hidden, at (540 - 100 i, [0x00496880]).
	if (!_guestsShown)
		return;
	for (uint i = 0; i < _guests.count; i++) {
		byte s = _guests.sprite[i];
		if (s != AtlantisGuests::kHidden && s < _guestSprites.size())
			drawSprite(dst, _guestSprites[s], 540 - 100 * (int)i, _guests.portraitY);
	}
	if (movingSprite >= 0 && (uint)movingSprite < _guestSprites.size())
		drawSprite(dst, _guestSprites[movingSprite], movingX, _guests.portraitY);
}

void CryOmni3DEngine_Atlantis::slideGuest(byte sprite, int fromX, int toX, int step) {
	// FUN_0042b480 / FUN_0042b6c0: one redrawn view per 10 px.  The original
	// frame pacing is not established (atlantis-re:
	// functions/scripts-guests.md); the port uses its warp frame delay.
	if (!_warpLoaded)
		return;
	for (int x = fromX; step < 0 ? x > toX : x < toX; x += step) {
		const Graphics::Surface *view = _omni3dMan.getSurface();
		if (!view)
			return;
		Graphics::ManagedSurface frame;
		frame.copyFrom(*view);
		drawGuests(frame, sprite, x);
		g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		if (shouldAbort())
			return;
	}
}

void CryOmni3DEngine_Atlantis::addGuest(int32 guestId) {
	// FUN_0042b480: the record is written, the portrait slides in from the
	// right edge, then the count grows.
	if (_guests.count >= AtlantisGuests::kSlots)
		return;
	AtlantisGuests next = _guests;
	next.add(guestId, _guestSpriteIds);
	slideGuest(next.sprite[_guests.count], 640, 540 - 100 * (int)_guests.count, -10);
	_guests = next;
}

void CryOmni3DEngine_Atlantis::talkToGuest(int32 guestId) {
	// FUN_00425828 with 0x00496144 = 1: the other guests are hidden only
	// when the guest has enabled subjects (the loop at 0x0042594d runs on
	// the subject path, after the JBE at 0x0042592a), then restored.
	byte saved[AtlantisGuests::kSlots];
	memcpy(saved, _guests.sprite, sizeof(saved));
	bool hasSubjects = false;
	for (const SujState &ss : _sujEnabled)
		if (ss.persoId == guestId) {
			hasSubjects = !ss.sujs.empty();
			break;
		}
	if (hasSubjects)
		for (uint i = 0; i < _guests.count; i++)
			if (_guests.id[i] != guestId)
				_guests.sprite[i] = AtlantisGuests::kHidden;
	interactNPC(guestId);
	if (_guests.count <= AtlantisGuests::kSlots)
		memcpy(_guests.sprite, saved, sizeof(saved));
}

void CryOmni3DEngine_Atlantis::subGuest(int32 guestId) {
	// FUN_0042b6c0: each matching record is hidden, slides out to the right,
	// then the following records move down.
	for (uint i = 0; i < _guests.count; i++) {
		if (_guests.id[i] != guestId)
			continue;
		byte s = _guests.sprite[i];
		_guests.sprite[i] = AtlantisGuests::kHidden;
		slideGuest(s, 540 - 100 * (int)i, 640, 10);
		_guests.removeAt(i);
	}
}

void CryOmni3DEngine_Atlantis::showPerso(int perso) {
	for (int i = (int)_hiddenPersos.size() - 1; i >= 0; i--)
		if (_hiddenPersos[i] == perso) _hiddenPersos.remove_at(i);
	for (SpritePlaceEntry &e : _spritePlaceList)
		if (e.persoId == perso) e.active = true;
	recompositeSpriteLayer();
	debugC(1, kDebugScript, "showPerso: %d", perso);
}

void CryOmni3DEngine_Atlantis::runConClick() {
	// atlantis.exe FUN_00425cf4 (flag 0x00496170 set: a play is counted).
	struct Ctx {
		static bool test(const ConSection &sec, void *ctx) {
			const CryOmni3DEngine_Atlantis *e = (const CryOmni3DEngine_Atlantis *)ctx;
			return e->evalSelSection(sec, 0, (int)e->_currentPlaceId);
		}
	};
	int idx = findConClickEntry(_conScript, _conPlayCounters, &Ctx::test, this);
	if (idx >= 0) {
		ConSection &sec = _conScript.sections()[idx];
		debugC(1, kDebugScript, "runConClick: section %d", sec.id);
		bumpConPlayCounter(sec.id);
		playConSection(sec);
	}

	// FUN_00425dd0: clicking zone 2000+N with object N in hand puts the
	// object back into the place when the place's sprite list holds it.
	const Object *held = _inventory.selectedObject();
	const int heldId = (held && held->idOBJ() != uint(-1)) ? (int)held->idOBJ() : 0;
	if (_gameVars.getValOrDefault("cliczone", 0) - 2000 == heldId) {
		for (const SpritePlaceEntry &e : _spritePlaceList) {
			if (e.placeId != _currentPlaceId || e.persoId != heldId)
				continue;
			showPerso(heldId);
			inventoryRemove(_inventory, (uint)heldId);
			_inventory.deselectObject();
			setArrowCursor();
			break;
		}
	}

	_gameVars["clicperso"] = 0;
	_gameVars["cliczone"] = 0;
}

void CryOmni3DEngine_Atlantis::bumpConPlayCounter(int section) {
	// A byte: it wraps after 255 plays, as in the original.
	if (section >= 0 && (uint)section < _conPlayCounters.size())
		_conPlayCounters[section]++;
	else
		warning("bumpConPlayCounter: section %d out of range", section);
}

bool CryOmni3DEngine_Atlantis::evalSelSection(const ConSection &sec,
                                               int departZone, int currentVue) const {
	if (sec.condOffset < 0)
		return true;
	EngineConEvaluator eval(_conVarNames, _inventory, _gameVars, _placeVisits,
	                        departZone, currentVue);
	return eval.evaluate(_conScript.text(), (uint32)sec.condOffset);
}

void CryOmni3DEngine_Atlantis::runArrivalSelSections(int arrivedVue, int fromVue) {
	// arriveevue = the place we arrived FROM (so both the marker form
	// (arriveevue>0)&(vue=N) and the specific (arriveevue=FROM)&(vue=TO) work).
	_gameVars["arriveevue"] = (fromVue > 0) ? fromVue : arrivedVue;

	// Single /sel pool, file order, first match fires (atlantis.exe
	// FUN_004280f8).  This is also what runs each chapter's intro section
	// (/sel(initflag=0), the first /sel after /FININIT).
	for (ConSection &sec : _conScript.sections()) {
		// /sel entries have no play counter (atlantis.exe FUN_004280f8).
		if (!sec.isEntry || sec.isCon || sec.isTimer)
			continue;
		if (!evalSelSection(sec, 0, arrivedVue))
			continue;
		debugC(1, kDebugScript, "runArrivalSelSections: vue=%d firing section %d",
		      arrivedVue, sec.id);
		playConSection(sec);
		break;   // first match only
	}

	_gameVars["arriveevue"] = 0;
}

void CryOmni3DEngine_Atlantis::runDepartureSelSections(int departZone) {
	// Same single-fire /sel scan; departure sections gate on (departvue=N) or
	// (departvue>0), arrival-only sections self-exclude (arriveevue is 0 here).
	for (ConSection &sec : _conScript.sections()) {
		if (!sec.isEntry || sec.isCon || sec.isTimer)
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

void CryOmni3DEngine_Atlantis::playArcMagique() {
	// atlantis.exe FUN_0042a100 @ 0x0042aa21..0x0042ac2b: SPRITE\2D\ARCANIM.SPR,
	// frames 0..19 drawn at the mouse position over the view (the place
	// sprites animate meanwhile, FUN_00425304); nothing when the file is
	// missing.  The original presents each frame with no timer (Flip or Blt,
	// FUN_004337d4), so its pace is the display's; the port uses the warp
	// loop's frame pacing.
	Common::Array<SubjSprite> anim;
	loadFontSpriteSet("ARCANIM.SPR", anim);
	if (anim.empty())
		return;
	const Common::Point mouse = getMousePos();
	for (uint i = 0; i < 20 && !shouldAbort(); i++) {
		animateSprites();
		const Graphics::Surface *view = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
		if (!view)
			break;
		Graphics::ManagedSurface frame;
		frame.copyFrom(*view);
		if (i < anim.size())
			drawSprite(frame, anim[i], mouse.x, mouse.y);
		g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		g_system->updateScreen();
		g_system->delayMillis(10);
		pollEvents();
	}
	debugC(1, kDebugScript, "playArcMagique: %u frames", anim.size());
}

void CryOmni3DEngine_Atlantis::hidePerso(int perso) {
	// atlantis.exe FUN_00427bd0.  The original keeps at most 0x100 ids; the
	// shipped scripts hide 145 distinct ids and 16 objects can be picked up,
	// so the limit is never reached.  Its second table (0x00555d10, the
	// transition SPF sprites) is filtered through _hiddenPersos at playback,
	// which matches its flags: FUN_00427da8 clears them for hidden ids when
	// the table is built.
	bool found = false;
	for (int h : _hiddenPersos) if (h == perso) { found = true; break; }
	if (!found)
		_hiddenPersos.push_back(perso);
	for (SpritePlaceEntry &e : _spritePlaceList)
		if (e.persoId == perso) e.active = false;
	recompositeSpriteLayer();
	debugC(1, kDebugScript, "execConSetCommand: hide perso %d", perso);
}

void CryOmni3DEngine_Atlantis::clickObject(int objId) {
	// atlantis.exe FUN_004278a0 for an object (0x100..0x1ff): ClicPerso = id,
	// the /con click scan (FUN_00425cf4), nothing more once GameOver is set,
	// then the pick-up of every row objectPickupRows() returns.
	debugC(1, kDebugScript, "clickObject: %d", objId);
	_gameVars["clicperso"] = objId;
	runConClick();
	if (shouldAbort() || _gameVars.getValOrDefault("gameover", 0) != 0)
		return;
	Common::Array<uint> rows;
	objectPickupRows(_spritePlaceList, (uint16)_currentPlaceId, objId, rows);
	for (uint r : rows) {
		const int id = _spritePlaceList[r].persoId;
		const uint icon = _spritePlaceList[r].icon;
		hidePerso(id);
		_gameVars["objetenmain"] = id;
		// The original appends the record (icon, id).  The port's records are
		// catalogue objects: the icon-th one has that id for every object the
		// original's hit-test can return (atlantis-re
		// experiments/2026-10-12-18-object-pickup.md).
		Object *obj = (icon < _objects.size() && (int)_objects[icon].idOBJ() == id)
		              ? &_objects[icon] : findInventoryObject(_objects, (uint)id);
		if (!obj || (int)obj->idCA() != (int)icon)
			warning("clickObject: no catalogue object for id %d icon %u", id, icon);
		bool keep = false;
		if (obj) {
			uint used = 0;
			for (Object *o : _inventory)
				if (o) used++;
			if (used < _inventory.size())
				inventoryAppend(_inventory, obj);
			else
				warning("clickObject: inventory full, object %d not added", id);
			keep = runPickupToolbar(obj);
		}
		if (shouldAbort())
			return;
		// A left click puts the object away; a right click keeps it in hand.
		if (!keep) {
			_gameVars["objetenmain"] = 0;
			_inventory.setSelectedObject(nullptr);
		}
	}
}

void CryOmni3DEngine_Atlantis::interactNPC(int persoId) {
	debugC(1, kDebugScript, "interactNPC: persoId=%d", persoId);
	// Expose clicperso so evalSelSection can match (clicperso=N) conditions.
	_gameVars["clicperso"] = persoId;

	// atlantis.exe FUN_00425828 @ 0x00425867..0x004258b7: with an object in
	// hand there is no conversation; the /con click scan (FUN_00425cf4) runs
	// and the object leaves the hand.
	// The port's ObjetEnMain is the inventory selection (liveValue).
	if (_inventory.selectedObject() != nullptr) {
		runConClick();
		_gameVars["objetenmain"] = 0;
		_inventory.setSelectedObject(nullptr);
		return;
	}
	// New conversation: forget the previous cam and capture the click-time
	// facing for the per-line default angle.
	_lastDialCam = 0;
	_dialBaseAlpha = _omni3dMan.getAlpha();
	_dialBaseAlphaValid = true;

	// atlantis.exe FUN_00425828: when the perso has enabled subjects, the
	// menu lists them in table order (EnableSuj appends, DisableSuj removes
	// and shifts) and is shown first (0x00425930..0x00425a8a).  Then the
	// eligible /con entries are walked in file order (FUN_004281f8) and the
	// first that fits plays (0x00425abe..0x00425b75): an entry with no /suj
	// line always fits; one with a /suj line fits when the perso has no
	// subjects, or when its list holds the chosen subject (FUN_004264a8); a
	// cancelled menu stops the walk at the first /suj entry.
	Common::Array<int> sujs;
	for (const SujState &ss : _sujEnabled)
		if (ss.persoId == persoId) { sujs = ss.sujs; break; }
	const bool persoHasMenu = !sujs.empty();
	int chosen = -1;
	if (persoHasMenu) {
		int idx = showSubjectMenu(sujs);
		if (shouldAbort())
			return;
		chosen = (idx >= 0) ? sujs[(uint)idx] : -1;
	}

	ConSection *play = nullptr;
	for (ConSection &sec : _conScript.sections()) {
		if (!sec.isCon)
			continue;
		// No filter by click variables: FUN_004281f8 hands every /con entry
		// to the evaluator, with ClicPerso = the NPC and ClicZone = the zone
		// under the pointer (set by the warp loop's caller).
		if (!conEntryAllowed(sec, _conPlayCounters))
			continue;
		if (!evalSelSection(sec, 0, (int)_currentPlaceId))
			continue;

		const bool hasSuj = sec.sujId != 0 || !sec.sujIds.empty();
		if (!hasSuj || !persoHasMenu) {
			play = &sec;
			break;
		}
		if (chosen == -1)
			break;
		bool holds = (sec.sujId == chosen);
		for (int s : sec.sujIds)
			if (s == chosen) { holds = true; break; }
		if (holds) {
			play = &sec;
			break;
		}
	}

	if (!play) {
		debugC(1, kDebugScript, "interactNPC: persoId=%d nothing to play (subject %d)", persoId, chosen);
	} else if (!shouldAbort()) {
		// The executor counts the play when it is entered from a /con click
		// (atlantis.exe FUN_0042844c @ 0x00428493).
		bumpConPlayCounter(play->id);
		playConSection(*play);
	}

	_gameVars["clicperso"] = 0;
	_dialBaseAlphaValid = false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
