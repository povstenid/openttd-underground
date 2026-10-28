/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_train.cpp Underground train movement implementation. */

#include "../stdafx.h"
#include "underground_train.h"
#include "multilayer_map.h"
#include "../vehicle_base.h"
#include "../train.h"
#include "../track_func.h"
#include "../direction_func.h"
#include "../map_func.h"
#include "../debug.h"

#include "../safeguards.h"

#include <unordered_map>

/** Map from vehicle index to underground state. */
static std::unordered_map<uint32_t, UndergroundTrainState> _underground_train_states;

UndergroundTrainState *GetUndergroundState(const Train *v)
{
	auto it = _underground_train_states.find(v->index.base());
	if (it == _underground_train_states.end()) return nullptr;
	return &it->second;
}

UndergroundTrainState &GetOrCreateUndergroundState(const Train *v)
{
	return _underground_train_states[v->index.base()];
}

void ClearUndergroundState(const Train *v)
{
	_underground_train_states.erase(v->index.base());
}

/**
 * Check if a tile has a portal that a train can enter from a given direction.
 */
bool HasEnterablePortal(TileIndex tile, DiagDirection enterdir)
{
	SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return false;

	const TileSlice &ps = _multilayer_map.GetSlice(sid);
	/* A surface train dives only when it moves into the mouth: a train crossing
	 * the portal tile from the tunnel side (surface rail continuing behind a
	 * flat portal) stays on the surface. */
	return GetPortalDiveDirection(tile, ps) == enterdir;
}

/** Whether a column carries underground rail (track, platform or ramp) at a depth. */
static bool HasUndergroundRailAt(TileIndex tile, int16_t depth)
{
	if (tile == INVALID_TILE) return false;
	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return false;
	SliceKind k = _multilayer_map.GetSlice(sid).kind;
	return k == SliceKind::Track || k == SliceKind::StationTile || k == SliceKind::Ramp;
}

/** Portal tile -> train that claimed its mouth for coming out. */
static std::unordered_map<uint32_t, VehicleID> _portal_exit_claims;

void ClaimPortalExit(TileIndex portal_tile, VehicleID train)
{
	Debug(misc, 2, "portal exit ({},{}) claimed by train {}", TileX(portal_tile), TileY(portal_tile), train.base());
	_portal_exit_claims[portal_tile.base()] = train;
}

void ReleasePortalExit(TileIndex portal_tile)
{
	Debug(misc, 2, "portal exit ({},{}) released", TileX(portal_tile), TileY(portal_tile));
	_portal_exit_claims.erase(portal_tile.base());
}

VehicleID GetPortalExitClaimant(TileIndex portal_tile)
{
	if (!IsPortalExitClaimed(portal_tile)) return VehicleID::Invalid(); // also drops a stale claim
	return _portal_exit_claims.find(portal_tile.base())->second;
}

std::vector<TileIndex> ReleasePortalExits(VehicleID train)
{
	std::vector<TileIndex> released;
	std::erase_if(_portal_exit_claims, [train, &released](const auto &claim) {
		if (claim.second != train) return false;
		released.emplace_back(claim.first);
		return true;
	});
	for (TileIndex t : released) Debug(misc, 2, "portal exit ({},{}) released by train {}", TileX(t), TileY(t), train.base());
	return released;
}

bool IsPortalExitClaimed(TileIndex tile, VehicleID train)
{
	if (_portal_exit_claims.empty()) return false;
	auto it = _portal_exit_claims.find(tile.base());
	if (it == _portal_exit_claims.end()) return false;
	/* A claim only stands while its train is on its way: below ground, or on
	 * the surface holding the path for its dive. */
	const Train *t = Train::GetIfValid(it->second);
	const UndergroundTrainState *st = t != nullptr ? GetUndergroundState(t) : nullptr;
	bool on_way = t != nullptr && (t->IsUnderground() || (st != nullptr && st->entry_reserved_portal != INVALID_TILE));
	if (t == nullptr || !t->IsFrontEngine() || t->vehstatus.Test(VehState::Crashed) || !on_way) {
		Debug(misc, 2, "portal exit ({},{}) claim of train {} dropped as stale", TileX(tile), TileY(tile), it->second.base());
		_portal_exit_claims.erase(it);
		return false;
	}
	return it->second != train;
}

void ClearUndergroundTrainRuntimeState()
{
	_underground_train_states.clear();
	_portal_exit_claims.clear();
}

std::vector<std::pair<VehicleID, UndergroundTrainState>> GetUndergroundTrainStates()
{
	std::vector<std::pair<VehicleID, UndergroundTrainState>> states;
	states.reserve(_underground_train_states.size());
	for (const auto &[index, state] : _underground_train_states) states.emplace_back(VehicleID(index), state);
	std::ranges::sort(states, {}, [](const auto &item) { return item.first; });
	return states;
}

void SetUndergroundTrainState(VehicleID train, const UndergroundTrainState &state)
{
	_underground_train_states[train.base()] = state;
}

std::vector<std::pair<TileIndex, VehicleID>> GetPortalExitClaims()
{
	std::vector<std::pair<TileIndex, VehicleID>> claims;
	claims.reserve(_portal_exit_claims.size());
	for (const auto &[tile, train] : _portal_exit_claims) claims.emplace_back(TileIndex(tile), train);
	std::ranges::sort(claims, {}, [](const auto &item) { return item.first; });
	return claims;
}

DiagDirection GetPortalDiveDirection(TileIndex tile, const TileSlice &ps)
{
	DiagDirection dir = static_cast<DiagDirection>(ps.portal.direction);
	if (dir >= DIAGDIR_END) return dir;
	int16_t depth = ps.span.z_bot;
	bool ahead = HasUndergroundRailAt(AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(dir)), depth);
	bool behind = HasUndergroundRailAt(AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(ReverseDiagDir(dir))), depth);
	return (!ahead && behind) ? ReverseDiagDir(dir) : dir;
}

/**
 * Get underground track bits at a tile and depth.
 */
TrackBits GetUndergroundTrackBits(TileIndex tile, int16_t depth, DiagDirection enterdir)
{
	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return TRACK_BIT_NONE;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);

	if (slice.kind == SliceKind::Track) {
		TrackBits all_tracks = static_cast<TrackBits>(slice.track.tracks);
		/* Filter by reachability from enterdir. */
		return all_tracks & DiagdirReachesTracks(enterdir);
	}

	if (slice.kind == SliceKind::StationTile) {
		TrackBits all_tracks = static_cast<TrackBits>(slice.station.tracks);
		return all_tracks & DiagdirReachesTracks(enterdir);
	}

	if (slice.kind == SliceKind::Ramp) {
		/* A ramp carries one straight track along its direction axis.
		 * Moving along ramp.direction goes entry_depth → exit_depth, so the
		 * train must arrive at the depth matching the side it enters from. */
		DiagDirection rd = static_cast<DiagDirection>(slice.ramp.direction);
		if (DiagDirToAxis(enterdir) != DiagDirToAxis(rd)) return TRACK_BIT_NONE;
		int16_t required_depth = (enterdir == rd) ? slice.ramp.entry_depth : slice.ramp.exit_depth;
		if (depth != required_depth) return TRACK_BIT_NONE;
		Track t = (DiagDirToAxis(rd) == AXIS_X) ? TRACK_X : TRACK_Y;
		return TrackToTrackBits(t) & DiagdirReachesTracks(enterdir);
	}

	/* Portal slices return NO tracks — exit is handled by TrainController
	 * dead-end detection: bits==NONE → check Portal → exit to surface. */

	return TRACK_BIT_NONE;
}

/**
 * Get the length of an underground station platform by walking in a direction.
 */
int GetUndergroundPlatformLength(TileIndex tile, int16_t depth, uint16_t station_id, DiagDirection dir)
{
	int length = 0;
	TileIndexDiffC diff = TileIndexDiffCByDiagDir(dir);

	while (true) {
		int x = TileX(tile);
		int y = TileY(tile);
		if (x < 0 || y < 0 || x >= (int)Map::SizeX() || y >= (int)Map::SizeY()) break;

		SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
		if (sid == INVALID_SLICE_ID) break;

		const TileSlice &slice = _multilayer_map.GetSlice(sid);
		if (slice.kind != SliceKind::StationTile) break;
		if (slice.station.station_id != station_id) break;

		length++;
		int nx = x + diff.x;
		int ny = y + diff.y;
		if (nx < 0 || ny < 0 || nx >= (int)Map::SizeX() || ny >= (int)Map::SizeY()) break;
		tile = TileXY(nx, ny);
	}

	return length;
}
