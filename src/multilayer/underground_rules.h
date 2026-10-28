/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file underground_rules.h What the surface takes of the ground below it:
 * the foundations of houses and the depth of water (metro v2, spec sections 3 and 4).
 *
 * Both are measured as a depth below the surface, g = GetMetroSurfaceZ(tile) - level.
 * Underground construction at a depth g <= f (house foundation) or g <= w
 * (water depth) is refused; nothing of this is stored in the savegame.
 */

#ifndef UNDERGROUND_RULES_H
#define UNDERGROUND_RULES_H

#include "../command_type.h"
#include "../house_type.h"
#include "../tile_type.h"
#include <span>

/** Deepest house foundation, in levels below the surface. */
static constexpr uint8_t MAX_HOUSE_FOUNDATION_DEPTH = 2;

/* Settings of the "Underground" settings page (spec section 5). */
uint GetFoundationThresholdHighPercent();
uint GetFoundationThresholdMidPercent();
bool AreSeaTunnelsAllowedByDepth();

void InvalidateHouseFoundationDepths();
uint8_t GetHouseTypeFoundationDepth(HouseID house);
uint8_t GetHouseFoundationDepth(TileIndex tile);
bool HasUndergroundWithinDepth(TileIndex tile, uint depth);
bool CanHouseFoundationBeBuilt(HouseID house, TileIndex tile);

bool IsUndergroundWaterTile(TileIndex tile);
uint8_t GetWaterDepth(TileIndex tile);

/** What the surface takes of the ground at a level of a column (for drawing and the land info). */
enum class UndergroundObstacle : uint8_t {
	None,            ///< Nothing: the level is soil (or not below the ground at all).
	HouseFoundation, ///< The foundation of a house reaches down to the level.
	Water,           ///< The level lies in the water column of a river, canal or the sea.
	SeaForbidden,    ///< Below the water of the sea, where underground construction is not allowed.
};

UndergroundObstacle GetUndergroundObstacle(TileIndex tile, int16_t level, uint *depth = nullptr);
CommandCost CheckUndergroundClearance(TileIndex tile, int16_t level);
CommandCost CheckNewWaterOverUnderground(std::span<const TileIndex> tiles, bool sea);

#endif /* UNDERGROUND_RULES_H */
