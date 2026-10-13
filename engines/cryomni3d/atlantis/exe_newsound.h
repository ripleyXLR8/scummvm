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

#ifndef CRYOMNI3D_ATLANTIS_EXE_NEWSOUND_H
#define CRYOMNI3D_ATLANTIS_EXE_NEWSOUND_H

#include "common/array.h"
#include "common/str.h"
#include "common/stream.h"

namespace CryOmni3D {
namespace Atlantis {

// The newsound APC names of atlantis.exe: the named-resource pointer table at
// VA 0x004961e0 (file offset 0x773e0) holds 17 WAM names, then the APC names
// /set(newsound=N) indexes (0-based), up to a "FIN" entry.  The exe is the
// MD5-checked build (DGROUP at vaddr 0x90000, raw 0x71200).
inline void readNewSoundNames(Common::SeekableReadStream &exe, Common::Array<Common::String> &names) {
	const uint32 kImageBase    = 0x400000;
	const uint32 kDgroupVaddr  = 0x90000;
	const uint32 kDgroupRawPtr = 0x71200;
	const uint32 kTableFileOff = 0x773e0;
	const int kSkipWams = 17;

	names.clear();
	exe.seek(kTableFileOff + kSkipWams * 4);
	for (int i = 0; i < 200 && !exe.eos(); i++) {
		uint32 va = exe.readUint32LE();
		if (va < 0x4b0000 || va > 0x4d0000)
			break;  // off the end of the data
		uint32 fileOff = (va - kImageBase - kDgroupVaddr) + kDgroupRawPtr;
		int64 saved = exe.pos();
		exe.seek(fileOff);
		Common::String s;
		while (!exe.eos()) {
			byte b = exe.readByte();
			if (b == 0)
				break;
			s += (char)b;
		}
		if (s.equalsIgnoreCase("FIN"))
			break;
		names.push_back(s);
		exe.seek(saved);
	}
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
