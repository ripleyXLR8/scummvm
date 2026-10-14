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

#ifndef CRYOMNI3D_ATLANTIS_CON_EVAL_H
#define CRYOMNI3D_ATLANTIS_CON_EVAL_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "cryomni3d/objects.h"
#include "cryomni3d/atlantis/con_script.h"

namespace CryOmni3D {
namespace Atlantis {

// The variable names of VARIAS.CON.  The line number is the variable index.
class ConVariableNames {
public:
	// Load VARIAS.CON; FUN_00426cb0 upper-cases the whole file (FUN_00429df0).
	void load(const char *data, uint32 size);
	bool empty() const { return _names.empty(); }

	// FUN_00429d20: index of the first line that starts with the len bytes of
	// key (strncmp), or -1.
	int index(const char *key, uint len) const;

	// Lower-case name of the variable with this index.
	const Common::String &name(uint index) const { return _names[index]; }
	uint size() const { return _names.size(); }

private:
	Common::Array<char> _text;               // upper-cased
	Common::Array<Common::String> _names;    // lower-case, one per line
};

// A /set command as atlantis.exe FUN_0042a100 reads it from the CON text.
struct ConSetCommand {
	bool valid = false;           // false: the original returns 0 and does nothing
	bool invent = false;          // INVENTnnn (FUN_0042b328)
	int32 inventId = 0;
	int32 inventValue = 0;
	bool changeWam = false;       // ChangeWam: a file name, no number
	Common::String wamName;
	Common::String name;          // lower-case VARIAS.CON name of the key
	int32 value = 0;
	uint32 keyEnd = 0;            // text offset of the byte that ended the key
	uint32 valuePos = 0;          // text offset of the first digit (value)
};

// FUN_0042a100's parse: from pos (the "/set"), skip to '(', copy the key up
// to one of "=<>!#,", upper-case it; INVENT: id = atoi(key + 6), value =
// atoi(byte after the terminator); otherwise look the key up by prefix
// (unknown: invalid); ChangeWam takes the text up to ')'; any other key takes
// the first digit run among the 16 bytes from the terminator (none: invalid).
void parseConSet(const ConVariableNames &names, const Common::Array<char> &text, uint32 pos,
                 ConSetCommand &out);

// The per-perso subject table of atlantis.exe (0x00602f10): 0x100 records
// of 0xe1 bytes, record perso - 0x200, holding a count byte and the subject
// ids (one byte each, in 14-byte entries) in the order they were enabled.
// See atlantis-re: functions/scripts-FUN_00425f6c.md.
class ConSubjects {
public:
	static const uint kRecords = 0x100;

	// FUN_00427634: memset(0x00602f10, 0, 0xe100).
	void clear();
	// FUN_00425f6c (EnableSuj) and FUN_0042606c (DisableSuj), on the CON
	// text from pos, the first digit after the key (ConSetCommand::valuePos).
	void enable(const Common::Array<char> &text, uint32 pos);
	void disable(const Common::Array<char> &text, uint32 pos);

	// The enabled subjects of a perso, in order (empty outside the table).
	const Common::Array<uint8> &subjects(int persoId) const;
	bool has(int persoId, int subject) const;
	// For the saves.
	void setSubjects(int persoId, const Common::Array<uint8> &subjects);

private:
	Common::Array<uint8> _records[kRecords];
	Common::Array<uint8> _none;
};

// The track list of LoadMusik / DelMusik (atlantis.exe FUN_0041f1d0 /
// FUN_0041faa0) from pos, the first digit after the key: track indexes
// (value - 1, a byte) while the byte before the pointer is not ')' and the
// index is not 0xff; the pointer steps 2, + 1 above 9, + 1 above 99 on the
// INDEX, so a value of 10 or 100 desynchronises the walk (CHAPI072).
Common::Array<uint8> conMusicList(const Common::Array<char> &text, uint32 pos);

// LoadMusik / DelMusik on the set of tracks in the install folder (bit i =
// track index i): LoadMusik acts only when the first index is below 0x20,
// skips the playing track (current = index + 1, 0x00772915) and copies a
// track that is not there (its index goes to `copies`); DelMusik removes
// every listed track but the playing one.
void conMusicApply(bool load, const Common::Array<uint8> &list, int current, uint32 &onDisk,
                   Common::Array<uint8> &copies);

// FUN_0042a100, AngleWarpX= / AngleWarpY=: the view angle in radians,
// value * pi / 180 computed on the x87 and stored as a float (pi is the
// float FUN_0045179a keeps at 0x00bcb594).
float conAngleRadians(int32 value);

// The CON clocks (atlantis.exe): ChapterTime, Time1 and Time2, in seconds.
// See atlantis-re: formats/scripts-con.md (timers).
struct ConClocks {
	uint32 chapterStart = 0;   // 0x006ce078
	uint32 time1Start = 0;     // 0x006ce088
	uint32 time2Start = 0;     // 0x006ce084
	int32 time1Offset = 0;     // 0x006ce04c
	int32 time2Offset = 0;     // 0x006ce050

	// FUN_0041c75c: (now - start) / 1000, unsigned, plus the offset.
	int32 chapterTime(uint32 now) const { return (int32)((now - chapterStart) / 1000); }
	int32 timerValue(int timer, uint32 now) const;
	// FUN_0042a100, Time1= / Time2=.
	void restart(int timer, int32 value, uint32 now);
	// FUN_0042c59c / FUN_0041c838: resume from the current values.
	void rebase(uint32 now, int32 time1, int32 time2);
};

// Evaluates the condition line of a CON section (/sel, /con, /tim) the way
// atlantis.exe FUN_004298ac does: on the script text itself, against the
// variables named in VARIAS.CON and the inventory.  Keys whose value comes
// from live engine state (vue, departvue, ...) are supplied by liveValue(),
// which the engine overrides.  See atlantis-re: functions/scripts-FUN_004298ac.md.
class ConEvaluator {
public:
	ConEvaluator(const ConVariableNames &names, const Common::HashMap<Common::String, int> &vars,
	             const Inventory &inventory) :
		_names(names), _vars(vars), _inventory(inventory) {}
	virtual ~ConEvaluator() {}

	// True when the condition line starting at text[offset] holds.  The
	// scans may run past the line, as in the original; bytes past the end of
	// the text read as 0.
	bool evaluate(const Common::Array<char> &text, uint32 offset) const;

	// Value of the variable with this VARIAS.CON index.
	int variableValue(int index) const;

	// Value of a variable by its lower-case VARIAS.CON name.
	int value(const Common::String &name) const;

protected:
	// Return true and set out for a key read from live engine state.
	virtual bool liveValue(const Common::String &, int &) const { return false; }

private:
	const ConVariableNames &_names;
	const Common::HashMap<Common::String, int> &_vars;
	const Inventory &_inventory;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
