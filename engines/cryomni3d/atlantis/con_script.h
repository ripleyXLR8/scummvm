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

// Atlantis chapter scenario scripts (.CON files in SCENAR\).
//
// The original keeps no parsed structure: it walks the text every time.
// ConScript keeps the text and lists what those walks find, in the order
// they find it (atlantis.exe; see atlantis-re: formats/scripts-con.md):
//   /INIT            — FUN_00425eb0 runs the commands of the following lines
//                      until a line has none
//   /N               — section number: the last "/digit" a walk passed
//   /sel, /Sel       — automatic entry (FUN_004280f8)
//   /con, /Con       — player-triggered entry (FUN_004281f8); /conN allows N
//                      plays, /con* any number
//   /tim1, /Tim1, /tim2, /Tim2 — timer entries (FUN_0042bef4, FUN_0042bfa4)
//   /sujN            — dialog subject gate of the entry it follows
//   /set, /Set       — game-state command; /go, /Go — jump to "/N"
//   [Speaker,params] text — dialog line
//   #                — comment
// Other spellings are not recognised.  A section can hold several entry
// lines; each is a separate entry point.  The keys of a condition are the
// variables of VARIAS.CON (con_eval.cpp).

#ifndef CRYOMNI3D_ATLANTIS_CON_SCRIPT_H
#define CRYOMNI3D_ATLANTIS_CON_SCRIPT_H

#include "common/array.h"
#include "common/str.h"

namespace CryOmni3D {
namespace Atlantis {

struct ConLine {
	Common::String speaker;  // e.g. "Seth", "Garde13"
	Common::String params;   // e.g. "cam3", "off" (comma-separated in source)
	Common::String text;

	// Optional panorama angle hints, sourced directly from the CON dialog
	// directive — when present, the original engine writes them straight into
	// the cyclo view's alpha/beta before playing the line (the dialog line
	// code, atlantis-re functions/video-FUN_0042ee88.md; FUN_00451915 stores
	// the angles of the current view; the address 0x004283e7 cited here
	// before is only a byte inside FUN_0042831c, the disp8 of the CMP at
	// 0x004283e5, from an older decompilation).  Syntax in CON files:
	//   [Speaker, X, Y, camN] text       — X,Y present (hasAngle = true)
	//   [Speaker, camN] text             — no numeric tokens (hasAngle = false)
	// X is a panorama-pixel offset for the horizontal angle; Y is the vertical
	// offset.  The dialog renderer converts them to radians using the
	// panorama dimensions (no hardcoded scale factor).
	bool hasAngle = false;
	int  angleX   = 0;
	int  angleY   = 0;
};

// One item in a section's ordered execution list.
// Either a dialog line (!isSet) or a /set command (isSet).
// Preserves the original CON file ordering so cinematics play before or after dialog
// exactly as authored.
struct ConSectionItem {
	bool           isSet = false;
	ConLine        line;    // valid when !isSet
	Common::String setCmd;  // valid when  isSet
	int32          offset = -1;  // position of the "/set" in ConScript::text()
};

// One term of an entry line's condition, used only to find ClicPerso and
// ClicZone; the evaluation itself reads the text (con_eval.cpp).
struct ConCondition {
	Common::String key;    // lowercase key name
	char           op;     // '=', '<', '>', '!' (not equal)
	int            value;
	char           nextSep = '\0';  // '&', '|', or '\0' (end of cond list)
};

// An entry point found by one of the original's scanners (/sel, /con,
// /tim1, /tim2), or a section header that no scanner reaches (isEntry false:
// only reachable through /go).  See atlantis-re: formats/scripts-con.md.
struct ConSection {
	int  id        = 0;       // the last "/N" before the entry line
	bool isEntry   = false;   // false: header only
	bool isCon     = false;   // /con or /Con (player-triggered)
	bool repeatable = false;  // /con* : not limited by the play counter
	int  conLimit  = 1;       // /conN : plays allowed by the counter (0 -> 1)
	bool isTimer   = false;   // /tim1, /Tim1, /tim2, /Tim2
	int  timerNum  = 0;       // 1 or 2
	int  clicPerso = 0;       // ClicPerso=N from conditions (0 = none); kept for interactNPC()
	int  clicZone  = 0;       // ClicZone=N from conditions (0 = none); for zone-click /con* sections
	int  sujId     = 0;       // /sujN subject gate (0 = always available;
	                          // first id from a comma-list; see sujIds)
	int  gotoId    = 0;       // /goN redirect (0 = no redirect)
	int32 condOffset = -1;    // where the scanner starts the evaluation (-1 = none)
	int32 lineOffset = -1;    // start of the entry line, or of the header
	int32 bodyOffset = -1;    // where the executor starts

	// /suj supports a comma-separated list (`/suj1,2` means "this
	// section fires when ANY of subjects 1 or 2 is enabled for the
	// clicked perso").  CHAPI005 section 113 is the canonical example:
	// `/con(FlagChp8=2)&(...) /suj1,2 /go89` -- the game-over branch is
	// only reachable when interactNPC() treats the section as available
	// for either suj 1 OR suj 2.  sujId == sujIds[0] for the menu icon.
	Common::Array<int>             sujIds;

	Common::Array<ConCondition>    conditions;  // terms of the entry line
	Common::Array<ConSectionItem>  items;       // what the section executor runs, up to /go
};

// Number of /con play counters: the byte array at 0x006cc6cc (0x5dc bytes).
static const uint kConPlayCounterCount = 0x5dc;

// FUN_004281f8: a /con entry is skipped while the play counter of its
// section has reached the entry's limit; /con* entries are never skipped.
// Sections past the end of the array read as 0 (the original reads other
// data there).  See atlantis-re: formats/scripts-con.md.
bool conEntryAllowed(const ConSection &sec, const Common::Array<byte> &counters);

class ConScript;

// Condition test supplied by the caller (the engine evaluates against its
// live state).
typedef bool (*ConEntryTest)(const ConSection &sec, void *ctx);

// FUN_00425cf4's choice after a zone click or with an object in hand: the
// index in sections() of the first /con entry the play counters allow, whose
// condition holds and whose body does not start with /Suj or /suj; -1 if none.
int findConClickEntry(const ConScript &script, const Common::Array<byte> &counters,
                      ConEntryTest test, void *ctx);

// FUN_004277c8 (the /sel scan, through the scanner FUN_004280f8): the index
// in sections() of the first /sel entry, in file order, whose condition
// holds; -1 if none.
int findSelEntry(const ConScript &script, ConEntryTest test, void *ctx);

// The track letter of a dialog line, the <L> of its voice, lip-sync and head
// files wav\ata<NNNN><L>.apc / .syc (FUN_0042f9e8 appends the letter byte
// 0x004961a8).  The section executor FUN_0042844c sets it to 'a' at the
// start (0x004284cb), adds 1 after each played line (0x00428b21) and per `[`
// line skipped after an Escape (0x00428b85); /go (FUN_00425c04, 0x00425c4b)
// sets it back to 'a'.  So it is the index of the line among the `[` lines
// of its part of the walk; the game-over narrator FUN_00426590 counts the
// same lines from the section header (0x004268b2..0x0042690f).  Upper case:
// the names in the BIG archives.
// See atlantis-re: experiments/2026-10-15-94-dialog-track-letters.md.
inline char conTrackLetter(uint lineIdx) {
	return (char)('A' + lineIdx);
}

// The narrator line of a game over: FUN_00426590 reads the line right after
// the /set(gameover=N) line when it starts with "[Narra" (0x004267e0..
// 0x004267f4) and takes its text from the byte after "[Narratrice]".
// items[setIdx] is the gameover= item; false when no such line follows.
// See atlantis-re: functions/scripts-FUN_00426590.md.
inline bool conGameOverNarrator(const Common::Array<ConSectionItem> &items, uint setIdx,
                                Common::String &text) {
	if (setIdx + 1 >= items.size() || items[setIdx + 1].isSet ||
	        !items[setIdx + 1].line.speaker.hasPrefix("Narra"))
		return false;
	text = items[setIdx + 1].line.text;
	return true;
}

// The 1999 build's alternative game-over narrations (FUN_00426570 of
// atlantis-gog-714752 @ 0x004267ce..0x00426882 and 0x004269e3; the 2001
// reference resets the counter before the test, 0x004267d9).  `count` is
// the per-section byte 0x6cccac[section]: 2 or more -> 0; n > 0 -> the
// line n lines after the first narrator line is used when it is also a
// narrator line (else the counter goes back to 0); then the counter is
// incremented.  Returns how many lines after the first narrator line the
// narration is (0 or n); the track letter grows by as much.  The first
// narration's letter is the reference's count of the section's lines; the
// 1999 build takes 0x004961a8 as left by the lines before, not shown to
// differ.  items[setIdx] is the gameover= item, items[setIdx + 1] its
// narrator line.  See atlantis-re: experiments/2026-10-17-08.
inline uint conAltGameOverNarration(const Common::Array<ConSectionItem> &items, uint setIdx,
                                    uint8 &count) {
	if (count >= 2)
		count = 0;
	uint pick = 0;
	if (count != 0) {
		const uint k = setIdx + 1 + count;
		if (k < items.size() && !items[k].isSet && items[k].line.speaker.hasPrefix("Narra"))
			pick = count;
		else
			count = 0;
	}
	count++;
	return pick;
}

// Escape during a talking-head line (FUN_0041a298 @ 0x0041a3c0 sets
// 0x00496134 while 0x00496188 == 1): after the line the section executor
// FUN_0042844c (0x00428b6d..0x00428bcd) walks the text up to the next '/'
// and adds 1 to the track letter for each '[' it passes, then runs that '/'
// line (/set or /go) and goes on.  In the item list: the dialog items from
// `next` on are skipped up to the first /set item (or the end of the items,
// where the /go or the next header is), each one counted in lineIdx.
// Returns the index of the item to run next.  Checked on every line of
// every walk of the game: no '/' stands inside a skipped line.
// See atlantis-re: experiments/2026-10-16-03-dialog-escape-skip.md.
inline uint conEscapeSkip(const Common::Array<ConSectionItem> &items, uint next, uint &lineIdx) {
	while (next < items.size() && !items[next].isSet) {
		lineIdx++;
		next++;
	}
	return next;
}

// The ArriveeVue of the place loop's first arrival scan after a chapter load
// (a checkpoint load, a game-over reload or a new game: all go through
// FUN_0042de5c): its chapter change FUN_00427634 sets ARRIVEEVUE to LastVue
// (0x0042777d), read from the checkpoint or 1 when there is none; the place
// loop FUN_00423ad8 runs the scan only when ARRIVEEVUE != 0 (0x00423b48).
// Returns false when no scan runs.
// See atlantis-re: functions/ui-FUN_0042de5c.md, experiments/2026-10-15-93.
inline bool arrivalAfterChapterLoad(uint lastVue, int &arrivee) {
	arrivee = (int)lastVue;
	return arrivee != 0;
}

// Parses a CON file buffer into sections and INIT commands.
class ConScript {
public:
	void parse(const char *buf, uint32 len);
	void reset();

	// FUN_00425c04 (/goN): the section that starts at the first "/N" of the
	// text, with the items the executor runs from there.  False if none.
	bool gotoSection(int id, ConSection &out) const;

	Common::Array<ConSection>       &sections()  { return _sections; }
	const Common::Array<ConSection> &sections()  const { return _sections; }
	const Common::Array<Common::String> &initCmds() const { return _initCmds; }
	const Common::Array<int32> &initCmdOffsets() const { return _initCmdOffsets; }
	// The file as loaded: the condition evaluator reads the condition lines
	// from it, as the original does.
	const Common::Array<char> &text() const { return _text; }

private:
	Common::Array<char>          _text;
	Common::Array<ConSection>    _sections;
	Common::Array<Common::String> _initCmds;
	Common::Array<int32>          _initCmdOffsets;
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_CON_SCRIPT_H
