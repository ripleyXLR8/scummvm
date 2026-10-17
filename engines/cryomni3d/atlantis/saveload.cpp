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

// Save / load.  This starts with the full game-state serialization and the
// per-chapter autosave on plain ScummVM slots; the named multi-episode player
// profiles, the in-game load screen and the GMM save list layer on later.

#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/savefile.h"
#include "common/str.h"
#include "common/textconsole.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/inventory.h"

namespace CryOmni3D {
namespace Atlantis {

// Fixed save-file block layout (1792 bytes), then an 'ATLS' extended block.
// The layout matches the full engine so saves stay forward-compatible; the
// reduced engine writes zeros for the original indexed state it does not model
// (place states, the 100-entry game-variable array) and carries its live state
// in the extended block instead.
static const int32  kSaveSize        = 1792;
static const uint   kSaveDescriptionLen = 20;
static const uint   kWAMNameLen      = 32;
static const uint32 kExtMagic        = 0x41544C53u;  // 'ATLS'
static const uint32 kExtVersion      = 10;
static const uint   kSaveInvBaseSlots = 20;   // inventory slots in the base block

// Player-profile file (cannot collide with the "atlantis.NNNN" save files).
static const char  *kPlayersFile    = "atlantis-players.dat";
// The original's profile file, read when the port's own file is absent; its
// p<L><N>.gam checkpoints are then imported once (importOriginalSaves).
static const char  *kOrigPlayersFile = "players.atl";

// Everything a save holds, in the order it is written (writePortSave).
struct PortSave {
	Common::String name;
	Common::Array<uint32> inventory;     // object index into _objects, -1 = empty
	uint32 chapter = 0;
	Common::String wamName;
	uint32 placeId = 0;
	double alpha = 0.0, beta = 0.0;
	Common::Array<int> hiddenPersos;
	bool keepInitSubjects = false;       // an imported checkpoint: the /INIT's
	Common::Array<Common::Pair<int, Common::Array<uint8> > > subjects;
	Common::Array<Common::Pair<Common::String, int32> > vars;   // the persistent ones
	uint32 conChapter = 0;
	uint32 musicTrack = 0;
	Common::Array<byte> conCounters;
	uint8 guestCount = 0;
	int32 guestId[AtlantisGuests::kSlots] = {};
	uint8 guestSprite[AtlantisGuests::kSlots] = {};
	bool guestsShown = true;
	uint8 incPerso = 0;
	uint32 lastVue = 1;
	uint8 disc = 1;                      // ext v10: 0x00772913, .gam field 0xa4e
};

static void writePortSave(Common::WriteStream *out, const PortSave &s) {
	char saveNameC[kSaveDescriptionLen + 1];
	memset(saveNameC, 0, sizeof(saveNameC));
	strncpy(saveNameC, s.name.c_str(), kSaveDescriptionLen);
	out->write(saveNameC, kSaveDescriptionLen);

	out->writeUint32LE(0);            // dummy padding
	out->writeUint32BE(0);
	out->writeUint32BE(0);
	for (uint i = 0; i < 200; i++)    // dialog variables (reserved)
		out->writeByte(0);

	// Inventory: the first 20 slots × uint32BE object index into _objects
	// (-1 = empty); slots 20.. go to ext v9.
	for (uint i = 0; i < kSaveInvBaseSlots; i++)
		out->writeUint32BE(i < s.inventory.size() ? s.inventory[i] : 0xffffffffu);

	out->writeUint32BE(s.chapter);

	char wamNameC[kWAMNameLen];
	memset(wamNameC, 0, sizeof(wamNameC));
	strncpy(wamNameC, s.wamName.c_str(), kWAMNameLen - 1);
	out->write(wamNameC, kWAMNameLen);

	out->writeUint32BE(s.placeId);
	out->writeDoubleBE(s.alpha);
	out->writeDoubleBE(s.beta);

	for (uint i = 0; i < 256; i++)    // place states (not modelled yet)
		out->writeUint32BE(0);
	for (uint i = 0; i < 100; i++)    // indexed game variables (not modelled yet)
		out->writeUint32BE(0);

	// Extended block: the live CON-script state.
	out->writeUint32BE(kExtMagic);
	out->writeUint32BE(kExtVersion);

	out->writeUint32BE((uint32)s.hiddenPersos.size());
	for (int pid : s.hiddenPersos)
		out->writeSint32BE(pid);

	// The subject table as (perso, count, ids) for the non-empty records;
	// 0xffffffff: keep the ones the chapter's /INIT sets (an imported
	// checkpoint: the original does not save the table).
	if (s.keepInitSubjects) {
		out->writeUint32BE(0xffffffffu);
	} else {
		out->writeUint32BE((uint32)s.subjects.size());
		for (uint i = 0; i < s.subjects.size(); i++) {
			out->writeSint32BE(s.subjects[i].first);
			out->writeUint32BE((uint32)s.subjects[i].second.size());
			for (uint8 v : s.subjects[i].second)
				out->writeSint32BE(v);
		}
	}

	// Named game variables — only the 7 persistent ones (VARIAS.CON idx 0-6);
	// the rest are chapter-transient and the original never saves them.
	out->writeUint32BE((uint32)s.vars.size());
	for (uint i = 0; i < s.vars.size(); i++) {
		const Common::String &key = s.vars[i].first;
		out->writeUint32BE((uint32)key.size());
		out->write(key.c_str(), key.size());
		out->writeSint32BE(s.vars[i].second);
	}

	out->writeUint32BE(s.conChapter);    // ext v2: active CHAPI*.CON
	// ext v3: the playing music track.  startmusik/stopmusik own the
	// selection; it is not a game variable, so persist it explicitly.
	out->writeUint32BE(s.musicTrack);
	out->writeUint32BE(0);               // ext v4: shown-perso count (not modelled yet)
	// ext v5: the /con play counters (the original saves the same 0x5dc
	// bytes, FUN_0042d6b8).
	out->write(s.conCounters.data(), s.conCounters.size());
	// ext v6: the guests (the original saves the list, FUN_0042d6b8 @
	// 0x0042db2a) and the shown flag.
	out->writeByte(s.guestCount);
	for (uint i = 0; i < AtlantisGuests::kSlots; i++) {
		out->writeSint32BE(s.guestId[i]);
		out->writeByte(s.guestSprite[i]);
	}
	out->writeByte(s.guestsShown ? 1 : 0);
	// ext v7: the IncPerso count (the original's .gam keeps it at 0x11e).
	out->writeByte(s.incPerso);
	// ext v8: LastVue (0x0049692c, .gam field 0x116), restored by a load.
	out->writeUint32BE(s.lastVue);
	// ext v9: inventory slots 20.. (the original's table holds 30 records,
	// 0x006ce138, all in its checkpoint).
	const uint32 extra = s.inventory.size() > kSaveInvBaseSlots ? s.inventory.size() - kSaveInvBaseSlots : 0;
	out->writeUint32BE(extra);
	for (uint i = kSaveInvBaseSlots; i < s.inventory.size(); i++)
		out->writeUint32BE(s.inventory[i]);
	// ext v10: the disc in use (the original's .gam keeps it at 0xa4e).
	out->writeByte(s.disc);
}

void CryOmni3DEngine_Atlantis::saveGame(uint saveNum, const Common::String &saveName) {
	Common::String saveFileName = getSaveStateName((int)saveNum - 1);
	Common::OutSaveFile *out = _saveFileMan->openForSaving(saveFileName);
	if (!out) {
		warning("saveGame: cannot open '%s' for writing", saveFileName.c_str());
		return;
	}
	PortSave s;
	s.name = saveName;
	for (uint i = 0; i < _inventory.size(); i++)
		s.inventory.push_back(_inventory[i] ? (uint32)(_inventory[i] - _objects.begin()) : 0xffffffffu);
	s.chapter = _currentChapter;
	s.wamName = _currentWAMName;
	s.placeId = _currentPlaceId;
	s.alpha = _omni3dMan.getAlpha();
	s.beta = _omni3dMan.getBeta();
	s.hiddenPersos = _hiddenPersos;
	for (uint r = 0; r < ConSubjects::kRecords; r++) {
		const Common::Array<uint8> &sujs = _subjects.subjects(0x200 + (int)r);
		if (!sujs.empty())
			s.subjects.push_back(Common::Pair<int, Common::Array<uint8> >(0x200 + (int)r, sujs));
	}
	for (const auto &kv : _gameVars)
		if (isPersistentGameVar(kv._key))
			s.vars.push_back(Common::Pair<Common::String, int32>(kv._key, kv._value));
	s.conChapter = _currentCONChapter;
	s.musicTrack = (uint32)_musicTrackId;
	s.conCounters = _conPlayCounters;
	s.guestCount = (uint8)_guests.count;
	for (uint i = 0; i < AtlantisGuests::kSlots; i++) {
		s.guestId[i] = _guests.id[i];
		s.guestSprite[i] = _guests.sprite[i];
	}
	s.guestsShown = _guestsShown;
	s.incPerso = _incPersoCount;
	s.lastVue = (uint32)_lastVue;
	s.disc = _currentDisc;
	writePortSave(out, s);
	out->finalize();
	delete out;
	debugC(1, kDebugScript, "saveGame: wrote slot %u ('%s')", saveNum, saveName.c_str());
}

void CryOmni3DEngine_Atlantis::importOriginalSaves() {
	// Only once: the port's players file does not exist yet.
	{
		Common::InSaveFile *own = _saveFileMan->openForLoading(kPlayersFile);
		if (own) {
			delete own;
			return;
		}
	}
	char letters[kMaxPlayers];
	{
		Common::InSaveFile *orig = _saveFileMan->openForLoading(kOrigPlayersFile);
		if (!orig)
			return;
		const bool ok = readOriginalPlayersLetters(*orig, letters);
		delete orig;
		if (!ok)
			return;
	}
	loadConVarNames();
	uint imported = 0;
	for (uint p = 0; p < kMaxPlayers; p++) {
		if (_players[p].empty())
			continue;
		for (uint ch = 2; ch < kPlayerSlotStride; ch++) {
			const Common::String fileName = Common::String::format("p%c%u.gam", letters[p], ch);
			Common::InSaveFile *in = _saveFileMan->openForLoading(fileName);
			if (!in)
				continue;
			Common::Array<byte> data((uint)in->size());
			if (data.size())
				in->read(data.data(), data.size());
			delete in;
			OrigCheckpoint cp;
			if (!parseOriginalCheckpoint(data.data(), data.size(), cp) || cp.chapter != ch) {
				warning("importOriginalSaves: %s is not a checkpoint of chapter %u", fileName.c_str(), ch);
				continue;
			}
			const int slot = episodeSaveSlot(p, ch);
			if (saveSlotExists(slot))
				continue;
			PortSave s;
			s.name = episodeName(ch);
			for (const OrigCheckpoint::Record &r : cp.inventory) {
				// the record's icon indexes the catalogue for every object the
				// original can hand out (experiments/2026-10-12-18)
				Object *obj = (r.sprite < _objects.size() && (int32)_objects[r.sprite].idOBJ() == r.id)
				              ? &_objects[r.sprite] : findInventoryObject(_objects, (uint)r.id);
				if (obj)
					s.inventory.push_back((uint32)(obj - &_objects.front()));
				else
					warning("importOriginalSaves: %s: no object %d", fileName.c_str(), r.id);
			}
			// The port's base WAM chapter (its saves keep 1 for the whole game;
			// the area WAM below replaces it on load) and the area WAM.
			s.chapter = 1;
			s.wamName = cp.wamName;
			s.placeId = (uint32)cp.placeId;
			// The original's load keeps the view angles it has; the
			// chapter's /INIT usually sets them (AngleWarpX).
			s.alpha = s.beta = 0.0;
			s.hiddenPersos = cp.hiddenPersos;
			s.keepInitSubjects = true;
			for (uint v = 0; v < 7 && v < _conVarNames.size(); v++) {
				Common::String key = _conVarNames.name(v);
				key.toLowercase();
				s.vars.push_back(Common::Pair<Common::String, int32>(key, cp.vars[v]));
			}
			s.conChapter = ch;
			s.musicTrack = 0;   // not in the checkpoint: the chapter's script starts it
			s.conCounters = cp.conCounters;
			s.conCounters.resize(kConPlayCounterCount, 0);
			s.guestCount = (uint8)cp.guestCount;
			for (uint g = 0; g < cp.guestCount && g < AtlantisGuests::kSlots; g++) {
				s.guestId[g] = cp.guestId[g];
				s.guestSprite[g] = cp.guestSprite[g];
			}
			s.incPerso = cp.incPerso;
			s.lastVue = (uint32)cp.lastVue;
			s.disc = cp.disc;
			Common::OutSaveFile *out = _saveFileMan->openForSaving(getSaveStateName(slot));
			if (!out)
				continue;
			writePortSave(out, s);
			out->finalize();
			delete out;
			imported++;
		}
	}
	// The port's players file now exists: the import does not run again.
	savePlayers();
	debugC(1, kDebugScript, "importOriginalSaves: %u checkpoints imported", imported);
}

bool CryOmni3DEngine_Atlantis::loadGame(uint saveNum) {
	// FUN_0042de5c @ 0x0042e309: the ambient sounds stop before the WAM loads.
	stopAmbient();
	Common::String saveFileName = getSaveStateName((int)saveNum - 1);
	Common::InSaveFile *in = _saveFileMan->openForLoading(saveFileName);
	if (!in) {
		warning("loadGame: cannot open '%s'", saveFileName.c_str());
		return false;
	}
	if (in->size() < kSaveSize) {
		warning("loadGame: '%s' too small (%d bytes)", saveFileName.c_str(), (int)in->size());
		delete in;
		return false;
	}

	char saveNameC[kSaveDescriptionLen];
	in->read(saveNameC, sizeof(saveNameC));
	(void)in->readUint32LE();
	(void)in->readUint32BE();
	(void)in->readUint32BE();
	for (uint i = 0; i < 200; i++)
		(void)in->readByte();

	// Inventory object indices — captured now, applied after the WAM/CON
	// re-entry below (setupChapterWAM on chapter 1 would otherwise clear it).
	Common::Array<uint> savedInv;
	for (uint i = 0; i < kSaveInvBaseSlots; i++)
		savedInv.push_back(in->readUint32BE());

	uint32 chapter = in->readUint32BE();
	char wamNameC[kWAMNameLen];
	in->read(wamNameC, kWAMNameLen);
	wamNameC[kWAMNameLen - 1] = '\0';
	uint32 placeId = in->readUint32BE();
	double alpha = in->readDoubleBE();
	double beta  = in->readDoubleBE();

	for (uint i = 0; i < 256; i++)    // place states (skipped)
		(void)in->readUint32BE();
	for (uint i = 0; i < 100; i++)    // indexed game variables (skipped)
		(void)in->readUint32BE();

	// Extended block.
	Common::Array<int> savedHiddenPersos;
	ConSubjects savedSubjects;
	bool keepInitSubjects = false;
	Common::HashMap<Common::String, int> savedGameVars;
	uint32 savedCONChapter = 0;
	Common::Array<byte> savedConCounters;   // empty: saved before ext v5
	AtlantisGuests savedGuests;            // ext v6
	bool savedGuestsShown = true;
	bool hasGuests = false;
	uint8 savedIncPerso = 0;
	int savedMusicTrack = 0;
	int32 savedLastVue = -1;                // ext v8
	int savedDisc = -1;                     // ext v10
	bool hasExt = (in->size() > kSaveSize);
	if (hasExt) {
		uint32 magic = in->readUint32BE();
		uint32 ver   = in->readUint32BE();
		if (magic != kExtMagic || ver < 1) {
			warning("loadGame: unrecognised extended block (magic=%08x ver=%u)", magic, ver);
			hasExt = false;
		} else {
			uint32 nHidden = in->readUint32BE();
			for (uint32 i = 0; i < nHidden && !in->eos(); i++)
				savedHiddenPersos.push_back(in->readSint32BE());

			uint32 nSuj = in->readUint32BE();
			if (nSuj == 0xffffffffu) {   // an imported checkpoint: keep /INIT's
				keepInitSubjects = true;
				nSuj = 0;
			}
			for (uint32 i = 0; i < nSuj && !in->eos(); i++) {
				const int32 persoId = in->readSint32BE();
				uint32 nK = in->readUint32BE();
				Common::Array<uint8> sujs;
				for (uint32 j = 0; j < nK && !in->eos(); j++)
					sujs.push_back((uint8)in->readSint32BE());
				savedSubjects.setSubjects(persoId, sujs);
			}

			uint32 nVars = in->readUint32BE();
			for (uint32 i = 0; i < nVars && !in->eos(); i++) {
				uint32 klen = in->readUint32BE();
				if (klen > 256) break;
				char kbuf[257];
				in->read(kbuf, klen);
				kbuf[klen] = '\0';
				int32 val = in->readSint32BE();
				savedGameVars[Common::String(kbuf)] = val;
			}
			if (ver >= 2 && !in->eos())
				savedCONChapter = in->readUint32BE();
			if (ver >= 3 && !in->eos())
				savedMusicTrack = (int)in->readUint32BE();
			// ext v4 shown-persos is not modelled yet — ignored.
			if (ver >= 4 && !in->eos())
				(void)in->readUint32BE();
			if (ver >= 5 && !in->eos()) {
				savedConCounters.resize(kConPlayCounterCount);
				in->read(savedConCounters.begin(), savedConCounters.size());
			}
			if (ver >= 6 && !in->eos()) {
				hasGuests = true;
				savedGuests.count = MIN<uint>(in->readByte(), AtlantisGuests::kSlots);
				for (uint i = 0; i < AtlantisGuests::kSlots; i++) {
					savedGuests.id[i] = in->readSint32BE();
					savedGuests.sprite[i] = in->readByte();
				}
				savedGuestsShown = in->readByte() != 0;
			}
			if (ver >= 7 && !in->eos())
				savedIncPerso = in->readByte();
			if (ver >= 8 && !in->eos())
				savedLastVue = (int32)in->readUint32BE();
			if (ver >= 9 && !in->eos()) {
				const uint32 extra = in->readUint32BE();
				for (uint32 i = 0; i < extra && i < 64 && !in->eos(); i++)
					savedInv.push_back(in->readUint32BE());
			}
			if (ver >= 10 && !in->eos())
				savedDisc = in->readByte();
		}
	}
	delete in;

	// Restore in FUN_0042de5c's order: the checkpoint's state is read first
	// (steps 1-3), then the saved WAM and place are set up (step 6), then
	// the chapter change FUN_00427634 runs (step 7): the transient variables
	// are zeroed, the subject table cleared, the CON loaded and its /INIT run
	// on top of the loaded state.  The original never goes through chapter
	// 1's WAM or CON here.
	_currentChapter = chapter;
	// Step 5: the saved disc, when it is not the current one (@ 0x0042e330:
	// FUN_0041eda8(disc - 1)).  Saves older than ext v10 have none.
	if (savedDisc >= 0 && savedDisc != _currentDisc)
		changeDisc((uint)(savedDisc - 1) & 0xff);
	// The hidden-perso list is one of the 25 fields read before the WAM load
	// (steps 2 and 6), whose list parse leaves its persos' rows inactive
	// (FUN_00424a20 -> FUN_00427d30).
	_hiddenPersos.clear();
	if (hasExt)
		_hiddenPersos = savedHiddenPersos;
	Common::String wamName(wamNameC);
	if (!wamName.empty())
		setupWAMByName(wamName);   // FUN_00421ebc on the saved WAM name
	if (wamName.empty() || !wamName.equalsIgnoreCase(_currentWAMName))
		setupChapterWAM((int)chapter);   // saves without a WAM name

	// FUN_0042de5c restores LastVue from the checkpoint (a successful load
	// jumps over the stores of 1 at 0x0042e285 / 0x0042e29e, which are for a
	// missing file: @ 0x0042e247); its chapter change FUN_00427634 copies
	// LastVue into ArriveeVue (0x0042777d) for the place loop's arrival scan
	// (_arrivalAfterLoad below; the current place set here is not the scan's
	// origin).  Saves before ext v8 did not keep it: 1, the earlier behaviour.
	_lastVue = savedLastVue >= 0 ? (uint)savedLastVue : 1;
	_currentPlaceId = 1;
	// The view angles are not in the checkpoint and FUN_0042de5c does not
	// write them: the live ones stay, unless the /INIT below sets AngleWarpX /
	// AngleWarpY (0x0042a9a9 / 0x0042a9e3).  72 of the 74 original checkpoints
	// and the no-file branch keep them (atlantis-re tools/emu/ui_load_state.py).
	(void)alpha;
	(void)beta;

	// Zero every persistent var before applying the save's (a persistent var
	// live now but absent from the save was 0 when written).
	{
		Common::Array<Common::String> persistKeys;
		for (const auto &kv : _gameVars)
			if (isPersistentGameVar(kv._key))
				persistKeys.push_back(kv._key);
		for (const Common::String &k : persistKeys)
			_gameVars[k] = 0;
	}

	// Play counters: from the save, or zero for saves written before they
	// were kept.
	for (uint i = 0; i < _conPlayCounters.size(); i++)
		_conPlayCounters[i] = (i < savedConCounters.size()) ? savedConCounters[i] : 0;

	// Guests: from the save, or none for saves written before they were kept.
	_guests = hasGuests ? savedGuests : AtlantisGuests();
	_guestsShown = hasGuests ? savedGuestsShown : true;
	_incPersoCount = savedIncPerso;

	// The saved subjects are not used: the original does not save the table
	// and its chapter change clears it before /INIT (memset 0x00602f10).
	(void)savedSubjects;
	(void)keepInitSubjects;
	if (hasExt) {
		for (const auto &kv : savedGameVars)
			if (isPersistentGameVar(kv._key))
				_gameVars[kv._key] = kv._value;
	}
	// Step 4 (both paths, after the reads): ZOOM = 0 and NEWSOUND = 0
	// (stores @ 0x0042e2b5 / 0x0042e2d1) and the last started NewSound
	// 0x006cd87c = 0, before the chapter change polls NewSound
	// (FUN_00427634 @ 0x0042e390).  A NewSound held by the checkpoint is
	// therefore never played on a reload.
	// See atlantis-re: functions/audio-FUN_00432a64.md, experiments/2026-10-15-77.
	_gameVars["zoom"] = 0;
	_gameVars["newsound"] = 0;
	_newSoundLast = 0;

	// The saved inventory, before /INIT (which may change it).
	for (uint i = 0; i < _inventory.size(); i++) {
		uint objId = i < savedInv.size() ? savedInv[i] : uint(-1);   // older saves: 20 slots
		if (objId >= _objects.size())
			objId = uint(-1);
		_inventory[i] = (objId != uint(-1)) ? (_objects.begin() + objId) : nullptr;
	}
	// Saves written before the inventory became a compact list can have holes.
	inventoryCompact(_inventory);

	// FUN_00427634: the CON of the saved chapter, its /INIT on the loaded
	// state (loadConScript keeps the persistent variables and clears the
	// transient ones and the subjects).
	if (savedCONChapter >= 1) {
		_currentCONChapter = savedCONChapter;
		loadConScript((int)savedCONChapter);
	}
	// Step 7: CHAPTER = the chapter byte after the /INIT (the checkpoint's
	// variable 0 holds the chapter left: the writer runs before the store).
	if (_currentCONChapter >= 1)
		_gameVars["chapter"] = (int)_currentCONChapter;

	_nextPlaceId = placeId;
	_arrivalScanPending = false;   // VUE = the loaded place (0x0042777d area)
	// The place loop's first arrival scan reads ARRIVEEVUE = the restored
	// LastVue, and does not run when it is 0 (0x00423b48): see gameStep.
	_arrivalAfterLoad = true;
	// FUN_0042de5c @ 0x0042e2ec: the bar shows records 0..8 again.
	_toolbar.resetInventoryFirst();

	// The music: FUN_0042de5c does not stop or start it (the track playing
	// before the load goes on, with its handle 0x006ce0c0 and track
	// 0x00772915; only the /INIT's StartMusik / StopMusik change it); step 8
	// sets the music sample to the music volume and the current volume
	// 0x00495e71 to it (0x0042e423..0x0042e499).  The port's save keeps a
	// track number, unused.
	(void)savedMusicTrack;
	{
		Common::StackLock lock(_musicFade.mutex);
		_musicFade.cur = _musicFade.vol;
	}

	_inventory.deselectObject();

	// The player's chapter byte (0x006ce246) and players.atl are not touched
	// here: FUN_0042de5c reads the byte only; a row click of the load picker
	// sets it, and only the checkpoint writer FUN_0042d6b8 and the player
	// screen FUN_0041dea4 write the file.

	debugC(1, kDebugScript, "loadGame: restored slot %u (chapter %u, place %u)",
	      saveNum, chapter, placeId);
	return true;
}

void CryOmni3DEngine_Atlantis::autosave() {
	// One named save per episode, in the current player's slot block.  The
	// original writes one on every chapter change, whatever its EPI.TXT name.
	uint chapter = _currentCONChapter;
	if (!checkpointWritten(chapter))
		return;
	Common::String name = episodeName(chapter);
	saveGame((uint)episodeSaveSlot((uint)_currentPlayer, chapter) + 1, name);
	if (_currentPlayer >= 0 && _currentPlayer < (int)kMaxPlayers) {
		_playerChapter[_currentPlayer] = (int)chapter;
		savePlayers();
	}
}

// --- Player profiles + episode names --------------------------------------

void CryOmni3DEngine_Atlantis::loadPlayers() {
	for (uint i = 0; i < kMaxPlayers; i++) {
		_players[i].clear();
		_playerChapter[i] = 0;
	}
	Common::InSaveFile *in = _saveFileMan->openForLoading(kPlayersFile);
	if (!in)
		in = _saveFileMan->openForLoading(kOrigPlayersFile);
	if (!in)
		return;
	if (!readPlayersFile(*in, _players, _playerChapter, _newPlayerMarker))
		warning("loadPlayers: unrecognised players file");
	delete in;
}

void CryOmni3DEngine_Atlantis::savePlayers() {
	Common::OutSaveFile *out = _saveFileMan->openForSaving(kPlayersFile, false);
	if (!out) {
		warning("savePlayers: cannot write %s", kPlayersFile);
		return;
	}
	writePlayersFile(*out, _players, _playerChapter);
	out->finalize();
	delete out;
}

void CryOmni3DEngine_Atlantis::deletePlayerSaves(uint player) {
	for (uint i = 0; i < kPlayerSlotStride; i++)
		_saveFileMan->removeSavefile(getSaveStateName(episodeSaveSlot(player, i)));
	if (player < kMaxPlayers)
		_playerChapter[player] = 0;
}

void CryOmni3DEngine_Atlantis::deletePlayer(uint player) {
	// FUN_0041e8a8: the deleted player's saves go, the later players move up
	// one slot (names, chapters and, here, their save files) and the last
	// slot is freed, so a player created afterwards comes last.
	if (player >= kMaxPlayers)
		return;
	deletePlayerSaves(player);
	for (uint q = player + 1; q < kMaxPlayers; q++) {
		for (uint i = 0; i < kPlayerSlotStride; i++) {
			const int from = episodeSaveSlot(q, i), to = episodeSaveSlot(q - 1, i);
			if (saveSlotExists(from))
				_saveFileMan->renameSavefile(getSaveStateName(from), getSaveStateName(to));
		}
		_players[q - 1] = _players[q];
		_playerChapter[q - 1] = _playerChapter[q];
	}
	_players[kMaxPlayers - 1].clear();
	_playerChapter[kMaxPlayers - 1] = 0;
}

bool CryOmni3DEngine_Atlantis::saveSlotExists(int absSlot) const {
	Common::InSaveFile *in = _saveFileMan->openForLoading(getSaveStateName(absSlot));
	if (!in)
		return false;
	delete in;
	return true;
}

uint CryOmni3DEngine_Atlantis::playerResumeSave(uint player) const {
	if (player >= kMaxPlayers)
		return 0;
	int ch = _playerChapter[player];
	if (ch >= 1 && ch < (int)kPlayerSlotStride) {
		int slot = episodeSaveSlot(player, (uint)ch);
		if (saveSlotExists(slot))
			return (uint)slot + 1;
	}
	// Fallback: a save written by the pre-episode build sat at local slot 0.
	int legacy = episodeSaveSlot(player, 0);
	if (saveSlotExists(legacy))
		return (uint)legacy + 1;
	return 0;
}

bool CryOmni3DEngine_Atlantis::playerHasSave(uint player) const {
	return playerResumeSave(player) != 0;
}

// SPRLIST\EPI.TXT, kept whole: episodeName extracts a chapter's name as the
// original load picker does (saveload_format.cpp).
void CryOmni3DEngine_Atlantis::loadEpisodeNames() {
	_epiText.clear();
	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeSprite, "EPI.TXT"));
	if (!s) {
		warning("loadEpisodeNames: cannot open SPRLIST\\EPI.TXT");
		return;
	}
	_epiText.resize((uint)s->size());
	if (!_epiText.empty())
		s->read(_epiText.data(), _epiText.size());
	debugC(1, kDebugSaveLoad, "loadEpisodeNames: %u bytes", _epiText.size());
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

namespace CryOmni3D {
namespace Atlantis {

void CryOmni3DEngine_Atlantis::dumpLoadState(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::FSNode(Common::Path(path, '/')))) {
		warning("dumpLoadState: cannot write %s", path.c_str());
		return;
	}
	Common::String s;
	s += Common::String::format("L wam %s chapter %u place %d lastvue %u alpha %.9g beta %.9g incperso %u "
	                            "disc %u invfirst %u\n", _currentWAMName.c_str(), _currentCONChapter,
	                            (int)_nextPlaceId, _lastVue, _omni3dMan.getAlpha(), _omni3dMan.getBeta(),
	                            _incPersoCount, _currentDisc, _toolbar.inventoryFirst());
	s += "V";
	for (uint i = 0; i < _conVarNames.size(); i++) {
		const int v = _gameVars.getValOrDefault(_conVarNames.name(i), 0);
		if (v)
			s += Common::String::format(" %u:%d", i, v);
	}
	s += "\nI";
	for (uint i = 0; i < _inventory.size(); i++)
		if (_inventory[i])
			s += Common::String::format(" %d:%u", (int)(_inventory[i] - _objects.begin()), _inventory[i]->idOBJ());
	s += Common::String::format("\nG %u", _guests.count);
	for (uint i = 0; i < _guests.count && i < AtlantisGuests::kSlots; i++)
		s += Common::String::format(" %d:%u", _guests.id[i], _guests.sprite[i]);
	s += "\nH";
	for (int id : _hiddenPersos)
		s += Common::String::format(" %d", id);
	s += "\nC";
	for (uint i = 0; i < _conPlayCounters.size(); i++)
		if (_conPlayCounters[i])
			s += Common::String::format(" %u:%u", i, _conPlayCounters[i]);
	s += "\nS";
	for (uint r = 0; r < ConSubjects::kRecords; r++) {
		const Common::Array<uint8> &ids = _subjects.subjects((int)(0x200 + r));
		if (ids.empty())
			continue;
		s += Common::String::format(" %u:", 0x200 + r);
		for (uint k = 0; k < ids.size(); k++)
			s += Common::String::format(k ? ",%u" : "%u", ids[k]);
	}
	s += "\n";
	out.write(s.c_str(), s.size());
	out.finalize();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
