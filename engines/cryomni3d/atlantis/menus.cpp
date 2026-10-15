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

// Sprite-font text rendering and (later) the menu / game-over screens.  This
// starts with the FONTMAX gold menu font; the menu screens, the game-over
// screen and the smaller player-name / credits fonts layer on as they land.

#include "common/config-manager.h"
#include "common/events.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/savefile.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "audio/decoders/apc.h"
#include "audio/mixer.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/atlantis/subtitle_layout.h"
#include "cryomni3d/atlantis/ambient_output.h"
#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Space advance from the exe width table at 0x4965ee[' '] = 22.
static const int kFontMaxSpaceAdvance = 22;
static const int kFontMinSpaceAdvance = 18;

// Glyph-sprite text blitter.  `layoutGlyphs` drive the pen advance so a hover
// swap to a differently-metricised glyph set never reflows the line;
// `drawGlyphs` supply the pixels, each anchored by its own xoff/yoff.  anchorY
// is the text baseline.
void CryOmni3DEngine_Atlantis::drawSprFontText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY,
        const Common::Array<SubjSprite> &drawGlyphs,
        const Common::Array<SubjSprite> &layoutGlyphs, int spaceAdvance,
        const uint8 *advanceTable, uint advanceCount) const {
	Common::Array<int> glyphWidths(layoutGlyphs.size());
	for (uint i = 0; i < layoutGlyphs.size(); ++i)
		glyphWidths[i] = layoutGlyphs[i].w;
	Common::Array<SprFontGlyph> pen;
	sprFontPen(text, anchorX, spaceAdvance, glyphWidths, advanceTable, advanceCount, pen);
	for (const SprFontGlyph &g : pen) {
		if (g.idx >= (int)drawGlyphs.size() || drawGlyphs[g.idx].pixels.empty())
			continue;
		const SubjSprite &spr = drawGlyphs[g.idx];   // pixels
		int dx = g.x - spr.xoff;
		int dy = anchorY - spr.yoff;
		for (int sy = 0; sy < (int)spr.h; ++sy) {
			int ty = dy + sy;
			if (ty < 0 || ty >= dst.h)
				continue;
			for (int sx = 0; sx < (int)spr.w; ++sx) {
				uint sidx = sy * spr.w + sx;
				uint16 pix = spr.pixels[sidx];
				if (spr.blend[sidx] == kSprTranspBlend)
					continue;
				int tx = dx + sx;
				if (tx < 0 || tx >= dst.w)
					continue;
				uint16 *dp = (uint16 *)dst.getBasePtr(tx, ty);
				if (spr.blend[sidx] != kSprNoBlend)
					pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
				*dp = pix;
			}
		}
	}
}

// FONTMAX.SPR menu font.  `hover` swaps in the FONTMAX2.SPR glyphs for colour;
// layout always uses FONTMAX so a hovered entry never shifts.
void CryOmni3DEngine_Atlantis::drawFontMaxText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool hover) const {
	drawSprFontText(dst, text, anchorX, anchorY,
	    (hover && !_fontMaxHoverSprites.empty()) ? _fontMaxHoverSprites
	                                             : _fontMaxSprites,
	    _fontMaxSprites, kFontMaxSpaceAdvance,
	    kFontMaxMeasureWidths, ARRAYSIZE(kFontMaxMeasureWidths));
}

// FONTMIN.SPR player-name font; FONTMIN2 is its hover variant.  Layout always
// uses FONTMIN so hovering only recolours.
void CryOmni3DEngine_Atlantis::drawFontMinText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool hover) const {
	drawSprFontText(dst, text, anchorX, anchorY,
	    (hover && !_fontMin2Sprites.empty()) ? _fontMin2Sprites : _fontMinSprites,
	    _fontMinSprites, kFontMinSpaceAdvance);
}

// CREDBLAN.SPR / CREDBLEU.SPR credits glyphs.  Advance widths come from the
// exe char-width table (_creditCharWidths); the glyph index is char-0x21
// (these atlases have NO space slot, unlike FONTMAX's char-0x20).  A space
// advances 6 without reading the table, both when a line is measured
// (FUN_0042073c @ 0x00420774) and when it is drawn (FUN_0041a72c @
// 0x0041ab0d / 0x0041abde).
int CryOmni3DEngine_Atlantis::creditCharAdvance(unsigned char c) const {
	if (c == ' ')
		return 6;
	return (int)_creditCharWidths[c];
}

int CryOmni3DEngine_Atlantis::creditStringWidth(const Common::String &text) const {
	int w = 0;
	for (uint i = 0; i < text.size(); i++)
		w += creditCharAdvance((unsigned char)text[i]);
	return w;
}

void CryOmni3DEngine_Atlantis::drawCreditText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool blue) const {
	const Common::Array<SubjSprite> &glyphs = blue ? _creditBleuSprites
	                                                : _creditBlanSprites;
	if (glyphs.empty())
		return;
	int cx = anchorX;
	for (uint i = 0; i < text.size(); i++) {
		unsigned char c = (unsigned char)text[i];
		int adv = creditCharAdvance(c);
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			cx += adv;
			continue;
		}
		int idx = (int)c - 0x21;   // no space slot in these atlases
		if (idx < 0 || idx >= (int)glyphs.size()) {
			cx += adv;
			continue;
		}
		const SubjSprite &spr = glyphs[idx];
		if (spr.pixels.empty()) {
			cx += adv;
			continue;
		}
		int dx = cx - spr.xoff;
		int dy = anchorY - spr.yoff;
		for (int sy = 0; sy < (int)spr.h; ++sy) {
			int ty = dy + sy;
			if (ty < 0 || ty >= dst.h)
				continue;
			for (int sx = 0; sx < (int)spr.w; ++sx) {
				uint sidx = sy * spr.w + sx;
				uint16 pix = spr.pixels[sidx];
				if (spr.blend[sidx] == kSprTranspBlend)
					continue;
				int tx = dx + sx;
				if (tx < 0 || tx >= dst.w)
					continue;
				uint16 *dp = (uint16 *)dst.getBasePtr(tx, ty);
				if (spr.blend[sidx] != kSprNoBlend)
					pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
				*dp = pix;
			}
		}
		cx += adv;
	}
}

// Game-over screen (atlantis.exe GereGameOver, FUN_00426590): show the
// full-screen verdict image with the narrator's commentary, then return so the
// caller reloads the chapter checkpoint (the death is gated on a persistent CON
// variable, so resuming in place would loop straight back into it).
void CryOmni3DEngine_Atlantis::showGameOver(int sectionId, int n,
        const Common::String &narratorText, char narratorTrack) {
	stopAmbient();   // FUN_00426590 @ 0x004265d5
	Common::String name = Common::String::format("GOVER%02d.TGA", n);
	Graphics::Surface *raw = loadTGA(kFileTypeImages, name.c_str());
	if (!raw) {
		warning("showGameOver: cannot load IMAGES\\%s", name.c_str());
		return;
	}

	const Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface img(640, 480, fmt);
	img.fillRect(Common::Rect(640, 480), 0);
	img.blitFrom(*raw);
	raw->free();
	delete raw;

	// Narrator subtitle (FUN_00426590 -> FUN_0042ffe8 / FUN_00430178, as for
	// dialog lines): split at 100 characters, line i of n at x 5,
	// y 0x1ce - (n - i + 1) * 14, black shadow at (+1, +1); drawn when the
	// subtitles are on.
	if (showSubtitles() && !narratorText.empty()) {
		const int leftMargin = 5;
		int widthBudget = img.w - leftMargin - 5;
		if (widthBudget < 32)
			widthBudget = 32;
		const Graphics::Font *guiFont = subtitleFont();
		if (guiFont) {
			Common::Array<Common::String> subLines;
			splitSubtitle(narratorText, subLines);
			const uint32 subColor  = fmt.RGBToColor(255, 255, 255);
			const uint32 subShadow = fmt.RGBToColor(0, 0, 0);
			for (uint i = 0; i < subLines.size(); ++i) {
				int by = subtitleLineY(i, subLines.size());
				guiFont->drawString(&img, subLines[i], leftMargin + 1, by + 1,
				                    widthBudget, subShadow, Graphics::kTextAlignLeft);
				guiFont->drawString(&img, subLines[i], leftMargin, by,
				                    widthBudget, subColor, Graphics::kTextAlignLeft);
			}
		}
	}

	g_system->copyRectToScreen(img.getPixels(), img.pitch, 0, 0, 640, 480);
	g_system->updateScreen();

	// The screen owns the player's attention: hide the cursor and drain the
	// click/key that ended the triggering dialog so it isn't read as a skip.
	showMouse(false);
	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	clearKeys();

	// Narrator voice: WAV\ATA<section4d><track>.APC — wait for it (skippable),
	// or hold the image ~5 s when there is no narrator audio.
	Common::String apcName = Common::String::format("ATA%04d%c.APC", sectionId, narratorTrack);
	Common::SeekableReadStream *apcFile =
	    narratorText.empty() ? nullptr : openBigFileStream(kFileTypeSound, apcName);
	Audio::PacketizedAudioStream *apc = apcFile ? Audio::makeAPCStream(*apcFile) : nullptr;

	if (apc) {
		int64 remaining = apcFile->size() - apcFile->pos();
		if (remaining > 0) {
			byte *buf = new byte[remaining];
			apcFile->read(buf, (uint32)remaining);
			apc->queuePacket(new Common::MemoryReadStream(buf, remaining, DisposeAfterUse::YES));
		}
		apc->finish();
		Audio::SoundHandle handle;
		_mixer->playStream(Audio::Mixer::kSpeechSoundType, &handle, apc);
		debugC(1, kDebugScript, "showGameOver: GOVER%02d.TGA + %s", n, apcName.c_str());
		// FUN_00426590 @ 0x00426b0c: until the sample ends or a left click
		// (0x0049611c, set on a VK_LBUTTON edge by FUN_0041a298).
		while (!shouldAbort() && _mixer->isSoundHandleActive(handle)) {
			pollEvents();
			if (getCurrentMouseButton() == 1)
				break;
			g_system->delayMillis(10);
		}
		_mixer->stopHandle(handle);
	} else if (!narratorText.empty()) {
		// @ 0x00426ae0: a narrator line whose APC is missing holds the image
		// for 5000 ms or until a left click.
		debugC(1, kDebugScript, "showGameOver: GOVER%02d.TGA (no narrator audio)", n);
		uint32 deadline = g_system->getMillis() + 5000;
		while (!shouldAbort() && g_system->getMillis() < deadline) {
			pollEvents();
			if (getCurrentMouseButton() == 1)
				break;
			g_system->delayMillis(10);
		}
	} else {
		// @ 0x004267f4: no "[Narra" line after the command: no wait.
		debugC(1, kDebugScript, "showGameOver: GOVER%02d.TGA (no narrator line)", n);
	}
	delete apcFile;

	// @ 0x00426bc2: the screen is cleared to black.
	img.fillRect(Common::Rect(640, 480), 0);
	g_system->copyRectToScreen(img.getPixels(), img.pitch, 0, 0, 640, 480);
	g_system->updateScreen();

	while (getCurrentMouseButton() != 0 && !shouldAbort()) {
		pollEvents();
		g_system->delayMillis(10);
	}
	clearKeys();
}

// Render a parsed menu: SPRMENU sprites first, then FONTMAX labels / toggle
// values on top (the hovered item in the bright FONTMAX2 glyphs — that is how
// the original tints the highlight).  Each text/toggle item's click rect is
// returned in hitRectsOut for the event loop's hit-test.
void CryOmni3DEngine_Atlantis::renderMenuLayout(Graphics::ManagedSurface &dst,
        const MenuLayout &layout, int hoveredTextIdx,
        Common::Array<Common::Rect> &hitRectsOut) const {
	hitRectsOut.clear();
	if (layout.menu >= 0 && !layout.raw.empty()) {
		// atlantis.exe FUN_0041fb6c: the text is walked byte by byte, sprites
		// and glyphs blitted in file order, the hit table rebuilt.  The toggles
		// are the options menu's DAT_004960a8 / DAT_004960ac: 1 shows the
		// first option (the port's selected 0).
		int flags[2] = { 0, 0 };
		uint nt = 0;
		for (uint i = 0; i < layout.items.size() && nt < 2; i++)
			if (layout.items[i].kind == MenuItem::kToggle)
				flags[nt++] = layout.items[i].selected == 0 ? 1 : 0;
		walkMenuText(layout.raw.data(), layout.raw.size(), layout.menu, hoveredTextIdx,
		             flags[0], flags[1], layout.walk);
		for (const MenuWalkOp &op : layout.walk.ops) {
			if (!op.sprite)
				drawFontMaxText(dst, Common::String((char)(op.index + 0x20)), op.x, op.y, op.hoverFont);
			else if (!op.altBank)
				blitMenuSprite(dst, (uint)op.index, op.x, op.y);
			// The /sprj= /sprr= /sprc= bank 0x006cd874 is never loaded by the
			// exe (no write found, only the shutdown free in FUN_0041a5b0);
			// no shipped file uses it.  [UNKNOWN] what such a blit would show.
		}
		for (const MenuWalkRecord &r : layout.walk.records) {
			// FUN_004210c0's strict, u16 test as a half-open rect.
			if (r.x < 0 || r.y < 0 || r.w <= 1 || r.h <= 1)
				hitRectsOut.push_back(Common::Rect());
			else
				hitRectsOut.push_back(Common::Rect(r.x + 1, r.y + 1, r.x + r.w, r.y + r.h));
		}
		return;
	}
	hitRectsOut.resize(layout.items.size());

	for (uint i = 0; i < layout.items.size(); i++) {
		const MenuItem &it = layout.items[i];
		if (it.kind == MenuItem::kSprite)
			blitMenuSprite(dst, (uint)it.sprIdx, it.hx, it.hy);
	}

	for (uint i = 0; i < layout.items.size(); i++) {
		const MenuItem &it = layout.items[i];
		Common::String label;
		if (it.kind == MenuItem::kText) {
			label = it.text;
		} else if (it.kind == MenuItem::kToggle && !it.options.empty()) {
			int sel = (it.selected >= 0 && it.selected < (int)it.options.size())
			          ? it.selected : 0;
			label = it.options[sel];
		} else {
			continue;
		}

		// Centred labels are positioned with the exe's measuring table, not the
		// glyph-sprite advances (atlantis.exe FUN_004206c8: (640 - w) / 2).
		int x = menuTextDrawX(it, label);
		drawFontMaxText(dst, label, x, it.anchorY, (int)i == hoveredTextIdx);
		hitRectsOut[i] = menuTextHitRect(it, x);
	}
}

void CryOmni3DEngine_Atlantis::fadeScreenToBlack() {
	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	for (int step = 0; step < 16 && !shouldAbort(); step++) {
		Graphics::Surface *s = g_system->lockScreen();
		if (s && fmt.bytesPerPixel == 2) {
			for (int y = 0; y < s->h; y++) {
				uint16 *row = (uint16 *)s->getBasePtr(0, y);
				for (int x = 0; x < s->w; x++) {
					uint8 r, g, b;
					fmt.colorToRGB(row[x], r, g, b);
					row[x] = (uint16)fmt.RGBToColor(r * 13 / 16, g * 13 / 16, b * 13 / 16);
				}
			}
		}
		g_system->unlockScreen();
		g_system->updateScreen();
		g_system->delayMillis(20);
	}
	Graphics::Surface *s = g_system->lockScreen();
	if (s)
		s->fillRect(Common::Rect(0, 0, s->w, s->h), 0);
	g_system->unlockScreen();
	g_system->updateScreen();
}

// Map a mouse position to the index of the hovered text/toggle item, or -1.
static int hitTestMenu(const MenuLayout &layout,
                       const Common::Array<Common::Rect> &rects,
                       const Common::Point &mouse) {
	if (layout.menu >= 0 && !layout.raw.empty())
		return menuWalkHit(layout.walk.records, mouse.x, mouse.y);
	for (uint i = 0; i < layout.items.size(); i++) {
		MenuItem::Kind k = layout.items[i].kind;
		if (k != MenuItem::kText && k != MenuItem::kToggle)
			continue;
		if (rects[i].contains(mouse))
			return (int)i;
	}
	return -1;
}

static const char *const kOmni3dInvertKey = "atlantis_omni3d_invert";


// Player/save-row text in FONTMIN (FONTMIN2 when hover), spaced by the exe's
// player width table so a hover swap only recolours, never reflows.
void CryOmni3DEngine_Atlantis::drawPlayerText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool hover,
        FontMinRenderer renderer) const {
	// The pen positions come from the original's renderer (fontMinBlits).
	const Common::Array<SubjSprite> &glyphs =
	    (hover && !_fontMin2Sprites.empty()) ? _fontMin2Sprites : _fontMinSprites;
	Common::Array<FontMinBlit> blits;
	fontMinBlits(renderer, text, anchorX, anchorY, blits);
	for (const FontMinBlit &b : blits) {
		if (b.idx >= glyphs.size())
			continue;
		const SubjSprite &spr = glyphs[b.idx];
		if (spr.pixels.empty())
			continue;
		const int dx = b.x - spr.xoff;
		const int dy = b.y - spr.yoff;
		for (int sy = 0; sy < (int)spr.h; ++sy) {
			const int ty = dy + sy;
			if (ty < 0 || ty >= dst.h)
				continue;
			for (int sx = 0; sx < (int)spr.w; ++sx) {
				const uint sidx = sy * spr.w + sx;
				uint16 pix = spr.pixels[sidx];
				if (spr.blend[sidx] == kSprTranspBlend)
					continue;
				const int tx = dx + sx;
				if (tx < 0 || tx >= dst.w)
					continue;
				uint16 *dp = (uint16 *)dst.getBasePtr(tx, ty);
				if (spr.blend[sidx] != kSprNoBlend)
					pix = blendSprPixel565(pix, *dp, spr.blend[sidx]);
				*dp = pix;
			}
		}
	}
}

// Player-management screen — the original game's startup select / create /
// delete-player flow.  The PREINTRO.TGA artwork and the SELEMENU.TXT frame
// ("SELECT PLAYER NAME" / "DELETE PLAYER NAME" / "OK") form the backdrop.
// Existing players and a "New Player" entry are listed in FONTMIN; creating a
// player shows a dotted name field above the list.  Returns the chosen player
// id, or -1 if quit.
int CryOmni3DEngine_Atlantis::displayPlayerScreen() {
	Graphics::PixelFormat fmt = g_system->getScreenFormat();

	// Backdrop: the PREINTRO.TGA menu artwork (the "Atlantis" title scene),
	// with the SELEMENU.TXT ornaments and labels composited on top — the same
	// background the main menu uses.
	Graphics::ManagedSurface bg;
	{
		Graphics::Surface *raw = loadTGA(kFileTypeImages, "PREINTRO.TGA");
		if (raw) {
			bg.copyFrom(*raw);
			raw->free();
			delete raw;
		} else {
			bg.create(640, 480, fmt);
			bg.fillRect(Common::Rect(0, 0, 640, 480), fmt.RGBToColor(0, 0, 0));
		}
	}
	MenuLayout decor;
	loadMenuLayout("SELEMENU.TXT", decor);
	Common::Array<Common::Rect> decorRects;
	// Locate the interactive "OK" and "DELETE PLAYER NAME" labels so they can
	// be hover-lit and hit-tested each frame — "OK" validates a name being
	// typed, "DELETE PLAYER NAME" toggles delete mode.
	int okIdx = -1, delIdx = -1;
	for (uint i = 0; i < decor.items.size(); i++) {
		if (decor.items[i].kind != MenuItem::kText)
			continue;
		if (decor.items[i].text == "OK")
			okIdx = (int)i;
		else if (decor.items[i].text == "DELETE PLAYER NAME")
			delIdx = (int)i;
	}
	// Player-row layout — the exact grid the original hit-tests with
	// (atlantis.exe FUN_004211f0, captured live): a row's clickable cell is
	// [kGridY0 + j*kGridStep, kGridY0 + (j+1)*kGridStep) x [kGridX0, +kGridW).
	// The FONTMAX baseline sits on the cell bottom.
	static const int kGridX0   = 175;
	static const int kGridW    = 320;
	static const int kGridY0   = 248;
	static const int kGridStep = 22;
	// The dotted name-entry field's fixed anchor, above the list
	// (atlantis.exe FUN_00420f60 caller at 0x41e3df: EAX=0xA0, EDX=0xBE).
	static const int kEditFieldX = 160;
	static const int kEditFieldY = 190;

	Graphics::ManagedSurface surf;
	surf.create(640, 480, fmt);
	// Render the SELEMENU frame once so decorRects is populated before the
	// first hit-test; it is re-rendered with hover state inside the loop.
	renderMenuLayout(surf, decor, -1, decorRects);

	int  editing    = -1;          // storage slot being typed into, or -1
	int  selected   = -1;          // existing player picked, shown above list
	Common::String editBuf;
	int  result     = -2;          // -2 running, -1 quit, >=0 chosen player
	uint32 lastClickMs = 0;        // press time of the last player-row click
	const uint32 kDoubleClickMs = 500;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	while (result == -2 && !shouldAbort()) {
		pollEvents();

		// Build the visible row list: existing players, then a "New Player"
		// entry while there is still room.  The dotted name-entry field is
		// drawn separately, above the list (see the redraw below).
		const int kRowNew = -2;
		int  rowSlot[kMaxPlayers + 1];
		int  numRows = 0;
		uint numPlayers = 0;
		for (uint i = 0; i < kMaxPlayers; i++) {
			if (!_players[i].empty()) {
				rowSlot[numRows++] = (int)i;
				numPlayers++;
			}
		}
		if ((editing >= 0 ? numPlayers + 1 : numPlayers) < kMaxPlayers)
			rowSlot[numRows++] = kRowNew;

		// Keyboard: while creating a player, capture the typed name.
		Common::KeyState k = getNextKey();
		if (k.keycode != Common::KEYCODE_INVALID) {
			if (editing >= 0) {
				if (k.keycode == Common::KEYCODE_ESCAPE) {
					editing = -1;
					editBuf.clear();
				} else if (k.keycode == Common::KEYCODE_RETURN
				        || k.keycode == Common::KEYCODE_KP_ENTER) {
					if (!editBuf.empty()) {
						_players[editing] = editBuf;
						savePlayers();
						result = editing;   // the new player becomes current
					}
				} else if (k.keycode == Common::KEYCODE_BACKSPACE) {
					if (!editBuf.empty())
						editBuf.deleteLastChar();
				} else if (nameKeyCode(k.ascii) >= 0x20 && nameKeyCode(k.ascii) <= 0xa7
				        && editBuf.size() < kPlayerNameLen) {
					// FUN_0041dea4 @ 0x0041e283: characters 0x20..0xa7 (the
					// FONTMIN glyphs); a leading space is refused, a leading
					// '.' is typed (the commit still refuses it).
					const uint c = nameKeyCode(k.ascii);
					if (!(editBuf.empty() && c == ' '))
						editBuf += (char)c;
				}
			} else if (k.keycode == Common::KEYCODE_ESCAPE) {
				result = -1;
				break;
			}
		}

		// Mouse hover / click.
		Common::Point mouse = getMousePos();
		int hovered = -1;
		// FUN_004211f0 with FUN_0041dea4's arguments (0x0041e08e): cell 320 x 22
		// at (175, 248), one column, numRows rows; its inclusive far edges give
		// an index past the list, which selects nothing.
		{
			const int g = menuGridHit(kGridX0, kGridY0, kGridW, kGridStep, 1, numRows, true,
			                          mouse.x, mouse.y);
			if (g >= 0 && g < numRows)
				hovered = g;
		}
		// Hover state for the interactive SELEMENU labels.
		bool okHover  = (okIdx  >= 0) && decorRects[okIdx].contains(mouse);
		bool delHover = (delIdx >= 0) && decorRects[delIdx].contains(mouse);

		if (getCurrentMouseButton() == 1) {
			uint32 now = g_system->getMillis();
			waitMouseRelease();
			if (editing >= 0) {
				// While creating a player, clicking OK validates the typed
				// name — the same as pressing Return.
				if (okHover && !editBuf.empty()) {
					_players[editing] = editBuf;
					savePlayers();
					result = editing;   // the new player becomes current
				}
			} else if (delHover) {
				// Delete the selected player after a confirmation dialog.
				// confirmQuit() shows the SUREMENU prompt over the bare title
				// backdrop (bg) rather than the live player screen, so the
				// list, "SELECT PLAYER NAME" / "DELETE PLAYER NAME" / "OK"
				// labels all vanish behind "ARE YOU SURE ?" — matching the
				// original.  SUREMENU.TXT carries its own ornament sprites.
				if (selected >= 0 && confirmQuit(bg)) {
					deletePlayer((uint)selected);
					savePlayers();
					selected = -1;
				}
			} else if (okHover) {
				// OK starts the game for the selected existing player.
				if (selected >= 0)
					result = selected;
			} else if (hovered >= 0) {
				int slot = rowSlot[hovered];
				if (slot >= 0) {                  // an existing player
					if (selected == slot
					        && now - lastClickMs < kDoubleClickMs) {
						result = slot;            // double-click → play
					} else {
						selected = slot;          // single click → show name
						lastClickMs = now;
					}
				} else if (slot == kRowNew) {
					// Create: type into the first free storage slot.
					for (uint i = 0; i < kMaxPlayers; i++) {
						if (_players[i].empty()) {
							editing = (int)i;
							editBuf.clear();
							selected = -1;
							break;
						}
					}
				}
			}
		}

		// Redraw.  The SELEMENU frame is re-rendered each frame so the "OK"
		// and "DELETE PLAYER NAME" labels can light up on hover.
		surf.blitFrom(bg);
		renderMenuLayout(surf, decor, okHover ? okIdx
		                              : (delHover ? delIdx : -1), decorRects);
		// Player rows use FONTMIN (FONTMIN2 for the hovered row), left-aligned
		// at the grid's X origin.
		for (int j = 0; j < numRows; j++) {
			int slot = rowSlot[j];
			Common::String label = (slot == kRowNew)
			    ? Common::String("New Player") : _players[slot];
			// FUN_00420d8c highlights the row under the mouse only (the grid
			// result, FUN_0041dea4 @ 0x0041e096).
			bool hot = (j == hovered);
			// FUN_00420d8c: x 185 (0xb9), y 270 + 22 j.
			drawPlayerText(surf, label, 185,
			               kGridY0 + (j + 1) * kGridStep, hot, kFontMinPlayerList);
		}
		// The name area above the list shows either the new name being typed
		// (with the dotted placeholder), or — once an existing player has been
		// picked — that player's name, plain and not editable.
		if (editing >= 0) {
			Common::String field = editBuf;
			while (field.size() < kPlayerNameLen)
				field += '.';
			drawPlayerText(surf, field, kEditFieldX, kEditFieldY, true, kFontMinNameField);
		} else if (selected >= 0) {
			drawPlayerText(surf, _players[selected], kEditFieldX, kEditFieldY, true, kFontMinNameField);
		}

		g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	clearKeys();
	return result == -2 ? -1 : result;
}

// atlantis.exe FUN_0041cc28's dim (FUN_004108e0, sprite_blend.h
// dimMenuPixel) over a whole backdrop.
static void dimMenuBackdrop(Graphics::ManagedSurface &surf) {
	const Graphics::PixelFormat &fmt = surf.format;
	if (fmt.bytesPerPixel != 2)
		return;
	const bool rgb565 = (fmt.gLoss == 2);
	for (int y = 0; y < surf.h; y++) {
		uint16 *row = (uint16 *)surf.getBasePtr(0, y);
		for (int x = 0; x < surf.w; x++)
			row[x] = dimMenuPixel(row[x], rgb565);
	}
}

// LOAD screen (LOADMENU.TXT): the current player's per-episode saves listed by
// their EPI.TXT names, with RESTART EPISODE / OK / Cancel and a draggable
// scrollbar (atlantis.exe FUN_0041cf78).  Returns the 1-based slot to load, or
// 0 on cancel.  Atlantis has no manual save, so this is load-only.
uint CryOmni3DEngine_Atlantis::displaySavePicker(bool saveMode, Common::String &outSaveName) {
	(void)saveMode;
	(void)outSaveName;

	// FUN_00420b48: rows at x 55 (0x37), baseline 130 (0x82) + 22 i; the hit
	// grid is FUN_004211f0 with FUN_0041cf78's arguments (0x0041d362): cell
	// 400 x 22 at (45, 108), one column, 11 rows.
	const int kListX = 55, kListY0 = 130, kRowStep = 22, kVisible = 11;
	Graphics::PixelFormat fmt = g_system->getScreenFormat();

	Graphics::ManagedSurface bg(640, 480, fmt);
	const Graphics::Surface *pano = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
	if (pano)
		bg.blitFrom(*pano);
	else
		bg.fillRect(Common::Rect(0, 0, 640, 480), fmt.RGBToColor(0, 0, 0));
	// FUN_0041cf78 starts with FUN_0041ce8c: the in-game menu's dimmed copy
	// of the view.
	dimMenuBackdrop(bg);

	MenuLayout layout;
	if (!loadMenuLayout("LOADMENU.TXT", layout))
		return 0;
	int restartIdx = -1, okIdx = -1, cancelIdx = -1;
	uint textOrd = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kText)
			continue;
		if (textOrd == 1)      restartIdx = (int)i;
		else if (textOrd == 2) okIdx      = (int)i;
		else if (textOrd == 3) cancelIdx  = (int)i;
		textOrd++;
	}

	struct Entry { int localSlot; Common::String name; };
	Common::Array<Entry> saves;
	int selected = -1;
	int lastRowClick = -1;
	uint32 lastRowClickMs = 0;
	for (uint ch = 0; ch < kPlayerSlotStride; ch++) {
		int abs = episodeSaveSlot((uint)_currentPlayer, ch);
		Common::InSaveFile *in = _saveFileMan->openForLoading(getSaveStateName(abs));
		if (!in)
			continue;
		char desc[21];
		memset(desc, 0, sizeof(desc));
		bool ok = (in->read(desc, 20) == 20);
		delete in;
		if (!ok)
			continue;
		Entry e;
		e.localSlot = (int)ch;
		e.name = episodeName(ch);
		if (e.name.empty())
			e.name = desc[0] ? Common::String(desc) : Common::String::format("Chapter %u", ch);
		saves.push_back(e);
	}

	// FUN_0041cf78 starts with no row chosen (0x00612080 = "") and the list
	// at offset 0 (0x00496605 = 0, 0x0041d273..).  A row click stores the
	// row's chapter in the player's chapter byte 0x006ce246[player]
	// (_playerChapter, memory only); OK leaves on record 6 and the caller
	// reloads the checkpoint of that byte (FUN_0041c838 @ 0x0041c9bc..,
	// FUN_0042de5c @ 0x0042dee7).  Cancel keeps the byte, so a later OK
	// without a row click reloads the chapter clicked in the cancelled
	// picker until a checkpoint (FUN_0042d6b8 @ 0x0042d836 / 0x0042dc7b)
	// stores the current chapter in it again.
	// See atlantis-re: functions/ui-FUN_0041cf78.md.
	const bool havePlayer = _currentPlayer >= 0 && _currentPlayer < (int)kMaxPlayers;
	auto chooseRow = [&](int entry) {
		if (havePlayer)
			_playerChapter[_currentPlayer] = saves[entry].localSlot;
	};
	auto byteSlot = [&]() -> uint {
		if (!havePlayer)
			return 0;
		const int ch = _playerChapter[_currentPlayer];
		if (ch < 0 || ch >= (int)kPlayerSlotStride)
			return 0;
		const int slot = episodeSaveSlot((uint)_currentPlayer, (uint)ch);
		return saveSlotExists(slot) ? (uint)slot + 1 : 0;
	};

	// FUN_0041cf78 step 4: the scrollbar (loadPickerScroll*, menu_layout.h).
	LoadPickerScroll sb;
	loadPickerScrollInit((int)saves.size(), sb);
	int thumbItem = -1, trackItem = -1;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kSprite)
			continue;
		if (layout.items[i].sprIdx == 13 && thumbItem < 0)
			thumbItem = (int)i;
		else if (layout.items[i].sprIdx == 4 && trackItem < 0)
			trackItem = (int)i;   // record 1, /spr=4
	}
	// The record latched by WM_LBUTTONDOWN (0x00419c47: 0x006cd844 = the
	// hover record): the track when the press starts strictly inside its
	// rectangle (FUN_004210c0's test); released by WM_LBUTTONUP.
	bool trackLatched = false;
	bool wasDown = false;
	// The chosen row's title (0x00612080), drawn at y 0x50 with the name
	// field renderer FUN_00420f60, x = (640 - its load-list width) / 2
	// (0x0041d431..0x0041d44d, 0x0041d4a8).
	Common::String title;
	int titleX = 0;

	Graphics::ManagedSurface surf(640, 480, fmt);
	Common::Array<Common::Rect> hitRects;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	// What the caller does once the picker ends on OK, RESTART EPISODE or a
	// double click: it reloads the checkpoint of the player's chapter byte
	// (FUN_0041c838 @ 0x0041c9bc.., FUN_0042de5c), and FUN_0042de5c starts a
	// new game at chapter 1 when that file does not exist.
	auto reloadResult = [&]() -> uint {
		const uint slot = byteSlot();
		return slot != 0 ? slot : kSavePickerNewGame;
	};

	uint result = 0;
	bool done = false;
	while (!done && !shouldAbort()) {
		pollEvents();

		Common::KeyState k = getNextKey();
		if (k.keycode == Common::KEYCODE_ESCAPE) {
			result = 0;
			break;
		} else if (k.keycode == Common::KEYCODE_RETURN || k.keycode == Common::KEYCODE_KP_ENTER) {
			// Port keys (the original reads none here): Enter acts as OK,
			// Up / Down as a row click.
			result = reloadResult();
			done = true;
			break;
		} else if (k.keycode == Common::KEYCODE_UP && selected > 0) {
			chooseRow(--selected);
		} else if (k.keycode == Common::KEYCODE_DOWN && selected + 1 < (int)saves.size()) {
			chooseRow(++selected);
		}
		if (selected >= 0 && (k.keycode == Common::KEYCODE_UP || k.keycode == Common::KEYCODE_DOWN)) {
			title = saves[selected].name;
			titleX = (640 - fontMinLoadListWidth(title)) / 2;
		}

		Common::Point mouse = getMousePos();
		const bool down = getCurrentMouseButton() == 1;
		const bool press = down && !wasDown;
		if (press && trackItem >= 0) {
			const MenuItem &t = layout.items[trackItem];
			trackLatched = t.hx < mouse.x && mouse.x < t.hx + t.w && t.hy < mouse.y && mouse.y < t.hy + t.h;
		}
		if (!down)
			trackLatched = false;
		wasDown = down;
		loadPickerScrollFrame(sb, trackLatched, down, mouse.y);
		const int scroll = sb.offset;

		int rowHover = -1;
		{
			const int g = menuGridHit(45, 108, 400, 22, 1, kVisible, true, mouse.x, mouse.y);
			if (g >= 0 && g < kVisible && scroll + g < (int)saves.size())
				rowHover = scroll + g;
		}
		int btnHover = hitRects.empty() ? -1 : hitTestMenu(layout, hitRects, mouse);
		if (btnHover != restartIdx && btnHover != okIdx && btnHover != cancelIdx)
			btnHover = -1;

		if (press && !trackLatched) {
			if (rowHover >= 0) {
				selected = rowHover;
				chooseRow(selected);
				title = saves[selected].name;
				titleX = (640 - fontMinLoadListWidth(title)) / 2;
				// A double click on a row loads it (FUN_0041cf78: the window
				// procedure sets 0x00496118 on WM_LBUTTONDBLCLK, the class
				// has CS_DBLCLKS).  The interval is Windows' own setting;
				// 500 ms is its default.
				const uint32 now = g_system->getMillis();
				if (rowHover == lastRowClick && now - lastRowClickMs <= 500) {
					waitMouseRelease();
					result = reloadResult();
					done = true;
					break;
				}
				lastRowClick = rowHover;
				lastRowClickMs = now;
			} else if (btnHover == okIdx) {
				// Record 6 ends the picker; the caller reloads.
				waitMouseRelease();
				result = reloadResult();
				done = true;
				break;
			} else if (btnHover == restartIdx) {
				// Record 5 stores the current chapter 0x00772909 in the
				// player's chapter byte once per listed file (0x0041d522..,
				// so not at all without a file), then the caller reloads.
				waitMouseRelease();
				if (havePlayer && !saves.empty())
					_playerChapter[_currentPlayer] = (int)_currentCONChapter;
				result = reloadResult();
				done = true;
				break;
			} else if (btnHover == cancelIdx) {
				waitMouseRelease();
				result = 0;
				break;
			}
		}

		if (thumbItem >= 0) {
			// The knob is drawn at its stored record position (FUN_0041fb6c
			// keeps a non-zero stored x / y).
			if (sb.knobX != 0)
				layout.items[thumbItem].hx = sb.knobX;
			layout.items[thumbItem].hy = sb.knobY;
		}

		surf.blitFrom(bg);
		renderMenuLayout(surf, layout, btnHover, hitRects);
		for (int i = 0; i + scroll < (int)saves.size() && i < kVisible; i++) {
			int entry = scroll + i;
			drawPlayerText(surf, saves[entry].name, kListX, kListY0 + i * kRowStep,
			               entry == rowHover, kFontMinLoadList);
		}
		if (!title.empty())
			drawPlayerText(surf, title, titleX, 0x50, true, kFontMinNameField);
		g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	clearKeys();
	return result;
}

bool CryOmni3DEngine_Atlantis::omni3dInvertY() const {
	return ConfMan.hasKey(kOmni3dInvertKey) && ConfMan.getBool(kOmni3dInvertKey);
}

// Options menu (OPTMENU.TXT): SUBTITLES and OMNI 3D (look-Y) toggles, plus a
// SOUND OPTIONS button (its sub-menu lands later) and OK.  Both toggles persist
// to ConfMan.  Authored "default" label first: SUBTITLES "ON&&OFF", OMNI 3D
// "MODE 2&&MODE 1".
void CryOmni3DEngine_Atlantis::displayOptionsMenu(const Graphics::ManagedSurface &bg) {
	MenuLayout layout;
	if (!loadMenuLayout("OPTMENU.TXT", layout))
		return;

	int soundIdx = -1, okIdx = -1, subIdx = -1, omniIdx = -1;
	int textOrd = 0, toggleOrd = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind == MenuItem::kText) {
			if (textOrd == 1)      soundIdx = (int)i;
			else if (textOrd == 4) okIdx    = (int)i;
			textOrd++;
		} else if (layout.items[i].kind == MenuItem::kToggle) {
			if (toggleOrd == 0)      subIdx  = (int)i;
			else if (toggleOrd == 1) omniIdx = (int)i;
			toggleOrd++;
		}
	}

	if (subIdx >= 0)
		layout.items[subIdx].selected = showSubtitles() ? 0 : 1;
	if (omniIdx >= 0)
		layout.items[omniIdx].selected = omni3dInvertY() ? 1 : 0;

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface surf(640, 480, fmt);
	Common::Array<Common::Rect> hitRects;
	int hovered = -1;
	bool done = false;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	surf.blitFrom(bg);
	renderMenuLayout(surf, layout, -1, hitRects);
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
	g_system->updateScreen();

	while (!done && !shouldAbort()) {
		pollEvents();
		// FUN_0041d5f4 ends only on a click on SOUND OPTIONS or OK
		// (records 1 and 6); it reads no key.
		getNextKey();

		int newHover = hitTestMenu(layout, hitRects, getMousePos());
		if (newHover != soundIdx && newHover != okIdx &&
		    newHover != subIdx && newHover != omniIdx)
			newHover = -1;
		if (newHover != hovered) {
			hovered = newHover;
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, hovered, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		}

		if (getCurrentMouseButton() == 1 && hovered >= 0) {
			waitMouseRelease();
			if (hovered == okIdx) {
				done = true;
			} else if (hovered == soundIdx) {
				// The original leaves the options screen for good when SOUND
				// OPTIONS is clicked: FUN_0041d5f4's loop exits on item 1, runs
				// the sound menu and returns to the in-game menu.
				displaySoundMenu(bg);
				done = true;
			} else {
				MenuItem &tog = layout.items[hovered];
				if (hovered == subIdx) {
					// FUN_0041d5f4: the SUBTITLES toggle with its click counter.
					bool on = showSubtitles();
					subtitleToggleClick(on, _subtitleToggleCount);
					ConfMan.setBool("subtitles", on);
					tog.selected = on ? 0 : 1;
				} else {
					if (!tog.options.empty())
						tog.selected = (tog.selected + 1) % (int)tog.options.size();
					if (hovered == omniIdx)
						ConfMan.setBool(kOmni3dInvertKey, tog.selected == 1);
				}
			}
			hovered = -1;
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, -1, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		}

		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	ConfMan.flushToDisk();
	clearKeys();
}

static const char *const kMasterVolKey = "atlantis_master_volume";

// Sound menu (SONMENU.TXT — atlantis.exe FUN_0041d940, applied in
// FUN_0041d5f4 @ 0x41d7e2).  Layout records, in file order:
//   0       centred "SOUND OPTIONS" title
//   1..4    slider tracks   /spr=3,285,Y,290,37
//   5..8    slider knobs    /spr=13,X,Y,32,32   (X is the live value store)
//   9       speaker icon    /spr=0,320,370,60,53 (decorative)
//   10..13  Volume / Music / Effects / Voices labels
//   14/15   Left / Right channel test    16/17  OK / Cancel
//
// Original semantics, all exe-verified:
//  - A mouse press latches the record under the cursor (WM_LBUTTONDOWN @
//    0x419c47).  While held with a *track* (1..4) latched, the matching
//    knob X follows mouseX-10, but only while 315 < mouseX < 528.
//  - Knob X positions live in the per-menu record table for the whole
//    session: first open takes the TXT defaults (460/465/470/490), dragging
//    updates them, and they are NEVER recomputed from the active volumes.
//  - OK converts each knob X with (X - 305) / 213 * 127 rounded toward zero (FPU consts
//    @ 0x4bc35b/0x4bc35f/0x4bc363) into the AIL 0..127 volumes, in order
//    master / music / effects / voices; Cancel applies nothing.
//  - Left/Right plays WAV\VAGUE_10.APC hard-panned (AIL pan 0 / 0x7f) at the
//    *applied* effects volume, frozen-screen, until the next click.
//  - Clicking a knob record itself (reachable only on its 1-px sliver above
//    the track rect) does nothing; the original toggles a write-only global
//    there (DAT_004960b0), dead code we do not reproduce.
//  - No keyboard handling: the loop only exits on an OK/Cancel click.
void CryOmni3DEngine_Atlantis::displaySoundMenu(const Graphics::ManagedSurface &bg) {
	MenuLayout layout;
	if (!loadMenuLayout("SONMENU.TXT", layout))
		return;

	// Track records then knob records, in file order.
	int trackItem[4], knobItem[4], nTrack = 0, nKnob = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kSprite)
			continue;
		if (nTrack < 4) trackItem[nTrack++] = (int)i;
		else if (nKnob < 4) knobItem[nKnob++] = (int)i;
	}
	if (nTrack < 4 || nKnob < 4)
		return;
	const int knobSpr = layout.items[knobItem[0]].sprIdx;

	// Knob X state persists across menu opens (the original's record table is
	// only seeded from the TXT when a slot is still zero — FUN_004207c0).
	for (int t = 0; t < 4; t++) {
		if (_sonMenuKnobX[t] == 0)
			_sonMenuKnobX[t] = layout.items[knobItem[t]].hx;
	}

	// Text records by ordinal: 0 title, 1..4 slider labels, 5 Left, 6 Right,
	// 7 OK, 8 Cancel.
	int leftIdx = -1, rightIdx = -1, okIdx = -1, cancelIdx = -1, titleIdx = -1;
	int textOrd = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kText)
			continue;
		if (textOrd == 0)      titleIdx  = (int)i;
		else if (textOrd == 5) leftIdx   = (int)i;
		else if (textOrd == 6) rightIdx  = (int)i;
		else if (textOrd == 7) okIdx     = (int)i;
		else if (textOrd == 8) cancelIdx = (int)i;
		textOrd++;
	}

	// Exe-exact hit-test: first strict-inside record in file order wins
	// (FUN_004210c0).  Sprite rects are the /spr W/H size hints; knob rects
	// follow the dragged X.
	auto hitTest = [&](const Common::Point &m) {
		for (uint i = 0; i < layout.items.size(); i++) {
			const MenuItem &it = layout.items[i];
			int rx, ry, rw, rh;
			if (it.kind == MenuItem::kSprite) {
				rx = it.hx;
				ry = it.hy;
				for (int t = 0; t < 4; t++)
					if ((int)i == knobItem[t])
						rx = _sonMenuKnobX[t];
				rw = it.w;
				rh = it.h;
			} else {
				int x = menuTextDrawX(it, it.text);
				Common::Rect r = menuTextHitRect(it, x);
				if (r.contains(m))
					return (int)i;
				continue;
			}
			if (m.x > rx && m.x < rx + rw && m.y > ry && m.y < ry + rh)
				return (int)i;
		}
		return -1;
	};

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface base(640, 480, fmt);
	base.blitFrom(bg);
	for (uint i = 0; i < layout.items.size(); i++) {
		const MenuItem &it = layout.items[i];
		bool isKnob = false;
		for (int t = 0; t < 4; t++)
			if ((int)i == knobItem[t])
				isKnob = true;
		if (it.kind == MenuItem::kSprite && !isKnob)
			blitMenuSprite(base, (uint)it.sprIdx, it.hx, it.hy);
	}

	// Left/Right channel test (FUN_0041db4c / FUN_0041dcf8): play
	// WAV\VAGUE_10.APC hard left or right and block, screen frozen, until the
	// next press.  The original frees the sample after the click rather than
	// stopping it; a short wave simply plays out, so we leave it running too.
	// It plays on Miles sample 0x006cc564, one of the six the ambient sounds
	// and the puzzles share (miles_samples.h): AIL_stop_sample then
	// AIL_start_sample on it (ui-FUN_0041d5f4.md steps 4 and 6).
	auto channelTest = [&](bool rightChannel) {
		milesSamples().stop(1);
		Common::SeekableReadStream *f = openBigFileStream(kFileTypeSound, "VAGUE_10.APC");
		if (f) {
			Audio::PacketizedAudioStream *apc = Audio::makeAPCStream(*f);
			if (apc) {
				int64 remaining = f->size() - f->pos();
				if (remaining > 0) {
					byte *buf = new byte[remaining];
					f->read(buf, (uint32)remaining);
					apc->queuePacket(new Common::MemoryReadStream(buf, remaining,
					                                              DisposeAfterUse::YES));
				}
				apc->finish();
				milesSamples().playOn(1, apc, Audio::Mixer::kMaxChannelVolume,
				                      rightChannel ? 127 : -127);
			}
			delete f;
		}
		// The original's wait for a click (FUN_0041db4c @ 0x0041dcb9,
		// FUN_0041dcf8 @ 0x0041de65) is entered with the click flag 0x0049611c
		// still set by the press that chose the button, and nothing clears
		// it: the wait ends at once and the menu runs on while the sample
		// plays (atlantis-re functions/ui-FUN_0041d5f4.md).  The original
		// frees the sample's buffer right after starting it; what that plays
		// is [UNKNOWN] — the port plays the sound.
	};

	Graphics::ManagedSurface surf(640, 480, fmt);
	int  grabbed = -1;       // record latched at press (DAT_006cd844)
	bool wasDown = false;
	bool done = false, commit = false;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	while (!done && !shouldAbort()) {
		pollEvents();
		getNextKey();   // drain; the original menu has no keyboard handling

		Common::Point mouse = getMousePos();
		int hit = hitTest(mouse);

		bool down = (getCurrentMouseButton() == 1);
		bool pressEdge = down && !wasDown;
		wasDown = down;
		if (pressEdge)
			grabbed = hit;        // WM_LBUTTONDOWN latch
		else if (!down)
			grabbed = -1;         // WM_LBUTTONUP release

		// Knob drag: a latched track (records 1..4 = trackItem[0..3]) drives
		// its knob to mouseX-10 while 315 < mouseX < 528 (exe @ 0x41d9ae).
		if (down && grabbed >= 0) {
			for (int t = 0; t < 4; t++) {
				if (grabbed == trackItem[t] && mouse.x > 315 && mouse.x < 528)
					_sonMenuKnobX[t] = mouse.x - 10;
			}
		}

		if (pressEdge) {
			if (hit >= 0 && (hit == leftIdx || hit == rightIdx)) {
				channelTest(hit == rightIdx);
				// The press ending the test latches like any other press: a
				// track keeps dragging, buttons do not fire (their click flag
				// was consumed inside the wait loop).
				grabbed = hitTest(getMousePos());
				wasDown = true;
			} else if (hit >= 0 && hit == okIdx) {
				commit = true;
				done = true;
			} else if (hit >= 0 && hit == cancelIdx) {
				done = true;
			}
		}

		surf.blitFrom(base);
		for (int t = 0; t < 4; t++)
			blitMenuSprite(surf, (uint)knobSpr, _sonMenuKnobX[t],
			               layout.items[knobItem[t]].hy);
		for (uint i = 0; i < layout.items.size(); i++) {
			const MenuItem &it = layout.items[i];
			if (it.kind != MenuItem::kText)
				continue;
			int x = menuTextDrawX(it, it.text);
			// Any hovered text record highlights except the title (record 0
			// never highlights outside the main menu — FUN_0041fb6c).
			bool hover = ((int)i == hit) && ((int)i != titleIdx);
			drawFontMaxText(surf, it.text, x, it.anchorY, hover);
		}
		g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	waitMouseRelease();
	clearKeys();

	if (commit) {
		// OK applies (knobX - 305) / 213 * 127, rounded toward zero, per
		// slider (menuVolumeFromKnob) — the AIL 0..127 volumes — rescaled here
		// to ScummVM's 0..kMaxMixerVolume.
		int v[4];
		for (int t = 0; t < 4; t++) {
			int a = menuVolumeFromKnob(_sonMenuKnobX[t]);
			v[t] = a * Audio::Mixer::kMaxMixerVolume / 127;
		}
		ConfMan.setInt(kMasterVolKey,   v[0]);
		ConfMan.setInt("music_volume",  v[1]);
		ConfMan.setInt("sfx_volume",    v[2]);
		ConfMan.setInt("speech_volume", v[3]);
		ConfMan.flushToDisk();
		syncSoundSettings();
	}
}

// "ARE YOU SURE ?" quit confirmation (SUREMENU.TXT) over the menu backdrop.
// The prompt label is inert; only the OK (confirm) and Cancel buttons react.
// Returns true to quit.
bool CryOmni3DEngine_Atlantis::confirmQuit(const Graphics::ManagedSurface &bg) {
	MenuLayout layout;
	if (!loadMenuLayout("SUREMENU.TXT", layout))
		return true;   // no confirmation data on this disc — just quit

	int confirmIdx = -1, cancelIdx = -1;
	uint textOrdinal = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kText)
			continue;
		if (textOrdinal == 1)
			confirmIdx = (int)i;   // OK
		else if (textOrdinal == 2)
			cancelIdx = (int)i;    // Cancel
		textOrdinal++;
	}

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface surf(640, 480, fmt);
	Common::Array<Common::Rect> hitRects;
	int hovered = -1;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	surf.blitFrom(bg);
	renderMenuLayout(surf, layout, -1, hitRects);
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
	g_system->updateScreen();

	while (!shouldAbort()) {
		pollEvents();
		if (getNextKey().keycode == Common::KEYCODE_ESCAPE)
			return false;

		int hit = hitTestMenu(layout, hitRects, getMousePos());
		if (hit != confirmIdx && hit != cancelIdx)
			hit = -1;
		if (hit != hovered) {
			hovered = hit;
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, hovered, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		}

		if (getCurrentMouseButton() == 1 && hovered >= 0) {
			waitMouseRelease();
			return hovered == confirmIdx;
		}

		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	return false;
}

// atlantis.exe FUN_0041eda8 (atlantis-re: functions/ui-FUN_0041eda8.md).
// FUN_0041cc28 dims the screen in place (FUN_004108e0: each 5-6-5 channel
// c becomes c - c / 2) and keeps it; the loop draws CDnMENU.TXT (menu 7 +
// disc) over it and presents, until the hovered record is 3 (OK) on a click
// or 1000 ticks of the 200 Hz timer (5 s) have passed since the loop began;
// then the disc's archive is opened and its label checked (always found:
// the port reads every disc), FUN_0041ce8c puts the screen back, and
// 0x00772913 = disc.
void CryOmni3DEngine_Atlantis::changeDisc(uint discIdx) {
	if (discIdx > 3)
		discIdx = 3;
	const Common::String name = Common::String::format("CD%uMENU.TXT", discIdx + 1);
	debugC(1, kDebugScript, "changeDisc: %s (disc %u -> %u)", name.c_str(), _currentDisc, discIdx + 1);

	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface saved(640, 480, fmt);
	{
		Graphics::Surface *scr = g_system->lockScreen();
		if (scr)
			saved.blitFrom(*scr);
		g_system->unlockScreen();
	}
	Graphics::ManagedSurface bg(640, 480, fmt);
	bg.blitFrom(saved);
	dimMenuBackdrop(bg);

	MenuLayout layout;
	if (loadMenuLayout(name.c_str(), layout)) {
		Graphics::ManagedSurface surf(640, 480, fmt);
		Common::Array<Common::Rect> hitRects;
		clearKeys();
		showMouse(true);
		setArrowCursor();
		const uint32 start = g_system->getMillis();
		int hovered = -1;
		while (!shouldAbort()) {
			pollEvents();
			hovered = hitTestMenu(layout, hitRects, getMousePos());
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, hovered, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
			g_system->updateScreen();
			if (hovered == 3 && getCurrentMouseButton() == 1) {
				debugC(1, kDebugScript, "changeDisc: OK after %u ms", g_system->getMillis() - start);
				waitMouseRelease();
				break;
			}
			if (g_system->getMillis() - start >= 5000) {   // 1000 ticks of 5 ms
				debugC(1, kDebugScript, "changeDisc: timeout");
				break;
			}
			musicUpdate();
			g_system->delayMillis(10);
		}
	}

	g_system->copyRectToScreen(saved.getPixels(), saved.pitch, 0, 0, 640, 480);
	g_system->updateScreen();
	_currentDisc = (uint8)(discIdx + 1);
}

// In-game (Escape) menu over a panorama snapshot.  Reproduces MAINMENU.TXT's
// three buttons by ordinal (LOAD GAME / OPTIONS MENU / QUIT GAME — never by
// label text, so localized discs work).  Returns 0 = resume, 28 = load (sets
// _loadedSave), 40 = quit.  The OPTIONS sub-menu, the in-game LOAD picker, the
// quit confirmation and the quit credits outro land in later commits — for now
// LOAD/OPTIONS just resume (load still works through the ScummVM menu) and QUIT
// signals quit directly.
uint CryOmni3DEngine_Atlantis::displayInGameMenu() {
	Graphics::PixelFormat fmt = g_system->getScreenFormat();
	Graphics::ManagedSurface bg(640, 480, fmt);
	if (_warpLoaded) {
		const Graphics::Surface *pano = _omni3dMan.getSurface();
		if (pano)
			bg.blitFrom(*pano);
	} else {
		bg.fillRect(Common::Rect(0, 0, 640, 480), fmt.RGBToColor(0, 0, 0));
	}
	// FUN_0041c838 -> FUN_0041cc28: the menu and its sub-menus (load,
	// options, sound, quit) are drawn over the view dimmed to half.
	dimMenuBackdrop(bg);

	MenuLayout layout;
	loadMenuLayout("MAINMENU.TXT", layout);

	enum Action { kNone = 0, kContinue = 1, kLoad = 3, kQuit = 5, kOptions = 6, kNewGame = 7 };
	static const int kMainMenuActions[] = { kLoad, kOptions, kQuit };
	Common::Array<int> actions;
	actions.resize(layout.items.size());
	uint textOrdinal = 0;
	for (uint i = 0; i < layout.items.size(); i++) {
		if (layout.items[i].kind != MenuItem::kText) {
			actions[i] = kNone;
			continue;
		}
		actions[i] = (textOrdinal < ARRAYSIZE(kMainMenuActions))
		             ? kMainMenuActions[textOrdinal] : kNone;
		textOrdinal++;
	}

	Graphics::ManagedSurface surf(640, 480, fmt);
	Common::Array<Common::Rect> hitRects;
	int hovered = -1;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	surf.blitFrom(bg);
	renderMenuLayout(surf, layout, -1, hitRects);
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
	g_system->updateScreen();

	uint retval = kContinue;
	bool done = false;
	while (!done && !shouldAbort()) {
		pollEvents();

		if (getNextKey().keycode == Common::KEYCODE_ESCAPE) {
			retval = kContinue;
			break;
		}

		Common::Point mouse = getMousePos();
		int newHover = hitTestMenu(layout, hitRects, mouse);
		if (newHover != hovered) {
			hovered = newHover;
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, hovered, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		}

		if (getCurrentMouseButton() == 1 && hovered >= 0) {
			int act = actions[hovered];
			waitMouseRelease();
			if (act == kQuit) {
				if (confirmQuit(bg)) {
					// Credits slideshow before signalling quit — only this
					// explicit QUIT GAME button runs it; other quit paths
					// (Escape dismiss, window close, GMM Quit) bypass the
					// outro by design.
					playQuitOutro();
					retval = kQuit;
					done = true;
					break;
				}
				// Cancelled — repaint the in-game menu and stay.
				hovered = -1;
				surf.blitFrom(bg);
				renderMenuLayout(surf, layout, -1, hitRects);
				g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
				g_system->updateScreen();
				continue;
			}
			if (act == kOptions) {
				displayOptionsMenu(bg);
				hovered = -1;
				surf.blitFrom(bg);
				renderMenuLayout(surf, layout, -1, hitRects);
				g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
				g_system->updateScreen();
				continue;
			}
			if (act == kLoad) {
				Common::String dummy;
				uint slot = displaySavePicker(false, dummy);
				if (slot == kSavePickerNewGame) {
					retval = kNewGame;
					done = true;
					break;
				}
				if (slot > 0) {
					_loadedSave = slot;
					retval = kLoad;
					done = true;
					break;
				}
				// Cancelled — repaint and stay in the menu.
				hovered = -1;
				surf.blitFrom(bg);
				renderMenuLayout(surf, layout, -1, hitRects);
				g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
				g_system->updateScreen();
				continue;
			}
			debugC(1, kDebugScript, "displayInGameMenu: action %d not handled", act);
		}

		musicUpdate();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	clearKeys();
	if (retval == kQuit)
		return 40;
	if (retval == kLoad)
		return 28;   // _loadedSave is set; the caller reloads it
	if (retval == kNewGame)
		return 27;   // the checkpoint to reload is missing: a new game
	return 0;
}

Common::String CryOmni3DEngine_Atlantis::episodeName(uint chapter) const {
	return epiEpisodeName(_epiText.data(), _epiText.size(), chapter);
}

// Parse a SPRLIST\*MENU.TXT layout (base name only, e.g. "MAINMENU.TXT").
bool CryOmni3DEngine_Atlantis::loadMenuLayout(const char *baseName,
        MenuLayout &out) const {
	Common::ScopedPtr<Common::SeekableReadStream> s(
	    openBigFileStream(kFileTypeSprite, baseName));
	if (!s) {
		warning("loadMenuLayout: cannot open SPRLIST\\%s", baseName);
		return false;
	}
	if (!out.loadFromStream(*s))
		return false;
	out.menu = menuNumber(baseName);
	return true;
}

uint CryOmni3DEngine_Atlantis::displayFilePicker(const Graphics::Surface *bgFrame,
        bool saveMode, Common::String &saveName) {
	(void)bgFrame;
	return displaySavePicker(saveMode, saveName);
}

// The main menu (startup) / in-game options entry point — the CryOmni3DEngine
// displayOptions() hook.  At startup it offers New Game / Load Game / Quit; in
// game, Continue / Load / New / Quit.  Returns 0 = continue, 27 = new game,
// 28 = load (sets _loadedSave), 40 = quit.
uint CryOmni3DEngine_Atlantis::displayOptions() {
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);

	struct Entry { const char *label; uint retval; };
	static const Entry kStartup[] = { {"New Game",27}, {"Load Game",28}, {"Quit",40} };
	static const Entry kInGame[]  = { {"Continue",0}, {"Load Game",28}, {"New Game",27}, {"Quit",40} };
	const Entry *entries = _isPlaying ? kInGame : kStartup;
	uint numEntries = _isPlaying ? ARRAYSIZE(kInGame) : ARRAYSIZE(kStartup);

	Graphics::PixelFormat fmt = g_system->getScreenFormat();

	// PREINTRO.TGA backdrop (black fallback) + the SELEMENU ornament sprites.
	Graphics::ManagedSurface bgSurf;
	{
		Graphics::Surface *raw = loadTGA(kFileTypeImages, "PREINTRO.TGA");
		if (raw) {
			bgSurf.copyFrom(*raw);
			raw->free();
			delete raw;
		} else {
			bgSurf.create(640, 480, fmt);
			bgSurf.fillRect(Common::Rect(0, 0, 640, 480), fmt.RGBToColor(0, 0, 0));
		}
	}
	blitMenuSprite(bgSurf,  8, 110, 182);
	blitMenuSprite(bgSurf,  7, 520, 184);
	blitMenuSprite(bgSurf,  9, 180, 270);
	blitMenuSprite(bgSurf, 10, 450, 340);

	Graphics::ManagedSurface surf(640, 480, fmt);
	const int lineH  = font ? (font->getFontHeight() + 4) : 20;
	const int blockH = (int)numEntries * lineH;
	const int y = (480 - blockH) / 2;
	const uint32 white  = fmt.RGBToColor(255, 255, 255);
	const uint32 yellow = fmt.RGBToColor(255, 255, 0);

	Common::Array<Common::Rect> rects;
	for (uint i = 0; i < numEntries; i++)
		rects.push_back(Common::Rect(160, y + (int)i * lineH, 480, y + (int)(i + 1) * lineH));

	auto redraw = [&](int hov) {
		surf.blitFrom(bgSurf);
		if (font)
			font->drawString(&surf, "ATLANTIS: The Lost Tales", 0, y - lineH * 2, 640,
			                 white, Graphics::kTextAlignCenter);
		for (uint i = 0; i < numEntries; i++) {
			uint32 col = ((int)i == hov) ? yellow : white;
			if (font)
				font->drawString(&surf, entries[i].label, rects[i].left, rects[i].top,
				                 rects[i].width(), col, Graphics::kTextAlignCenter);
		}
		g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		g_system->updateScreen();
	};

	showMouse(true);
	setArrowCursor();
	int hovered = -1;
	redraw(-1);

	while (!shouldAbort()) {
		pollEvents();
		Common::Point mouse = getMousePos();
		int newHover = -1;
		for (int i = 0; i < (int)numEntries; i++)
			if (rects[i].contains(mouse)) { newHover = i; break; }
		if (newHover != hovered) {
			hovered = newHover;
			redraw(hovered);
		}

		if (getCurrentMouseButton() == 1 && hovered >= 0) {
			uint retval = entries[hovered].retval;
			waitMouseRelease();
			if (retval == 28) {
				Common::String dummy;
				uint slot = displayFilePicker(nullptr, false, dummy);
				if (slot == kSavePickerNewGame)
					return 27;
				if (slot > 0) {
					_loadedSave = slot;
					return 28;
				}
				hovered = -1;
				redraw(-1);
				continue;
			}
			return retval;
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	return 40;   // window close
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
