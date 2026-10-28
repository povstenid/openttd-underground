/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file pedestrian_arrivals.h Purely visual crowds leaving vehicles that delivered passengers.
 *
 * Like the rest of the pedestrians this never touches the game state.
 */

#ifndef PEDESTRIAN_ARRIVALS_H
#define PEDESTRIAN_ARRIVALS_H

struct Station;
struct Vehicle;

/** Counters of the arrival crowds since the game was started or loaded. */
struct PedestrianArrivalStats {
	uint64_t passengers_delivered = 0; ///< Passengers delivered by vehicles while pedestrians were shown.
	uint64_t passengers_offscreen = 0; ///< Of those, passengers delivered too far from every viewport to show.
	uint64_t no_house = 0;             ///< Of those, passengers at stations without a passenger accepting house.
	uint64_t spawned = 0;              ///< Pedestrians sent walking to houses.
};

void OnPedestrianPassengersDelivered(const Station *st, const Vehicle *part, uint amount);
void ResetPedestrianArrivals();
const PedestrianArrivalStats &GetPedestrianArrivalStats();

#endif /* PEDESTRIAN_ARRIVALS_H */
