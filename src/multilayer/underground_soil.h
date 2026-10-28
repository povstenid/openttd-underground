/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_soil.h The soil around underground construction, by the depth below the surface. */

#ifndef UNDERGROUND_SOIL_H
#define UNDERGROUND_SOIL_H

#include "../tile_type.h"
#include "../strings_type.h"

/**
 * Soil layers by the depth below the surface of the tile (metro v2 spec 2.1).
 * Depth 1 is Earth, depth 10 GraniteBlack, depth 11 and deeper Obsidian.
 * Below water (river, canal, sea) the earth and clay give way to sand:
 * depths 1..3 SandLight, Sand, SandDark, then dark clay and the rock as on land.
 * The sand layers come last so the numbers of the others keep their meaning.
 * The soil is not saved: it follows from the height of the land and the level.
 */
enum class SoilLayer : uint8_t {
	Earth,        ///< Depth 1: black earth with roots.
	ClayLight,    ///< Depth 2: light (ochre) clay.
	Clay,         ///< Depth 3: clay.
	ClayDark,     ///< Depth 4: dark clay.
	MarbleLight,  ///< Depth 5: light marble.
	MarbleGrey,   ///< Depth 6: grey marble.
	GranitePink,  ///< Depth 7: pink granite.
	GraniteGrey,  ///< Depth 8: grey granite.
	GraniteDark,  ///< Depth 9: dark granite.
	GraniteBlack, ///< Depth 10: black granite.
	Obsidian,     ///< Depth 11 and deeper: obsidian.
	SandLight,    ///< Depth 1 below water: light wet sand.
	Sand,         ///< Depth 2 below water: sand.
	SandDark,     ///< Depth 3 below water: dark sand.
	End,          ///< End marker.
};

/** Number of soil layers. */
static constexpr uint SOIL_LAYER_COUNT = static_cast<uint>(SoilLayer::End);
/** Number of soil layers of dry land, by depth: Earth .. Obsidian. */
static constexpr uint SOIL_LAND_LAYER_COUNT = static_cast<uint>(SoilLayer::Obsidian) + 1;
/** Number of sand layers below water: SandLight .. SandDark. */
static constexpr uint SOIL_SAND_LAYER_COUNT = SOIL_LAYER_COUNT - SOIL_LAND_LAYER_COUNT;

/**
 * Whether a soil layer is one of the sand layers below water.
 * @param layer The layer.
 * @return True for SandLight, Sand and SandDark.
 */
inline bool IsSandSoilLayer(SoilLayer layer)
{
	return layer >= SoilLayer::SandLight && layer < SoilLayer::End;
}

/**
 * Depth of a level below the surface of a tile: GetMetroSurfaceZ(tile) - level.
 * At least 1 for every valid underground level; 0 for the mouth of a hillside portal.
 * @param tile  Tile column.
 * @param level Absolute level (z of the floor of the slice).
 * @return Depth below the surface, in levels.
 */
int GetDepthBelowSurface(TileIndex tile, int16_t level);

/**
 * Soil at a depth below the surface.
 * @param depth Depth below the surface; depths below 1 count as 1.
 * @param under_water The surface is water (river, canal, sea): sand instead of earth and clay.
 * @return The soil layer.
 */
SoilLayer GetSoilLayerAtDepth(int depth, bool under_water = false);

/**
 * Soil of a tile at a level.
 * @param tile  Tile column.
 * @param level Absolute level.
 * @return The soil layer at the depth of the level below the surface of the tile.
 */
SoilLayer GetSoilLayer(TileIndex tile, int16_t level);

/**
 * Name of a soil layer.
 * @param layer The layer.
 * @return Its name, e.g. "Dark clay".
 */
StringID GetSoilLayerName(SoilLayer layer);

#endif /* UNDERGROUND_SOIL_H */
