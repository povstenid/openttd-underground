/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file pedestrians.h Purely visual pedestrians walking between houses and stations.
 *
 * The pedestrians mirror real passenger generation but never touch the game
 * state: they are not saved, use only the interactive random generator and are
 * local to every client.
 */

#ifndef PEDESTRIANS_H
#define PEDESTRIANS_H

#include "cargo_type.h"
#include "tile_type.h"

struct DrawPixelInfo;
struct Station;

/** Counters of the pedestrian system since the game was started or loaded. */
struct PedestrianStats {
	uint64_t passengers_seen = 0;     ///< Passengers houses handed to stations while pedestrians were shown.
	uint64_t passengers_offscreen = 0; ///< Of those, passengers generated too far from every viewport to show.
	uint64_t spawned = 0;             ///< Pedestrians created.
	uint64_t arrived = 0;             ///< Pedestrians that reached their goal.
	uint64_t dropped = 0;             ///< Pedestrians not created because the limit was reached.
};

/** Where a pedestrian is going, and what happens when it gets there. */
enum class PedestrianGoal : uint8_t {
	EnterStation, ///< Walks from a house to a station and disappears into it.
	EnterHouse,   ///< Walks from a station to a house and disappears into it.
};

/** While alive, passengers handed to stations are taken to come from this house tile. */
class PedestrianSourceScope {
public:
	explicit PedestrianSourceScope(TileIndex house);
	~PedestrianSourceScope();
};

void OnPedestrianPassengersGenerated(const Station *st, CargoType cargo, uint amount);
bool IsNearPedestrianViewport(int x, int y);
bool FindPedestrianTarget(const Station *st, int x, int y, int &tx, int &ty);
bool SpawnPedestrian(int from_x, int from_y, int to_x, int to_y, PedestrianGoal goal);
void PedestriansTick();
void ResetPedestrians();
void ViewportAddPedestrians(DrawPixelInfo *dpi);

size_t GetPedestrianCount();
const PedestrianStats &GetPedestrianStats();

#endif /* PEDESTRIANS_H */
