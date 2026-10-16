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

#include "cryomni3d/atlantis/music_tracks.h"

namespace CryOmni3D {
namespace Atlantis {

// Transcribed in full from atlantis.exe's table at VA 0x00495eec (13-byte
// records), upper-cased.
const char *const kMusicTrackNames[kMusicTrackCount] = {
	nullptr,           // index 0 unused
	"01ATLAN1.APC",    // track  1 — Atlantis area 1 ambient
	"10ATLAN2.APC",    // track  2
	"23_PALA1.APC",    // track  3
	"02AUBERG.APC",    // track  4
	"24_RATS.APC",     // track  5
	"DAUPHINS.APC",    // track  6
	"05SSTRON.APC",    // track  7
	"METAMORF.APC",    // track  8
	"LABYRINT.APC",    // track  9
	"31CRANES.APC",    // track 10
	"STONEHEG.APC",    // track 11
	"ASCENSE1.APC",    // track 12
	"FORET_35.APC",    // track 13
	"23_PRET1.APC",    // track 14
	"20SORCIE.APC",    // track 15
	"06SPITZ2.APC",    // track 16
	"30SHAMAN.APC",    // track 17
	"21PACQUE.APC",    // track 18
	"08TISSE2.APC",    // track 19
	"33INTRO.APC",     // track 20 — narration for CINEM020.HNM
	"HANGAR1.APC",     // track 21
	"04GENERI.APC",    // track 22
	"TETE1.APC",       // track 23
	"TETE2.APC",       // track 24
	"03ATTACK.APC",    // track 25
	"09VOLTOT.APC",    // track 26
	"CINE101.APC",     // track 27
	"LAC.APC",         // track 28
	"CINE147.APC",     // track 29
	"CINE172.APC",     // track 30
	"CINE194.APC",     // track 31
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D
