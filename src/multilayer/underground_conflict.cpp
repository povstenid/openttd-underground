/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_conflict.cpp Conflict checks between the underground network, terrain and vanilla tunnels. */

#include "../stdafx.h"
#include "underground_conflict.h"
#include "multilayer_map.h"
#include "../map_func.h"
#include "../tile_map.h"
#include "../tunnel_map.h"

#include "../safeguards.h"

bool HasUndergroundBelowHeight(TileIndex tile, int new_tile_height)
{
	if (!_multilayer_map.IsAllocated() || !IsValidTile(tile)) return false;

	for (SliceID sid : _multilayer_map.GetColumn(tile).slices) {
		const TileSlice &slice = _multilayer_map.GetSlice(sid);
		/* The Surface slice is a sentinel with a fake span; never a physical conflict. */
		if (slice.kind == SliceKind::Surface || slice.kind == SliceKind::End) continue;
		if (slice.span.z_top > new_tile_height) return true;
	}
	return false;
}

bool HasUndergroundSliceAtZ(TileIndex tile, int z)
{
	if (!_multilayer_map.IsAllocated() || !IsValidTile(tile)) return false;
	if (z < INT16_MIN || z > INT16_MAX) return false;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(z));
	if (sid == INVALID_SLICE_ID || !_multilayer_map.IsValidSlice(sid)) return false;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	return slice.kind != SliceKind::Surface && slice.kind != SliceKind::End;
}

bool HasUndergroundPortal(TileIndex tile)
{
	if (!_multilayer_map.IsAllocated() || !IsValidTile(tile)) return false;
	return _multilayer_map.FindSliceByKind(tile, SliceKind::Portal) != INVALID_SLICE_ID;
}

bool IsVanillaTunnelInWayOfUnderground(TileIndex tile, int16_t z)
{
	if (!IsValidTile(tile)) return false;

	/* Vanilla tunnels are built at GetTileZ() of their entrances, which is a
	 * TileHeight-unit value (GetTilePixelZ() is the world-unit variant).
	 * Metro depths use the same unit, so no conversion is needed.
	 * Tunnels can never be below sea level; IsTunnelInWay would walk to the
	 * map border for negative z without finding anything. */
	if (z < 0) return false;

	/* The tile itself may be a tunnel entrance at this level. */
	if (IsTunnelTile(tile) && GetTileZ(tile) == z) return true;

	return IsTunnelInWay(tile, z);
}
