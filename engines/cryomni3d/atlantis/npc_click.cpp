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

#include "common/util.h"

#include "cryomni3d/atlantis/npc_click.h"

namespace CryOmni3D {
namespace Atlantis {

bool parseSpritePlaceLine(const Common::String &line, SpritePlaceEntry &entry) {
	if (line.empty() || line[0] == '#' || line[0] == ';')
		return false;
	Common::Array<Common::String> tok;
	const char *tp = line.c_str();
	while (*tp) {
		while (*tp == ' ' || *tp == '\t') tp++;
		if (!*tp) break;
		const char *te = tp;
		while (*te && *te != ' ' && *te != '\t') te++;
		tok.push_back(Common::String(tp, (uint32)(te - tp)));
		tp = te;
	}
	if (tok.size() < 4)
		return false;

	entry.placeId = (uint16)atoi(tok[0].c_str());
	entry.type    = atoi(tok[1].c_str());
	entry.frames  = atoi(tok[2].c_str());
	entry.icon    = 0;
	entry.persoId = 0;
	entry.interactive = false;
	// Trailing '-' on the placeId token marks the sprite as start-inactive.
	entry.startActive = !tok[0].empty() && tok[0][tok[0].size() - 1] != '-';
	entry.active = entry.startActive;
	entry.frame  = 0;

	// Two row formats, distinguished by token count (never 5 or 7):
	//   4 tokens: placeId type frames spwFile
	//   6 tokens: placeId type frames -persoId angle spwFile
	if (tok.size() >= 6) {
		int signedId  = atoi(tok[3].c_str());
		entry.persoId = (signedId < 0) ? -signedId : signedId;
		entry.icon    = (uint8)ABS(atoi(tok[4].c_str()));
		// A leading '-' on the angle token marks the NPC's home (clickable)
		// place — atof drops the sign on a zero magnitude, so check the raw
		// token.
		entry.interactive = (!tok[4].empty() && tok[4][0] == '-');
		entry.spwFile = tok[5];
		entry.idMarked = (tok[3][0] == '-');
		// The rest of the line after the fifth token and one separator.
		const char *rest = line.c_str();
		for (uint t = 0; t < 5; t++) {
			while (*rest == ' ' || *rest == '\t') rest++;
			while (*rest && *rest != ' ' && *rest != '\t') rest++;
		}
		if (*rest == ' ' || *rest == '\t')
			rest++;
		entry.rawFile = rest;
	} else {
		entry.spwFile = tok[3];
	}

	return true;
}

void initNpcClickRects(const Common::Array<AtlantisNPCEntry> &npcs, Common::Array<NpcClickRect> &rects) {
	rects.resize(npcs.size());
	for (uint i = 0; i < npcs.size(); i++) {
		rects[i].x = npcs[i].rectX;
		rects[i].y = npcs[i].rectY;
		rects[i].w = npcs[i].rectW;
		rects[i].h = npcs[i].rectH;
	}
}

bool npcRowMatches(const Common::Array<AtlantisNPCEntry> &npcs, const SpritePlaceEntry &row) {
	if (!row.idMarked || !row.interactive)
		return false;
	for (uint i = 0; i < npcs.size(); i++)
		if ((int)npcs[i].persoId == row.persoId && npcs[i].spriteFile == row.rawFile)
			return true;
	return false;
}

bool readSpwFrame0(Common::SeekableReadStream &spw, SpwFrame0 &f) {
	if (!spw.seek(0x14))
		return false;
	uint32 off = spw.readUint32LE();
	if (spw.err() || !spw.seek(off))
		return false;
	f.x = (int16)spw.readUint16LE();
	spw.readUint16LE();
	f.f4 = spw.readSint32LE();
	f.f8 = spw.readSint32LE();
	f.y = (int16)spw.readUint16LE();
	return !spw.err();
}

void applyNpcRowRect(const Common::Array<AtlantisNPCEntry> &npcs, const SpritePlaceEntry &row,
                     const SpwFrame0 &f, Common::Array<NpcClickRect> &rects) {
	if (!row.idMarked || !row.interactive)
		return;
	for (uint i = 0; i < npcs.size() && i < rects.size(); i++) {
		if ((int)npcs[i].persoId != row.persoId || npcs[i].spriteFile != row.rawFile)
			continue;
		rects[i].x = f.x;
		rects[i].y = f.y;
		rects[i].w = (int16)(((int32)f.x <= f.f8) ? f.f8 - f.x : f.x - f.f8);
		rects[i].h = (int16)(((int32)f.y <= f.f4) ? f.f4 - f.y : f.y - f.f4);
	}
}

uint32 npcHitTest(const Common::Array<AtlantisNPCEntry> &npcs, const Common::Array<NpcClickRect> &rects,
                  const Common::Array<SpritePlaceEntry> &entries, uint16 vue, int x, int y) {
	const uint32 px = (uint32)(int32)x, py = (uint32)(int32)y;
	for (uint i = 0; i < npcs.size() && i < rects.size(); i++) {
		// FUN_0045240c enumerates the views of the FIRST entry with this id.
		const AtlantisNPCEntry *first = &npcs[i];
		for (uint k = 0; k < i; k++)
			if (npcs[k].persoId == npcs[i].persoId) { first = &npcs[k]; break; }
		bool inView = false;
		for (uint16 v : first->vues)
			if (v == vue) { inView = true; break; }
		if (!inView)
			continue;
		for (const SpritePlaceEntry &e : entries) {
			if (e.placeId != vue || !e.active)
				continue;
			if (e.persoId != (int)npcs[i].persoId || npcs[i].spriteFile != e.rawFile)
				continue;
			const NpcClickRect &r = rects[i];
			if (px >= (uint32)(int32)r.x && px <= (uint32)((int32)r.x + r.w) &&
			        py >= (uint32)(int32)r.y && py <= (uint32)((int32)r.y + r.h))
				return npcs[i].persoId;
		}
	}
	return 0;
}

void objectPickupRows(const Common::Array<SpritePlaceEntry> &entries, uint16 vue, int id,
                      Common::Array<uint> &rows) {
	rows.clear();
	for (uint i = 0; i < entries.size(); i++)
		if (entries[i].placeId == vue && entries[i].persoId == id && entries[i].type != 3)
			rows.push_back(i);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
