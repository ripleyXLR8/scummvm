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

#include "cryomni3d/atlantis/con_eval.h"

namespace CryOmni3D {
namespace Atlantis {

namespace {

// FUN_00451b7a (atoi) character classes, from the table at 0x004bf600.
bool isAtoiSpace(byte c) {
	return (c >= 9 && c <= 13) || c == ' ';
}

bool isAtoiDigit(byte c) {
	return c >= '0' && c <= '9';
}

// Bytes of the script text; past the end reads 0.
struct TextCursor {
	const Common::Array<char> &text;
	explicit TextCursor(const Common::Array<char> &t) : text(t) {}
	byte at(uint32 pos) const { return pos < text.size() ? (byte)text[pos] : 0; }
};

// FUN_00451b7a.
int32 atoiAt(const TextCursor &t, uint32 pos) {
	while (isAtoiSpace(t.at(pos)))
		pos++;
	byte sign = t.at(pos);
	if (sign == '+' || sign == '-')
		pos++;
	uint32 n = 0;
	while (isAtoiDigit(t.at(pos)))
		n = n * 10 + (t.at(pos++) - '0');
	return (int32)(sign == '-' ? 0u - n : n);
}

int32 atoiBuf(const byte *buf, uint len, uint pos) {
	while (pos < len && isAtoiSpace(buf[pos]))
		pos++;
	byte sign = pos < len ? buf[pos] : 0;
	if (sign == '+' || sign == '-')
		pos++;
	uint32 n = 0;
	while (pos < len && isAtoiDigit(buf[pos]))
		n = n * 10 + (buf[pos++] - '0');
	return (int32)(sign == '-' ? 0u - n : n);
}

// FUN_00429bf4: separator after the current term.  1 for '|', 0 for '&' or
// the end of the line, unchanged when another '(' comes first.
int pendingOr(const TextCursor &t, uint32 pos, int pending) {
	for (;;) {
		byte c = t.at(pos);
		if (c == '(')
			return pending;
		if (c == '|')
			return 1;
		if (c == '&')
			return 0;
		pos++;
		if (c == '\r')
			return 0;
		if (pos > t.text.size() + 1)   // the original would run off the buffer
			return 0;
	}
}

// FUN_00429c64: position after the next '&' of the line, or -1.
int64 skipToAnd(const TextCursor &t, uint32 pos) {
	for (;;) {
		byte c = t.at(pos);
		if (c == '\r')
			return -1;
		pos++;
		if (c == '&')
			return pos;
		if (pos > t.text.size() + 1)
			return -1;
	}
}

// FUN_00429cb0: false on a key terminator.
bool isKeyChar(byte c) {
	return c != '=' && c != '<' && c != '>' && c != '!' && c != '#' && c != ',';
}

// FUN_00429eb0 / 0x00429f44 / 0x00429fd8 / 0x0042a06c: the value is the first
// digit run within 16 bytes after the operator; no digit fails the test.
// The compare is unsigned.
bool compareTerm(const TextCursor &t, uint32 pos, byte op, uint32 var) {
	for (uint i = 0; i < 16; i++) {
		if (!isAtoiDigit(t.at(pos + i)))
			continue;
		uint32 v = (uint32)atoiAt(t, pos + i);
		switch (op) {
		case '=': return var == v;
		case '<': return var < v;
		case '>': return var > v;
		default:  return var != v;
		}
	}
	return false;
}

} // End of anonymous namespace

void parseConSet(const ConVariableNames &names, const Common::Array<char> &text, uint32 pos,
                 ConSetCommand &out) {
	out = ConSetCommand();
	const TextCursor t(text);
	uint32 p = pos;
	// Head loop: bounded by the size of the CON file, like the evaluator.
	for (uint32 i = 0; i < text.size(); i++) {
		if (t.at(p++) == '(')
			break;
	}
	// The key buffer is 13 zeroed bytes (0x0042a129); longer keys overflow
	// the stack in the original and are cut here.
	byte key[13];
	memset(key, 0, sizeof(key));
	uint len = 0;
	do {
		if (len < sizeof(key))
			key[len] = t.at(p);
		len++;
		p++;
	} while (isKeyChar(t.at(p)) && p < text.size() + 1);
	out.keyEnd = p;
	const uint klen = MIN<uint>(len, sizeof(key));
	for (uint i = 0; i < klen; i++)
		if (key[i] >= 'a' && key[i] <= 'z')
			key[i] -= 0x20;

	if (memcmp(key, "INVENT", 6) == 0) {
		out.valid = out.invent = true;
		out.inventId = atoiBuf(key, sizeof(key), 6);
		out.inventValue = atoiAt(t, p + 1);
		return;
	}

	int index = names.index((const char *)key, klen);
	if (index < 0) {
		// The original shows an error dialog.
		warning("CON /set: unknown variable '%s'", Common::String((const char *)key, klen).c_str());
		return;
	}
	out.name = names.name((uint)index);
	if (out.name == "changewam") {
		for (uint32 q = p + 1; q < text.size() && t.at(q) != ')'; q++)
			out.wamName += (char)t.at(q);
		out.valid = out.changeWam = true;
		return;
	}
	for (uint i = 0; i < 16; i++) {
		if (isAtoiDigit(t.at(p + i))) {
			out.value = atoiAt(t, p + i);
			out.valuePos = p + i;
			out.valid = true;
			return;
		}
	}
}

void ConSubjects::clear() {
	for (uint i = 0; i < kRecords; i++)
		_records[i].clear();
}

namespace {

// The list walk of FUN_00425f6c / FUN_0042606c: a subject id is a byte
// (atoi truncated), the pointer then steps 2, +1 above 9, +1 above 99 (on
// the byte), until the byte before it is ')'.  The original reads on past
// the line when no ')' falls there (a malformed list); the port stops at
// the end of the text.
template<typename Fn>
void walkSubjectList(const TextCursor &t, uint32 p, Fn fn) {
	for (;;) {
		const uint8 subject = (uint8)atoiAt(t, p);
		fn(subject);
		p += 2;
		if (subject > 9)
			p++;
		if (subject > 99)
			p++;
		if (t.at(p - 1) == ')')
			break;
		if (p > t.text.size()) {
			warning("CON EnableSuj / DisableSuj: no ')' ends the subject list");
			break;
		}
	}
}

} // End of anonymous namespace

void ConSubjects::enable(const Common::Array<char> &text, uint32 pos) {
	const TextCursor t(text);
	// FUN_00425f6c: record atoi - 0x200; the list starts 4 bytes on (a
	// 3-digit perso and '=').
	const int32 record = atoiAt(t, pos) - 0x200;
	if (record < 0 || record >= (int32)kRecords) {
		// The original writes outside the table.
		warning("CON EnableSuj: perso %d outside the subject table", record + 0x200);
		return;
	}
	Common::Array<uint8> &list = _records[record];
	walkSubjectList(t, pos + 4, [&](uint8 subject) {
		for (uint k = 0; k < list.size(); k++)
			if (list[k] == subject)
				return;
		list.push_back(subject);
	});
}

void ConSubjects::disable(const Common::Array<char> &text, uint32 pos) {
	const TextCursor t(text);
	// FUN_0042606c: record = the low byte of atoi (the same record as
	// EnableSuj for persos 0x200..0x2ff); the first match is removed and
	// the entries after it shift down.
	Common::Array<uint8> &list = _records[(uint8)atoiAt(t, pos)];
	walkSubjectList(t, pos + 4, [&](uint8 subject) {
		for (uint k = 0; k < list.size(); k++)
			if (list[k] == subject) {
				list.remove_at(k);
				return;
			}
	});
}

const Common::Array<uint8> &ConSubjects::subjects(int persoId) const {
	const int record = persoId - 0x200;
	if (record < 0 || record >= (int)kRecords)
		return _none;
	return _records[record];
}

bool ConSubjects::has(int persoId, int subject) const {
	const Common::Array<uint8> &list = subjects(persoId);
	for (uint k = 0; k < list.size(); k++)
		if (list[k] == subject)
			return true;
	return false;
}

void ConSubjects::setSubjects(int persoId, const Common::Array<uint8> &list) {
	const int record = persoId - 0x200;
	if (record >= 0 && record < (int)kRecords)
		_records[record] = list;
}

Common::Array<uint8> conMusicList(const Common::Array<char> &text, uint32 pos) {
	const TextCursor t(text);
	Common::Array<uint8> out;
	uint8 idx = (uint8)(atoiAt(t, pos) - 1);
	uint32 p = pos;
	while (t.at(p - 1) != ')' && idx != 0xff && p <= text.size()) {
		out.push_back(idx);
		p += 2;
		if (idx > 9)
			p++;
		if (idx > 99)
			p++;
		idx = (uint8)(atoiAt(t, p) - 1);
	}
	return out;
}

void conMusicApply(bool load, const Common::Array<uint8> &list, int current, uint32 &onDisk,
                   Common::Array<uint8> &copies) {
	if (list.empty() || (load && list[0] >= 0x20))
		return;
	for (uint i = 0; i < list.size(); i++) {
		const uint idx = list[i];
		if ((int)idx + 1 == current || idx >= 32)
			continue;
		const uint32 bit = 1u << idx;
		if (load) {
			if (!(onDisk & bit)) {
				copies.push_back((uint8)idx);
				onDisk |= bit;
			}
		} else {
			onDisk &= ~bit;
		}
	}
}

float conAngleRadians(int32 value) {
	const float pi = 3.14159265358979323846f;
	return (float)((double)value * (double)pi / 180.0);
}

int32 ConClocks::timerValue(int timer, uint32 now) const {
	if (timer == 1)
		return time1Offset + (int32)((now - time1Start) / 1000);
	return time2Offset + (int32)((now - time2Start) / 1000);
}

void ConClocks::restart(int timer, int32 value, uint32 now) {
	// start = now - value s, or, when the clock is not that far yet,
	// start = now and the value becomes the offset.
	const uint32 ms = (uint32)value * 1000;
	uint32 &start = (timer == 1) ? time1Start : time2Start;
	int32 &offset = (timer == 1) ? time1Offset : time2Offset;
	offset = 0;
	if (now > ms) {
		start = now - ms;
	} else {
		start = now;
		offset = value;
	}
}

void ConClocks::rebase(uint32 now, int32 time1, int32 time2) {
	time1Start = time2Start = chapterStart = now;
	time1Offset = time1;
	time2Offset = time2;
}

void ConVariableNames::load(const char *data, uint32 size) {
	_text.resize(size);
	_names.clear();
	Common::String line;
	for (uint32 i = 0; i < size; i++) {
		char c = data[i];
		_text[i] = (c >= 'a' && c <= 'z') ? (char)(c - 0x20) : c;
		if (c == '\n') {
			if (!line.empty() && line.lastChar() == '\r')
				line.deleteLastChar();
			line.toLowercase();
			_names.push_back(line);
			line.clear();
		} else {
			line += c;
		}
	}
	if (!line.empty()) {
		line.toLowercase();
		_names.push_back(line);
	}
}

int ConVariableNames::index(const char *key, uint len) const {
	// Line by line: strncmp(line, key, len), first match wins.
	uint32 pos = 0;
	int index = 0;
	while (pos < _text.size()) {
		bool match = true;
		for (uint i = 0; i < len; i++) {
			byte a = pos + i < _text.size() ? (byte)_text[pos + i] : 0;
			byte b = (byte)key[i];
			if (a != b) {
				match = false;
				break;
			}
			if (a == 0)
				break;
		}
		if (match)
			return index;
		while (pos < _text.size() && _text[pos] != '\n')
			pos++;
		pos++;
		index++;
	}
	return -1;
}

int ConEvaluator::variableValue(int index) const {
	if (index < 0 || (uint)index >= _names.size())
		return 0;
	return value(_names.name((uint)index));
}

int ConEvaluator::value(const Common::String &name) const {
	int live;
	if (liveValue(name, live))
		return live;
	Common::HashMap<Common::String, int>::const_iterator it = _vars.find(name);
	return (it != _vars.end()) ? it->_value : 0;
}

bool ConEvaluator::evaluate(const Common::Array<char> &text, uint32 offset) const {
	const TextCursor t(text);
	// The key buffer of the original is a 16-byte stack array that is never
	// cleared or terminated; FUN_00429e54 reads it past the key.  Its initial
	// content is undefined in the original: it is zero here, as in the
	// emulation that produced the reference results.
	byte key[16];
	memset(key, 0, sizeof(key));
	int pending = 0;
	uint32 p = offset;

	// The original bounds the loop by the size of the CON file.
	for (uint32 iter = 0; iter < text.size(); iter++) {
		byte c = t.at(p);
		if (c == '\r' || c == '\n')
			return true;
		p++;
		if (c != '(')
			continue;

		pending = pendingOr(t, p, pending);
		uint len = 0;
		do {
			key[len++] = t.at(p);
			if (len >= 13)
				return false;
			p++;
		} while (isKeyChar(t.at(p)));
		for (uint i = 0; i < len; i++)
			if (key[i] >= 'a' && key[i] <= 'z')
				key[i] -= 0x20;

		// FUN_00429e54: "INVENT" then atoi over the rest of the buffer.
		int32 objId = (memcmp(key, "INVENT", 6) == 0) ? atoiBuf(key, sizeof(key), 6) : 0;
		if (objId == 0) {
			int index = _names.index((const char *)key, len);
			if (index == -1) {
				// The original shows an error dialog here.
				warning("CON condition: unknown variable '%s'",
				        Common::String((const char *)key, len).c_str());
				return false;
			}
			const uint32 var = (uint32)variableValue(index);
			static const byte kOps[4] = { '=', '<', '>', '!' };
			for (uint k = 0; k < 4; k++) {
				if (t.at(p) != kOps[k])
					continue;
				if (!compareTerm(t, p + 1, kOps[k], var)) {
					if (pending == 0)
						return false;
				} else if (pending == 1) {
					int64 next = skipToAnd(t, p);
					if (next < 0)
						return true;
					p = (uint32)next;
				}
			}
		} else {
			// The operator is ignored: the term holds when a record with this
			// id has the value after the operator (records hold 1), or when
			// there is no such record and that value starts with '0'.
			bool found = false;
			for (uint i = 0; i < _inventory.size(); i++) {
				const Object *obj = _inventory[i];
				if (!obj || obj->idOBJ() != (uint)objId)
					continue;
				if (atoiAt(t, p + 1) == 1) {
					found = true;
					break;
				}
				if (pending != 1)
					return false;
				pending = 0;
			}
			if (!found && t.at(p + 1) != '0') {
				if (pending == 1)
					pending = 0;
				else
					return false;
			}
		}
	}
	return false;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
