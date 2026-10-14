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

#include "common/stream.h"
#include "common/debug.h"

#include "cryomni3d/cryomni3d.h"
#include "cryomni3d/atlantis/wam_parser.h"

namespace CryOmni3D {
namespace Atlantis {

// Binary WAM format (Atlantis), reverse-engineered from the loader in
// atlantis.exe (FUN at ~0x42xxxx, _GL_ReadFile_20 calls at decompile
// lines 18491-18560 + sub-WAM/refs/NPC sections at 18563-18596).
//
// Every record uses FIXED-SIZE fields — no C-string terminators are
// needed to walk the stream.  Names are stored in NUL-padded 13-byte
// slots; a name shorter than the slot is followed by NUL bytes that
// must be skipped, not interpreted as a record-end sentinel.
//
//   File header (16 bytes):
//     uint8[14] zeros
//     uint16 LE nPlaces
//
//   nPlaces × place record:
//     char[13]   cycloHnm (NUL-padded; sometimes leading NUL when the
//                          name is stored without its 'a' prefix —
//                          e.g. "\0tl16217.hnm")
//     uint16 LE  placeId
//     uint8      unk
//     uint8      nTransitions
//     nTransitions × 32-byte transition:
//       char[13] videoName  (NUL-padded)
//       uint16 LE dstPlaceId
//       uint8     flags
//       float32   srcAlpha, srcBeta, dstAlpha, dstBeta
//     uint8      nZones
//     nZones × 10-byte zone:
//       int16    x, y, w, h, action
//     uint8      nTrailing
//     nTrailing × 17-byte ambient record (skipped; pecheur-style places
//                                         carry these, ordinary places
//                                         have nTrailing == 0)
//
//   Sub-WAM cross-area transitions — always exactly 8 fixed 18-byte
//   entries (NOT sentinel-terminated; unused slots are zero-filled):
//     uint8     unk
//     uint16 LE srcPlaceId
//     uint16 LE dstPlaceId
//     char[13]  wamName
//
//   File-ref section:
//     uint8     nRefs
//     nRefs × 14-byte entry:  uint8 type (1=SPW, 2=SPF, 3=VUE) + char[13] name
//
//   NPC section:
//     uint8     nNPCs
//     nNPCs × 108-byte block:
//       uint16 LE index
//       uint32 LE persoId
//       char[13]  spriteFile
//       (rest = unrelated per-NPC fields, skipped)
//
// Earlier versions of this parser used C-string-with-sentinel logic and a
// signature scan to relocate past misaligned data — neither was correct.
// The file uses nPlaces from the header as authoritative; the leading-NUL
// names ("\0tl16NNN.hnm") are not sentinels but stub place records that
// sit alongside the proper "atl16NNN.hnm" copies (e.g. palais2.wam has 83
// places total — both a stub at offset 0x3d1 with placeId=0 AND a proper
// atl16217.hnm at offset 0x27d4 with placeId=217).  The sentinel-based
// parser bailed at the first stub and missed the proper copy, leaving
// CON-script transitions like vue 218 → 217 with no destination.

// A 13-byte NUL-terminated name, as FUN_00421ebc copies it: a name whose
// first byte is NUL is empty (92 records of 18 WAMs, all with id 0).
static void readFixedName13(Common::SeekableReadStream &s, Common::String &out) {
	char buf[14];
	s.read(buf, 13);
	buf[13] = 0;
	out = Common::String(buf);
}

void AtlantisWAMParser::clear() {
	_places.clear();
	_subWAMs.clear();
	_npcEntries.clear();
	spwListFile.clear();
	spfListFile.clear();
	centVueFile.clear();
}

void AtlantisWAMParser::loadStream(Common::SeekableReadStream &stream) {
	clear();

	for (int i = 0; i < 14; i++)
		stream.readByte();
	uint16 nPlaces = stream.readUint16LE();

	for (uint16 p = 0; p < nPlaces && !stream.err() && !stream.eos(); p++) {
		AtlantisPlace place;

		readFixedName13(stream, place.cycloHnm);
		place.placeId = stream.readUint16LE();
		/* unk = */     stream.readByte();
		uint8 nTransitions = stream.readByte();

		for (uint8 t = 0; t < nTransitions; t++) {
			AtlantisTransition trans;
			readFixedName13(stream, trans.videoName);
			trans.dstPlaceId = stream.readUint16LE();
			trans.flags      = stream.readByte();
			trans.srcAlpha   = stream.readFloatLE();
			trans.srcBeta    = stream.readFloatLE();
			trans.dstAlpha   = stream.readFloatLE();
			trans.dstBeta    = stream.readFloatLE();
			place.transitions.push_back(trans);
		}

		uint8 nZones = stream.readByte();
		for (uint8 z = 0; z < nZones; z++) {
			AtlantisZone zone;
			zone.x      = stream.readSint16LE();
			zone.y      = stream.readSint16LE();
			zone.w      = stream.readSint16LE();
			zone.h      = stream.readSint16LE();
			zone.action = stream.readSint16LE();
			place.zones.push_back(zone);
		}

		// The ambient-sound references (place +0x2b2 count, +0x2b3 records).
		uint8 nAmbient = stream.readByte();
		for (uint8 i = 0; i < nAmbient; i++) {
			AtlantisAmbientRef ref;
			ref.objectId = stream.readByte();
			/* the editor's pointer, replaced by FUN_0043187c = */ stream.readUint32LE();
			ref.t = stream.readFloatLE();
			ref.m = stream.readFloatLE();
			ref.c = stream.readFloatLE();
			place.ambientRefs.push_back(ref);
		}

		debugC(2, kDebugFile, "  place %u '%s' trans=%u zones=%u",
		      place.placeId, place.cycloHnm.c_str(), nTransitions, nZones);
		_places.push_back(place);
	}

	// Sub-WAM section: always 8 fixed 18-byte slots; unused slots are
	// zero-filled and produce src=0 (filtered out below).
	for (int s = 0; s < 8 && !stream.err() && !stream.eos(); s++) {
		/* unk = */              stream.readByte();
		uint16 srcPlace        = stream.readUint16LE();
		uint16 dstPlace        = stream.readUint16LE();
		Common::String name;
		readFixedName13(stream, name);
		if (srcPlace == 0)
			continue;
		AtlantisSubWAM sub;
		sub.srcPlaceId = srcPlace;
		sub.dstPlaceId = dstPlace;
		sub.wamName    = name;
		_subWAMs.push_back(sub);
		debugC(2, kDebugFile, "  sub-WAM: %u->%u '%s'", srcPlace, dstPlace, name.c_str());
	}

	if (stream.err() || stream.eos())
		return;

	// File-ref section: nRefs byte + nRefs × 14-byte entry.
	uint8 nRefs = stream.readByte();
	for (uint8 r = 0; r < nRefs; r++) {
		uint8 type = stream.readByte();
		Common::String name;
		readFixedName13(stream, name);
		if (type == 1)
			spwListFile = name;
		else if (type == 2)
			spfListFile = name;
		else if (type == 3)
			centVueFile = name;
		_fileRefs.push_back(name);
		debugC(2, kDebugFile, "  file-ref type=%u: '%s'", (uint)type, name.c_str());
	}

	if (stream.err() || stream.eos())
		return;

	// NPC section: nNPCs byte + nNPCs × 108-byte block.  Each block
	// carries far more than persoId+spriteFile (per-NPC dialog hooks,
	// animation tables, ...), but only those two fields feed the port.
	uint8 nNPCs = stream.readByte();
	for (uint8 n = 0; n < nNPCs; n++) {
		/* index = */    stream.readUint16LE();
		uint32 persoId = stream.readUint32LE();
		Common::String spriteName;
		readFixedName13(stream, spriteName);
		AtlantisNPCEntry npc;
		for (uint32 p = 2 + 4 + 13; p < 0x1e; p++)
			stream.readByte();
		for (uint k = 0; k < 17; k++) {
			uint32 bits = stream.readUint32LE();
			for (uint b = (k == 0) ? 1 : 0; b < 32; b++)
				if (bits >> b & 1)
					npc.vues.push_back((uint16)(32 * k + b));
		}
		npc.rectX = stream.readSint16LE();
		npc.rectY = stream.readSint16LE();
		npc.rectW = stream.readSint16LE();
		npc.rectH = stream.readSint16LE();
		for (uint32 p = 0x6a; p < 108; p++)
			stream.readByte();
		npc.persoId    = persoId;
		npc.spriteFile = spriteName;
		_npcEntries.push_back(npc);
		debugC(2, kDebugFile, "  NPC perso=%u sprite='%s'", persoId, spriteName.c_str());
	}

	if (stream.err() || stream.eos())
		return;

	// The 3D ambient sound objects: count byte + 100-byte records (0x0076f6c6).
	uint8 nObjects = stream.readByte();
	for (uint8 n = 0; n < nObjects && !stream.err() && !stream.eos(); n++) {
		AtlantisAmbientObject obj;
		obj.id = stream.readByte();
		readFixedName13(stream, obj.name);
		for (int k = 0; k < 3; k++)
			obj.pos[k] = stream.readFloatLE();
		obj.loudness = stream.readFloatLE();
		for (uint32 p = 0x1e; p < 0x2c; p++)   // the gains (outputs) and two zero bytes
			stream.readByte();
		for (int k = 0; k < 4; k++)
			obj.weights[k] = stream.readByte();
		for (int k = 0; k < 4; k++)
			readFixedName13(stream, obj.variants[k]);
		_ambientObjects.push_back(obj);
		debugC(2, kDebugFile, "  ambient object %u '%s'", obj.id, obj.name.c_str());
	}
}

void parseCentreVue(const byte *buf, uint32 size, Common::HashMap<int32, AtlantisCentrePos> &table) {
	// FUN_004315b4 @ 0x00431721..0x0043184a: atoi / strtod on a NUL-ended copy.
	Common::Array<char> text(size + 1);
	memcpy(text.data(), buf, size);
	text[size] = '\0';
	const char *t = text.data();
	auto at = [&](uint32 p, const char *word, uint32 n) {
		return p + n <= size && strncmp(t + p, word, n) == 0;
	};
	uint32 p = 0;
	if (size == 0)
		return;
	do {
		if (at(p, "frame", 5)) {
			p += 6;
			const int32 n = atoi(t + MIN(p, size));
			do {
				if (at(p, "camera", 6)) {
					p += 7;
					const float v1 = (float)strtod(t + MIN(p, size), nullptr);
					float v2 = 0.0f;
					do {
						if (at(p, " ", 1)) {
							p++;
							v2 = (float)strtod(t + p, nullptr);
							break;
						}
						p++;
					} while (size > p);
					do {
						if (at(p, " ", 1)) {
							p++;
							const float v3 = (float)strtod(t + p, nullptr);
							AtlantisCentrePos pos = { -v2, v1, -v3 };
							table[n] = pos;
							break;
						}
						p++;
					} while (size > p);
					break;
				}
				p++;
			} while (size > p);
		}
		p++;
	} while (size > p);
}

const AtlantisPlace *AtlantisWAMParser::findPlaceById(uint16 placeId) const {
	for (const AtlantisPlace &p : _places) {
		if (p.placeId == placeId)
			return &p;
	}
	return nullptr;
}

const AtlantisTransition *AtlantisPlace::findTransition(uint16 dstPlaceId) const {
	for (const AtlantisTransition &t : transitions) {
		if (t.dstPlaceId == dstPlaceId)
			return &t;
	}
	return nullptr;
}

uint16 AtlantisPlace::hitTest(int16 x, int16 y) const {
	// atlantis.exe FUN_00422bb0: the zone fields are sign-extended, then every
	// compare is unsigned (JC/JNC), so a zone with a negative x or y never
	// matches.  See atlantis-re: functions/FUN_00422bb0.md.
	const uint32 px = (uint32)(int32)x;
	const uint32 py = (uint32)(int32)y;
	uint16 best = 0;
	for (const AtlantisZone &z : zones) {
		const int32 zx = z.x, zy = z.y;
		if (px >= (uint32)zx && (uint32)(zx + z.w) >= px &&
		    py >= (uint32)zy && (uint32)(zy + z.h) >= py &&
		    (uint16)z.action > best)
			best = (uint16)z.action;
	}
	return best;
}

} // namespace Atlantis
} // namespace CryOmni3D
