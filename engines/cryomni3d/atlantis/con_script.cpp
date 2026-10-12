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

#include "common/array.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "cryomni3d/atlantis/con_script.h"

namespace CryOmni3D {
namespace Atlantis {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Strip trailing whitespace and carriage returns from a String.
static void rtrim(Common::String &s) {
	while (!s.empty()) {
		char c = s.lastChar();
		if (c == ' ' || c == '\t' || c == '\r')
			s.deleteLastChar();
		else
			break;
	}
}

// Parse a compound condition string of the form:
//   (key op val)&(key op val)&...
// where op is '=', '<', or '>'.
// Keys are lowercased. Unknown/malformed fragments are silently skipped.
static void parseConditions(const Common::String &str, Common::Array<ConCondition> &out) {
	const char *p = str.c_str();
	while (*p) {
		// Skip to the next '('
		while (*p && *p != '(') p++;
		if (!*p) break;
		p++; // consume '('

		// Find matching ')'
		const char *start = p;
		while (*p && *p != ')') p++;
		if (!*p) break;
		Common::String cond(start, (uint32)(p - start));
		p++; // consume ')'

		// Find the operator character
		const char *cp = cond.c_str();
		const char *opPtr = nullptr;
		char op = '=';
		for (const char *q = cp; *q; q++) {
			if (*q == '=' || *q == '<' || *q == '>' || *q == '!') {
				opPtr = q;
				op = *q;
				break;
			}
		}
		if (!opPtr) continue;

		ConCondition cc;
		cc.key = Common::String(cp, (uint32)(opPtr - cp));
		cc.key.toLowercase();
		cc.op    = op;
		cc.value = atoi(opPtr + 1);

		// Record the separator that FOLLOWS this condition, so the evaluator
		// can decide whether it is an AND-link or an OR-link.  See the comment
		// on ConCondition for the left-to-right short-circuit semantics this
		// drives (mirrors atlantis.exe FUN_004298ac).
		while (*p == ' ') p++;
		if (*p == '|') {
			cc.nextSep = '|';
			p++;
		} else if (*p == '&') {
			cc.nextSep = '&';
			p++;
		}
		while (*p == ' ') p++;

		out.push_back(cc);
	}
}

// [Speaker,params] text — dialog line.
static ConLine parseDialogLine(const Common::String &line) {
	uint close = 1;
	while (close < line.size() && line[close] != ']') close++;
	Common::String block = line.substr(1, close - 1);
	Common::String text;
	if (close + 1 < line.size()) {
		if (line[close + 1] == ' ')
			text = line.substr(close + 2);
		else
			text = line.substr(close + 1);
	}

	ConLine cl;
	uint comma = block.find(',');
	if (comma == Common::String::npos) {
		cl.speaker = block;
	} else {
		cl.speaker = block.substr(0, comma);
		cl.params  = block.substr(comma + 1);
	}
	cl.text = text;

	// Extract optional numeric angle tokens.  The original CON
	// syntax is `[Speaker, X, Y, camN]` — the two tokens after
	// the speaker are pixel-space panorama offsets that the
	// engine writes directly into the cyclo view (see header
	// comment on ConLine).  When only `camN` or `off` follows
	// the speaker, no angle is encoded.
	//
	// Implementation: split params by comma, recognise a leading
	// pair of integer-only tokens as (X, Y).  Anything else (a
	// "camN", "off", or trailing string token) terminates the
	// scan.  Tokens are trimmed of surrounding whitespace.
	if (!cl.params.empty()) {
		Common::Array<Common::String> partTok;
		const char *q = cl.params.c_str();
		while (*q) {
			while (*q == ' ' || *q == '\t') q++;
			const char *qe = q;
			while (*qe && *qe != ',') qe++;
			const char *trimEnd = qe;
			while (trimEnd > q &&
			       (trimEnd[-1] == ' ' || trimEnd[-1] == '\t'))
				trimEnd--;
			partTok.push_back(
			    Common::String(q, (uint32)(trimEnd - q)));
			if (*qe == ',') q = qe + 1;
			else q = qe;
		}
		auto isIntToken = [](const Common::String &s) -> bool {
			if (s.empty()) return false;
			uint i = 0;
			if (s[0] == '-' || s[0] == '+') i = 1;
			if (i >= s.size()) return false;
			for (; i < s.size(); ++i)
				if (!Common::isDigit(s[i])) return false;
			return true;
		};
		if (partTok.size() >= 2 &&
		    isIntToken(partTok[0]) && isIntToken(partTok[1])) {
			cl.hasAngle = true;
			cl.angleX   = atoi(partTok[0].c_str());
			cl.angleY   = atoi(partTok[1].c_str());
		}
	}

	return cl;
}

// ---------------------------------------------------------------------------
// The original's text walks (atlantis.exe).  See atlantis-re:
// formats/scripts-con.md and functions/scripts-FUN_0042844c.md.
// ---------------------------------------------------------------------------

namespace {

class ConText {
public:
	explicit ConText(const Common::Array<char> &t) : _t(t) {}

	uint32 size() const { return _t.size(); }
	byte at(uint32 pos) const { return pos < _t.size() ? (byte)_t[pos] : 0; }

	// strncmp(text + pos, s, n) == 0
	bool match(uint32 pos, const char *s, uint n) const {
		for (uint i = 0; i < n; i++)
			if (at(pos + i) != (byte)s[i])
				return false;
		return true;
	}

	bool isHeader(uint32 pos) const {
		return at(pos) == '/' && at(pos + 1) >= '0' && at(pos + 1) <= '9';
	}

	// FUN_00451b7a (atoi).
	int32 atoi(uint32 pos) const {
		while ((at(pos) >= 9 && at(pos) <= 13) || at(pos) == ' ')
			pos++;
		byte sign = at(pos);
		if (sign == '+' || sign == '-')
			pos++;
		uint32 n = 0;
		while (at(pos) >= '0' && at(pos) <= '9')
			n = n * 10 + (at(pos++) - '0');
		return (int32)(sign == '-' ? 0u - n : n);
	}

	// First CR or LF at or after pos.
	uint32 lineEnd(uint32 pos) const {
		while (pos < _t.size() && at(pos) != '\r' && at(pos) != '\n')
			pos++;
		return pos;
	}

	uint32 lineStart(uint32 pos) const {
		while (pos > 0 && at(pos - 1) != '\r' && at(pos - 1) != '\n')
			pos--;
		return pos;
	}

	Common::String line(uint32 pos) const {
		uint32 end = lineEnd(pos);
		Common::String s(_t.begin() + MIN(pos, end), end - MIN(pos, end));
		return s;
	}

private:
	const Common::Array<char> &_t;
};

// "/set(" + command + optional ")" on the rest of the line.
ConSectionItem setItem(const ConText &t, uint32 pos) {
	Common::String s = t.line(pos);
	rtrim(s);
	ConSectionItem item;
	item.isSet = true;
	item.offset = (int32)pos;
	item.setCmd = s.size() > 5 ? s.substr(5) : Common::String();
	if (!item.setCmd.empty() && item.setCmd.lastChar() == ')')
		item.setCmd.deleteLastChar();
	return item;
}

const int32 kStop = -1;

// FUN_0042831c: from pos to the end of the line, run the first /Set, /set
// (FUN_0042a100) or /Go, /go (FUN_00425c04).  Returns where the caller
// resumes: past the CR after a /set, kStop after a /go (gotoId is set),
// 0 when the line has neither (a '#' ends the search).
int32 scanCommand(const ConText &t, uint32 pos, ConSection &sec) {
	while (pos < t.size()) {
		if (t.match(pos, "/Set", 4) || t.match(pos, "/set", 4)) {
			sec.items.push_back(setItem(t, pos));
			return (int32)t.lineEnd(pos) + 1;
		}
		if (t.match(pos, "/Go", 3) || t.match(pos, "/go", 3)) {
			sec.gotoId = t.atoi(pos + 3);
			return kStop;
		}
		if (t.at(pos) == '#')
			return 0;
		pos++;
		if (t.at(pos) == '\r')
			return 0;
	}
	return 0;
}

// FUN_0042844c, the section executor, as a walk: the dialog lines and the
// /set commands it runs from pos, up to a /go or the next "/digit".
void readBody(const ConText &t, uint32 pos, ConSection &sec) {
	while (pos < t.size()) {
		if (t.at(pos) == '[') {
			Common::String s = t.line(pos);
			rtrim(s);
			ConSectionItem item;
			item.isSet = false;
			item.line = parseDialogLine(s);
			sec.items.push_back(item);
			// The dialog code copies the text up to the CR (0x00428785).
			while (pos < t.size() && t.at(pos) != '\r')
				pos++;
		}
		int32 next = scanCommand(t, pos, sec);
		if (next == kStop)
			return;
		if (next > 0)
			pos = (uint32)next;
		if (t.at(pos) == '#') {
			while (pos < t.size() && t.at(pos++) != '\n')
				;
		}
		pos++;
		if (t.isHeader(pos))
			return;
	}
}

// FUN_00425c04: the first "/N" of the text, or -1.
int32 findHeader(const ConText &t, int id) {
	for (uint32 pos = 0; pos < t.size(); pos++)
		if (t.isHeader(pos) && t.atoi(pos + 1) == id)
			return (int32)pos;
	return -1;
}

enum EntryKind { kEntrySel, kEntryCon, kEntryTim1, kEntryTim2 };

struct Entry {
	uint32 evalOffset;
	int id;
	EntryKind kind;
	int conLimit;
	bool repeatable;
};

// FUN_00427fa8: the section number is the last "/digit" the walk passed.
void trackSection(const ConText &t, uint32 pos, int &id) {
	if (t.isHeader(pos))
		id = t.atoi(pos + 1);
}

// The scanners FUN_004280f8 (/sel) and FUN_004281f8 (/con), walked with an
// evaluator that always fails and no play counter, so that every entry they
// can reach is listed in the order they meet it.
void scanSelCon(const ConText &t, bool con, Common::Array<Entry> &out) {
	int id = 0;
	uint32 pos = 0;
	while (pos < t.size()) {
		trackSection(t, pos, id);
		if (!con && (t.match(pos, "/Sel", 4) || t.match(pos, "/sel", 4))) {
			Entry e = { pos, id, kEntrySel, 1, false };
			out.push_back(e);
			pos++;
		} else if (con && (t.match(pos, "/Con", 4) || t.match(pos, "/con", 4))) {
			pos += 4;
			int limit = t.atoi(pos);
			if (limit == 0)
				limit = 1;
			Entry e = { pos, id, kEntryCon, limit, t.at(pos) == '*' };
			out.push_back(e);
			pos++;
		}
		// '#' skips to after the CR, then one more byte (the LF).
		if (t.at(pos) == '#') {
			while (pos < t.size() && t.at(pos++) != '\r')
				;
			pos++;
		}
		pos++;
	}
}

// FUN_0042bef4 (/tim1, /Tim1) and FUN_0042bfa4 (/tim2, /Tim2).
void scanTimer(const ConText &t, int n, Common::Array<Entry> &out) {
	const char *lower = (n == 1) ? "/tim1" : "/tim2";
	const char *upper = (n == 1) ? "/Tim1" : "/Tim2";
	int id = 0;
	uint32 pos = 0;
	while (pos < t.size()) {
		trackSection(t, pos, id);
		if (t.match(pos, lower, 5) || t.match(pos, upper, 5)) {
			pos += 5;
			Entry e = { pos, id, n == 1 ? kEntryTim1 : kEntryTim2, 1, false };
			out.push_back(e);
			pos++;
		}
		pos++;
	}
}

bool entryLess(const Entry &a, const Entry &b) {
	return a.evalOffset < b.evalOffset;
}

} // End of anonymous namespace

// ---------------------------------------------------------------------------
// ConScript
// ---------------------------------------------------------------------------

void ConScript::reset() {
	_text.clear();
	_sections.clear();
	_initCmds.clear();
	_initCmdOffsets.clear();
}

void ConScript::parse(const char *buf, uint32 len) {
	reset();
	_text.resize(len);
	if (len)
		memcpy(_text.begin(), buf, len);
	const ConText t(_text);

	// FUN_00425eb0: after "/INIT" + CRLF, run the commands line by line until
	// a line has none.
	for (uint32 pos = 0; pos < t.size(); pos++) {
		if (t.match(pos, "/INIT", 5)) {
			int32 at = (int32)pos + 7;
			for (uint guard = 0; at > 0 && guard < t.size(); guard++) {
				ConSection init;
				at = scanCommand(t, (uint32)at, init);
				if (!init.items.empty())
				{
					_initCmds.push_back(init.items[0].setCmd);
					_initCmdOffsets.push_back(init.items[0].offset);
				}
				if (at == kStop)
					at = findHeader(t, init.gotoId);
			}
			break;
		}
		if (t.at(pos) == '#') {
			while (pos < t.size() && t.at(pos++) != '\r')
				;
			pos++;
		}
	}

	Common::Array<Entry> entries;
	scanSelCon(t, false, entries);
	scanSelCon(t, true, entries);
	scanTimer(t, 1, entries);
	scanTimer(t, 2, entries);
	Common::sort(entries.begin(), entries.end(), entryLess);

	// Sections with no entry point, reachable only through /go: every "/N"
	// at the start of a line whose number no entry carries.
	struct Header {
		uint32 offset;
		int id;
	};
	Common::Array<Header> headers;
	for (uint32 pos = 0; pos < t.size(); pos = t.lineEnd(pos) + 1) {
		if (t.isHeader(pos)) {
			int id = t.atoi(pos + 1);
			bool covered = (id == 0);
			for (uint i = 0; i < entries.size() && !covered; i++)
				covered = (entries[i].id == id);
			if (!covered) {
				Header h = { pos, id };
				headers.push_back(h);
			}
		}
	}

	uint e = 0, h = 0;
	while (e < entries.size() || h < headers.size()) {
		ConSection sec;
		if (h >= headers.size() || (e < entries.size() && entries[e].evalOffset < headers[h].offset)) {
			const Entry &en = entries[e++];
			sec.id = en.id;
			sec.isEntry = true;
			sec.isCon = (en.kind == kEntryCon);
			sec.repeatable = en.repeatable;
			sec.conLimit = en.conLimit;
			sec.isTimer = (en.kind == kEntryTim1 || en.kind == kEntryTim2);
			sec.timerNum = (en.kind == kEntryTim1) ? 1 : (en.kind == kEntryTim2) ? 2 : 0;
			sec.condOffset = (int32)en.evalOffset;
			sec.lineOffset = (int32)t.lineStart(en.evalOffset);
			Common::String cond = t.line(en.evalOffset);
			parseConditions(cond, sec.conditions);
			for (const ConCondition &cc : sec.conditions) {
				if (cc.key == "clicperso") sec.clicPerso = cc.value;
				if (cc.key == "cliczone")  sec.clicZone  = cc.value;
			}
			// The evaluator stops at the end of the line; the body starts two
			// bytes later (callers of FUN_004298ac add 2).
			sec.bodyOffset = (int32)t.lineEnd(en.evalOffset) + 2;
			readBody(t, (uint32)sec.bodyOffset, sec);
		} else {
			const Header &hd = headers[h++];
			sec.id = hd.id;
			sec.lineOffset = (int32)hd.offset;
			sec.bodyOffset = (int32)hd.offset;
			readBody(t, hd.offset, sec);
		}
		_sections.push_back(sec);
	}

	// /sujN[,M,...] lines: the subject gate of the entry they follow.
	for (uint32 pos = 0; pos < t.size(); pos = t.lineEnd(pos) + 1) {
		Common::String line = t.line(pos);
		rtrim(line);
		if (line.size() >= 5 && scumm_strnicmp(line.c_str(), "/suj", 4) == 0
		        && Common::isDigit(line[4])) {
			ConSection *owner = nullptr;
			for (ConSection &sec : _sections)
				if (sec.isEntry && sec.lineOffset < (int32)pos)
					owner = &sec;
			if (!owner)
				continue;
			const char *ids = line.c_str() + 4;
			while (*ids) {
				owner->sujIds.push_back(atoi(ids));
				while (*ids && *ids != ',') ids++;
				if (*ids == ',') ids++;
			}
			if (!owner->sujIds.empty())
				owner->sujId = owner->sujIds[0];
		}
	}
}

bool conEntryAllowed(const ConSection &sec, const Common::Array<byte> &counters) {
	if (sec.repeatable)
		return true;
	byte played = (sec.id >= 0 && (uint)sec.id < counters.size()) ? counters[sec.id] : 0;
	return played < sec.conLimit;
}

int findConClickEntry(const ConScript &script, const Common::Array<byte> &counters,
                      ConEntryTest test, void *ctx) {
	const Common::Array<char> &text = script.text();
	const Common::Array<ConSection> &secs = script.sections();
	for (uint i = 0; i < secs.size(); i++) {
		const ConSection &sec = secs[i];
		if (!sec.isEntry || !sec.isCon || !conEntryAllowed(sec, counters))
			continue;
		if (!test(sec, ctx))
			continue;
		// 0x00425d47..0x00425d6d: subject entries are skipped and the scan
		// goes on.
		const uint32 b = (uint32)sec.bodyOffset;
		if (b + 4 <= text.size() && (!strncmp(&text[b], "/Suj", 4) || !strncmp(&text[b], "/suj", 4)))
			continue;
		return (int)i;
	}
	return -1;
}

bool ConScript::gotoSection(int id, ConSection &out) const {
	const ConText t(_text);
	int32 pos = findHeader(t, id);
	if (pos < 0)
		return false;
	out = ConSection();
	out.id = id;
	out.lineOffset = pos;
	readBody(t, (uint32)pos, out);
	return true;
}

} // namespace Atlantis
} // namespace CryOmni3D
