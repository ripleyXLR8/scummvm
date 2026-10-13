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

#include "common/archive.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"

#include "cryomni3d/atlantis/bigfile.h"

namespace CryOmni3D {
namespace Atlantis {

BigFileArchive::BigFileArchive() {
}

BigFileArchive::~BigFileArchive() {
	close();
}

bool BigFileArchive::open(const Common::Path &path) {
	Common::File *file = new Common::File();
	if (!file->open(path)) {
		delete file;
		return false;
	}

	// Header: "BigFile 1.00\0\0\0\0" (16 bytes)
	char magic[12];
	file->read(magic, 12);
	if (memcmp(magic, "BigFile 1.00", 12) != 0) {
		warning("BigFileArchive: bad magic in '%s'", path.toString().c_str());
		file->close();
		delete file;
		return false;
	}
	file->seek(4, SEEK_CUR); // skip remaining 4 magic bytes

	// The file count, data length and header length fields are only printed
	// by cryo.dll's _GL_OpenBigFile (cryo-dll-543d8db2 @ 0x00416052).
	file->skip(16);

	const uint disc = _files.size();

	// Directory entries, eight uint32 fields then the name, until an entry
	// whose "next" field is 0.  The data starts after the directory, whose
	// length is recomputed as 0x20 + sum(nameLen + 0x20) (FUN_00415dac).  A
	// name already present (an earlier entry, or an earlier-mounted disc) is
	// skipped: _GL_OpenFileBigFile takes the first match.
	uint32 headerLen = 0x20;
	uint32 next;
	do {
		uint32 nameLen = file->readUint32LE();
		/* name pointer at pack time = */ file->readUint32LE();
		uint32 size    = file->readUint32LE();
		/* unused = */  file->readUint32LE();
		/* packed size = */ file->readUint32LE();
		uint32 flags   = file->readUint32LE();
		uint32 adr     = file->readUint32LE();
		next           = file->readUint32LE();
		if (file->eos() || file->err()) {
			warning("BigFileArchive: truncated directory in '%s'", path.toString().c_str());
			break;
		}

		// The name ends at its first NUL; names are compared upper-case
		// (FUN_0041d0d9 upper-cases a-z of both sides).
		Common::String name;
		bool ended = false;
		for (uint32 j = 0; j < nameLen; j++) {
			char c = (char)file->readByte();
			if (c == '\0')
				ended = true;
			else if (!ended)
				name += c;
		}
		name.toUppercase();
		headerLen += nameLen + 0x20;

		if (flags & 0x1000)
			warning("BigFileArchive: '%s' is LZW-packed, which is not supported", name.c_str());
		if (!_entries.contains(name)) {
			Entry e;
			e.adr  = adr;
			e.size = size;
			e.disc = disc;
			_entries[name] = e;
		}
	} while (next != 0);

	_files.push_back(file);
	_dataBases.push_back(headerLen);
	return true;
}

void BigFileArchive::close() {
	for (uint i = 0; i < _files.size(); i++) {
		_files[i]->close();
		delete _files[i];
	}
	_files.clear();
	_dataBases.clear();
	_entries.clear();
}

// static
Common::String BigFileArchive::normalizePath(const Common::Path &path) {
	Common::String s = path.toString('\\');
	s.toUppercase();
	return s;
}

bool BigFileArchive::hasFile(const Common::String &name) const {
	return _entries.contains(name);
}

bool BigFileArchive::hasFile(const Common::Path &path) const {
	return hasFile(normalizePath(path));
}

int BigFileArchive::listMembers(Common::ArchiveMemberList &list) const {
	int count = 0;
	for (Common::HashMap<Common::String, Entry>::const_iterator it = _entries.begin();
	        it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberPtr(
		    new Common::GenericArchiveMember(Common::Path(it->_key), *this)));
		++count;
	}
	return count;
}

const Common::ArchiveMemberPtr BigFileArchive::getMember(const Common::Path &path) const {
	if (!hasFile(path))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
}

Common::SeekableReadStream *BigFileArchive::createReadStreamForMember(const Common::Path &path) const {
	return createReadStreamForMember(normalizePath(path));
}

Common::SeekableReadStream *BigFileArchive::createReadStreamForMember(const Common::String &name) const {
	Common::HashMap<Common::String, Entry>::const_iterator it = _entries.find(name);
	if (it == _entries.end())
		return nullptr;

	const Entry &e = it->_value;
	if (e.disc >= _files.size())
		return nullptr;

	Common::File *file = _files[e.disc];
	file->seek(_dataBases[e.disc] + e.adr);

	byte *buf = (byte *)malloc(e.size);
	if (!buf)
		return nullptr;

	if (file->read(buf, e.size) != e.size) {
		free(buf);
		return nullptr;
	}

	return new Common::MemoryReadStream(buf, e.size, DisposeAfterUse::YES);
}

} // namespace Atlantis
} // namespace CryOmni3D
