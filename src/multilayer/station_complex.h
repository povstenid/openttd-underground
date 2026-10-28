/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_complex.h StationComplex: underground stations with surface exits. */

#ifndef STATION_COMPLEX_H
#define STATION_COMPLEX_H

#include "multilayer_types.h"
#include <vector>

/**
 * Role of a node within a StationComplex.
 */
enum class StationRole : uint8_t {
	Platform,    ///< Underground platform where trains stop.
	ExitSurface, ///< Surface exit providing passenger catchment.
	PortalTrack, ///< Track portal connecting to the station.
};

/**
 * Kind of a surface exit (StationRole::ExitSurface node).
 *
 * Exits are only ever built by the player; a station with platforms but no
 * exit has no passenger catchment. The numeric values are stored in savegames
 * and must stay stable.
 */
enum class StationExitType : uint8_t {
	Pavilion  = 0, ///< Entrance building: a vanilla bus station tile of the same station on the surface.
	Underpass = 1, ///< Stairs on a straight road tile; the road stays usable, nothing is built.
};

/** Check whether an exit type is a valid, player buildable exit. */
inline bool IsExplicitStationExit(StationExitType type)
{
	return type == StationExitType::Pavilion || type == StationExitType::Underpass;
}

/**
 * A node in a StationComplex — either a platform, surface exit, or portal.
 */
struct StationComplexNode {
	TileRef ref;          ///< Location in the multi-layer map.
	StationRole role;     ///< What this node does.
	uint16_t station_id;  ///< Index of the parent Station in the station pool.
	int16_t depth;        ///< Z level of this node.
	StationExitType exit_type = StationExitType::Pavilion; ///< Kind of exit (only meaningful for StationRole::ExitSurface).

	/** Is this a surface exit the player built (pavilion or underpass)? */
	bool IsExplicitExit() const { return this->role == StationRole::ExitSurface && IsExplicitStationExit(this->exit_type); }
};

/**
 * A StationComplex extends a normal Station with multi-level structure.
 *
 * It wraps an existing Station (by station_id) and adds:
 * - Underground platforms (trains stop here)
 * - Surface exits (provide catchment area for passengers)
 * - Portal tracks (connect surface rail to underground platforms)
 *
 * Passengers are generated at the station only if at least one
 * ExitSurface node exists. The catchment area is the union of
 * all ExitSurface tiles' individual catchments.
 *
 * From the link graph's perspective, a StationComplex is just a
 * normal station — no new cargo distribution logic needed.
 */
struct StationComplex {
	uint16_t station_id = 0;                 ///< Station pool index.
	std::vector<StationComplexNode> nodes;   ///< All nodes in this complex.

	/** Check if this complex has any surface exits (for catchment). */
	bool HasSurfaceExit() const
	{
		for (const auto &n : this->nodes) {
			if (n.role == StationRole::ExitSurface) return true;
		}
		return false;
	}

	/** Get all platform nodes. */
	std::vector<const StationComplexNode *> GetPlatforms() const
	{
		std::vector<const StationComplexNode *> result;
		for (const auto &n : this->nodes) {
			if (n.role == StationRole::Platform) result.push_back(&n);
		}
		return result;
	}

	/** Get all surface exit nodes. */
	std::vector<const StationComplexNode *> GetExits() const
	{
		std::vector<const StationComplexNode *> result;
		for (const auto &n : this->nodes) {
			if (n.role == StationRole::ExitSurface) result.push_back(&n);
		}
		return result;
	}

	/** Get the minimum depth of any platform. */
	int16_t GetDeepestLevel() const
	{
		int16_t deepest = INT16_MAX;
		for (const auto &n : this->nodes) {
			if (n.role == StationRole::Platform && n.depth < deepest) {
				deepest = n.depth;
			}
		}
		return deepest == INT16_MAX ? 0 : deepest;
	}
};

/**
 * Global registry of StationComplex instances.
 * Keyed by station_id. Only stations with underground components
 * have entries here; normal surface-only stations are not tracked.
 */
#include <unordered_map>

extern std::unordered_map<uint16_t, StationComplex> _station_complexes;

/** Find or create a StationComplex for a given station ID. */
StationComplex &GetOrCreateStationComplex(uint16_t station_id);

/** Remove a StationComplex. */
void RemoveStationComplex(uint16_t station_id);

/** Check if routing to this station must target a metro portal (no surface rail tiles). */
bool StationUsesPortalRouting(uint16_t station_id);

/** Re-resolve station complex node references after loading a savegame. */
void ResolveStationComplexesAfterLoad();

/** Catchment radius a surface exit of the given type provides. */
uint GetStationExitCatchmentRadius(StationExitType type);

/**
 * Find the explicit (player built) surface exit on a tile.
 * @param tile Surface tile.
 * @return The exit node, or nullptr when the tile has no pavilion or underpass.
 */
const StationComplexNode *FindExplicitStationExitAt(TileIndex tile);

/** Check whether a tile is the surface building (bus station tile) of a pavilion exit. */
bool IsStationExitPavilionTile(TileIndex tile);

/**
 * Drop the explicit exit node on a tile, e.g. when its pavilion tile was
 * removed by bankruptcy or flooding. Does not touch the surface tile.
 * @param tile Surface tile of the exit.
 * @return Station id the exit belonged to, or UINT16_MAX when there was none.
 */
uint16_t ForgetExplicitStationExitAt(TileIndex tile);

/** Mark the per-tile lookup of explicit exits stale; call after changing exit nodes. */
void InvalidateStationExitCache();

#endif /* STATION_COMPLEX_H */
