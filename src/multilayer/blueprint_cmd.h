/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint_cmd.h Command for pasting metro blueprints. */

#ifndef BLUEPRINT_CMD_H
#define BLUEPRINT_CMD_H

#include "../command_type.h"

std::tuple<CommandCost, uint16_t, uint16_t> CmdPasteUndergroundBlueprint(DoCommandFlags flags, TileIndex tile, int16_t depth, uint8_t options, const std::string &data);

/* Pasting houses removes them, which changes town ratings between the test and the execution; the command checks the money itself. */
DEF_CMD_TRAIT(Commands::PasteUndergroundBlueprint, CmdPasteUndergroundBlueprint, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater, CommandFlag::NoTest}), CommandType::LandscapeConstruction)

void CcPasteUndergroundBlueprint(Commands cmd, const CommandCost &result, uint16_t built, uint16_t left_out);

#endif /* BLUEPRINT_CMD_H */
