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

#include "common/config-manager.h"
#include "common/translation.h"
#include "engines/enhancements.h"
#include "gui/widget.h"

#include "cryomni3d/atlantis/options_widget.h"

namespace CryOmni3D {
namespace Atlantis {

AtlantisOptionsWidget::AtlantisOptionsWidget(GuiObject *boss, const Common::String &name, const Common::String &domain) :
		OptionsContainerWidget(boss, name, "AtlantisGameOptionsDialog", domain) {
	GUI::StaticTextWidget *text = new GUI::StaticTextWidget(widgetsBoss(), _dialogLayout + ".enhancementsLabel", _("Enhancements:"));
	text->setAlign(Graphics::TextAlign::kTextAlignStart);
	_group1 = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + ".enhancementGroup1",
		_("Fix original bugs"),
		_("Fixes bugs which were present in the original release, and noticeable graphical/audio glitches."));
}

// The value an engine starts with (engines/engine.cpp registers it as the
// default when an engine is created, which the launcher has not done).
static int32 enhancementFlags(const Common::String &domain) {
	return ConfMan.hasKey("enhancements", domain) ? (int32)ConfMan.getInt("enhancements", domain)
	       : (kEnhGameBreakingBugFixes | kEnhGrp1);
}

void AtlantisOptionsWidget::load() {
	_group1->setState((enhancementFlags(_domain) & kEnhGrp1) != 0);
}

bool AtlantisOptionsWidget::save() {
	int32 flags = enhancementFlags(_domain);
	flags |= kEnhGameBreakingBugFixes;   // always active
	if (_group1->getState())
		flags |= kEnhGrp1;
	else
		flags &= ~kEnhGrp1;
	ConfMan.setInt("enhancements", flags, _domain);
	return true;
}

void AtlantisOptionsWidget::defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName, const Common::String &overlayedLayout) const {
	layouts.addDialog(layoutName, overlayedLayout)
		.addLayout(GUI::ThemeLayout::kLayoutVertical, 5)
			.addPadding(0, 0, 8, 8)
			.addWidget("enhancementsLabel", "OptionsLabel")
			.addWidget("enhancementGroup1", "Checkbox")
		.closeLayout()
	.closeDialog();
}

} // End of namespace Atlantis
} // End of namespace CryOmni3D
