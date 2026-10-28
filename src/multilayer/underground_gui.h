/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_gui.h GUI for underground construction. */

#ifndef UNDERGROUND_GUI_H
#define UNDERGROUND_GUI_H

#include "../core/geometry_type.hpp"
#include "../direction_type.h"
#include "../rail_type.h"
#include "../tile_type.h"
#include "../viewport_type.h"
#include "../window_type.h"
#include "underground_view.h"

/** Open the railway construction toolbar building on the viewed underground level. */
void ShowUndergroundToolbar();

/**
 * Toggle one of the view buttons (X-ray, levels below, levels above): switch to
 * \a mode, or back from it when it is already shown. Back is the view the
 * first of these buttons was pressed in, the ordinary or the slice view; without
 * the railway construction toolbar always the ordinary view.
 */
void ToggleUndergroundViewButton(UndergroundViewMode mode);

/*
 * The underground tools of the railway construction toolbar. While an
 * underground view is shown, the toolbar builds on the viewed level; the
 * portal and station exit tools work from the surface too.
 */
void ShowUndergroundStationBuilder(Window *parent, RailType railtype);
void SetUndergroundStationBuilderRailType(RailType railtype);
void CloseUndergroundStationBuilder();
bool UndergroundRailToolPlaceObject(WidgetID tool, TileIndex tile, RailType railtype);
bool UndergroundRailToolPlaceMouseUp(WidgetID tool, ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc,
	TileIndex start_tile, TileIndex end_tile, RailType railtype, bool remove);
void UpdateUndergroundRailToolGhost(WidgetID tool, RailType railtype);

/** The ramp a ramp tool would build, shown under the cursor before it is built (client-side only). */
struct UndergroundRampGhost {
	TileIndex tile = INVALID_TILE; ///< Tile of the ramp, INVALID_TILE when none is shown.
	DiagDirection dir = INVALID_DIAGDIR; ///< Direction of travel from the entry level to the exit level, like RampPayload::direction.
	int16_t entry_depth = 0; ///< The viewed level.
	int16_t exit_depth = 0;  ///< The level below or above it.
	RailType railtype = INVALID_RAILTYPE; ///< Track type built.
	bool buildable = false;  ///< The command would succeed.

	bool operator==(const UndergroundRampGhost &) const = default;
};

const UndergroundRampGhost *GetUndergroundRampGhost(TileIndex tile);

/**
 * Show (or hide) the tooltip with the soil and its price multipliers while a
 * tool of the railway construction toolbar hovers a tile of the viewed underground level.
 * @param tile The tile under the cursor, INVALID_TILE when there is none.
 */
void UpdateUndergroundSoilTooltip(TileIndex tile);

#endif /* UNDERGROUND_GUI_H */
