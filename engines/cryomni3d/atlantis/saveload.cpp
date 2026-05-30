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
#include "common/savefile.h"
#include "common/str.h"
#include "common/textconsole.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/engine.h"

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
static const uint32 kExtVersion      = 4;

void CryOmni3DEngine_Atlantis::saveGame(uint saveNum, const Common::String &saveName) {
	Common::String saveFileName = getSaveStateName((int)saveNum - 1);
	Common::OutSaveFile *out = _saveFileMan->openForSaving(saveFileName);
	if (!out) {
		warning("saveGame: cannot open '%s' for writing", saveFileName.c_str());
		return;
	}

	char saveNameC[kSaveDescriptionLen + 1];
	memset(saveNameC, 0, sizeof(saveNameC));
	strncpy(saveNameC, saveName.c_str(), kSaveDescriptionLen);
	out->write(saveNameC, kSaveDescriptionLen);

	out->writeUint32LE(0);            // dummy padding
	out->writeUint32BE(0);
	out->writeUint32BE(0);
	for (uint i = 0; i < 200; i++)    // dialog variables (reserved)
		out->writeByte(0);

	// Inventory (20 slots × uint32BE object index into _objects; -1 = empty).
	for (Inventory::const_iterator it = _inventory.begin(); it != _inventory.end(); ++it) {
		uint objId = uint(-1);
		if (*it != nullptr)
			objId = (uint)(*it - _objects.begin());
		out->writeUint32BE(objId);
	}

	out->writeUint32BE(_currentChapter);

	char wamNameC[kWAMNameLen];
	memset(wamNameC, 0, sizeof(wamNameC));
	strncpy(wamNameC, _currentWAMName.c_str(), kWAMNameLen - 1);
	out->write(wamNameC, kWAMNameLen);

	out->writeUint32BE(_currentPlaceId);
	out->writeDoubleBE(_omni3dMan.getAlpha());
	out->writeDoubleBE(_omni3dMan.getBeta());

	for (uint i = 0; i < 256; i++)    // place states (not modelled yet)
		out->writeUint32BE(0);
	for (uint i = 0; i < 100; i++)    // indexed game variables (not modelled yet)
		out->writeUint32BE(0);

	// Extended block: the live CON-script state.
	out->writeUint32BE(kExtMagic);
	out->writeUint32BE(kExtVersion);

	out->writeUint32BE((uint32)_hiddenPersos.size());
	for (int pid : _hiddenPersos)
		out->writeSint32BE(pid);

	out->writeUint32BE((uint32)_sujEnabled.size());
	for (const SujState &ss : _sujEnabled) {
		out->writeSint32BE(ss.persoId);
		out->writeUint32BE((uint32)ss.sujs.size());
		for (int s : ss.sujs)
			out->writeSint32BE(s);
	}

	// Named game variables — only the 7 persistent ones (VARIAS.CON idx 0-6);
	// the rest are chapter-transient and the original never saves them.
	uint32 nPersist = 0;
	for (const auto &kv : _gameVars)
		if (isPersistentGameVar(kv._key))
			nPersist++;
	out->writeUint32BE(nPersist);
	for (const auto &kv : _gameVars) {
		if (!isPersistentGameVar(kv._key))
			continue;
		uint32 klen = (uint32)kv._key.size();
		out->writeUint32BE(klen);
		out->write(kv._key.c_str(), klen);
		out->writeSint32BE(kv._value);
	}

	out->writeUint32BE(_currentCONChapter);   // ext v2: active CHAPI*.CON
	out->writeUint32BE(0);                     // ext v3: music track (not modelled yet)
	out->writeUint32BE(0);                     // ext v4: shown-perso count (not modelled yet)

	out->finalize();
	delete out;
	debugC(1, kDebugScript, "saveGame: wrote slot %u ('%s')", saveNum, saveName.c_str());
}

bool CryOmni3DEngine_Atlantis::loadGame(uint saveNum) {
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
	for (Inventory::iterator it = _inventory.begin(); it != _inventory.end(); ++it)
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
	Common::Array<SujState> savedSujEnabled;
	Common::HashMap<Common::String, int> savedGameVars;
	uint32 savedCONChapter = 0;
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
			for (uint32 i = 0; i < nSuj && !in->eos(); i++) {
				SujState ss;
				ss.persoId = in->readSint32BE();
				uint32 nK = in->readUint32BE();
				for (uint32 j = 0; j < nK && !in->eos(); j++)
					ss.sujs.push_back(in->readSint32BE());
				savedSujEnabled.push_back(ss);
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
			// ext v3 music + v4 shown-persos are not modelled yet — ignored.
		}
	}
	delete in;

	// Restore.  setupChapterWAM() loads the chapter WAM + CON and runs INIT
	// (establishing a baseline the extended block then overrides).
	_currentChapter = chapter;
	setupChapterWAM((int)chapter);

	Common::String wamName(wamNameC);
	if (!wamName.empty() && !wamName.equalsIgnoreCase(_currentWAMName))
		setupWAMByName(wamName);

	// Reload the active CON script (its chapter is independent of the WAM
	// chapter, and loadConScript() clears the transient half of _gameVars —
	// essential for a game-over reload of the current chapter).
	if (savedCONChapter >= 1) {
		_currentCONChapter = savedCONChapter;
		loadConScript((int)savedCONChapter);
	}

	_nextPlaceId = placeId;
	_omni3dMan.setAlpha(alpha);
	_omni3dMan.setBeta(beta);
	for (byte &b : _placeVisits)      // visit counters aren't persisted yet
		b = 0;

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

	if (hasExt) {
		_hiddenPersos = savedHiddenPersos;
		_sujEnabled   = savedSujEnabled;
		for (const auto &kv : savedGameVars)
			if (isPersistentGameVar(kv._key))
				_gameVars[kv._key] = kv._value;
	}

	// Apply the saved inventory now that setupChapterWAM/loadConScript (which
	// can clear or repopulate it) have run.
	for (uint i = 0; i < _inventory.size() && i < savedInv.size(); i++) {
		uint objId = savedInv[i];
		if (objId >= _objects.size())
			objId = uint(-1);
		_inventory[i] = (objId != uint(-1)) ? (_objects.begin() + objId) : nullptr;
	}

	_autosavePending = false;   // a load is not itself a chapter transition
	_inventory.deselectObject();
	debugC(1, kDebugScript, "loadGame: restored slot %u (chapter %u, place %u)",
	      saveNum, chapter, placeId);
	return true;
}

void CryOmni3DEngine_Atlantis::autosave() {
	// Per-chapter checkpoint on a single fixed slot.  The named multi-episode
	// profiles land with the player-management screen; for now one slot holds
	// the most recent chapter entry (enough for the game-over reload).
	if (_currentCONChapter < 1)
		return;
	saveGame(1, Common::String::format("Chapter %u", _currentCONChapter));
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
