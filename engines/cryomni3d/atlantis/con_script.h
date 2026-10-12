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
	// the cyclo view's alpha/beta before playing the line (atlantis.exe.c
	// FUN_004283e7 line 22381+22543, then FUN_00451915 line 47181 stores into
	// the global current-view angle).  Syntax in CON files:
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
	// The file as loaded: the condition evaluator reads the condition lines
	// from it, as the original does.
	const Common::Array<char> &text() const { return _text; }

private:
	Common::Array<char>          _text;
	Common::Array<ConSection>    _sections;
	Common::Array<Common::String> _initCmds;
};

} // namespace Atlantis
} // namespace CryOmni3D

#endif // CRYOMNI3D_ATLANTIS_CON_SCRIPT_H
