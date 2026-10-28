/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_conflict.h Conflict checks between the underground network, terrain and vanilla tunnels. */

#ifndef UNDERGROUND_CONFLICT_H
#define UNDERGROUND_CONFLICT_H

#include "../tile_type.h"

#include <cstdint>

/**
 * Would lowering the surface of a column to the given height expose underground slices?
 * @param tile The tile column (its TileHeight is the north corner height).
 * @param new_tile_height The new TileHeight, in TileHeight units.
 * @return True if any non-Surface slice has span.z_top > new_tile_height,
 *         i.e. would no longer lie strictly below the surface.
 */
bool HasUndergroundBelowHeight(TileIndex tile, int new_tile_height);

/**
 * Is there an underground (non-Surface) slice occupying the given z in a column?
 * @param tile The tile column.
 * @param z    Z level in TileHeight units.
 * @return True if a non-Surface slice contains z.
 */
bool HasUndergroundSliceAtZ(TileIndex tile, int z);

/**
 * Does the column contain a Portal slice (at any depth)?
 * @param tile The tile column.
 * @return True if a Portal slice exists in the column.
 */
bool HasUndergroundPortal(TileIndex tile);

/**
 * Is a vanilla tunnel passing through (or ending at) the given tile at the given z?
 * Both the tunnel z (GetTileZ) and the metro depth are expressed in TileHeight units.
 * @param tile The tile column.
 * @param z    Underground depth / z level in TileHeight units.
 * @return True if a vanilla tunnel occupies z at this tile.
 */
bool IsVanillaTunnelInWayOfUnderground(TileIndex tile, int16_t z);

#endif /* UNDERGROUND_CONFLICT_H */
