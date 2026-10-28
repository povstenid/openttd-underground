/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pbs.cpp PBS support routines. */

#include "stdafx.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "train.h"
#include "newgrf_station.h"
#include "pathfinder/follow_track.hpp"
#include "multilayer/multilayer_map.h"
#include "multilayer/underground_train.h"
#include "multilayer/underground_signal.h"
#include "map_func.h"
#include "signal_func.h"
#include "track_func.h"
#include "direction_func.h"

#include <unordered_map>
#include <unordered_set>

#include "safeguards.h"

/**
 * Get the reserved trackbits for any tile, regardless of type.
 * @param t the tile
 * @return the reserved trackbits. TRACK_BIT_NONE on nothing reserved or
 *     a tile without rail.
 */
TrackBits GetReservedTrackbits(TileIndex t)
{
	switch (GetTileType(t)) {
		case TileType::Railway:
			if (IsRailDepot(t)) return GetDepotReservationTrackBits(t);
			if (IsPlainRail(t)) return GetRailReservationTrackBits(t);
			break;

		case TileType::Road:
			if (IsLevelCrossing(t)) return GetCrossingReservationTrackBits(t);
			break;

		case TileType::Station:
			if (HasStationRail(t)) return GetStationReservationTrackBits(t);
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) return GetTunnelBridgeReservationTrackBits(t);
			break;

		default:
			break;
	}
	return TRACK_BIT_NONE;
}

/**
 * Set the reservation for a complete station platform.
 * @pre IsRailStationTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert(IsRailStationTile(start));
	assert(GetRailStationAxis(start) == DiagDirToAxis(dir));

	do {
		SetRailStationReservation(tile, b);
		MarkTileDirtyByTile(tile);
		tile = TileAdd(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}

/**
 * Try to reserve a specific track on a tile
 * @param tile the tile
 * @param t the track
 * @param trigger_stations whether to call station randomisation trigger
 * @return \c true if reservation was successful, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 */
bool TryReserveRailTrack(TileIndex tile, Track t, bool trigger_stations)
{
	assert(HasTrack(TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)), t));

	if (_settings_client.gui.show_track_reservation) {
		/* show the reserved rail if needed */
		if (IsBridgeTile(tile)) {
			MarkBridgeDirty(tile);
		} else {
			MarkTileDirtyByTile(tile);
		}
	}

	switch (GetTileType(tile)) {
		case TileType::Railway:
			if (IsPlainRail(tile)) return TryReserveTrack(tile, t);
			if (IsRailDepot(tile)) {
				if (!HasDepotReservation(tile)) {
					SetDepotReservation(tile, true);
					MarkTileDirtyByTile(tile); // some GRFs change their appearance when tile is reserved
					return true;
				}
			}
			break;

		case TileType::Road:
			if (IsLevelCrossing(tile) && !HasCrossingReservation(tile)) {
				SetCrossingReservation(tile, true);
				UpdateLevelCrossing(tile, false);
				return true;
			}
			break;

		case TileType::Station:
			if (HasStationRail(tile) && !HasStationReservation(tile)) {
				SetRailStationReservation(tile, true);
				if (trigger_stations) {
					auto *st = BaseStation::GetByTile(tile);
					TriggerStationRandomisation(st, tile, StationRandomTrigger::PathReservation);
					TriggerStationAnimation(st, tile, StationAnimationTrigger::PathReservation);
				}
				MarkTileDirtyByTile(tile); // some GRFs need redraw after reserving track
				return true;
			}
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && !GetTunnelBridgeReservationTrackBits(tile)) {
				SetTunnelBridgeReservation(tile, true);
				return true;
			}
			break;

		default:
			break;
	}
	return false;
}

/**
 * Lift the reservation of a specific track on a tile
 * @param tile the tile
 * @param t the track
 */
void UnreserveRailTrack(TileIndex tile, Track t)
{
	assert(HasTrack(TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)), t));

	if (_settings_client.gui.show_track_reservation) {
		if (IsBridgeTile(tile)) {
			MarkBridgeDirty(tile);
		} else {
			MarkTileDirtyByTile(tile);
		}
	}

	switch (GetTileType(tile)) {
		case TileType::Railway:
			if (IsRailDepot(tile)) {
				SetDepotReservation(tile, false);
				MarkTileDirtyByTile(tile);
				break;
			}
			if (IsPlainRail(tile)) UnreserveTrack(tile, t);
			break;

		case TileType::Road:
			if (IsLevelCrossing(tile)) {
				SetCrossingReservation(tile, false);
				UpdateLevelCrossing(tile);
			}
			break;

		case TileType::Station:
			if (HasStationRail(tile)) {
				SetRailStationReservation(tile, false);
				MarkTileDirtyByTile(tile);
			}
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) SetTunnelBridgeReservation(tile, false);
			break;

		default:
			break;
	}
}


/**
 * Follow a reservation starting from a specific tile to the end.
 * @param o The owner of the track to follow; tracks of other owners are excluded.
 * @param rts The rail types to follow the reservation for; rail types not in the mask are excluded.
 * @param tile The start tile.
 * @param trackdir The start trackdir on the tile.
 * @param ignore_oneway Whether one way signals are to be ignored.
 * @return The end of the path.
 */
static PBSTileInfo FollowReservation(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, bool ignore_oneway = false)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return PBSTileInfo(tile, trackdir, false);

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	while (ft.Follow(tile, trackdir)) {
		/* The follower jumps through a metro tunnel to the tile beyond its far
		 * portal: a reservation there belongs to a train that came out, the
		 * surface path ends at the mouth. */
		if (ft.is_portal) break;
		TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) {
			if (ft.is_station) {
				/* Check skipped station tiles as well, maybe our reservation ends inside the station. */
				TileIndexDiff diff = TileOffsByDiagDir(ft.exitdir);
				while (ft.tiles_skipped-- > 0) {
					ft.new_tile -= diff;
					if (HasStationReservation(ft.new_tile)) {
						tile = ft.new_tile;
						trackdir = DiagDirToDiagTrackdir(ft.exitdir);
						break;
					}
				}
			}
			break;
		}

		/* Can't have more than one reserved trackdir */
		Trackdir new_trackdir = FindFirstTrackdir(reserved);

		/* One-way signal against us. The reservation can't be ours as it is not
		 * a safe position from our direction and we can never pass the signal. */
		if (!ignore_oneway && HasOnewaySignalBlockingTrackdir(ft.new_tile, new_trackdir)) break;

		tile = ft.new_tile;
		trackdir = new_trackdir;

		if (first_loop) {
			/* Update the start tile after we followed the track the first
			 * time. This is necessary because the track follower can skip
			 * tiles (in stations for example) which means that we might
			 * never visit our original starting tile again. */
			start_tile = tile;
			start_trackdir = trackdir;
			first_loop = false;
		} else {
			/* Loop encountered? */
			if (tile == start_tile && trackdir == start_trackdir) break;
		}
		/* Depot tile? Can't continue. */
		if (IsRailDepotTile(tile)) break;
		/* Non-pbs signal? Reservation can't continue. */
		if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
	}

	return PBSTileInfo(tile, trackdir, false);
}

/**
 * Helper struct for finding the best matching vehicle on a specific track.
 */
struct FindTrainOnTrackInfo {
	PBSTileInfo res; ///< Information about the track.
	Train *best;     ///< The currently "best" vehicle we have found.

	/** Init the best location to nullptr always! */
	FindTrainOnTrackInfo() : best(nullptr) {}
};

/**
 * Find the best matching vehicle on a tile.
 * @param[in,out] info State of the finding a vehicle on a reservation.
 * @param tile The tile to search for.
 */
static void CheckTrainsOnTrack(FindTrainOnTrackInfo &info, TileIndex tile)
{
	for (Vehicle *v : VehiclesOnTile(tile)) {
		if (v->type != VEH_TRAIN || v->vehstatus.Test(VehState::Crashed)) continue;
		/* A surface reservation can only belong to a train on the surface layer. */
		if (!IsVehicleOnSurfaceLayer(v)) continue;

		Train *t = Train::From(v);
		if (t->track == TRACK_BIT_WORMHOLE || HasBit(static_cast<TrackBits>(t->track), TrackdirToTrack(info.res.trackdir))) {
			t = t->First();

			/* ALWAYS return the lowest ID (anti-desync!) */
			if (info.best == nullptr || t->index < info.best->index) info.best = t;
		}
	}
}

/**
 * Follow a train reservation to the last tile.
 *
 * @param v the vehicle
 * @param train_on_res Is set to a train we might encounter
 * @returns The last tile of the reservation or the current train tile if no reservation present.
 */
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res)
{
	assert(v->type == VEH_TRAIN);

	/* Underground trains have no surface reservations. */
	if (v->IsUnderground()) {
		return PBSTileInfo(v->tile, TRACKDIR_BEGIN, false);
	}

	TileIndex tile = v->tile;
	Trackdir  trackdir = v->GetVehicleTrackdir();

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) return PBSTileInfo(tile, trackdir, false);

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(v->owner, GetAllCompatibleRailTypes(v->railtypes), tile, trackdir);
	ftoti.res.okay = IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg);
	if (train_on_res != nullptr) {
		CheckTrainsOnTrack(ftoti, ftoti.res.tile);
		if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		if (*train_on_res == nullptr && IsRailStationTile(ftoti.res.tile)) {
			/* The target tile is a rail station. The track follower
			 * has stopped on the last platform tile where we haven't
			 * found a train. Also check all previous platform tiles
			 * for a possible train. */
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; *train_on_res == nullptr && IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				CheckTrainsOnTrack(ftoti, st_tile);
				if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
			}
		}
		if (*train_on_res == nullptr && IsTileType(ftoti.res.tile, TileType::TunnelBridge)) {
			/* The target tile is a bridge/tunnel, also check the other end tile. */
			CheckTrainsOnTrack(ftoti, GetOtherTunnelBridgeEnd(ftoti.res.tile));
			if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		}
	}
	return ftoti.res;
}

/**
 * Find the train which has reserved a specific path.
 *
 * @param tile A tile on the path.
 * @param track A reserved track on the tile.
 * @return The vehicle holding the reservation or nullptr if the path is stray.
 */
Train *GetTrainForReservation(TileIndex tile, Track track)
{
	assert(HasReservedTracks(tile, TrackToTrackBits(track)));
	Trackdir  trackdir = TrackToTrackdir(track);

	RailTypes rts = GetRailTypeInfo(GetTileRailType(tile))->compatible_railtypes;

	/* Follow the path from tile to both ends, one of the end tiles should
	 * have a train on it. We need FollowReservation to ignore one-way signals
	 * here, as one of the two search directions will be the "wrong" way. */
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		/* If the tile has a one-way block signal in the current trackdir, skip the
		 * search in this direction as the reservation can't come from this side.*/
		if (HasOnewaySignalBlockingTrackdir(tile, ReverseTrackdir(trackdir)) && !HasPbsSignalOnTrackdir(tile, trackdir)) continue;

		FindTrainOnTrackInfo ftoti;
		ftoti.res = FollowReservation(GetTileOwner(tile), rts, tile, trackdir, true);

		CheckTrainsOnTrack(ftoti, ftoti.res.tile);
		if (ftoti.best != nullptr) return ftoti.best;

		/* Special case for stations: check the whole platform for a vehicle. */
		if (IsRailStationTile(ftoti.res.tile)) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				CheckTrainsOnTrack(ftoti, st_tile);
				if (ftoti.best != nullptr) return ftoti.best;
			}
		}

		/* Special case for bridges/tunnels: check the other end as well. */
		if (IsTileType(ftoti.res.tile, TileType::TunnelBridge)) {
			CheckTrainsOnTrack(ftoti, GetOtherTunnelBridgeEnd(ftoti.res.tile));
			if (ftoti.best != nullptr) return ftoti.best;
		}
	}

	return nullptr;
}

/**
 * Determine whether a certain track on a tile is a safe position to end a path.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param include_line_end Should end-of-line tiles be considered safe?
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @return True if it is a safe position
 */
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg)
{
	if (IsRailDepotTile(tile)) return true;

	/* A portal mouth is a safe position when the train is heading INTO the
	 * underground: the surface reservation legitimately ends here and the
	 * underground path is reserved separately (YAPF3D) at the portal. */
	{
		SliceID portal_sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
		if (portal_sid != INVALID_SLICE_ID) {
			const TileSlice &ps = _multilayer_map.GetSlice(portal_sid);
			if (TrackdirToExitdir(trackdir) == GetPortalDiveDirection(tile, ps)) return true;
		}
	}

	if (IsTileType(tile, TileType::Railway)) {
		/* For non-pbs signals, stop on the signal tile. */
		if (HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return true;
	}

	/* Check next tile. For performance reasons, we check for 90 degree turns ourself. */
	CFollowTrackRail ft(v, GetAllCompatibleRailTypes(v->railtypes));

	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}

	/* Check for reachable tracks. */
	ft.new_td_bits &= DiagdirReachesTrackdirs(ft.exitdir);
	if (!ft.is_portal && Rail90DegTurnDisallowed(GetTileRailType(ft.old_tile), GetTileRailType(ft.new_tile), forbid_90deg)) ft.new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);
	if (ft.new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;

	if (ft.new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.new_td_bits);
		/* PBS signal on next trackdir? Safe position. */
		if (HasPbsSignalOnTrackdir(ft.new_tile, td)) return true;
		/* One-way PBS signal against us? Safe if end-of-line is allowed. */
		if (IsTileType(ft.new_tile, TileType::Railway) && HasSignalOnTrackdir(ft.new_tile, ReverseTrackdir(td)) &&
				GetSignalType(ft.new_tile, TrackdirToTrack(td)) == SIGTYPE_PBS_ONEWAY) {
			return include_line_end;
		}
	}

	return false;
}

/**
 * Check if a safe position is free.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @return True if the position is free
 */
bool IsWaitingPositionFree(const Train *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg)
{
	Track     track = TrackdirToTrack(trackdir);
	TrackBits reserved = GetReservedTrackbits(tile);

	/* Tile reserved? Can never be a free waiting position. */
	if (TrackOverlapsTracks(reserved, track)) return false;

	/* Not reserved and depot or not a pbs signal -> free. */
	if (IsRailDepotTile(tile)) return true;
	if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, track))) return true;

	/* Check the next tile, it has to be free as well. Do not filter for compatible railtypes
	 * to make sure we never accidentally join up reservations. */
	CFollowTrackRail ft(v, RailTypes{});

	if (!ft.Follow(tile, trackdir)) return true;

	/* The follow teleported through an underground portal: the tile beyond the
	 * exit portal is irrelevant for the surface waiting position — the
	 * underground path is reserved separately (YAPF3D). */
	if (ft.is_portal) return true;

	/* Check for reachable tracks. */
	ft.new_td_bits &= DiagdirReachesTrackdirs(ft.exitdir);
	if (Rail90DegTurnDisallowed(GetTileRailType(ft.old_tile), GetTileRailType(ft.new_tile), forbid_90deg)) ft.new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);

	return !HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits));
}

/* === Underground PBS === */

/**
 * Check if an underground track slice has a signal (of any type) on a given track.
 * @param ref Underground tile reference.
 * @param t The track to check.
 * @return True if a signal is present in either direction.
 */
bool HasUndergroundSignalOnTrack(const TileRef &ref, Track t)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return false;

	return ::HasUndergroundSignalOnTrack(_multilayer_map.GetSlice(ref.slice), t);
}

/**
 * Reservation state of a slice that can carry one.
 * @param slice The slice.
 * @param[out] reserved Reserved track bits.
 * @param[out] owner The train holding them (meaningful only when reserved != TRACK_BIT_NONE).
 * @return False for slices that carry no reservation (portals).
 */
static bool GetUndergroundReservation(const TileSlice &slice, TrackBits &reserved, VehicleID &owner)
{
	switch (slice.kind) {
		case SliceKind::Track:
			reserved = static_cast<TrackBits>(slice.track.reserved);
			owner = VehicleID(slice.track.reserved_by);
			return true;
		case SliceKind::StationTile:
			reserved = static_cast<TrackBits>(slice.station.reserved);
			owner = VehicleID(slice.station.reserved_by);
			return true;
		case SliceKind::Ramp:
			/* A ramp carries one straight track; a train on it holds it like any track. */
			reserved = static_cast<TrackBits>(slice.ramp.reserved);
			owner = VehicleID(slice.ramp.reserved_by);
			return true;
		default:
			reserved = TRACK_BIT_NONE;
			owner = VehicleID::Invalid();
			return false;
	}
}

/**
 * Whether a track of a plain underground track slice is held in the second
 * owner slot. Two trains can hold tracks of one tile only when those do not
 * overlap: the upper and the lower or the left and the right one. The lower
 * and the right track keep their own owner.
 * @param t The track.
 * @return True for the lower and the right track.
 */
static bool IsSecondUndergroundOwnerTrack(Track t)
{
	return t == TRACK_LOWER || t == TRACK_RIGHT;
}

/**
 * The train holding a reserved track of an underground slice.
 * @param slice The slice.
 * @param t The track; it must be reserved.
 * @return The train.
 */
static VehicleID GetUndergroundTrackOwner(const TileSlice &slice, Track t)
{
	switch (slice.kind) {
		case SliceKind::Track: return VehicleID(IsSecondUndergroundOwnerTrack(t) ? slice.track.reserved_by2 : slice.track.reserved_by);
		case SliceKind::StationTile: return VehicleID(slice.station.reserved_by);
		case SliceKind::Ramp: return VehicleID(slice.ramp.reserved_by);
		default: return VehicleID::Invalid();
	}
}

/**
 * The reserved tracks of an underground slice held by a given train, or by
 * any train but it.
 * @param slice The slice.
 * @param owner The train.
 * @param others False: the tracks the train holds; true: the tracks other trains hold.
 * @return The tracks.
 */
static TrackBits GetUndergroundTracksHeld(const TileSlice &slice, VehicleID owner, bool others)
{
	TrackBits reserved;
	VehicleID first_owner;
	if (!GetUndergroundReservation(slice, reserved, first_owner)) return TRACK_BIT_NONE;
	TrackBits result = TRACK_BIT_NONE;
	for (Track r : SetTrackBitIterator(reserved)) {
		if ((GetUndergroundTrackOwner(slice, r) == owner) != others) result |= TrackToTrackBits(r);
	}
	return result;
}

/**
 * Get reserved trackbits for an underground slice, whoever holds them.
 * @param ref The underground tile reference.
 * @return Reserved trackbits, or TRACK_BIT_NONE.
 */
TrackBits GetUndergroundReservedTrackbits(const TileRef &ref)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return TRACK_BIT_NONE;

	TrackBits reserved;
	VehicleID owner;
	GetUndergroundReservation(_multilayer_map.GetSlice(ref.slice), reserved, owner);
	return reserved;
}

/**
 * Get the trackbits of an underground slice reserved for a given train.
 * @param ref The underground tile reference.
 * @param owner The train.
 * @return Reserved trackbits held by that train, or TRACK_BIT_NONE.
 */
TrackBits GetUndergroundReservedTrackbits(const TileRef &ref, VehicleID owner)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return TRACK_BIT_NONE;

	return GetUndergroundTracksHeld(_multilayer_map.GetSlice(ref.slice), owner, false);
}

/**
 * The train holding the reservation of an underground slice.
 * @param ref The underground tile reference.
 * @return The train, or VehicleID::Invalid() when nothing is reserved.
 */
VehicleID GetUndergroundReservationOwner(const TileRef &ref)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return VehicleID::Invalid();

	const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	TrackBits reserved;
	VehicleID owner;
	GetUndergroundReservation(slice, reserved, owner);
	return reserved == TRACK_BIT_NONE ? VehicleID::Invalid() : GetUndergroundTrackOwner(slice, FindFirstTrack(reserved));
}

/**
 * Whether a track of an underground slice is reserved for a train other than the given one.
 * @param ref The underground tile reference.
 * @param t The track.
 * @param owner The train asking; VehicleID::Invalid() asks about any reservation at all.
 * @return True if another train holds a reservation overlapping the track.
 */
bool IsUndergroundTrackReservedByOther(const TileRef &ref, Track t, VehicleID owner)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return false;

	const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	TrackBits reserved;
	VehicleID first_owner;
	GetUndergroundReservation(slice, reserved, first_owner);
	if (!TrackOverlapsTracks(reserved, t)) return false;
	if (owner == VehicleID::Invalid()) return true;
	/* Only the tracks of other trains that overlap this one count. */
	return TrackOverlapsTracks(GetUndergroundTracksHeld(slice, owner, true), t);
}

/**
 * Whether a train other than the given one stands on or holds tracks of an
 * underground slice.
 * @param tile The tile of the slice.
 * @param sid The slice.
 * @param slice The slice itself.
 * @param tracks The tracks to look at.
 * @param owner The train that does not count; VehicleID::Invalid() counts every train.
 * @return True if the tracks are occupied.
 */
static bool IsUndergroundSliceOccupied(TileIndex tile, SliceID sid, const TileSlice &slice, TrackBits tracks, VehicleID owner)
{
	TrackBits reserved;
	VehicleID res_owner;
	if (GetUndergroundReservation(slice, reserved, res_owner) && (reserved & tracks) != TRACK_BIT_NONE &&
			(owner == VehicleID::Invalid() || (GetUndergroundTracksHeld(slice, owner, true) & tracks) != TRACK_BIT_NONE)) {
		return true;
	}
	return HasVehicleOnTile(tile, [&](const Vehicle *u) {
		if (u->type != VEH_TRAIN) return false;
		const Train *t = Train::From(u);
		if (owner != VehicleID::Invalid() && t->First()->index == owner) return false;
		if (slice.kind == SliceKind::Portal) return true; /* Diving or surfacing: the mouth is in use. */
		if (!t->IsUnderground() || static_cast<SliceID>(t->underground_slice) != sid) return false;
		return slice.kind == SliceKind::Ramp || (static_cast<TrackBits>(t->track) & tracks) != TRACK_BIT_NONE;
	});
}

/** A signal at the far end of an underground signal block, facing out of it. */
struct UndergroundBlockExit {
	TileRef ref; ///< The signal tile.
	Trackdir td; ///< The trackdir the signal faces.
};

/**
 * Explore the block behind an underground signal, like ExploreSegment does on
 * the surface: every track connected to the tile behind the signal, at its
 * level and over ramps, up to the next signals. A signal stands at the far
 * edge of its tile along the direction it faces, so the tile of a signal
 * facing out of the block belongs to the block (the train waits there), the
 * tile of a signal facing into it does not. The block also ends at portals.
 * @param ref The signal tile.
 * @param td The trackdir the signal faces.
 * @param owner The train asking; its own parts and reservations do not count.
 * @param[out] exits If not nullptr, receives the signals facing out of the block.
 * @return True if a train or another train's reservation occupies the block.
 */
static bool ExploreUndergroundSignalBlock(const TileRef &ref, Trackdir td, VehicleID owner, std::vector<UndergroundBlockExit> *exits)
{
	/* Entering a tile at a level, moving in a direction. */
	struct Position {
		TileIndex tile;
		int16_t depth;
		DiagDirection dir;
	};
	std::vector<Position> todo;
	std::unordered_set<uint64_t> seen;
	auto leave = [&todo](TileIndex from, int16_t depth, DiagDirection dir) {
		TileIndex next = AddTileIndexDiffCWrap(from, TileIndexDiffCByDiagDir(dir));
		if (next != INVALID_TILE) todo.push_back({next, depth, dir});
	};
	leave(ref.tile, GetUndergroundLeaveDepth(_multilayer_map.GetSlice(ref.slice), TrackdirToExitdir(td)), TrackdirToExitdir(td));

	/* Bound the work on very large blocks; what lies beyond is not looked at. */
	for (uint guard = 0; !todo.empty() && guard < 4096; guard++) {
		Position p = todo.back();
		todo.pop_back();

		SliceID sid = _multilayer_map.FindSliceAt(p.tile, p.depth);
		if (sid == INVALID_SLICE_ID) continue;
		uint64_t key = (static_cast<uint64_t>(sid) << 2) | p.dir;
		if (!seen.insert(key).second) continue;

		const TileSlice &s = _multilayer_map.GetSlice(sid);
		switch (s.kind) {
			case SliceKind::Track:
			case SliceKind::StationTile: {
				TrackBits tracks = static_cast<TrackBits>(s.kind == SliceKind::Track ? s.track.tracks : s.station.tracks);
				TrackBits masked = tracks & DiagdirReachesTracks(p.dir);
				if (masked == TRACK_BIT_NONE) break;
				/* Two parallel tracks on one tile are not connected. */
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = masked;

				Track t = FindFirstTrack(masked);
				if (s.kind == SliceKind::Track && KillFirstBit(tracks) == TRACK_BIT_NONE && HasUndergroundSignalOnTrack(s.track, t)) {
					Trackdir sig_td = TrackEnterdirToTrackdir(t, p.dir);
					if (HasUndergroundSignalOnTrackdir(s.track, sig_td)) {
						/* The signal faces out of the block at the far edge of its tile. */
						if (IsUndergroundSliceOccupied(p.tile, sid, s, tracks, owner)) return true;
						if (exits != nullptr) exits->push_back({TileRef{p.tile, sid}, sig_td});
					}
					/* Otherwise its back stands at the near edge: the tile is beyond the block. */
					break;
				}

				if (IsUndergroundSliceOccupied(p.tile, sid, s, tracks, owner)) return true;
				/* Leave through every other side a track of the tile touches. */
				for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
					if (d == ReverseDiagDir(p.dir)) continue;
					if ((tracks & DiagdirReachesTracks(ReverseDiagDir(d))) != TRACK_BIT_NONE) leave(p.tile, p.depth, d);
				}
				break;
			}

			case SliceKind::Ramp: {
				DiagDirection rd = static_cast<DiagDirection>(s.ramp.direction);
				if (DiagDirToAxis(p.dir) != DiagDirToAxis(rd)) break;
				if (p.depth != (p.dir == rd ? s.ramp.entry_depth : s.ramp.exit_depth)) break;
				if (HasUndergroundSignalOnTrack(s, GetRampTrack(s.ramp))) {
					/* A signal on a ramp ends the block like one on plain track. */
					Trackdir sig_td = DiagDirToDiagTrackdir(p.dir);
					if (HasUndergroundSignalOnTrackdir(s, sig_td)) {
						if (IsUndergroundSliceOccupied(p.tile, sid, s, TRACK_BIT_ALL, owner)) return true;
						if (exits != nullptr) exits->push_back({TileRef{p.tile, sid}, sig_td});
					}
					break;
				}
				if (IsUndergroundSliceOccupied(p.tile, sid, s, TRACK_BIT_ALL, owner)) return true;
				leave(p.tile, p.dir == rd ? s.ramp.exit_depth : s.ramp.entry_depth, p.dir);
				break;
			}

			case SliceKind::Portal:
				/* The block ends at the mouth; a train in it holds the block. */
				if (IsUndergroundSliceOccupied(p.tile, sid, s, TRACK_BIT_ALL, owner)) return true;
				break;

			default:
				break;
		}
	}
	return false;
}

/** How deep chains of pre-signals are followed to find a green exit. */
static constexpr uint MAX_UNDERGROUND_PRESIGNAL_CHAIN = 8;

/**
 * State of an underground block signal (block, entry, exit or combo), as the
 * surface signal update computes it: red while a train or another train's
 * reservation occupies the block behind it; an entry or combo signal is also
 * red when that block has pre-signal exits (exit or combo signals) and none of
 * them is green.
 * @param ref The signal tile.
 * @param td The trackdir the signal faces.
 * @param owner The train asking, VehicleID::Invalid() for none.
 * @param level How many pre-signal blocks deep this is.
 * @return The signal state.
 */
static SignalState GetUndergroundBlockSignalState(const TileRef &ref, Trackdir td, VehicleID owner, uint level, std::unordered_map<uint64_t, SignalState> &memo)
{
	/* Chains of combo signals branch and may loop: each signal's block is
	 * explored once per evaluation, and a signal met again while its own
	 * evaluation is still under way counts as red. */
	uint64_t key = (static_cast<uint64_t>(ref.slice) << 4) | static_cast<uint64_t>(td);
	auto [it, inserted] = memo.try_emplace(key, SIGNAL_STATE_RED);
	if (!inserted) return it->second;

	const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	SignalType type = GetUndergroundSignalType(slice, TrackdirToTrack(td));
	bool entry = type == SIGTYPE_ENTRY || type == SIGTYPE_COMBO;

	std::vector<UndergroundBlockExit> exits;
	SignalState state = SIGNAL_STATE_GREEN;
	if (ExploreUndergroundSignalBlock(ref, td, owner, entry ? &exits : nullptr)) {
		state = SIGNAL_STATE_RED;
	} else if (entry) {
		bool has_exit = false;
		bool green_exit = false;
		for (const UndergroundBlockExit &e : exits) {
			/* The other side of a two-way combo signal is no exit of its own block. */
			if (e.ref == ref && e.td == ReverseTrackdir(td)) continue;
			const TileSlice &es = _multilayer_map.GetSlice(e.ref.slice);
			SignalType exit_type = GetUndergroundSignalType(es, TrackdirToTrack(e.td));
			if (exit_type != SIGTYPE_EXIT && exit_type != SIGTYPE_COMBO) continue;
			has_exit = true;
			if (level >= MAX_UNDERGROUND_PRESIGNAL_CHAIN || GetUndergroundBlockSignalState(e.ref, e.td, owner, level + 1, memo) == SIGNAL_STATE_GREEN) {
				green_exit = true;
				break;
			}
		}
		if (has_exit && !green_exit) state = SIGNAL_STATE_RED;
	}
	memo[key] = state;
	return state;
}

/**
 * Get the computed state of an underground signal; there is no stored state,
 * it is computed when asked for.
 * Path signals: GREEN if no conflicting reservation of another train exists
 * on the tile behind the signal, RED otherwise. The asking train's own
 * reservation (the block it already holds past the signal) never shows red.
 * Block signals (block, entry, exit, combo): see GetUndergroundBlockSignalState.
 * @param ref Underground tile reference (tile WITH the signal).
 * @param td The trackdir the train wants to travel.
 * @param owner The asking train; VehicleID::Invalid() treats any reservation as blocking (drawing).
 * @return SIGNAL_STATE_GREEN or SIGNAL_STATE_RED.
 */
SignalState GetUndergroundSignalState(const TileRef &ref, Trackdir td, VehicleID owner)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return SIGNAL_STATE_RED;

	const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	if (!CanSliceCarrySignals(slice)) return SIGNAL_STATE_RED;

	if (!HasUndergroundSignalOnTrackdir(slice, td)) return SIGNAL_STATE_GREEN; /* No signal = free */

	if (!IsPbsSignal(GetUndergroundSignalType(slice, TrackdirToTrack(td)))) {
		std::unordered_map<uint64_t, SignalState> memo;
		return GetUndergroundBlockSignalState(ref, td, owner, 0, memo);
	}

	/* Check the tile behind the signal for a conflicting reservation. */
	DiagDirection exitdir = TrackdirToExitdir(td);
	TileIndex next_tile = AddTileIndexDiffCWrap(ref.tile, TileIndexDiffCByDiagDir(exitdir));
	if (next_tile == INVALID_TILE) return SIGNAL_STATE_GREEN;

	/* Past a signal on a ramp the track goes on at the ramp's other level. */
	int16_t depth = GetUndergroundLeaveDepth(slice, exitdir);
	SliceID next_sid = _multilayer_map.FindSliceAt(next_tile, depth);
	if (next_sid == INVALID_SLICE_ID) return SIGNAL_STATE_GREEN; /* No track ahead = free */

	const TileSlice &next_slice = _multilayer_map.GetSlice(next_sid);
	TrackBits next_tracks;
	switch (next_slice.kind) {
		case SliceKind::Track: next_tracks = static_cast<TrackBits>(next_slice.track.tracks); break;
		case SliceKind::StationTile: next_tracks = static_cast<TrackBits>(next_slice.station.tracks); break;
		case SliceKind::Ramp: next_tracks = TrackToTrackBits(AxisToTrack(DiagDirToAxis(static_cast<DiagDirection>(next_slice.ramp.direction)))); break;
		default: return SIGNAL_STATE_GREEN;
	}

	TileRef next_ref{next_tile, next_sid};
	for (Track t : SetTrackBitIterator(next_tracks & DiagdirReachesTracks(exitdir))) {
		if (IsUndergroundTrackReservedByOther(next_ref, t, owner)) return SIGNAL_STATE_RED;
	}
	return SIGNAL_STATE_GREEN;
}

/**
 * Try to reserve a track on an underground slice for a train.
 * Reserving a track the same train already holds succeeds without change.
 * @param ref The underground tile reference.
 * @param t The track to reserve.
 * @param owner The train the reservation is for.
 * @return True if the track is now reserved for the train.
 */
bool TryReserveUndergroundTrack(const TileRef &ref, Track t, VehicleID owner)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return false;

	TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	TrackBits trackbit = TrackToTrackBits(t);

	TrackBits reserved;
	VehicleID res_owner;
	if (!GetUndergroundReservation(slice, reserved, res_owner)) return false;

	if ((reserved & trackbit) != TRACK_BIT_NONE) return GetUndergroundTrackOwner(slice, t) == owner;
	/* Another track of the tile is reserved: allowed beside it when the two do
	 * not overlap, by any train on a plain track tile (parallel tracks), by the
	 * same train elsewhere. */
	if (TrackOverlapsTracks(reserved, t)) return false;
	if (reserved != TRACK_BIT_NONE && slice.kind != SliceKind::Track && res_owner != owner) return false;

	if (slice.kind == SliceKind::Track) {
		slice.track.reserved = static_cast<uint8_t>(reserved | trackbit);
		if (IsSecondUndergroundOwnerTrack(t)) {
			slice.track.reserved_by2 = owner.base();
		} else {
			slice.track.reserved_by = owner.base();
		}
	} else if (slice.kind == SliceKind::Ramp) {
		/* The ramp's only track, whatever axis track was asked for. */
		slice.ramp.reserved = static_cast<uint8_t>(TrackToTrackBits(AxisToTrack(DiagDirToAxis(static_cast<DiagDirection>(slice.ramp.direction)))));
		slice.ramp.reserved_by = owner.base();
	} else {
		slice.station.reserved = static_cast<uint8_t>(reserved | trackbit);
		slice.station.reserved_by = owner.base();
	}
	return true;
}

/**
 * Free an underground PBS path reservation ahead of a position.
 *
 * Walks the chain of reserved, connected tracks at the same depth starting
 * from the tile after (ref, td) and unreserves them. Only the given train's
 * own reservation is freed: the walk stops at a tile another train holds, at
 * a one-way signal against the walking direction (the reservation of a train
 * waiting there starts behind it) and after a tile carrying a signal
 * along the walking direction, because Yapf3DReservePath never reserves past
 * the first signal.
 *
 * Used when an underground train reverses: the tiles ahead of it would
 * otherwise stay reserved forever and deadlock the line.
 * @param ref The underground tile the train front is on.
 * @param td  The trackdir the train was facing before reversing.
 * @param owner The train whose reservation is freed.
 */
void FreeUndergroundPathAhead(const TileRef &ref, Trackdir td, VehicleID owner)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice) || !IsValidTrackdir(td)) return;

	/* The level the walk continues at; a ramp under the front already decides
	 * the level of the side it is left by. */
	int16_t depth;
	{
		const TileSlice &start = _multilayer_map.GetSlice(ref.slice);
		if (start.kind == SliceKind::Ramp) {
			DiagDirection rd = static_cast<DiagDirection>(start.ramp.direction);
			depth = TrackdirToExitdir(td) == rd ? start.ramp.exit_depth : start.ramp.entry_depth;
		} else {
			depth = start.span.z_bot;
		}
	}

	/* On a loop the path ahead can run round onto the tiles the train's own
	 * tail stands on: those stay held until its last vehicle leaves them. */
	const Train *owner_train = Train::GetIfValid(owner);
	auto owner_stands_on = [owner_train](TileIndex t, SliceID s) {
		for (const Train *u = owner_train; u != nullptr; u = u->Next()) {
			if (u->IsUnderground() && TileIndex(u->underground_tile_raw) == t && static_cast<SliceID>(u->underground_slice) == s) return true;
		}
		return false;
	};

	TileIndex tile = ref.tile;
	Trackdir cur_td = td;
	/* Guard against malformed reservation loops. */
	for (uint guard = 0; guard < Map::Size(); guard++) {
		DiagDirection exitdir = TrackdirToExitdir(cur_td);
		TileIndex next = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(exitdir));
		if (next == INVALID_TILE) return;

		SliceID sid = _multilayer_map.FindSliceAt(next, depth);
		if (sid == INVALID_SLICE_ID) return;
		if (owner_stands_on(next, sid)) return;
		const TileSlice &slice = _multilayer_map.GetSlice(sid);

		/* A ramp: freed like any track, the path runs on at its other level. */
		if (slice.kind == SliceKind::Ramp) {
			DiagDirection rd = static_cast<DiagDirection>(slice.ramp.direction);
			if (DiagDirToAxis(exitdir) != DiagDirToAxis(rd)) return;
			if (depth != (exitdir == rd ? slice.ramp.entry_depth : slice.ramp.exit_depth)) return;
			if (slice.ramp.reserved == 0 || VehicleID(slice.ramp.reserved_by) != owner) return;
			cur_td = DiagDirToDiagTrackdir(exitdir);
			if (HasUndergroundOnewaySignalBlockingTrackdir(slice, cur_td)) return;
			UnreserveUndergroundTrack(TileRef{next, sid}, DiagDirToDiagTrack(exitdir));
			depth = exitdir == rd ? slice.ramp.exit_depth : slice.ramp.entry_depth;
			tile = next;
			if (HasUndergroundSignalOnTrackdir(slice, cur_td)) return;
			continue;
		}

		TrackBits reserved;
		VehicleID res_owner;
		if (!GetUndergroundReservation(slice, reserved, res_owner)) return;
		/* Only this train's own tracks; another may hold a parallel one here. */
		TrackBits own = GetUndergroundTracksHeld(slice, owner, false);
		if (own == TRACK_BIT_NONE) return;

		TrackBits reachable = own & DiagdirReachesTracks(exitdir);
		if (reachable == TRACK_BIT_NONE) return;

		Track t = FindFirstTrack(reachable);
		Trackdir next_td = TrackEnterdirToTrackdir(t, exitdir);
		if (!IsValidTrackdir(next_td)) return;

		/* A one-way signal against us: not our reservation. */
		if (HasUndergroundOnewaySignalBlockingTrackdir(slice, next_td)) return;

		UnreserveUndergroundTrack(TileRef{next, sid}, t);

		cur_td = next_td;
		tile = next;

		/* Reservation chains end at the first signal along the path. */
		if (HasUndergroundSignalOnTrackdir(slice, cur_td)) return;
	}
}

/**
 * Unreserve a track on an underground slice.
 * @param ref The underground tile reference.
 * @param t The track to unreserve.
 */
void UnreserveUndergroundTrack(const TileRef &ref, Track t)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) return;

	TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	TrackBits mask = ~TrackToTrackBits(t);

	if (slice.kind == SliceKind::Track) {
		slice.track.reserved = static_cast<uint8_t>(static_cast<TrackBits>(slice.track.reserved) & mask);
		TrackBits left = static_cast<TrackBits>(slice.track.reserved);
		if ((left & (TRACK_BIT_LOWER | TRACK_BIT_RIGHT)) == TRACK_BIT_NONE) slice.track.reserved_by2 = 0;
		if ((left & ~(TRACK_BIT_LOWER | TRACK_BIT_RIGHT)) == TRACK_BIT_NONE) slice.track.reserved_by = 0;
	} else if (slice.kind == SliceKind::StationTile) {
		slice.station.reserved = static_cast<uint8_t>(static_cast<TrackBits>(slice.station.reserved) & mask);
		if (slice.station.reserved == 0) slice.station.reserved_by = 0;
	} else if (slice.kind == SliceKind::Ramp) {
		/* The ramp has one track: freeing any of it frees the ramp. */
		slice.ramp.reserved = 0;
		slice.ramp.reserved_by = 0;
	}
}
