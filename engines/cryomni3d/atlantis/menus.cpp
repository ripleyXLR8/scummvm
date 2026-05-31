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

#include "common/events.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "audio/decoders/apc.h"
#include "audio/mixer.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Space advance from the exe width table at 0x4965eb[' '] = 22.
static const int kFontMaxSpaceAdvance = 22;
static const int kFontMinSpaceAdvance = 18;

// FONTMAX text item hit-box: glyphs render with their bottom near anchorY, so
// the click rect spans [anchorY - 32, anchorY + 4].
static const int kFontMaxHitTop    = 32;
static const int kFontMaxHitBottom = 4;

int CryOmni3DEngine_Atlantis::fontMaxCharAdvance(unsigned char c) const {
	if (c == ' ')
		return kFontMaxSpaceAdvance;
	int idx = (int)c - 0x20;
	if (idx < 0 || idx >= (int)_fontMaxSprites.size())
		return 0;
	return _fontMaxSprites[idx].w;
}

int CryOmni3DEngine_Atlantis::fontMaxStringWidth(const Common::String &text) const {
	int w = 0;
	for (uint i = 0; i < text.size(); ++i)
		w += fontMaxCharAdvance((unsigned char)text[i]);
	return w;
}

// Glyph-sprite text blitter.  `layoutGlyphs` drive the pen advance so a hover
// swap to a differently-metricised glyph set never reflows the line;
// `drawGlyphs` supply the pixels, each anchored by its own xoff/yoff.  anchorY
// is the text baseline.
void CryOmni3DEngine_Atlantis::drawSprFontText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY,
        const Common::Array<SubjSprite> &drawGlyphs,
        const Common::Array<SubjSprite> &layoutGlyphs, int spaceAdvance) const {
	int cx = anchorX;
	for (uint i = 0; i < text.size(); ++i) {
		unsigned char c = (unsigned char)text[i];
		if (c == ' ') {
			cx += spaceAdvance;
			continue;
		}
		int idx = (int)c - 0x20;
		if (idx < 0 || idx >= (int)layoutGlyphs.size())
			continue;
		const SubjSprite &lay = layoutGlyphs[idx];   // advance + anchor
		if (idx < (int)drawGlyphs.size() && !drawGlyphs[idx].pixels.empty()) {
			const SubjSprite &spr = drawGlyphs[idx]; // pixels
			int dx = cx - spr.xoff;
			int dy = anchorY - spr.yoff;
			for (int sy = 0; sy < (int)spr.h; ++sy) {
				int ty = dy + sy;
				if (ty < 0 || ty >= dst.h)
					continue;
				for (int sx = 0; sx < (int)spr.w; ++sx) {
					uint sidx = sy * spr.w + sx;
					uint16 pix = spr.pixels[sidx];
					if (pix == kSprTransp)
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
		cx += lay.w;
	}
}

// FONTMAX.SPR menu font.  `hover` swaps in the FONTMAX2.SPR glyphs for colour;
// layout always uses FONTMAX so a hovered entry never shifts.
void CryOmni3DEngine_Atlantis::drawFontMaxText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool hover) const {
	drawSprFontText(dst, text, anchorX, anchorY,
	    (hover && !_fontMaxHoverSprites.empty()) ? _fontMaxHoverSprites
	                                             : _fontMaxSprites,
	    _fontMaxSprites, kFontMaxSpaceAdvance);
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
// (these atlases have NO space slot, unlike FONTMAX's char-0x20).
int CryOmni3DEngine_Atlantis::creditCharAdvance(unsigned char c) const {
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
				if (pix == kSprTransp)
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

	// Narrator subtitle, bottom-anchored with a drop shadow — the proportional
	// kBigGUIFont (Arial-like), matching playSingleConLine; honours the
	// subtitle toggle (the voice still plays when it is off).
	if (showSubtitles() && !narratorText.empty()) {
		const int leftMargin = 5;
		int widthBudget = img.w - leftMargin - 5;
		if (widthBudget < 32)
			widthBudget = 32;
		const Graphics::Font *guiFont =
		    FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
		if (!guiFont)
			guiFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
		if (guiFont) {
			int subLineH = guiFont->getFontHeight() + 2;
			Common::Array<Common::String> subLines;
			guiFont->wordWrapText(narratorText, widthBudget, subLines);
			const uint32 subColor  = fmt.RGBToColor(255, 255, 255);
			const uint32 subShadow = fmt.RGBToColor(0, 0, 0);
			int subBaseY = img.h - (int)subLines.size() * subLineH - 6;
			if (subBaseY < 0)
				subBaseY = 0;
			for (uint i = 0; i < subLines.size(); ++i) {
				int by = subBaseY + (int)i * subLineH;
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
		while (!shouldAbort() && _mixer->isSoundHandleActive(handle)) {
			pollEvents();
			if (checkKeysPressed(1, Common::KEYCODE_SPACE) || getCurrentMouseButton() != 0)
				break;
			g_system->delayMillis(10);
		}
		_mixer->stopHandle(handle);
	} else {
		debugC(1, kDebugScript, "showGameOver: GOVER%02d.TGA (no narrator audio)", n);
		uint32 deadline = g_system->getMillis() + 5000;
		while (!shouldAbort() && g_system->getMillis() < deadline) {
			pollEvents();
			if (checkKeysPressed(1, Common::KEYCODE_SPACE) || getCurrentMouseButton() != 0)
				break;
			g_system->delayMillis(10);
		}
	}
	delete apcFile;

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

		int w = fontMaxStringWidth(label);
		int x = it.centerX ? (640 - w) / 2 : it.anchorX;
		drawFontMaxText(dst, label, x, it.anchorY, (int)i == hoveredTextIdx);
		hitRectsOut[i] = Common::Rect(x, it.anchorY - kFontMaxHitTop,
		                              x + w, it.anchorY + kFontMaxHitBottom);
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
	for (uint i = 0; i < layout.items.size(); i++) {
		MenuItem::Kind k = layout.items[i].kind;
		if (k != MenuItem::kText && k != MenuItem::kToggle)
			continue;
		if (rects[i].contains(mouse))
			return (int)i;
	}
	return -1;
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

	MenuLayout layout;
	loadMenuLayout("MAINMENU.TXT", layout);

	enum Action { kNone = 0, kContinue = 1, kLoad = 3, kQuit = 5, kOptions = 6 };
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
				retval = kQuit;
				done = true;
				break;
			}
			// LOAD / OPTIONS: their screens land later; resume for now.
			debugC(1, kDebugScript, "displayInGameMenu: action %d not yet available", act);
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	clearKeys();
	return (retval == kQuit) ? 40 : 0;
}

Common::String CryOmni3DEngine_Atlantis::episodeName(uint chapter) const {
	if (chapter < 1 || chapter > _episodeNames.size())
		return Common::String();
	return _episodeNames[chapter - 1];
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
	return out.loadFromStream(*s);
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
