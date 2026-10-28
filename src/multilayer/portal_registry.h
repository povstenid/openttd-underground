/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file portal_registry.h Precomputed portal-to-portal connections for YAPF integration. */

#ifndef PORTAL_REGISTRY_H
#define PORTAL_REGISTRY_H

#include "../tile_type.h"
#include "../direction_type.h"
#include <vector>

/**
 * A precomputed connection between two underground portals.
 * Used by YAPF FollowTileExit to "teleport" through underground,
 * exactly like vanilla tunnels teleport through wormholes.
 */
struct PortalConnection {
	TileIndex entry_tile;     ///< Entry portal tile.
	TileIndex exit_tile;      ///< Exit portal tile.
	int path_length;          ///< Number of underground tiles between portals.
	DiagDirection entry_dir;  ///< Direction entering the portal.
	DiagDirection exit_dir;   ///< Direction exiting at the other end.
	int16_t depth;            ///< Underground depth.
};

/**
 * Find all portal connections reachable from a given entry portal tile.
 * @param entry_tile The portal tile to query.
 * @return Vector of connections (may be empty if no exit portals reachable).
 */
const std::vector<PortalConnection> &GetPortalConnections(TileIndex entry_tile);

/**
 * Recompute all portal connections.
 * Called after building/removing underground tracks or portals.
 * Uses BFS through underground Track slices to find connected portal pairs.
 */
void RecomputePortalConnections();

/**
 * While one exists, RecomputePortalConnections() only notes that the
 * connections are stale; the last one to go recomputes them once. For
 * commands that build many pieces, such as pasting a blueprint.
 */
class PortalConnectionsBatch {
public:
	PortalConnectionsBatch();
	~PortalConnectionsBatch();
	PortalConnectionsBatch(const PortalConnectionsBatch &) = delete;
	PortalConnectionsBatch &operator=(const PortalConnectionsBatch &) = delete;
};

/**
 * Clear all cached portal connections.
 */
void ClearPortalConnections();

#endif /* PORTAL_REGISTRY_H */
