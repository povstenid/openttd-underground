/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_soil.cpp The soil around underground construction, by the depth below the surface. */

#include "../stdafx.h"
#include "underground_soil.h"
#include "underground_cmd.h"
#include "underground_rules.h"
#include "../table/strings.h"

#include "../safeguards.h"

int GetDepthBelowSurface(TileIndex tile, int16_t level)
{
	return GetMetroSurfaceZ(tile) - level;
}

SoilLayer GetSoilLayerAtDepth(int depth, bool under_water)
{
	if (depth < 1) depth = 1;
	if (under_water && depth <= static_cast<int>(SOIL_SAND_LAYER_COUNT)) {
		return static_cast<SoilLayer>(to_underlying(SoilLayer::SandLight) + depth - 1);
	}
	if (depth > static_cast<int>(SOIL_LAND_LAYER_COUNT)) depth = SOIL_LAND_LAYER_COUNT;
	return static_cast<SoilLayer>(depth - 1);
}

SoilLayer GetSoilLayer(TileIndex tile, int16_t level)
{
	return GetSoilLayerAtDepth(GetDepthBelowSurface(tile, level), IsUndergroundWaterTile(tile));
}

StringID GetSoilLayerName(SoilLayer layer)
{
	static const StringID names[] = {
		STR_UNDERGROUND_SOIL_EARTH,
		STR_UNDERGROUND_SOIL_CLAY_LIGHT,
		STR_UNDERGROUND_SOIL_CLAY,
		STR_UNDERGROUND_SOIL_CLAY_DARK,
		STR_UNDERGROUND_SOIL_MARBLE_LIGHT,
		STR_UNDERGROUND_SOIL_MARBLE_GREY,
		STR_UNDERGROUND_SOIL_GRANITE_PINK,
		STR_UNDERGROUND_SOIL_GRANITE_GREY,
		STR_UNDERGROUND_SOIL_GRANITE_DARK,
		STR_UNDERGROUND_SOIL_GRANITE_BLACK,
		STR_UNDERGROUND_SOIL_OBSIDIAN,
		STR_UNDERGROUND_SOIL_SAND_LIGHT,
		STR_UNDERGROUND_SOIL_SAND,
		STR_UNDERGROUND_SOIL_SAND_DARK,
	};
	static_assert(std::size(names) == SOIL_LAYER_COUNT);
	if (layer >= SoilLayer::End) return STR_EMPTY;
	return names[static_cast<uint>(layer)];
}
