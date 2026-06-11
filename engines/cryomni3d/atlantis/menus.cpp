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

#include "cryomni3d/atlantis/engine.h"
#include "cryomni3d/atlantis/sprite_blend.h"

namespace CryOmni3D {
namespace Atlantis {

// Space advance from the exe width table at 0x4965ee[' '] = 22.
static const int kFontMaxSpaceAdvance = 22;
static const int kFontMinSpaceAdvance = 18;

// FONTMAX *measuring* width table (atlantis.exe @ 0x4965ee, glyph idx = char).
// The original uses it only to centre "<>,Y>" labels and size their hit rects
// (FUN_004206c8); pen advance while drawing comes from the glyph sprites.
static const uint8 kFontMaxMeasureWidths[137] = {
	22,10,15,15,16,24,20, 8,16,24,13, 8, 8,24, 7,14,20, 9,19,19,
	17,16,18,17,20,18, 7, 8, 8, 8, 8,18, 8,21,23,21,23,19,20,22,
	22,14,15,20,19,28,24,28,20,27,21,19,24,23,21,28,26,22,21, 8,
	19, 8,11, 8, 8,18,19,17,19,18,14,19,19,10,11,18,10,23,20,20,
	19,19,15,15,12,20,18,23,20,19,17,18, 7,20,13,18,21,20,18,18,
	18,18,18,19,18,18,18,13,11,13,21,21,19,26,32,20,20,20,20,20,
	19,28,23,17,24, 0,25,20,18,13,20,20,20,24, 2,12,16
};

static int fontMaxMeasureWidth(const Common::String &text) {
	int w = 0;
	for (uint i = 0; i < text.size(); ++i) {
		int idx = (int)(unsigned char)text[i] - 0x20;
		if (idx >= 0 && idx < (int)ARRAYSIZE(kFontMaxMeasureWidths))
			w += kFontMaxMeasureWidths[idx];
	}
	return w;
}

// Exe-exact menu text hit rect (FUN_0041fb6c).  A text record's clickable box
// is X-11 / Y-28, height 28; width is the measured string for centred items
// but 22 px per raw spec character for positioned ones.  The original tests
// strict inside (left < x < left+W), which a Common::Rect reproduces with its
// bounds shrunk by one on the left/top.
static Common::Rect menuTextHitRect(const MenuItem &it, int drawX) {
	int w;
	if (it.centerX)
		w = fontMaxMeasureWidth(it.text);
	else
		w = it.rawLen * 22;
	int left = drawX - 11;
	int top  = it.anchorY - 28;
	return Common::Rect(left + 1, top + 1, left + w, top + 28);
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

		// Centred labels are positioned with the exe's measuring table, not the
		// glyph-sprite advances (atlantis.exe FUN_004206c8: (640 - w) / 2).
		int x = it.centerX ? (640 - fontMaxMeasureWidth(label)) / 2 : it.anchorX;
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

// FONTMIN per-char advance widths (atlantis.exe @ 0x496697), glyph idx char-0x20.
static const uint8 kPlayerCharWidths[137] = {
	15, 8,13, 6,13,20,17, 6,13,20,11, 6, 4,20,15,12,17, 8,16,16,
	14,13,15,13,16,15, 7, 6, 6, 6, 6,20, 6,17,19,18,19,16,16,19,
	18,12,12,17,16,24,20,23,17,22,17,15,19,20,17,23,22,18,17, 7,
	16, 6,10, 6, 7,16,16,15,15,16,12,16,17, 8, 9,16, 7,19,16,17,
	16,15,13,12,10,17,15,19,17,15,14,16, 6,17,11,16,17,17,15,16,
	15,16,16,16,15,15,15,11,10,10,18,18,16,17,17,17,17,15,23,19,
	15,19, 0,21,17,16,10,17,17,10,20,14,14,13, 3, 5, 8
};

// Player/save-row text in FONTMIN (FONTMIN2 when hover), spaced by the exe's
// player width table so a hover swap only recolours, never reflows.
void CryOmni3DEngine_Atlantis::drawPlayerText(Graphics::ManagedSurface &dst,
        const Common::String &text, int anchorX, int anchorY, bool hover) const {
	const Common::Array<SubjSprite> &glyphs =
	    (hover && !_fontMin2Sprites.empty()) ? _fontMin2Sprites : _fontMinSprites;
	int cx = anchorX;
	for (uint i = 0; i < text.size(); ++i) {
		int gi = (int)(unsigned char)text[i] - 0x20;
		if (gi < 0 || gi >= (int)glyphs.size())
			continue;
		const SubjSprite &spr = glyphs[gi];
		if (!spr.pixels.empty()) {
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
		if (gi < (int)ARRAYSIZE(kPlayerCharWidths))
			cx += kPlayerCharWidths[gi];
	}
}

// LOAD screen (LOADMENU.TXT): the current player's per-episode saves listed by
// their EPI.TXT names, with RESTART EPISODE / OK / Cancel and a draggable
// scrollbar (atlantis.exe FUN_0041cf78).  Returns the 1-based slot to load, or
// 0 on cancel.  Atlantis has no manual save, so this is load-only.
uint CryOmni3DEngine_Atlantis::displaySavePicker(bool saveMode, Common::String &outSaveName) {
	(void)saveMode;
	(void)outSaveName;

	const int kListX = 50, kListY0 = 133, kRowStep = 20, kVisible = 11;
	Graphics::PixelFormat fmt = g_system->getScreenFormat();

	Graphics::ManagedSurface bg(640, 480, fmt);
	const Graphics::Surface *pano = _warpLoaded ? _omni3dMan.getSurface() : nullptr;
	if (pano)
		bg.blitFrom(*pano);
	else
		bg.fillRect(Common::Rect(0, 0, 640, 480), fmt.RGBToColor(0, 0, 0));

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
		if (ch == _currentCONChapter)
			selected = (int)saves.size();
		saves.push_back(e);
	}

	auto rowWidth = [&](const Common::String &s) {
		int w = 0;
		for (uint i = 0; i < s.size(); i++) {
			int gi = (int)(unsigned char)s[i] - 0x20;
			if (gi >= 0 && gi < (int)ARRAYSIZE(kPlayerCharWidths))
				w += kPlayerCharWidths[gi];
		}
		return w;
	};

	const int maxScroll = MAX(0, (int)saves.size() - kVisible);
	int scroll = (selected >= 0) ? CLIP(selected - kVisible / 2, 0, maxScroll) : 0;

	// Scrollbar track (LOADMENU /spr=13 thumb) — interactive Y span [123,338].
	const int kBarTop = 123, kBarBot = 338, kBarH = kBarBot - kBarTop;
	const int kBarX0 = 595, kBarX1 = 640;
	const bool scrollable = maxScroll > 0;
	int thumbItem = -1;
	for (uint i = 0; i < layout.items.size(); i++)
		if (layout.items[i].kind == MenuItem::kSprite && layout.items[i].sprIdx == 13) {
			thumbItem = (int)i;
			break;
		}

	Graphics::ManagedSurface surf(640, 480, fmt);
	Common::Array<Common::Rect> hitRects;

	clearKeys();
	showMouse(true);
	setArrowCursor();

	uint result = 0;
	bool done = false;
	while (!done && !shouldAbort()) {
		pollEvents();

		Common::KeyState k = getNextKey();
		if (k.keycode == Common::KEYCODE_ESCAPE) {
			result = 0;
			break;
		} else if ((k.keycode == Common::KEYCODE_RETURN || k.keycode == Common::KEYCODE_KP_ENTER)
		        && selected >= 0) {
			result = (uint)episodeSaveSlot((uint)_currentPlayer, (uint)saves[selected].localSlot) + 1;
			done = true;
			break;
		} else if (k.keycode == Common::KEYCODE_UP && selected > 0) {
			selected--;
		} else if (k.keycode == Common::KEYCODE_DOWN && selected + 1 < (int)saves.size()) {
			selected++;
		}
		if (selected >= 0) {
			if (selected < scroll) scroll = selected;
			else if (selected >= scroll + kVisible) scroll = selected - kVisible + 1;
		}

		Common::Point mouse = getMousePos();
		int rowHover = -1;
		for (int i = 0; i + scroll < (int)saves.size() && i < kVisible; i++) {
			int entry = scroll + i;
			int ay = kListY0 + i * kRowStep;
			int w  = MAX(rowWidth(saves[entry].name), 60);
			if (Common::Rect(kListX, ay - 17, kListX + w, ay + 4).contains(mouse)) {
				rowHover = entry;
				break;
			}
		}
		int btnHover = hitRects.empty() ? -1 : hitTestMenu(layout, hitRects, mouse);
		if (btnHover != restartIdx && btnHover != okIdx && btnHover != cancelIdx)
			btnHover = -1;

		bool draggingBar = false;
		if (scrollable && getCurrentMouseButton() == 1 &&
		        mouse.x >= kBarX0 && mouse.x <= kBarX1 &&
		        mouse.y >= kBarTop && mouse.y <= kBarBot) {
			scroll = CLIP((mouse.y - kBarTop) * maxScroll / kBarH, 0, maxScroll);
			draggingBar = true;
		}
		if (!draggingBar && getCurrentMouseButton() == 1) {
			waitMouseRelease();
			if (rowHover >= 0) {
				selected = rowHover;
			} else if (btnHover == okIdx && selected >= 0) {
				result = (uint)episodeSaveSlot((uint)_currentPlayer, (uint)saves[selected].localSlot) + 1;
				done = true;
				break;
			} else if (btnHover == restartIdx) {
				int slot = episodeSaveSlot((uint)_currentPlayer, _currentCONChapter);
				if (saveSlotExists(slot)) {
					result = (uint)slot + 1;
					done = true;
					break;
				}
			} else if (btnHover == cancelIdx) {
				result = 0;
				break;
			}
		}

		if (thumbItem >= 0)
			layout.items[thumbItem].hy = scrollable
			    ? kBarTop + (maxScroll > 0 ? scroll * kBarH / maxScroll : 0) : -100;

		surf.blitFrom(bg);
		renderMenuLayout(surf, layout, btnHover, hitRects);
		for (int i = 0; i + scroll < (int)saves.size() && i < kVisible; i++) {
			int entry = scroll + i;
			drawPlayerText(surf, saves[entry].name, kListX, kListY0 + i * kRowStep,
			               entry == selected);
		}
		g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
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
		if (getNextKey().keycode == Common::KEYCODE_ESCAPE)
			break;

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
				// The SOUND OPTIONS sub-menu lands in a later commit.
				debugC(1, kDebugScript, "displayOptionsMenu: sound menu not yet available");
			} else {
				MenuItem &tog = layout.items[hovered];
				if (!tog.options.empty())
					tog.selected = (tog.selected + 1) % (int)tog.options.size();
				if (hovered == subIdx)
					ConfMan.setBool("subtitles", tog.selected == 0);
				else if (hovered == omniIdx)
					ConfMan.setBool(kOmni3dInvertKey, tog.selected == 1);
			}
			hovered = -1;
			surf.blitFrom(bg);
			renderMenuLayout(surf, layout, -1, hitRects);
			g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, 640, 480);
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	ConfMan.flushToDisk();
	clearKeys();
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

		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	return false;
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
				if (confirmQuit(bg)) {
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

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	clearKeys();
	if (retval == kQuit)
		return 40;
	if (retval == kLoad)
		return 28;   // _loadedSave is set; the caller reloads it
	return 0;
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
			font->drawString(&surf, "ATLANTIS: The Lost Tale", 0, y - lineH * 2, 640,
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
