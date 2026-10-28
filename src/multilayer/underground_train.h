/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_train.h Underground train movement support. */

#ifndef UNDERGROUND_TRAIN_H
#define UNDERGROUND_TRAIN_H

#include "multilayer_types.h"
#include "../track_type.h"
#include "../direction_type.h"
#include "../vehicle_type.h"

struct Train;

/**
 * Per-train underground state.
 * Stored externally (not in Train struct, to minimize core changes).
 */
struct UndergroundTrainState {
	bool is_underground = false;  ///< True if train is currently underground.
	TileRef current_ref;          ///< Current position in multi-layer map.
	int16_t depth = 0;            ///< Current underground depth.
	TileIndex entry_reserved_portal = INVALID_TILE; ///< Portal through which the train holds an underground path reserved before entering it.
	uint64_t retry_after_tick = 0; ///< Tick from which a reservation that just failed may be tried again (path_backoff_interval).
};

/**
 * Get the underground state for a train.
 * Returns nullptr if the train has no underground state.
 */
UndergroundTrainState *GetUndergroundState(const Train *v);

/**
 * Create or get underground state for a train entering underground.
 */
UndergroundTrainState &GetOrCreateUndergroundState(const Train *v);

/**
 * Remove underground state when train returns to surface.
 */
void ClearUndergroundState(const Train *v);

/**
 * Check if a tile has a portal that a train can enter.
 * @param tile The surface tile.
 * @param enterdir Direction the train is entering from.
 * @return True if there's an enterable portal.
 */
bool HasEnterablePortal(TileIndex tile, DiagDirection enterdir);

/**
 * The direction a train moves in when it dives into a portal (the tunnel
 * side of the mouth). Normally the portal's recorded direction; when the
 * underground track evidently lies on the other side (a portal built with
 * the direction the wrong way round), that side.
 * @param tile The portal tile.
 * @param ps The portal slice.
 * @return The dive direction.
 */
DiagDirection GetPortalDiveDirection(TileIndex tile, const TileSlice &ps);

/*
 * Exit claims. A train below ground is not seen by the surface signal blocks.
 * A train that has reserved its way up to an exit portal claims the mouth:
 * from then on the surface signalling treats the mouth as occupied (like a
 * train in a vanilla tunnel is part of the block in front of it), so block
 * signals in front of it show red and trains do not leave a depot into it.
 * Claims are game state (they decide signal aspects) and are saved.
 */

/** Claim the mouth of an exit portal for a train coming out of it. */
void ClaimPortalExit(TileIndex portal_tile, VehicleID train);

/** Give up the claim on one portal mouth (the train came out there). */
void ReleasePortalExit(TileIndex portal_tile);

/** The train that claimed the mouth on this tile, VehicleID::Invalid() for none. */
VehicleID GetPortalExitClaimant(TileIndex portal_tile);

/**
 * Give up every claim of a train (it turned round, crashed or is gone).
 * @return The portal tiles whose claim was given up.
 */
std::vector<TileIndex> ReleasePortalExits(VehicleID train);

/**
 * Whether the mouth on this tile is claimed by a train below ground.
 * @param tile The tile.
 * @param train Optional train whose own claim does not count.
 */
bool IsPortalExitClaimed(TileIndex tile, VehicleID train = VehicleID::Invalid());

/** Forget all per-train underground state and exit claims (new game, loading a game). */
void ClearUndergroundTrainRuntimeState();

/**
 * All per-train underground states, ordered by vehicle, for saving.
 * @return Pairs of vehicle and its state.
 */
std::vector<std::pair<VehicleID, UndergroundTrainState>> GetUndergroundTrainStates();

/**
 * Set the per-train underground state of a vehicle, when loading.
 * @param train The vehicle.
 * @param state Its state.
 */
void SetUndergroundTrainState(VehicleID train, const UndergroundTrainState &state);

/**
 * All exit claims, ordered by portal tile, for saving.
 * @return Pairs of portal tile and the train holding its mouth.
 */
std::vector<std::pair<TileIndex, VehicleID>> GetPortalExitClaims();

/**
 * Get the underground track bits available at a given tile and depth.
 * This is the underground equivalent of GetTileTrackStatus.
 * @param tile Surface tile position.
 * @param depth Underground depth.
 * @param enterdir Direction entering the tile.
 * @return Available TrackBits.
 */
TrackBits GetUndergroundTrackBits(TileIndex tile, int16_t depth, DiagDirection enterdir);

/**
 * Get the length of an underground station platform.
 * @param tile Starting tile.
 * @param depth Underground depth.
 * @param station_id Station to match.
 * @param dir Direction to walk along the platform.
 * @return Platform length in tiles.
 */
int GetUndergroundPlatformLength(TileIndex tile, int16_t depth, uint16_t station_id, DiagDirection dir);

#endif /* UNDERGROUND_TRAIN_H */
