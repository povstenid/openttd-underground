/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_gui.h Functions/types etc. related to the rail GUI. */

#ifndef RAIL_GUI_H
#define RAIL_GUI_H

#include "rail_type.h"
#include "signal_type.h"
#include "tile_type.h"
#include "dropdown_type.h"

struct Window *ShowBuildRailToolbar(RailType railtype);
void ReinitGuiAfterToggleElrail(bool disable);
void ResetSignalVariant(int32_t = 0);
void InitializeRailGUI();
DropDownList GetRailTypeDropDownList(bool for_replacement = false, bool all_option = false);

/** What the signal tool builds; see GetSignalToolSelection. */
struct SignalToolSelection {
	SignalType type;          ///< Type of new or converted signals.
	SignalVariant variant;    ///< Variant of new or converted signals.
	bool convert;             ///< The convert button of the signal selection window is down.
	SignalType cycle_start;   ///< First type Ctrl+click cycles through.
	SignalType cycle_end;     ///< Last type Ctrl+click cycles through.
};

RailType PickRailTypeAtTile(TileIndex tile, std::optional<int16_t> depth);
void ShowBuildRailToolbarWithPicker();

/** Value of the "copy track type from a tile" item in the rail type menu of the main toolbar. */
static constexpr int RAILTYPE_MENU_PICK = 0x100;
bool CheckPickedRailType(RailType rt, TileIndex tile);
SignalToolSelection GetSignalToolSelection(std::optional<SignalType> signal_on_tile);

/** Settings for which signals are shown by the signal GUI. */
enum SignalGUISettings : uint8_t {
	SIGNAL_GUI_PATH = 0, ///< Show path signals only.
	SIGNAL_GUI_ALL = 1,  ///< Show all signals, including block and presignals.
};

/** Settings for which signals are cycled through by control-clicking on the signal with the signal tool. */
enum SignalCycleSettings : uint8_t {
	SIGNAL_CYCLE_GROUP = 0, ///< Cycle through current signal group (block or path) only.
	SIGNAL_CYCLE_ALL = 1,   ///< Cycle through all signals visible to the player.
};

#endif /* RAIL_GUI_H */
