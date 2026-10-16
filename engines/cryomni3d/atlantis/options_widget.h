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

#ifndef CRYOMNI3D_ATLANTIS_OPTIONS_WIDGET_H
#define CRYOMNI3D_ATLANTIS_OPTIONS_WIDGET_H

#include "gui/ThemeEval.h"
#include "gui/widget.h"

namespace CryOmni3D {
namespace Atlantis {

// The game options of an Atlantis target: ScummVM's enhancement groups
// (engines/enhancements.h) that change something in this engine
// (atlantis-re decisions/0016).  Group 1, "fix original bugs": with it off
// the music plays through the original's streaming ring.
class AtlantisOptionsWidget : public GUI::OptionsContainerWidget {
public:
	AtlantisOptionsWidget(GuiObject *boss, const Common::String &name, const Common::String &domain);
	~AtlantisOptionsWidget() override {}

	void load() override;
	bool save() override;

private:
	void defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName, const Common::String &overlayedLayout) const override;

	GUI::CheckboxWidget *_group1 = nullptr;
};

} // End of namespace Atlantis
} // End of namespace CryOmni3D

#endif
