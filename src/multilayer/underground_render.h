/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_render.h Underground tile rendering declarations. */

#ifndef UNDERGROUND_RENDER_H
#define UNDERGROUND_RENDER_H

#include "../tile_type.h"
#include "../tile_cmd.h"

/** Check if a tile has underground content at any depth. */
bool HasUndergroundContent(TileIndex tile);

/** Check if a tile has a portal (visible on surface). */
bool HasPortalOnTile(TileIndex tile);

/** Move a surface signal on a dive portal tile to the surface end of the cut. */
bool PlaceDivePortalSignal(TileIndex tile, Axis axis, uint &x, uint &y);

/** Draw portal overlay on the surface tile (always visible). */
void DrawPortalOverlay(const TileInfo *ti);

/** Check if a tile carries an underpass exit of an underground station (drawn over the road). */
bool HasStationExitOverlay(TileIndex tile);

/** Draw the underpass exit marker over its road tile. */
void DrawStationExitOverlay(const TileInfo *ti);

/** Draw underground overlay for a tile (X-ray underground view mode). */
void DrawUndergroundTile(const TileInfo *ti);

/** Draw a tile column cut by the slice view: flat slab at the plane plus the viewed level's content. */
void DrawUndergroundSliceTile(const TileInfo *ti);

#endif /* UNDERGROUND_RENDER_H */
