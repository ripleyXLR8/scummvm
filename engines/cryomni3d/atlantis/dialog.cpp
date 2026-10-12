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

// In-game conversation playback.  This grows over several commits: it starts
// with the per-character dialog resource table (INITDIAL.TXT) and the dialog
// camera picker; per-line UBB talking-head playback, the subject (topic) menu
// and the F3DC mouth pipeline are layered on as their subsystems land.

#include "common/array.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/stream.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "audio/decoders/apc.h"
#include "audio/mixer.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#ifdef USE_HNM
#include "video/hnm_decoder.h"
#endif

#include "cryomni3d/omni3d.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Build the per-character dialog resource table from DIALOG\INITDIAL.TXT.  Each
// row is whitespace-separated:
//   persoId unk anim3DC anim3DA animM s3d cam2 cam3 cam4 cam5 [spf2..spf5]
// "none" tokens are the original engine's "unavailable" sentinel and map to
// empty strings here.
void CryOmni3DEngine_Atlantis::loadDialInitData() {
	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeDialog, "INITDIAL.TXT"));
	if (!s) {
		warning("loadDialInitData: cannot open DIALOG\\INITDIAL.TXT");
		return;
	}

	uint32 sz = (uint32)s->size();
	Common::Array<char> buf(sz + 1);
	s->read(buf.data(), sz);
	buf[sz] = '\0';

	const char *p = buf.data();
	while (*p) {
		while (*p && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')) p++;
		if (!*p) break;

		Common::Array<Common::String> tok;
		while (*p && *p != '\r' && *p != '\n') {
			while (*p && (*p == ' ' || *p == '\t')) p++;
			if (!*p || *p == '\r' || *p == '\n') break;
			const char *te = p;
			while (*te && *te != ' ' && *te != '\t' && *te != '\r' && *te != '\n') te++;
			tok.push_back(Common::String(p, (uint32)(te - p)));
			p = te;
		}

		if (tok.empty() || tok[0].equalsIgnoreCase("FIN")) break;
		if (tok.size() < 10) continue;

		int persoId = atoi(tok[0].c_str());
		if (persoId <= 0) continue;

		// The player character ("hero") is detected by the s3d field.
		if (tok[5].equalsIgnoreCase("hero"))
			_heroPersoId = persoId;

		DialInitEntry entry;
		for (int i = 0; i < 4; i++)
			entry.camUBB[i] = tok[6 + i];      // cam2..cam5 basenames

		// Column 4 = animM basename (mouth vertex animation, DIALOG\PERSO\<base>.3DA).
		if (!tok[4].equalsIgnoreCase("none"))
			entry.animMBase = tok[4];
		// Column 5 = s3d basename (scene-graph mesh, DIALOG\PERSO\<base>.S3D).
		if (!tok[5].equalsIgnoreCase("none"))
			entry.s3dBase = tok[5];
		// Column 2 = anim3DC basename (body mesh, DIALOG\PERSO\<base>.3DC).
		if (!tok[2].equalsIgnoreCase("none"))
			entry.anim3DCBase = tok[2];
		// Columns 10..13 = per-cam SPF overlay basenames (optional).
		for (int i = 0; i < 4; i++) {
			if (10 + i < (int)tok.size() && !tok[10 + i].equalsIgnoreCase("none"))
				entry.camSPF[i] = tok[10 + i];
		}

		_dialInitData[persoId] = entry;
	}

	debugC(1, kDebugScript, "loadDialInitData: %u entries from INITDIAL.TXT, heroPersoId=%d",
	      _dialInitData.size(), _heroPersoId);
}

// Per-line camera picker.  Re-implements the random-cam loop from atlantis.exe
// FUN_0042844c (lines 22460-22470):
//
//   while (cam == prevCam || INITDIAL[persoId].camUBB[cam-2] is "none")
//       cam = (rand() & 3) + 2;          // 2..5
//
// An explicit `camN` from a `[Speaker, X, Y, camN]` directive is honoured when
// it is valid and available; otherwise we fall through to the random pick.
// Returns 2..5.
int CryOmni3DEngine_Atlantis::pickDialCam(int persoId, int explicitCam) {
	bool avail[4] = { false, false, false, false };
	int availCount = 0;
	Common::HashMap<int, DialInitEntry>::const_iterator it = _dialInitData.find(persoId);
	if (it != _dialInitData.end()) {
		for (int i = 0; i < 4; i++) {
			const Common::String &s = it->_value.camUBB[i];
			if (!s.empty() && !s.equalsIgnoreCase("none")) {
				avail[i] = true;
				availCount++;
			}
		}
	}

	// Explicit cam: honour it if valid + available (or if we have no data at all).
	if (explicitCam >= 2 && explicitCam <= 5) {
		if (availCount == 0 || avail[explicitCam - 2])
			return explicitCam;
	}

	// No INITDIAL data → deterministic cam2 close-up.
	if (availCount == 0)
		return 2;

	// Random pick — same `(rand & 3) + 2` shape, rejecting the previous cam
	// and unavailable cams.
	int cam = 2;
	for (int tries = 0; tries < 16; tries++) {
		cam = (int)_dialRng.getRandomNumber(3) + 2;   // 2..5
		if (cam == _lastDialCam && availCount > 1)
			continue;
		if (avail[cam - 2])
			return cam;
	}
	// Fallback: first available cam, preferring one other than the last.
	for (int c = 2; c <= 5; c++)
		if (avail[c - 2] && c != _lastDialCam)
			return c;
	for (int c = 2; c <= 5; c++)
		if (avail[c - 2])
			return c;
	return 2;
}

void CryOmni3DEngine_Atlantis::drawGivenObject(Graphics::Surface &dst) const {
	if (_dialogGivenObjSprite < 0 || (uint)_dialogGivenObjSprite >= _objSprites.size())
		return;
	const SubjSprite &spr = _objSprites[(uint)_dialogGivenObjSprite];
	if (spr.pixels.empty())
		return;
	// Centre horizontally; anchor the icon's bottom a fixed gap above the
	// screen edge, above the subtitle strip (the original's DAT_0049619c
	// overlay; the exact Y isn't recoverable so we use a fixed inset).
	const int cx = dst.w / 2;
	const int objBottom = dst.h - 56;
	const int cy = objBottom - ((int)spr.h - spr.yoff);
	const int dx = cx - spr.xoff;
	const int dy = cy - spr.yoff;
	for (int y = 0; y < (int)spr.h; y++) {
		int sy = dy + y;
		if (sy < 0 || sy >= dst.h) continue;
		for (int x = 0; x < (int)spr.w; x++) {
			int sx = dx + x;
			if (sx < 0 || sx >= dst.w) continue;
			uint sidx = y * spr.w + x;
			uint16 pix = spr.pixels[sidx];
			if (pix == kSprTransp) continue;
			uint16 *dp = (uint16 *)dst.getBasePtr(sx, sy);
			if (spr.blend[sidx] != kSprNoBlend)
				pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
			*dp = pix;
		}
	}
}

void CryOmni3DEngine_Atlantis::drawSubjectSprite(Graphics::Surface &dst,
        uint sprIdx, int cx, int cy) const {
	if (sprIdx >= _subjSprites.size())
		return;
	const SubjSprite &spr = _subjSprites[sprIdx];
	if (spr.pixels.empty())
		return;
	// cx,cy = centre point; xoff,yoff = hotspot offset from top-left.
	int dx = cx - spr.xoff;
	int dy = cy - spr.yoff;
	for (int y = 0; y < (int)spr.h; y++) {
		int sy = dy + y;
		if (sy < 0 || sy >= dst.h) continue;
		for (int x = 0; x < (int)spr.w; x++) {
			int sx = dx + x;
			if (sx < 0 || sx >= dst.w) continue;
			uint sidx = y * spr.w + x;
			uint16 pix = spr.pixels[sidx];
			if (pix == kSprTransp) continue;
			uint16 *dp = (uint16 *)dst.getBasePtr(sx, sy);
			if (spr.blend[sidx] != kSprNoBlend)
				pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
			*dp = pix;
		}
	}
}

// Subject-choice menu, reproduced from atlantis.exe's subject grid (draw at
// FUN_004261a0, hit-test at FUN_004211f0):
//  - Each subject is two sprites: the border (SUJETS.SPR sprite 0) at the
//    cell top-left, then the icon (sprite = sujId) at the cell centre.
//  - Cells are 50x50, 4 rows per column, columns step 50px in X from X=450.
//  - For <=4 subjects the single column is bottom-anchored (startY=330-50*N);
//    for >=5 the grid starts at Y=180.  No hover highlight; right-click cancels.
// Returns the chosen option index, or -1 on cancel.
int CryOmni3DEngine_Atlantis::showSubjectMenu(const Common::Array<ConSection *> &options) {
	const int kScreenW = 640, kScreenH = 480;
	const uint nOpts = options.size();
	if (nOpts == 0)
		return -1;

	const int kBorderX  = 450;  // exe 0x1c2 — column-0 cell origin X
	const int kColStepX = 50;
	const int kRowStepY = 50;
	const int kCell     = 50;
	const int kIconDX   = 25;   // icon anchor = cell top-left + (25,25)
	const int kIconDY   = 25;
	const int kPerCol   = 4;
	const int startY = (nOpts < 5) ? (int)(330 - 50 * nOpts) : 180;

	struct IconLayout { int bx, by; uint optIdx; };
	Common::Array<IconLayout> layout;
	for (uint i = 0; i < nOpts; i++) {
		int col = (int)i / kPerCol;
		int row = (int)i % kPerCol;
		layout.push_back({kBorderX + col * kColStepX, startY + row * kRowStepY, i});
	}

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface frame(kScreenW, kScreenH, fmt);

	showMouse(true);
	setArrowCursor();

	// Drain the press that opened the menu so it isn't read as an immediate
	// (and missing) selection that closes the menu the instant it appears.
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}

	int result = -1;
	bool done = false;
	while (!done && !shouldAbort()) {
		pollEvents();

		// Panoramic background → frame buffer.
		const Graphics::Surface *bg = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
		if (bg)
			frame.blitFrom(*bg);
		else
			frame.fillRect(Common::Rect(kScreenW, kScreenH), 0);

		Graphics::Surface *rawPtr = frame.surfacePtr();
		for (const IconLayout &il : layout) {
			drawSubjectSprite(*rawPtr, 0, il.bx, il.by);
			int sujId = options[il.optIdx]->sujId;
			uint sprIdx = (sujId > 0 && (uint)sujId < _subjSprites.size())
			              ? (uint)sujId : 0;
			drawSubjectSprite(*rawPtr, sprIdx, il.bx + kIconDX, il.by + kIconDY);
		}

		g_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, kScreenW, kScreenH);
		g_system->updateScreen();

		if (getCurrentMouseButton() == 1) {
			Common::Point click = getMousePos();
			while (getCurrentMouseButton() != 0 && !shouldAbort()) {
				pollEvents();
				g_system->delayMillis(10);
			}
			for (const IconLayout &il : layout) {
				if (click.x >= il.bx && click.x < il.bx + kCell &&
				    click.y >= il.by && click.y < il.by + kCell) {
					result = (int)il.optIdx;
					break;
				}
			}
			done = true;
		} else if (getCurrentMouseButton() == 2) {
			done = true;   // right-click cancels
		}
		g_system->delayMillis(10);
	}

	showMouse(false);
	return result;
}

// Play a fired CON section: run its /set commands and dialog lines in source
// order, then follow any /go redirect.  The game-over screen and the F3DC
// mouth pipeline layer on as their subsystems land.
void CryOmni3DEngine_Atlantis::playConSection(ConSection &sec) {
	// Hide the cursor for the duration of the section; restore it after.
	bool cursorWasVisible = showMouse(false);

	// Drain the gesture that triggered us so the first line isn't immediately
	// skipped by a still-held click (departure /sel sections fire mid-click).
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	clearKeys();

	// A /set(chapter=N) reloads _conScript and frees `sec`; remember the entry
	// chapter so the loop stops the instant that happens.
	const uint entryChapter = _currentCONChapter;
	int lineIdx = 0;
	// A /set(gameover=N) with N != 0 ends the section (atlantis.exe FUN_0042844c
	// breaks the line loop once the GameOver variable goes non-zero); the lines
	// that follow feed the game-over screen instead of playing as dialog.
	int            pendingGameOver = 0;
	Common::String gameOverText;
	char           gameOverTrack   = 'A';
	for (uint itemIdx = 0; itemIdx < sec.items.size(); ++itemIdx) {
		const ConSectionItem &item = sec.items[itemIdx];
		if (shouldAbort())
			break;

		if (item.isSet) {
			// /set(gameover=N), N != 0: trigger the game-over screen
			// (FUN_00426590).  Its narrator commentary is the line right after
			// the command when it starts with "[Narra" (@ 0x004267ea); the APC
			// track is 'a' + the number of '[' from the section header to it,
			// which is the dialog line index for all 144 game overs shipped.
			if (scumm_strnicmp(item.setCmd.c_str(), "gameover=", 9) == 0 &&
			        atoi(item.setCmd.c_str() + 9) != 0) {
				execConSetCommand(item.setCmd, item.offset);
				pendingGameOver = atoi(item.setCmd.c_str() + 9);
				if (itemIdx + 1 < sec.items.size() && !sec.items[itemIdx + 1].isSet &&
				        sec.items[itemIdx + 1].line.speaker.hasPrefix("Narra")) {
					gameOverText  = sec.items[itemIdx + 1].line.text;
					gameOverTrack = (char)('A' + lineIdx);
				}
				break;
			}
			execConSetCommand(item.setCmd, item.offset);
			// A chapter reload reparsed _conScript: `sec`/`item`/the section
			// list are dangling now.  Stop at once.
			if (_currentCONChapter != entryChapter)
				break;
			continue;
		}

		// Dialog line — resolve the on-screen speaker and the talking-head shot.
		// sec.clicPerso is deliberately not used: a dialog freely interleaves
		// lines from several characters, so only the speaker name identifies
		// who is talking on this line.
		int persoId = resolveSpeakerPersoId(item.line.speaker);
		int explicitCam = 0;
		{
			const char *camTag = strstr(item.line.params.c_str(), "cam");
			if (camTag) sscanf(camTag, "cam%d", &explicitCam);
			if (explicitCam < 2 || explicitCam > 5) explicitCam = 0;
		}
		int camNum = pickDialCam(persoId, explicitCam);

		// Off-camera variants (off / off0 / off1) play as voiceover: no head
		// shot, no FOV change, view left where the player was facing.
		const bool isOffCam = item.line.params.equalsIgnoreCase("off")
		                   || item.line.params.equalsIgnoreCase("off0")
		                   || item.line.params.equalsIgnoreCase("off1");

		Common::String vid;
		Common::String spfBase;
		if (persoId > 0 && !isOffCam) {
			Common::HashMap<int, DialInitEntry>::const_iterator it =
			    _dialInitData.find(persoId);
			if (it != _dialInitData.end()) {
				Common::String fn = it->_value.camUBB[camNum - 2];
				if (!fn.empty() && !fn.equalsIgnoreCase("none")) {
					fn.toUppercase();
					vid = Common::String("PERSO\\") + fn + ".UBB";
				}
				// Per-cam SPF overlay (INITDIAL columns 10..13).  The parser
				// already strips "none" to empty, so a non-empty basename
				// here means we have an overlay to composite.
				spfBase = it->_value.camSPF[camNum - 2];
			}
		}
		// Remember for the next line so consecutive lines don't repeat a cam.
		_lastDialCam = camNum;

		// Per-line panorama angle (atlantis.exe FUN_0042844c):
		//   • explicit [Speaker, X, Y, camN] → alpha=X°, beta=Y°
		//   • bare / camN-only → preserve the click-time alpha, beta=0, with
		//     hero (reverse-shot) lines flipped 180°
		//   • off-camera → no change (view stays where the player aimed)
		bool effHasAngle = item.line.hasAngle;
		int  effAngleX   = item.line.angleX;
		int  effAngleY   = item.line.angleY;
		const bool isHero = (persoId == _heroPersoId && _heroPersoId > 0);
		if (!effHasAngle && _warpLoaded && !isOffCam) {
			// _dialBaseAlpha is the click-time facing during an NPC interaction
			// (set by the NPC layer that lands later); /sel-triggered dialog
			// falls back to the live alpha.
			const double baseAlpha = _dialBaseAlphaValid
			                       ? _dialBaseAlpha : _omni3dMan.getAlpha();
			double effDegX = baseAlpha * 180.0 / M_PI;
			if (isHero)
				effDegX += 180.0;
			double dx = effDegX - 360.0 * floor(effDegX / 360.0);
			effHasAngle = true;
			effAngleX   = (int)(dx + 0.5);
			effAngleY   = 0;
		}
		// camNum=0 lands outside playSingleConLine's 2..5 range, so the FOV
		// change is skipped while the line still plays as voiceover.
		const int effCamNum = isOffCam ? 0 : camNum;
		debugC(2, kDebugScript, "playConSection: speaker='%s' params='%s' persoId=%d "
		         "cam=%d vid='%s' spf='%s' angle=(%d,%d)%s",
		      item.line.speaker.c_str(), item.line.params.c_str(), persoId,
		      effCamNum, vid.c_str(), spfBase.c_str(), effAngleX, effAngleY,
		      effHasAngle ? "" : " (none)");
		playSingleConLine(sec.id, (char)('A' + lineIdx++), item.line.text, vid,
		                  spfBase,
		                  effHasAngle, effAngleX, effAngleY, effCamNum,
		                  /*interactiveDialog=*/sec.isCon);
	}

	// Game-over screen (GereGameOver): show the verdict image + narrator, then
	// reload the chapter checkpoint.  Run after the loop so the click/key that
	// ended the last dialog line has already drained.
	if (pendingGameOver != 0) {
		showGameOver(sec.id, pendingGameOver, gameOverText, gameOverTrack);
		_gameVars["gameover"] = 0;   // restored to its pre-section value
		// A game-over is gated on a persistent CON variable, so resuming in
		// place would loop straight back into the death — reload the chapter
		// checkpoint (the per-episode autosave), falling back to the player's
		// resume save, or a fresh start when no checkpoint exists yet.
		int ckSlot = (_currentPlayer >= 0)
		           ? episodeSaveSlot((uint)_currentPlayer, _currentCONChapter) : -1;
		uint reload = (ckSlot >= 0 && saveSlotExists(ckSlot))
		            ? (uint)ckSlot + 1
		            : (_currentPlayer >= 0 ? playerResumeSave((uint)_currentPlayer) : 0);
		if (reload != 0) {
			_loadedSave   = reload;
			_abortCommand = kAbortLoadGame;
		} else {
			warning("playConSection: game-over with no checkpoint; restarting");
			_abortCommand = kAbortNewGame;
		}
	}

	// Restore the cursor to whatever it was before the section.
	showMouse(cursorWasVisible);

	// /go N — chain to the continuation section once this body finishes.
	// Test the chapter guard FIRST: a /set(chapter=N) in the body reparsed
	// _conScript and left `sec` dangling, so reading sec.gotoId before the
	// guard would be a use-after-free.  Skip the chain on a pending game-over
	// (the reload replays the chapter) or any pending abort.
	if (_currentCONChapter == entryChapter && sec.gotoId > 0 && !shouldAbort() &&
	        pendingGameOver == 0 && _abortCommand == kAbortNoAbort) {
		// The executor resumes at the first "/N" header of the text
		// (atlantis.exe FUN_00425c04).
		ConSection target;
		if (_conScript.gotoSection(sec.gotoId, target)) {
			for (const ConSection &s : _conScript.sections()) {
				if (s.id == sec.gotoId) {
					target.isCon = s.isCon;
					break;
				}
			}
			playConSection(target);
		}
	}

	// Drain the skip click that ended the last line so the outer warp loop
	// doesn't read it as a fresh navigation press.
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	clearKeys();
}

// Single dialog line: stream a UBB talking-head video over the warp panorama,
// play the matching APC speech track and overlay the subtitle.
//
// All geometry is data-driven: the UBB header gives the composite size, the
// screen format drives the CLUT8 conversion, and the GUI font drives the
// subtitle metrics.  The UBB frame is decoded into a CLUT8 buffer; the IA
// chunk's FUN_00450b08 opcode stream drives the per-pixel SKIP/RUN composite
// (SKIP leaves the cyclo panorama showing through, RUN paints palette[idx]
// with optional edge anti-aliasing).  The animated mouth mesh and the SPF
// overlays layer on top once the F3DC pipeline lands.
void CryOmni3DEngine_Atlantis::playSingleConLine(int sectionId, char track,
                                                  const Common::String &text,
                                                  const Common::String &videoName,
                                                  const Common::String &spfBaseName,
                                                  bool hasAngle,
                                                  int angleX, int angleY,
                                                  int camNum,
                                                  bool interactiveDialog) {
	(void)interactiveDialog;  // used by the NPC sprite-hiding layer (later PR)
	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	const int screenW = (int)g_system->getWidth();
	const int screenH = (int)g_system->getHeight();

	// Per-cam FOV (zoom).  Source: atlantis.exe DGROUP VA 0x00496080, indexed
	// cam2..cam5 → table[0..3].  Save the current FOV so the panorama restores
	// to its navigation default after the line.
	static const float kCamFovDeg[4] = {20.68f, 30.0f, 48.0f, 48.26f};
	const double savedHFov = _omni3dMan.getHFov();
	bool fovChanged = false;
	if (_warpLoaded && camNum >= 2 && camNum <= 5) {
		_omni3dMan.setHFov((double)kCamFovDeg[camNum - 2] * M_PI / 180.0);
		fovChanged = true;
	}

	// Per-line panorama angle (data-driven; degrees → radians).
	if (_warpLoaded && hasAngle) {
		_omni3dMan.setAlpha(((double)angleX) * M_PI / 180.0);
		_omni3dMan.setBeta(((double)angleY) * M_PI / 180.0);
		_gameVars["anglewarpx"] = (int)(_omni3dMan.getAlpha() * 180.0 / M_PI);
		_gameVars["anglewarpy"] = (int)(_omni3dMan.getBeta()  * 180.0 / M_PI);
	}

#ifdef USE_HNM
	Video::HNMDecoder *bgDecoder = nullptr;
	Graphics::Surface ubbConvFrame;
	byte ubbPal[256 * 3];
	memset(ubbPal, 0, sizeof(ubbPal));

	if (!videoName.empty()) {
		Common::SeekableReadStream *vs = openBigFileStream(kFileTypeDialog, videoName);
		if (!vs) {
			debugC(1, kDebugScript, "playSingleConLine: %s not found in BigFile",
			      videoName.c_str());
		} else {
			// UBB2 is 8-bit paletted; decode into CLUT8 and convert per-pixel so
			// we can colour-key and bleed the cyclo through SKIP runs.
			Graphics::PixelFormat clut8 = Graphics::PixelFormat::createFormatCLUT8();
			bgDecoder = new Video::HNMDecoder(clut8, true);  // loop=true
			if (!bgDecoder->loadStream(vs)) {
				delete bgDecoder;
				bgDecoder = nullptr;
			} else {
				ubbConvFrame.create(bgDecoder->getWidth(), bgDecoder->getHeight(), fmt);
				// Opt in to IA-chunk opcode capture so the decoder stops
				// discarding the FUN_00450b08 SKIP/RUN stream as audio.
				bgDecoder->setCaptureOpcodeStream(true);
				bgDecoder->start();
				debugC(1, kDebugScript, "playSingleConLine: %s loaded (%ux%u)",
				      videoName.c_str(), bgDecoder->getWidth(), bgDecoder->getHeight());
			}
		}
	}

	// Optional per-cam SPF overlay — the dialog mouth animation that the
	// original engine composites on top of the talking head every UBB frame.
	// The UBB stream has the mouth region painted as palette index 0 (the
	// canonical Cryo chroma key); the SPF fills it in.  Files live alongside
	// the UBB at `DIALOG\PERSO\<base>.SPF` (24 of them on the shipped CD),
	// NOT under `SPRITE\UBB\` (that prefix holds the 205 transition-video
	// SPFs used by playTransitionVideo for navigation walk-ons).  Only ~24
	// dialog cams have a mouth SPF — for the rest, the mouth is baked into
	// the UBB itself, so a missing file is normal and not a warning.
	Common::SeekableReadStream *spfStream = nullptr;
	uint spfFrameCount = 0;
	uint spfFrameIdx = 0;
	// SPF screen bbox — the rectangle the overlay actually paints into on
	// screen.  The dialog SPF frame headers we sampled (BAPAN1K3.SPF,
	// GRDAN1C5.SPF) have ZEROED (x1, y_bot, x2, y_top) fields, unlike
	// navigation SPFs: they position themselves entirely via ROW_SEP opcodes
	// in the flat-buffer pixel stream, so the bbox only emerges from
	// rendering.  Probe by compositing frame[0] onto a scratch surface
	// seeded with a sentinel colour the SPF can never emit, then scan for
	// non-sentinel pixels.  Diagnostics only.
	int spfBboxX1 = -1, spfBboxY1 = -1, spfBboxX2 = -1, spfBboxY2 = -1;
	if (!spfBaseName.empty()) {
		Common::String spfFile = spfBaseName;
		spfFile.toUppercase();
		Common::String spfPath = Common::String("PERSO\\") + spfFile + ".SPF";
		spfStream = openBigFileStream(kFileTypeDialog, spfPath);
		if (spfStream) {
			if (spfStream->seek(4))
				spfFrameCount = spfStream->readUint32LE();
			// Composite frame[0] onto a scratch RGB565 surface seeded with
			// 0xFFFE.  SPF palettes emit RGB565 colours; 0xFFFE is reserved
			// (kSprTransp is 0x0001 and pure white is 0xFFFF), so any pixel
			// left at 0xFFFE was untouched and any other value was painted
			// by the SPF.
			Graphics::PixelFormat probeFmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
			Graphics::Surface probe;
			probe.create(640, 480, probeFmt);
			const uint16 sentinel = 0xFFFE;
			uint16 *probePix = (uint16 *)probe.getPixels();
			for (int i = 0; i < 640 * 480; i++) probePix[i] = sentinel;
			compositeSpfFrame(spfStream, 0, probe);
			for (int py = 0; py < 480; py++) {
				const uint16 *row = (const uint16 *)probe.getBasePtr(0, py);
				for (int px = 0; px < 640; px++) {
					if (row[px] != sentinel) {
						if (spfBboxX1 < 0 || px < spfBboxX1) spfBboxX1 = px;
						if (spfBboxX2 < 0 || px > spfBboxX2) spfBboxX2 = px;
						if (spfBboxY1 < 0 || py < spfBboxY1) spfBboxY1 = py;
						if (spfBboxY2 < 0 || py > spfBboxY2) spfBboxY2 = py;
					}
				}
			}
			probe.free();
			spfStream->seek(0);
		}
		debugC(1, kDebugScript, "playSingleConLine: SPF overlay '%s' %s "
		         "(%u frames, screen bbox [%d,%d]-[%d,%d])",
		      spfPath.c_str(),
		      spfStream ? "loaded" : "absent (mouth baked in UBB)",
		      spfFrameCount,
		      spfBboxX1, spfBboxY1, spfBboxX2, spfBboxY2);
	}
#endif

	// Subtitle pre-wrap.  Width budget is the composite width less symmetric
	// 5-px margins (matching the original's x=5 origin).
	const int subLeftMargin = 5, subRightMargin = 5;
	int subWidthBudget = screenW - subLeftMargin - subRightMargin;
#ifdef USE_HNM
	if (ubbConvFrame.getPixels())
		subWidthBudget = ubbConvFrame.w - subLeftMargin - subRightMargin;
#endif
	if (subWidthBudget < 32) subWidthBudget = 32;

	// Subtitles render in a proportional sans-serif font, matching the original
	// (Arial via GDI) — kBigGUIFont is ScummVM's built-in Helvetica, compiled
	// in so it works on the no-FreeType Windows build too.
	const Graphics::Font *guiFont =
	    FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (!guiFont)
		guiFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	Common::Array<Common::String> subLines;
	int subLineH = 0;
	if (showSubtitles() && !text.empty() && guiFont) {
		subLineH = guiFont->getFontHeight() + 2;
		guiFont->wordWrapText(text, subWidthBudget, subLines);
	}
	const uint32 subColor  = fmt.RGBToColor(255, 255, 255);
	const uint32 subShadow = fmt.RGBToColor(0, 0, 0);

	bool emptyBackbufferCleared = false;

	auto drawFrame = [&]() {
		const Graphics::Surface *warpView =
		    _warpLoaded ? _omni3dMan.getSurface() : nullptr;

#ifdef USE_HNM
		if (bgDecoder && ubbConvFrame.getPixels()) {
			const Graphics::Surface *f = nullptr;
			if (bgDecoder->needsUpdate())
				f = bgDecoder->decodeNextFrame();

			if (f) {
				if (bgDecoder->hasDirtyPalette())
					memcpy(ubbPal, bgDecoder->getPalette(), 256 * 3);

				// Seed the composite with the cyclo panorama so SKIP runs show
				// it through (mirrors FUN_00451d8b); black if no panorama.
				if (warpView && warpView->w == ubbConvFrame.w &&
				                warpView->h == ubbConvFrame.h &&
				                warpView->format == fmt) {
					const uint copyRowBytes = ubbConvFrame.w * fmt.bytesPerPixel;
					for (int row = 0; row < ubbConvFrame.h; row++)
						memcpy(ubbConvFrame.getBasePtr(0, row),
						       warpView->getBasePtr(0, row), copyRowBytes);
				} else {
					ubbConvFrame.fillRect(Common::Rect(ubbConvFrame.w, ubbConvFrame.h), 0);
				}

				const int fw = f->w, fh = f->h;
				const byte *srcBase = (const byte *)f->getBasePtr(0, 0);
				uint32 opSize = 0;
				const byte *opcodes = bgDecoder->getOpcodeStream(opSize);

				uint16 palLUT[256];
				for (int i = 0; i < 256; i++)
					palLUT[i] = (uint16)fmt.RGBToColor(ubbPal[i*3+0], ubbPal[i*3+1],
					                                  ubbPal[i*3+2]);

				if (opcodes && opSize >= 4 && fmt.bytesPerPixel == 2) {
					// Walk the FUN_00450b08 opcode stream: 0=SKIP (4-byte, 24-bit
					// runlen), 1=RUN with one shade byte, 2=RUN with per-pixel
					// shade.  shade>>3 == 0x1f is solid; lower blends the head
					// edge against the cyclo seed (anti-aliasing).
					const byte *op = opcodes, *opEnd = opcodes + opSize;
					uint32 pos = 0;
					const uint32 frameN = (uint32)fw * (uint32)fh;
					uint16 *dst16 = (uint16 *)ubbConvFrame.getPixels();
					while (op + 2 <= opEnd && pos < frameN) {
						const byte type = op[0];
						const byte runHi = op[1];
						if (type == 0) {
							if (op + 4 > opEnd) break;
							const uint32 runlen = ((uint32)op[1]) |
							                      ((uint32)op[2] << 8) |
							                      ((uint32)op[3] << 16);
							if (runlen == 0) break;            // terminator
							pos += runlen;
							op += 4;
						} else if (type == 1) {
							if (op + 3 > opEnd) break;
							const uint32 runlen = runHi;
							const byte *src = srcBase + pos;
							const uint shade = (uint)(op[2] >> 3);
							if (shade >= 0x1f) {
								for (uint32 i = 0; i < runlen && pos + i < frameN; i++)
									dst16[pos + i] = palLUT[src[i]];
							} else {
								for (uint32 i = 0; i < runlen && pos + i < frameN; i++)
									dst16[pos + i] = blendUbbPixel565(
									    palLUT[src[i]], dst16[pos + i], shade);
							}
							pos += runlen;
							op += 3;
						} else if (type == 2) {
							if (op + 2 + runHi > opEnd) break;
							const uint32 runlen = runHi;
							const byte *src = srcBase + pos;
							const byte *shadeBytes = op + 2;
							for (uint32 i = 0; i < runlen && pos + i < frameN; i++) {
								const uint shade = (uint)(shadeBytes[i] >> 3);
								dst16[pos + i] = (shade >= 0x1f)
								    ? palLUT[src[i]]
								    : blendUbbPixel565(palLUT[src[i]],
								                       dst16[pos + i], shade);
							}
							pos += runlen;
							op += 2 + runlen;
						} else {
							warning("playSingleConLine: unknown UBB opcode 0x%02x at +%u",
							        type, (uint)(op - opcodes));
							break;
						}
					}
				} else {
					// No opcode stream (or non-16bpp) — flat composite so the
					// head is at least visible.
					const uint bpp = fmt.bytesPerPixel;
					const int fp = f->pitch;
					for (int fy = 0; fy < fh; fy++) {
						const byte *src = srcBase + fy * fp;
						byte *dst = (byte *)ubbConvFrame.getBasePtr(0, fy);
						for (int fx = 0; fx < fw; fx++, dst += bpp) {
							const uint32 c = palLUT[src[fx]];
							if (bpp == 2)      *(uint16 *)dst = (uint16)c;
							else if (bpp == 4) *(uint32 *)dst = c;
							else               memcpy(dst, &c, bpp);
						}
					}
				}

				// Per-cam SPF overlay (INITDIAL columns 10..13).  The
				// original engine composites the matching SPF frame on top
				// of every UBB frame in the dialog playback loop at
				// FUN_0042f9e8 (atlantis.exe.c:26609-26734), sharing the
				// UBB's frame counter.  We mirror that exactly, plus a
				// modulo so the SPF LOOPS when the UBB outlives it —
				// compositeSpfFrame clamps to the last frame on its own,
				// which freezes the overlay while the head keeps talking
				// (visible on EnnemiPilot whose UBB runs 30+ frames against
				// a much shorter SPF).  Drawn on top of the UBB head.
				if (spfStream) {
					const uint sfi = (spfFrameCount > 0)
					    ? (spfFrameIdx % spfFrameCount)
					    : spfFrameIdx;
					compositeSpfFrame(spfStream, sfi, ubbConvFrame);
				}

				// "Just received" item icon: an NPC that gave the player an
				// object this dialog shows its icon centre-bottom over the head.
				if (fmt.bytesPerPixel == 2)
					drawGivenObject(ubbConvFrame);

				// Advance the SPF frame counter once per fresh UBB frame —
				// the same one-to-one mapping the original engine uses
				// (atlantis.exe.c FUN_0042f9e8:26710).
				++spfFrameIdx;
			}
		} else
#endif
		if (warpView) {
			// No video: if an item was just given, overlay its icon on a copy
			// of the panorama before showing it.
			if (_dialogGivenObjSprite >= 0 && fmt.bytesPerPixel == 2) {
				Graphics::Surface tmp;
				tmp.create(warpView->w, warpView->h, fmt);
				tmp.copyFrom(*warpView);
				drawGivenObject(tmp);
				g_system->copyRectToScreen(tmp.getPixels(), tmp.pitch, 0, 0, tmp.w, tmp.h);
				tmp.free();
			} else {
				g_system->copyRectToScreen(warpView->getPixels(), warpView->pitch,
				                           0, 0, warpView->w, warpView->h);
			}
		} else if (!emptyBackbufferCleared) {
			Graphics::ManagedSurface clr(screenW, screenH, fmt);
			clr.fillRect(Common::Rect(screenW, screenH), 0);
			g_system->copyRectToScreen(clr.getPixels(), clr.pitch, 0, 0, screenW, screenH);
			emptyBackbufferCleared = true;
		}

		// Subtitle overlay, drawn directly onto the composite (no opaque bar),
		// bottom-anchored with a 1-px drop shadow.
#ifdef USE_HNM
		if (bgDecoder && ubbConvFrame.getPixels()) {
			Graphics::ManagedSurface compose(ubbConvFrame.w, ubbConvFrame.h, fmt);
			const uint rowBytes = ubbConvFrame.w * fmt.bytesPerPixel;
			for (int row = 0; row < ubbConvFrame.h; row++)
				memcpy(compose.getBasePtr(0, row), ubbConvFrame.getBasePtr(0, row), rowBytes);

			int subAreaH = (int)subLines.size() * subLineH;
			int subBaseY = compose.h - subAreaH - 6;
			if (subBaseY < 0) subBaseY = 0;
			if (guiFont) {
				for (uint i = 0; i < subLines.size(); ++i) {
					int by = subBaseY + (int)i * subLineH;
					guiFont->drawString(&compose, subLines[i], subLeftMargin + 1, by + 1,
					    subWidthBudget, subShadow, Graphics::kTextAlignLeft);
					guiFont->drawString(&compose, subLines[i], subLeftMargin, by,
					    subWidthBudget, subColor, Graphics::kTextAlignLeft);
				}
			}
			int blitX = (screenW - compose.w) / 2;
			int blitY = (screenH - compose.h) / 2;
			if (blitX < 0) blitX = 0;
			if (blitY < 0) blitY = 0;
			g_system->copyRectToScreen(compose.getPixels(), compose.pitch,
			                           blitX, blitY, compose.w, compose.h);
		} else
#endif
		if (!subLines.empty() && subLineH > 0) {
			int subAreaH = (int)subLines.size() * subLineH;
			Graphics::ManagedSurface overlay(screenW, subAreaH + 4, fmt);
			int srcY = screenH - subAreaH - 6;
			if (srcY < 0) srcY = 0;
			if (warpView) {
				int rowsAvail = warpView->h - srcY;
				if (rowsAvail > overlay.h) rowsAvail = overlay.h;
				const uint rowBytes = warpView->w * fmt.bytesPerPixel;
				for (int row = 0; row < rowsAvail; row++)
					memcpy(overlay.getBasePtr(0, row),
					       warpView->getBasePtr(0, srcY + row), rowBytes);
			} else {
				overlay.fillRect(Common::Rect(overlay.w, overlay.h), 0);
			}
			if (guiFont) {
				for (uint i = 0; i < subLines.size(); ++i) {
					int by = (int)i * subLineH;
					guiFont->drawString(&overlay, subLines[i], subLeftMargin + 1, by + 1,
					    subWidthBudget, subShadow, Graphics::kTextAlignLeft);
					guiFont->drawString(&overlay, subLines[i], subLeftMargin, by,
					    subWidthBudget, subColor, Graphics::kTextAlignLeft);
				}
			}
			g_system->copyRectToScreen(overlay.getPixels(), overlay.pitch,
			                           0, srcY, overlay.w, overlay.h);
		}
		g_system->updateScreen();
	};

	// Linger scales with subtitle length (≈80 ms/char, min 1.5 s) so longer
	// lines aren't cut short when no audio exists.
	int lingerMs = text.empty() ? 1500 : MAX(1500, (int)text.size() * 80);

	// One click/key skips only one line: drain a still-held skip from the
	// previous line so this one watches for a fresh press.
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	clearKeys();

	Common::String apcName = Common::String::format("ATA%04d%c.APC", sectionId, track);
	Common::SeekableReadStream *apcFile = openBigFileStream(kFileTypeSound, apcName);

	if (!apcFile) {
		for (int ms = 0; ms < lingerMs && !shouldAbort(); ms += 20) {
			drawFrame();
			pollEvents();
			if (checkKeysPressed(1, Common::KEYCODE_SPACE) || getCurrentMouseButton() != 0)
				break;
			g_system->delayMillis(20);
		}
	} else {
		Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*apcFile);
		if (apc) {
			int64 remaining = apcFile->size() - apcFile->pos();
			if (remaining > 0) {
				byte *buf = new byte[remaining];
				apcFile->read(buf, (uint32)remaining);
				apc->queuePacket(new Common::MemoryReadStream(buf, remaining,
				                                              DisposeAfterUse::YES));
			}
			apc->finish();
			Audio::SoundHandle handle;
			_mixer->playStream(Audio::Mixer::kSpeechSoundType, &handle, apc);
			debugC(1, kDebugScript, "playSingleConLine: %s", apcName.c_str());

			while (!shouldAbort() && _mixer->isSoundHandleActive(handle)) {
				drawFrame();
				pollEvents();
				if (checkKeysPressed(1, Common::KEYCODE_SPACE) || getCurrentMouseButton() != 0) {
					_mixer->stopHandle(handle);
					break;
				}
				g_system->delayMillis(10);
			}
			_mixer->stopHandle(handle);
		}
		delete apcFile;

		// Fixed 500 ms subtitle tail after the audio ends.
		for (int ms = 0; ms < 500 && !shouldAbort(); ms += 20) {
			drawFrame();
			pollEvents();
			if (checkKeysPressed(1, Common::KEYCODE_SPACE) || getCurrentMouseButton() != 0)
				break;
			g_system->delayMillis(20);
		}
	}

#ifdef USE_HNM
	delete bgDecoder;
	delete spfStream;
	ubbConvFrame.free();
#endif
	if (fovChanged)
		_omni3dMan.setHFov(savedHFov);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
