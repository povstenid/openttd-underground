/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file pedestrian_path.h Road paths of the purely visual pedestrians.
 *
 * Distance fields over the road tiles around a goal tile, built lazily and
 * kept in a small cache. They only read the map and are never saved.
 */

#ifndef PEDESTRIAN_PATH_H
#define PEDESTRIAN_PATH_H

#include "direction_type.h"
#include "road_type.h"
#include "tile_type.h"

/** What a pedestrian on a road tile does next. */
enum class PedestrianStep : uint8_t {
	Move,   ///< Walk to the neighbouring tile in the returned direction.
	AtGoal, ///< The tile touches the goal, leave the road.
	NoPath, ///< The goal can't be reached along the roads from this tile.
};

static constexpr uint PEDESTRIAN_NO_PATH = UINT_MAX; ///< Distance of a tile from which the goal is not reachable.

RoadBits GetPedestrianRoadBits(TileIndex tile);
uint GetPedestrianPathDistance(TileIndex goal, TileIndex tile, uint8_t &hint);
PedestrianStep GetPedestrianStep(TileIndex goal, TileIndex tile, uint8_t &hint, DiagDirection &dir);
void PedestrianPathsTick();
void ResetPedestrianPaths();

#endif /* PEDESTRIAN_PATH_H */
