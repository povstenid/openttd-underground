/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_cost.cpp Prices of underground construction (metro v2 spec 2.3). */

#include "../stdafx.h"
#include "underground_cost.h"
#include "underground_cmd.h"
#include "../economy_func.h"
#include "../rail.h"
#include "../settings_type.h"

#include "../safeguards.h"

/** Settings of the tunnel soil multipliers, by layer; the three sand layers share one. */
static constexpr uint16_t UndergroundSettings::*TUNNEL_SOIL_COST[] = {
	&UndergroundSettings::tunnel_cost_earth,
	&UndergroundSettings::tunnel_cost_clay_light,
	&UndergroundSettings::tunnel_cost_clay,
	&UndergroundSettings::tunnel_cost_clay_dark,
	&UndergroundSettings::tunnel_cost_marble_light,
	&UndergroundSettings::tunnel_cost_marble_grey,
	&UndergroundSettings::tunnel_cost_granite_pink,
	&UndergroundSettings::tunnel_cost_granite_grey,
	&UndergroundSettings::tunnel_cost_granite_dark,
	&UndergroundSettings::tunnel_cost_granite_black,
	&UndergroundSettings::tunnel_cost_obsidian,
	&UndergroundSettings::tunnel_cost_sand, // SandLight
	&UndergroundSettings::tunnel_cost_sand, // Sand
	&UndergroundSettings::tunnel_cost_sand, // SandDark
};
static_assert(std::size(TUNNEL_SOIL_COST) == SOIL_LAYER_COUNT);

/** Settings of the station soil multipliers, by layer; the three sand layers share one. */
static constexpr uint16_t UndergroundSettings::*STATION_SOIL_COST[] = {
	&UndergroundSettings::station_cost_earth,
	&UndergroundSettings::station_cost_clay_light,
	&UndergroundSettings::station_cost_clay,
	&UndergroundSettings::station_cost_clay_dark,
	&UndergroundSettings::station_cost_marble_light,
	&UndergroundSettings::station_cost_marble_grey,
	&UndergroundSettings::station_cost_granite_pink,
	&UndergroundSettings::station_cost_granite_grey,
	&UndergroundSettings::station_cost_granite_dark,
	&UndergroundSettings::station_cost_granite_black,
	&UndergroundSettings::station_cost_obsidian,
	&UndergroundSettings::station_cost_sand, // SandLight
	&UndergroundSettings::station_cost_sand, // Sand
	&UndergroundSettings::station_cost_sand, // SandDark
};
static_assert(std::size(STATION_SOIL_COST) == SOIL_LAYER_COUNT);

/** A multiplier of x1 in tenths. */
static constexpr uint MULTIPLIER_ONE = 10;

uint GetUndergroundSoilMultiplier(UndergroundCostKind kind, SoilLayer layer)
{
	if (layer >= SoilLayer::End) layer = SoilLayer::Obsidian;
	const UndergroundSettings &s = _settings_game.underground;
	switch (kind) {
		case UndergroundCostKind::Signal: return MULTIPLIER_ONE;
		case UndergroundCostKind::Station: return s.*STATION_SOIL_COST[static_cast<uint>(layer)];
		default: return s.*TUNNEL_SOIL_COST[static_cast<uint>(layer)];
	}
}

SoilLayer GetUndergroundCostSoil(UndergroundCostKind kind, TileIndex tile, int16_t level)
{
	if (kind == UndergroundCostKind::Portal) return SoilLayer::Earth;
	return GetSoilLayer(tile, level);
}

/**
 * Surface price of what is built, times the base and kind multipliers, in tenths.
 * @param kind     What is built.
 * @param railtype Its rail type.
 * @return The price before the soil, times ten.
 */
static Money GetUndergroundPriceBeforeSoil(UndergroundCostKind kind, RailType railtype)
{
	if (railtype >= RAILTYPE_END) railtype = RAILTYPE_RAIL;
	const UndergroundSettings &s = _settings_game.underground;
	Money surface;
	uint kind_multiplier = MULTIPLIER_ONE;
	switch (kind) {
		case UndergroundCostKind::Track:
			surface = RailBuildCost(railtype);
			break;
		case UndergroundCostKind::Station:
			/* A tile of a plain surface rail station, as CmdBuildRailStation charges it. */
			surface = _price[Price::BuildStationRail] + RailBuildCost(railtype);
			break;
		case UndergroundCostKind::Ramp:
			surface = RailBuildCost(railtype);
			kind_multiplier = s.ramp_cost_multiplier;
			break;
		case UndergroundCostKind::Portal:
			surface = RailBuildCost(railtype);
			kind_multiplier = s.portal_cost_multiplier;
			break;
		case UndergroundCostKind::Signal:
			surface = _price[Price::BuildSignals];
			break;
		default: NOT_REACHED();
	}
	return surface * s.base_cost_multiplier * kind_multiplier;
}

/**
 * Price with a soil multiplier.
 * @param before_soil Price before the soil, times ten.
 * @param soil        Soil multiplier in tenths.
 * @return The price.
 */
static Money ApplyUndergroundSoil(Money before_soil, uint soil)
{
	Money cost = before_soil * soil / (MULTIPLIER_ONE * MULTIPLIER_ONE);
	return std::max<Money>(cost, 0);
}

Money GetUndergroundBuildCost(UndergroundCostKind kind, RailType railtype, TileIndex tile, int16_t level)
{
	uint soil = GetUndergroundSoilMultiplier(kind, GetUndergroundCostSoil(kind, tile, level));
	return ApplyUndergroundSoil(GetUndergroundPriceBeforeSoil(kind, railtype), soil);
}

uint16_t GetUndergroundPaidSoil(UndergroundCostKind kind, TileIndex tile, int16_t level)
{
	return static_cast<uint16_t>(GetUndergroundSoilMultiplier(kind, GetUndergroundCostSoil(kind, tile, level)));
}

Money GetUndergroundRemoveRefund(UndergroundCostKind kind, RailType railtype, TileIndex tile, int16_t level, uint16_t paid_soil)
{
	const UndergroundSettings &s = _settings_game.underground;
	uint soil = GetUndergroundSoilMultiplier(kind, GetUndergroundCostSoil(kind, tile, level));
	if (paid_soil != 0) soil = std::min<uint>(soil, paid_soil);
	Money before_soil = GetUndergroundPriceBeforeSoil(kind, railtype);
	Money build = ApplyUndergroundSoil(before_soil, soil);
	Money base = s.refund_soil ? build : ApplyUndergroundSoil(before_soil, std::min(soil, MULTIPLIER_ONE));
	Money refund = base * std::min<uint>(s.refund_percent, 100) / 100;
	return std::clamp<Money>(refund, 0, build);
}
