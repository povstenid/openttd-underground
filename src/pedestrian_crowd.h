/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file pedestrian_crowd.h Purely visual crowds of passengers waiting at stations.
 *
 * The crowds follow the real number of passengers waiting at a station but
 * never touch the game state: they live only on the client, are not saved and
 * use deterministic hashes for everything they draw.
 */

#ifndef PEDESTRIAN_CROWD_H
#define PEDESTRIAN_CROWD_H

struct Station;
struct TileInfo;

void DrawPedestrianCrowd(const TileInfo *ti);
bool FindPedestrianCrowdTarget(const Station *st, int hx, int hy, bool reserve, int &tx, int &ty);
void ResetPedestrianCrowds();

#endif /* PEDESTRIAN_CROWD_H */
