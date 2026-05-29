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
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "engines/util.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"

#include "cryomni3d/atlantis/engine.h"

namespace CryOmni3D {
namespace Atlantis {

// BigFile sub-directory for each FileType (see openBigFileStream()).  Indexed
// by the FileType enum; names are stored uppercase as in the DOS archive.
static const char *const kBigFileSubdirs[] = {
	"CYCLO\\",         // kFileTypeCyclo
	"UBB_VUE\\",       // kFileTypeTransition
	"DIALOG\\",        // kFileTypeDialog
	"SYC\\",           // kFileTypeSync
	"FONTS\\",         // kFileTypeFont
	"SPRLIST\\",       // kFileTypeSprite
	"WAM\\",           // kFileTypeWAM
	"OBJETS\\",        // kFileTypeObject
	"MENU\\",          // kFileTypeMenu
	"SCENAR\\",        // kFileTypeScript
	"WAV\\",           // kFileTypeSound
	"SPRITE\\CYCLO\\", // kFileTypeSpriteCyclo
	"SPRITE\\UBB\\",   // kFileTypeSpriteUbb
	"SPRITE\\2D\\",    // kFileTypeSprite2D
	"IMAGES\\",        // kFileTypeImages
	"PUZZLES\\",       // kFileTypePuzzles
};

CryOmni3DEngine_Atlantis::CryOmni3DEngine_Atlantis(OSystem *syst,
        const CryOmni3DGameDescription *gamedesc) :
	CryOmni3DEngine(syst, gamedesc),
	_mainPalette(nullptr),
	_warpLoaded(false),
	_propAnimFrames(0),
	_propAnimFrame(0),
	_propAnimNextMs(0) {
}

CryOmni3DEngine_Atlantis::~CryOmni3DEngine_Atlantis() {
	_cycloSurface.free();
	_warpSurface.free();
	delete[] _mainPalette;
}

bool CryOmni3DEngine_Atlantis::hasFeature(EngineFeature f) const {
	// No kSupportsSavingDuringRuntime: Atlantis autosaves on chapter
	// transitions and offers no manual save anywhere — not even via the
	// ScummVM global menu.  Loading is still permitted.
	return CryOmni3DEngine::hasFeature(f)
	       || (f == kSupportsLoadingDuringRuntime);
}

Common::Path CryOmni3DEngine_Atlantis::getFilePath(FileType fileType,
        const Common::String &baseName) const {
	// Files outside the BigFile (none currently); placeholder for future expansion.
	return Common::Path(baseName);
}

Common::SeekableReadStream *CryOmni3DEngine_Atlantis::openBigFileStream(FileType fileType,
        const Common::String &baseName) const {
	uint typeIdx = (uint)fileType;
	if (typeIdx >= ARRAYSIZE(kBigFileSubdirs))
		return nullptr;

	Common::String fullName = kBigFileSubdirs[typeIdx];
	fullName += baseName;
	// The BigFile stores names case-sensitively as they appear in the DOS archive.
	// Convert to uppercase to match archive entries.
	fullName.toUppercase();

	return _bigFile.createReadStreamForMember(fullName);
}

void CryOmni3DEngine_Atlantis::setupPalette(const byte *colors, uint start, uint num) {
	// Atlantis runs in 16bpp true colour; the base palette helpers are
	// no-ops in that mode (see CryOmni3DEngine::setPalette).
	setPalette(colors, start, num);
}

void CryOmni3DEngine_Atlantis::makeTranslucent(Graphics::Surface &dst,
        const Graphics::Surface &src) const {
	// Translucency mapping is paletted-only; unused by the 16bpp skeleton.
	(void)dst;
	(void)src;
}

bool CryOmni3DEngine_Atlantis::loadCycloHNM(const Common::String &name) {
#ifdef USE_HNM
	Common::SeekableReadStream *stream = openBigFileStream(kFileTypeCyclo, name);
	if (!stream && !name.hasSuffix(".hnm") && !name.hasSuffix(".HNM")) {
		// Some WAM entries use .tga extension but the archive only has .hnm
		Common::String hnmName = name;
		const char *dot = strrchr(hnmName.c_str(), '.');
		if (dot) {
			hnmName = Common::String(hnmName.c_str(), dot - hnmName.c_str()) + ".hnm";
			stream = openBigFileStream(kFileTypeCyclo, hnmName);
		}
	}
	if (!stream) {
		warning("Cannot open cyclo HNM: %s", name.c_str());
		return false;
	}

	// HNM6 panoramas are 16-bit true color; use the screen pixel format.
	// HNMDecoder takes ownership of the stream when loadStream() is called.
	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Video::HNMDecoder *decoder = new Video::HNMDecoder(fmt);

	// loadStream takes ownership of stream immediately (even on failure).
	if (!decoder->loadStream(stream)) {
		warning("Cannot load cyclo HNM: %s", name.c_str());
		delete decoder;
		return false;
	}

	decoder->start();

	const Graphics::Surface *frame = nullptr;
	while (!frame && !decoder->endOfVideo()) {
		if (decoder->needsUpdate())
			frame = decoder->decodeNextFrame();
		else
			decoder->decodeNextFrame();
	}

	if (!frame) {
		warning("No frames in cyclo HNM: %s", name.c_str());
		delete decoder;
		return false;
	}

	_cycloSurface.free();
	_cycloSurface.copyFrom(*frame);
	_warpSurface.free();
	_warpSurface.copyFrom(*frame);

	if (decoder->hasDirtyPalette())
		memcpy(_mainPalette, decoder->getPalette(), 3 * 256);

	delete decoder;
	_warpLoaded = true;
	_propAnimFrames = 0;
	_propAnimFrame  = 0;
	_propAnimNextMs = 0;
	debugC(1, kDebugScript, "loadCycloHNM: %s (%dx%d)", name.c_str(), _warpSurface.w, _warpSurface.h);
	return true;
#else
	warning("HNM support not compiled in; cannot load cyclo %s", name.c_str());
	return false;
#endif
}

Common::Error CryOmni3DEngine_Atlantis::run() {
	CryOmni3DEngine::run();

	// Open the game's data archives.  The full game spans four discs
	// (BIGCD1-4.BIG), all merged into one index.  Disc 1 is required; discs
	// 2-4 are mounted if present.
	Common::Path bigFilePath("BIGCD1.BIG");
	if (!_bigFile.open(bigFilePath)) {
		bigFilePath = Common::Path("bigcd1.big");
		if (!_bigFile.open(bigFilePath))
			return Common::Error(Common::kPathDoesNotExist, "Cannot open BIGCD1.BIG");
	}
	for (int disc = 2; disc <= 4; disc++) {
		if (!_bigFile.open(Common::Path(Common::String::format("BIGCD%d.BIG", disc)))
		        && !_bigFile.open(Common::Path(Common::String::format("bigcd%d.big", disc))))
			warning("Atlantis: BIGCD%d.BIG not found - its chapters will be unavailable", disc);
	}
	debugC(1, kDebugScript, "Atlantis: data archives mounted, %u files total", _bigFile.numFiles());

	// Register with SearchMan so Common::File::open() can find archive files.
	SearchMan.add("Atlantis_BigFile", &_bigFile, 0, false);

	_omni3dMan.init(75. / 180. * M_PI);
	_mainPalette = new byte[3 * 256]();

	// Atlantis uses HNM6 16-bit true colour panoramas.
	Graphics::PixelFormat screenFmt(2, 5, 6, 5, 0, 11, 5, 0, 0); // RGB565
	initGraphics(640, 480, &screenFmt);
	_omni3dMan.setPixelFormat(screenFmt);

	// Load the chapter-1 navigation map and show its first place's cyclorama.
	// (Place transitions, zones and CON-driven flow land in later commits;
	// this skeleton just proves boot + archive + WAM + panorama + look.)
	Common::ScopedPtr<Common::SeekableReadStream> wamStream(
	    openBigFileStream(kFileTypeWAM, "ATLAN1.WAM"));
	if (!wamStream) {
		SearchMan.remove("Atlantis_BigFile");
		return Common::Error(Common::kReadingFailed, "Cannot open WAM\\ATLAN1.WAM");
	}
	_wam.loadStream(*wamStream);
	if (_wam.getPlaces().empty()) {
		SearchMan.remove("Atlantis_BigFile");
		return Common::Error(Common::kReadingFailed, "ATLAN1.WAM has no places");
	}

	const AtlantisPlace &place = _wam.getPlaces()[0];
	if (loadCycloHNM(place.cycloHnm)) {
		_omni3dMan.setSourceSurface(&_warpSurface);
		setupPalette(_mainPalette, 0, 256);
	}
	// Full 360-degree horizontal rotation; vertical tilt limited to +/-45 degrees.
	_omni3dMan.setBetaMinConstraint(-M_PI / 4.);
	_omni3dMan.setBetaMaxConstraint(M_PI / 4.);
	setMousePos(Common::Point(320, 240));

	showMouse(true);
	bool quit = false;
	while (!shouldAbort() && !quit) {
		pollEvents();
		Common::KeyState key;
		while ((key = getNextKey()).keycode != Common::KEYCODE_INVALID) {
			if (key.keycode == Common::KEYCODE_ESCAPE)
				quit = true;
		}

		// Edge-style look: steer the panorama toward the cursor's offset from
		// the screen centre (a placeholder for the full warp-cursor handler).
		Common::Point mouse = getMousePos();
		_omni3dMan.setAlpha(_omni3dMan.getAlpha() + (mouse.x - 320) * 0.0008);
		_omni3dMan.setBeta(_omni3dMan.getBeta()  - (mouse.y - 240) * 0.0008);

		const Graphics::Surface *s = _omni3dMan.getSurface();
		if (s)
			g_system->copyRectToScreen(s->getPixels(), s->pitch, 0, 0, s->w, s->h);
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	SearchMan.remove("Atlantis_BigFile");
	return Common::kNoError;
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
