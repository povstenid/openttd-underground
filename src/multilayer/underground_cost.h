/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_cost.h Prices of underground construction (metro v2 spec 2.3): surface price x base multiplier x soil. */

#ifndef UNDERGROUND_COST_H
#define UNDERGROUND_COST_H

#include "../economy_type.h"
#include "../rail_type.h"
#include "../tile_type.h"
#include "underground_soil.h"

/** What is built underground, for its price. */
enum class UndergroundCostKind : uint8_t {
	Track,   ///< One piece of track: rail price x base x tunnel soil.
	Station, ///< One platform tile: surface station tile price x base x station soil.
	Ramp,    ///< A ramp: rail price x base x ramp multiplier x tunnel soil of its lower level.
	Portal,  ///< A portal: rail price x base x portal multiplier x tunnel soil of the first underground level.
	Signal,  ///< A signal (or a change of its variant): surface signal price x base, no soil.
};

/**
 * Soil price multiplier of a kind of construction in a soil layer, from the settings.
 * @param kind  What is built; stations have their own multipliers, signals none (10).
 * @param layer The soil.
 * @return The multiplier in tenths (15 = x1.5).
 */
uint GetUndergroundSoilMultiplier(UndergroundCostKind kind, SoilLayer layer);

/**
 * Soil the price of a kind of construction at a level of a tile is reckoned in:
 * a portal is always reckoned in the first underground level (a hillside mouth
 * lies at the surface itself), other kinds at their level.
 * @param kind  What is built.
 * @param tile  Tile column.
 * @param level Level of the construction; for a ramp its lower level.
 * @return The soil layer.
 */
SoilLayer GetUndergroundCostSoil(UndergroundCostKind kind, TileIndex tile, int16_t level);

/**
 * Price of building something underground. Every underground cost goes
 * through here, so test runs, Shift estimates and blueprints charge the same.
 * @param kind     What is built.
 * @param railtype Its rail type (unused for signals); invalid types count as RAILTYPE_RAIL.
 * @param tile     Tile column.
 * @param level    Level of the construction; for a ramp its lower level.
 * @return The price, never negative.
 */
Money GetUndergroundBuildCost(UndergroundCostKind kind, RailType railtype, TileIndex tile, int16_t level);

/**
 * Soil price multiplier charged for building something at a level of a tile,
 * to be kept in the slice (paid_soil) so refunds never exceed what was paid,
 * whatever happens to the land above later.
 * @param kind  What is built.
 * @param tile  Tile column.
 * @param level Level of the construction; for a ramp its lower level.
 * @return The multiplier in tenths.
 */
uint16_t GetUndergroundPaidSoil(UndergroundCostKind kind, TileIndex tile, int16_t level);

/**
 * Money returned when something underground is removed: underground.refund_percent
 * of the build price (with or without the soil, by underground.refund_soil).
 * The soil is the cheaper of the soil now and the soil paid for, so neither
 * raising the land above nor old prices make removal pay more than building.
 * @param kind      What is removed.
 * @param railtype  Its rail type (unused for signals).
 * @param tile      Tile column.
 * @param level     Level of the construction; for a ramp its lower level.
 * @param paid_soil Soil multiplier paid (the slice's paid_soil, tenths); 0 when not known (signals).
 * @return The refund, never negative; the command charges it as a negative cost.
 */
Money GetUndergroundRemoveRefund(UndergroundCostKind kind, RailType railtype, TileIndex tile, int16_t level, uint16_t paid_soil = 0);

#endif /* UNDERGROUND_COST_H */
