/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_cmd.cpp Commands for building and removing underground rail tracks. */

#include "../stdafx.h"
#include "underground_cmd.h"
#include "multilayer_map.h"
#include "station_complex.h"
#include "underground_conflict.h"
#include "underground_rules.h"
#include "underground_cost.h"
#include "../command_func.h"
#include "../company_func.h"
#include "../rail.h"
#include "../rail_map.h"
#include "../track_func.h"
#include "../tile_map.h"
#include "../slope_func.h"
#include "../tunnel_map.h"
#include "../rail_cmd.h"
#include "../train.h"
#include "../tilearea_type.h"
#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../debug.h"
#include "portal_registry.h"
#include "underground_train.h"
#include "../station_base.h"
#include "../station_kdtree.h"
#include "../town.h"
#include "../station_func.h"
#include "../signal_func.h"
#include "../pbs.h"
#include "underground_signal.h"
#include "../cheat_type.h"
#include "../table/strings.h"
#include "../viewport_func.h"
#include "../vehicle_func.h"
#include "../company_base.h"
#include "../company_gui.h"
#include "../road_map.h"
#include "../economy_func.h"
#include "../strings_func.h"

#include <set>

#include <vector>

#include "../safeguards.h"

int16_t GetMetroGlobalMaxZ()
{
	if (Map::Size() == 0) return 0;

	static int16_t highest_surface_z = 0;
	if (_tile_heights_changed) {
		highest_surface_z = 0;
		for (uint i = 0; i < Map::Size(); ++i) {
			highest_surface_z = std::max<int16_t>(highest_surface_z, GetMetroSurfaceZ(TileIndex(i)));
		}
		_tile_heights_changed = false;
	}

	/* Underground slices live strictly below the surface tile, so the highest
	 * globally usable underground z is one below the highest surface tile. */
	return std::max<int16_t>(GetMetroGlobalMinZ(), static_cast<int16_t>(highest_surface_z - 1));
}

/* === Infrastructure accounting === */

/** Infrastructure pieces contributed by one underground slice. */
struct UndergroundSliceInfrastructure {
	RailType railtype = INVALID_RAILTYPE; ///< Rail type the rail pieces are counted under.
	uint32_t rail = 0;     ///< Number of rail pieces.
	uint32_t signals = 0;  ///< Number of signals.
	uint32_t stations = 0; ///< Number of station tiles.
};

/**
 * Determine the infrastructure contribution of a single slice.
 * Track: one rail piece per track bit plus one signal per (directional) signal bit.
 * StationTile: one rail piece and one station tile (like a vanilla rail station tile).
 * Ramp: one rail piece plus its signals.
 * Portal: one rail piece for the portal slice itself; its surface rail tile is counted by vanilla.
 * @param s The slice.
 * @return The pieces this slice contributes.
 */
static UndergroundSliceInfrastructure GetUndergroundSliceInfrastructure(const TileSlice &s)
{
	UndergroundSliceInfrastructure p;
	switch (s.kind) {
		case SliceKind::Track:
			p.railtype = static_cast<RailType>(s.track.railtype);
			p.rail = CountBits(s.track.tracks);
			/* signal_mask holds surface-style directional signal bits; count them like vanilla does. */
			p.signals = CountBits(s.track.signal_mask);
			break;
		case SliceKind::StationTile:
			p.railtype = static_cast<RailType>(s.station.railtype);
			p.rail = 1;
			p.stations = 1;
			break;
		case SliceKind::Ramp:
			p.railtype = static_cast<RailType>(s.ramp.railtype);
			p.rail = 1;
			p.signals = CountBits(s.ramp.signal_mask);
			break;
		case SliceKind::Portal:
			p.railtype = static_cast<RailType>(s.portal.railtype);
			p.rail = 1;
			break;
		default:
			break;
	}
	if (p.railtype >= RAILTYPE_END) p.rail = 0;
	return p;
}

/**
 * Add the infrastructure contribution of a slice to the counts of a company.
 * @param c     Company to update (may be nullptr, then nothing happens).
 * @param s     The slice.
 * @param delta +1 to add the slice, -1 to remove it.
 */
static void ApplyUndergroundSliceInfrastructure(Company *c, const TileSlice &s, int delta)
{
	if (c == nullptr) return;
	UndergroundSliceInfrastructure p = GetUndergroundSliceInfrastructure(s);
	if (p.rail != 0) c->infrastructure.rail[p.railtype] += static_cast<int32_t>(p.rail) * delta;
	c->infrastructure.signal += static_cast<int32_t>(p.signals) * delta;
	c->infrastructure.station += static_cast<int32_t>(p.stations) * delta;
}

/**
 * Account for an in-place change of a slice: apply the difference between the
 * contribution before and after the change to the slice owner.
 * @param owner  Raw owner byte of the slice.
 * @param before Contribution before the change.
 * @param after  Contribution after the change (empty when the slice was freed).
 */
static void ApplyUndergroundInfrastructureDelta(uint8_t owner, const UndergroundSliceInfrastructure &before, const UndergroundSliceInfrastructure &after)
{
	Company *c = Company::GetIfValid(owner);
	if (c == nullptr) return;
	if (before.rail != 0) c->infrastructure.rail[before.railtype] -= before.rail;
	if (after.rail != 0) c->infrastructure.rail[after.railtype] += after.rail;
	c->infrastructure.signal += after.signals - before.signals;
	c->infrastructure.station += after.stations - before.stations;
	DirtyCompanyInfrastructureWindows(c->index);
}

/**
 * Add or remove the whole infrastructure contribution of a slice to/from its owner.
 * @param s     The slice (its owner field selects the company).
 * @param delta +1 to add, -1 to remove.
 */
void UpdateUndergroundSliceInfrastructure(const TileSlice &s, int delta)
{
	Company *c = Company::GetIfValid(s.owner);
	if (c == nullptr) return;
	ApplyUndergroundSliceInfrastructure(c, s, delta);
	DirtyCompanyInfrastructureWindows(c->index);
}

/**
 * Add the infrastructure of all underground slices to the company counts.
 * Called from AfterLoadCompanyStats() after the vanilla counts have been rebuilt from the map,
 * which makes it cover both loading savegames and the desync cache checker.
 */
void AddUndergroundInfrastructureCounts()
{
	if (!_multilayer_map.IsAllocated()) return;

	for (uint i = 0; i < Map::Size(); i++) {
		for (SliceID sid : _multilayer_map.GetColumn(TileIndex(i)).slices) {
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind == SliceKind::Surface || s.kind == SliceKind::End) continue;
			ApplyUndergroundSliceInfrastructure(Company::GetIfValid(s.owner), s, 1);
		}
	}
}

/**
 * Savegame conversion: portals saved before SLV_METRO_PORTAL_RAILTYPE have no rail type.
 * Derive it from the surface rail tile the portal was built with, or fall back to the
 * rail type of the underground track the portal leads to, or the first rail type.
 */
void AfterLoadUndergroundPortalRailtypes()
{
	if (!_multilayer_map.IsAllocated()) return;

	for (uint i = 0; i < Map::Size(); i++) {
		TileIndex tile(i);
		for (SliceID sid : _multilayer_map.GetColumn(tile).slices) {
			TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind != SliceKind::Portal || s.portal.railtype < RAILTYPE_END) continue;

			RailType rt = INVALID_RAILTYPE;
			if (IsTileType(tile, TileType::Railway)) {
				rt = GetRailType(tile);
			} else {
				DiagDirection dir = static_cast<DiagDirection>(s.portal.direction);
				if (dir < DIAGDIR_END) {
					/* The underground side of a portal lies in its direction (see BFSFromPortal). */
					TileIndexDiffC step = TileIndexDiffCByDiagDir(dir);
					TileIndex next = TileAddWrap(tile, step.x, step.y);
					if (next != INVALID_TILE) {
						SliceID nid = _multilayer_map.FindSliceAt(next, s.span.z_bot);
						if (nid != INVALID_SLICE_ID) {
							const TileSlice &n = _multilayer_map.GetSlice(nid);
							if (n.kind == SliceKind::Track) rt = static_cast<RailType>(n.track.railtype);
							if (n.kind == SliceKind::StationTile) rt = static_cast<RailType>(n.station.railtype);
						}
					}
				}
			}
			s.portal.railtype = static_cast<uint8_t>(rt < RAILTYPE_END ? rt : RAILTYPE_BEGIN);
		}
	}
}

/**
 * Bring the Station object of removed platforms in line with what vanilla does for
 * surface rail stations: once no platform is left, the station complex and the train
 * facility go away, and the station is deleted by the regular "unused station" timeout
 * when nothing else is left of it either.
 * @param station_id The station that lost platforms (or exits).
 */
static void UpdateStationAfterPlatformRemoval(uint16_t station_id)
{
	auto sc_it = _station_complexes.find(station_id);
	bool has_platform = false;
	if (sc_it != _station_complexes.end()) {
		has_platform = !sc_it->second.GetPlatforms().empty();
		if (!has_platform && !sc_it->second.HasSurfaceExit()) RemoveStationComplex(station_id);
	}

	Station *st = Station::GetIfValid(StationID(station_id));
	if (st == nullptr) return;

	if (!has_platform && st->facilities.Test(StationFacility::Train)) {
		bool has_surface_rail = false;
		if (st->train_station.tile != INVALID_TILE) {
			for (TileIndex t : st->train_station) {
				if (st->TileBelongsToRailStation(t)) {
					has_surface_rail = true;
					break;
				}
			}
		}
		if (!has_surface_rail) {
			st->train_station = TileArea(INVALID_TILE, 0, 0);
			st->facilities.Reset(StationFacility::Train);
		}
	}
	st->AfterStationTileSetChange(false, StationType::Rail);
}

/**
 * Transfer or remove all underground slices of a company.
 * Called from ChangeOwnershipOfCompanyItems() after vanilla has deleted/transferred the
 * company's vehicles and changed the owner of (or cleared) its surface tiles, and before
 * the owner of its Station objects is changed.
 * @param old_owner Company whose slices are affected.
 * @param new_owner Company taking over, or INVALID_OWNER / OWNER_NONE to remove the slices.
 */
void ChangeUndergroundOwnership(Owner old_owner, Owner new_owner)
{
	if (!_multilayer_map.IsAllocated()) return;

	Company *old_company = Company::GetIfValid(old_owner);
	Company *new_company = Company::GetIfValid(new_owner);
	const bool remove = (new_company == nullptr);

	/* The old company's trains have already been deleted by now. Any train still underground
	 * belongs to another company; never free a slice such a train stands on, so it cannot keep
	 * a dangling reference. Such slices are left behind without owner instead. */
	std::set<SliceID> occupied;
	std::set<uint32_t> occupied_tiles;
	if (remove) {
		for (const Train *t : Train::Iterate()) {
			if (!t->IsUnderground()) continue;
			occupied.insert(static_cast<SliceID>(t->underground_slice));
			occupied_tiles.insert(t->underground_tile_raw);
		}
	}

	std::set<uint16_t> affected_stations;
	bool changed = false;

	if (remove) {
		/* Explicit exits of the company's stations go with the company. Pavilion bus station
		 * tiles were already cleared by ChangeTileOwner (which dropped their nodes); what is
		 * left are underpasses (and pavilions from old savegames without a building). */
		for (auto &[station_id, sc] : _station_complexes) {
			const Station *st = Station::GetIfValid(StationID(station_id));
			if (st == nullptr || st->owner != old_owner) continue;
			size_t erased = std::erase_if(sc.nodes, [](const StationComplexNode &n) {
				if (!n.IsExplicitExit()) return false;
				MarkTileDirtyByTile(n.ref.tile);
				return true;
			});
			if (erased != 0) affected_stations.insert(station_id);
		}
		InvalidateStationExitCache();
	}

	for (uint i = 0; i < Map::Size(); i++) {
		TileIndex tile(i);
		std::vector<SliceID> &slices = _multilayer_map.GetColumn(tile).slices;
		for (size_t idx = 0; idx < slices.size();) {
			SliceID sid = slices[idx];
			TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind == SliceKind::Surface || s.kind == SliceKind::End || s.owner != old_owner.base()) {
				idx++;
				continue;
			}

			changed = true;
			ApplyUndergroundSliceInfrastructure(old_company, s, -1);

			if (!remove) {
				/* Merger: hand the slice over, together with its infrastructure counts. */
				s.owner = new_owner.base();
				ApplyUndergroundSliceInfrastructure(new_company, s, 1);
				idx++;
				continue;
			}

			if (occupied.contains(sid) || occupied_tiles.contains(tile.base())) {
				s.owner = OWNER_NONE.base();
				idx++;
				continue;
			}

			if (s.kind == SliceKind::StationTile) {
				uint16_t station_id = s.station.station_id;
				int16_t depth = s.span.z_bot;
				auto sc_it = _station_complexes.find(station_id);
				if (sc_it != _station_complexes.end()) {
					auto &nodes = sc_it->second.nodes;
					/* Match by tile and depth: slice ids stored in nodes are not stable across save/load. */
					std::erase_if(nodes, [&](const StationComplexNode &n) {
						return n.ref.tile == tile && n.role == StationRole::Platform && n.depth == depth;
					});
				}
				affected_stations.insert(station_id);
			}

			/* Track, Ramp and Portal slices (and the StationTile itself) are simply freed.
			 * The portal's surface rail is a vanilla rail tile, already handled by ChangeTileOwner. */
			slices.erase(slices.begin() + idx);
			_multilayer_map.FreeSlice(sid);
		}
	}

	for (uint16_t station_id : affected_stations) UpdateStationAfterPlatformRemoval(station_id);

	if (old_company != nullptr) DirtyCompanyInfrastructureWindows(old_company->index);
	if (new_company != nullptr) DirtyCompanyInfrastructureWindows(new_company->index);

	if (changed) RecomputePortalConnections();
}


/**
 * Portal tile whose surface rail CmdRemovePortal is currently removing, or INVALID_TILE.
 * Commands run synchronously and in the same order on every client, so this
 * guard is deterministic in multiplayer; it only ever lives for the duration
 * of the nested RemoveRail call inside CmdRemovePortal.
 */
static TileIndex _portal_surface_rail_removal_tile = INVALID_TILE;

/**
 * Check whether any train is using a given slice or tile underground.
 * Trains address their position by TileRef{tile, SliceID}; SliceIDs are reused
 * after FreeSlice, so freeing a slice a train stands on corrupts its state.
 * Every vehicle part is checked, so a consist straddling several tiles is
 * protected on each of them.
 */
static bool UndergroundSliceInUse(TileIndex tile, SliceID sid)
{
	for (const Train *t : Train::Iterate()) {
		if (!t->IsUnderground()) continue;
		if (t->underground_slice == sid) return true;
		if (t->underground_tile_raw == tile.base()) return true;
	}
	return false;
}

CommandCost CheckPortalSurfaceRailRemoval(TileIndex tile, TrackBits removed_tracks, DoCommandFlags flags)
{
	/* Bankruptcy cleanup must always be able to clear the tile. */
	if (flags.Test(DoCommandFlag::Bankrupt)) return CommandCost();
	if (!_multilayer_map.IsAllocated() || !IsValidTile(tile)) return CommandCost();
	if (tile == _portal_surface_rail_removal_tile) return CommandCost();

	SliceID portal_sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (portal_sid == INVALID_SLICE_ID) return CommandCost();

	/* Extra tracks a player added to a portal tile may be removed freely, but
	 * neither the portal's own axis track nor the last remaining track. */
	const TileSlice &ps = _multilayer_map.GetSlice(portal_sid);
	TrackBits portal_track = AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(ps.portal.direction)));
	if ((removed_tracks & portal_track) != TRACK_BIT_NONE) return CommandCost(STR_ERROR_MUST_REMOVE_PORTAL_FIRST);

	if (IsTileType(tile, TileType::Railway) && IsPlainRail(tile)) {
		TrackBits present = GetTrackBits(tile);
		if ((present & ~removed_tracks) == TRACK_BIT_NONE) return CommandCost(STR_ERROR_MUST_REMOVE_PORTAL_FIRST);
	}

	return CommandCost();
}

CommandCost CheckPortalSurfaceRailBuild(TileIndex tile, Track track)
{
	if (!_multilayer_map.IsAllocated() || !IsValidTile(tile)) return CommandCost();

	SliceID portal_sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (portal_sid == INVALID_SLICE_ID) return CommandCost();

	const TileSlice &ps = _multilayer_map.GetSlice(portal_sid);
	Track portal_track = AxisToTrack(DiagDirToAxis(static_cast<DiagDirection>(ps.portal.direction)));
	if (track != portal_track) return CommandCost(STR_ERROR_UNDERGROUND_PORTAL_TRACK_ONLY);
	return CommandCost();
}

/**
 * Cycle the signal side of a track: both -> one side -> the other side -> both
 * for block signals; path signals go one side -> the other side -> one side,
 * like CycleSignalSide on the surface.
 * @param tp The track or ramp slice payload.
 * @param track The track with the signal.
 */
template <UndergroundSignalPayload P>
static void CycleUndergroundSignalSide(P &tp, Track track)
{
	uint8_t track_mask = SignalOnTrack(track);
	uint8_t shift = FindFirstBit(track_mask);
	uint8_t sig = (tp.signal_mask & track_mask) >> shift;
	if (--sig == 0) sig = IsPbsSignal(GetUndergroundSignalType(tp, track)) ? 2 : 3;
	tp.signal_mask = (tp.signal_mask & ~track_mask) | ((sig << shift) & track_mask);
}

/**
 * Check whether a vanilla tunnel occupies the given depth of a column.
 * Honours the "crossing tunnels" cheat the same way CmdBuildTunnel does.
 * @param tile  Tile column.
 * @param depth Underground depth (TileHeight units).
 * @return True if construction at this depth must be refused.
 */
static bool IsBlockedByVanillaTunnel(TileIndex tile, int16_t depth)
{
	if (_cheats.crossing_tunnels.value) return false;
	return IsVanillaTunnelInWayOfUnderground(tile, depth);
}

/**
 * Validate an underground depth for a column, with a player-visible reason.
 * @param tile  Tile column.
 * @param depth Underground depth (TileHeight units).
 * @return Success, or the reason the depth cannot be used.
 */
static CommandCost CheckUndergroundDepth(TileIndex tile, int16_t depth)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (depth >= GetMetroSurfaceZ(tile)) return CommandCost(STR_ERROR_UNDERGROUND_DEPTH_INVALID);
	if (depth < GetMetroMinZ(tile)) return CommandCost(STR_ERROR_UNDERGROUND_TOO_DEEP);
	return CommandCost();
}

/**
 * Check a level for work on what is already built there (removal, signals):
 * only that it lies below the surface. The depth limits are for new
 * construction; what was built before a limit was raised stays workable.
 * @param tile  Tile column.
 * @param depth Underground depth (TileHeight units).
 * @return Success, or the reason the depth cannot be used.
 */
static CommandCost CheckUndergroundExistingDepth(TileIndex tile, int16_t depth)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (depth >= GetMetroSurfaceZ(tile)) return CommandCost(STR_ERROR_UNDERGROUND_DEPTH_INVALID);
	return CommandCost();
}

/**
 * Check that the current company owns an underground slice.
 * @param slice Slice to check.
 * @param tile  Tile column of the slice (for town ownership messages).
 * @return Success, or STR_ERROR_OWNED_BY / STR_ERROR_AREA_IS_OWNED_BY_ANOTHER.
 */
static CommandCost CheckUndergroundSliceOwnership(const TileSlice &slice, TileIndex tile)
{
	if (slice.owner == _current_company.base()) return CommandCost();
	if (slice.owner >= OWNER_END.base()) return CommandCost(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
	return CheckOwnership(Owner(slice.owner), tile);
}

/**
 * Build a single underground rail track at a given depth.
 * @param flags   Command flags.
 * @param tile    Surface tile position (x, y).
 * @param railtype Type of rail to build.
 * @param track   Track orientation.
 * @param depth   Z depth (negative, e.g. -1 = first underground level).
 * @return The cost of the operation or an error.
 */
CommandCost CmdBuildUndergroundRail(DoCommandFlags flags, TileIndex tile, RailType railtype, Track track, int16_t depth)
{
	Debug(misc, 4, "CmdBuildUndergroundRail: tile={} rt={} track={} depth={} flags_exec={}",
		tile.base(), (int)railtype, (int)track, depth, flags.Test(DoCommandFlag::Execute));

	/* Validate parameters. */
	if (railtype >= RAILTYPE_END) {
		Debug(misc, 4, "  FAIL: railtype >= RAILTYPE_END ({})", (int)railtype);
		return CMD_ERROR;
	}
	/* Note: we skip ValParamRailType which checks company availability.
	 * Underground rail uses any valid railtype regardless of company research. */
	if (!IsValidTrack(track)) {
		Debug(misc, 4, "  FAIL: IsValidTrack");
		return CMD_ERROR;
	}
	/* Check if tile is within map bounds. */
	if (!IsValidTile(tile)) {
		Debug(misc, 4, "  FAIL: IsValidTile");
		return CMD_ERROR;
	}

	CommandCost depth_ret = CheckUndergroundDepth(tile, depth);
	if (depth_ret.Failed()) {
		Debug(misc, 4, "  FAIL: z check (z={} surface={} min={})", depth, GetMetroSurfaceZ(tile), GetMetroMinZ(tile));
		return depth_ret;
	}

	if (IsBlockedByVanillaTunnel(tile, depth)) {
		Debug(misc, 4, "  FAIL: vanilla tunnel in the way (z={})", depth);
		return CommandCost(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);
	}

	Debug(misc, 4, "  Validation passed, checking existing slices...");

	/* Underground tiles can be built under any surface tile. */

	TrackBits trackbit = TrackToTrackBits(track);

	/* Calculate the ZSpan for this depth level. */
	int16_t z_bot = depth;
	int16_t z_top = depth + 1;

	/* Check if there's already a Track slice at this depth. */
	SliceID existing = _multilayer_map.FindSliceAt(tile, depth);
	Debug(misc, 4, "  FindSliceAt result: {}", existing);
	if (existing != INVALID_SLICE_ID) {
		TileSlice &slice = _multilayer_map.GetSlice(existing);

		/* Building over content that already carries this track is a free
		 * no-op, like dragging vanilla rail across existing pieces. Without
		 * this, extending a line by starting the drag on its last tile (or
		 * dragging across a station/portal) fails the whole build. */
		switch (slice.kind) {
			case SliceKind::Track:
				if (slice.track.tracks & trackbit) return CommandCost();
				break;
			case SliceKind::StationTile:
				if (slice.station.tracks & trackbit) return CommandCost();
				return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
			case SliceKind::Portal:
				/* Only the portal's own axis; it is already there. */
				if (AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(slice.portal.direction))) & trackbit) return CommandCost();
				return CommandCost(STR_ERROR_UNDERGROUND_PORTAL_TRACK_ONLY);
			case SliceKind::Ramp:
				if (AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(slice.ramp.direction))) & trackbit) return CommandCost();
				return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
			default:
				return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY); /* Something else is here. */
		}

		/* New track under a house or water that reaches down to here is refused; what is already there stays. */
		CommandCost clearance = CheckUndergroundClearance(tile, depth);
		if (clearance.Failed()) return clearance;

		/* Can add more track bits to existing slice. */
		CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
		if (owner_ret.Failed()) return owner_ret;
		if (slice.track.railtype != static_cast<uint8_t>(railtype)) {
			return CommandCost(STR_ERROR_UNDERGROUND_RAIL_TYPE_MISMATCH);
		}
		/* Like on the surface signals only stand on tracks that do not cross. */
		if (slice.track.signal_mask != 0 && TracksOverlap(static_cast<TrackBits>(slice.track.tracks) | trackbit)) {
			return CommandCost(STR_ERROR_MUST_REMOVE_SIGNALS_FIRST);
		}

		if (flags.Test(DoCommandFlag::Execute)) {
			UndergroundSliceInfrastructure before = GetUndergroundSliceInfrastructure(slice);
			slice.track.tracks |= trackbit;
			/* Refunds of any piece never exceed the cheapest piece paid for. */
			uint16_t paid = GetUndergroundPaidSoil(UndergroundCostKind::Track, tile, depth);
			slice.track.paid_soil = slice.track.paid_soil == 0 ? paid : std::min(slice.track.paid_soil, paid);
			ApplyUndergroundInfrastructureDelta(slice.owner, before, GetUndergroundSliceInfrastructure(slice));
		}
	} else {
		/* Need to create a new Track slice. */
		Debug(misc, 4, "  No existing slice, creating new. Execute={}", flags.Test(DoCommandFlag::Execute));
		CommandCost clearance = CheckUndergroundClearance(tile, depth);
		if (clearance.Failed()) return clearance;
		if (flags.Test(DoCommandFlag::Execute)) {
			SliceID new_id = _multilayer_map.AllocateSlice();
			Debug(misc, 4, "  Allocated slice id={}", new_id);
			TileSlice &slice = _multilayer_map.GetSlice(new_id);
			slice.kind = SliceKind::Track;
			slice.span = {z_top, z_bot};
			slice.owner = _current_company.base();
			slice.track.railtype = static_cast<uint8_t>(railtype);
			slice.track.tracks = trackbit;
			slice.track.signal_mask = 0;
			slice.track.reserved = 0;
			slice.track.paid_soil = GetUndergroundPaidSoil(UndergroundCostKind::Track, tile, depth);

			if (!_multilayer_map.InsertSlice(tile, new_id)) {
				Debug(misc, 4, "  FAIL: InsertSlice ZSpan conflict");
				_multilayer_map.FreeSlice(new_id);
				return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
			}
			UpdateUndergroundSliceInfrastructure(_multilayer_map.GetSlice(new_id), 1);
			Debug(misc, 4, "  Slice inserted successfully!");
		}
	}

	/* Calculate cost: rail price * underground multipliers * soil. */
	Money cost = GetUndergroundBuildCost(UndergroundCostKind::Track, railtype, tile, depth);
	Debug(misc, 4, "  SUCCESS! Cost={}", cost);
	if (flags.Test(DoCommandFlag::Execute)) {
		MarkTileDirtyByTile(tile);
		RecomputePortalConnections();
	}
	return CommandCost(EXPENSES_CONSTRUCTION, cost);
}

/* Per-trackdir tile step, same table the surface rail dragger uses. */
static const TileIndexDiffC _ug_trackdelta[] = {
	{ -1,  0 }, {  0,  1 }, { -1,  0 }, {  0,  1 }, {  1,  0 }, {  0,  1 },
	{  0,  0 },
	{  0,  0 },
	{  1,  0 }, {  0, -1 }, {  0, -1 }, {  1,  0 }, {  0, -1 }, { -1,  0 },
	{  0,  0 },
	{  0,  0 }
};

/** Orient a trackdir so that walking it leads from start to end (vanilla auto-drag rules). */
static CommandCost ValidateUndergroundAutoDrag(Trackdir *trackdir, TileIndex start, TileIndex end)
{
	if (!IsValidTrack(TrackdirToTrack(*trackdir))) return CMD_ERROR;

	int dx = TileX(end) - TileX(start);
	int dy = TileY(end) - TileY(start);

	int trdx = _ug_trackdelta[*trackdir].x;
	int trdy = _ug_trackdelta[*trackdir].y;
	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx += _ug_trackdelta[*trackdir ^ 1].x;
		trdy += _ug_trackdelta[*trackdir ^ 1].y;
	}

	while ((trdx <= 0 && dx > 0) || (trdx >= 0 && dx < 0) ||
			(trdy <= 0 && dy > 0) || (trdy >= 0 && dy < 0)) {
		if (!HasBit(*trackdir, 3)) {
			SetBit(*trackdir, 3);
			trdx = -trdx;
			trdy = -trdy;
		} else {
			return CMD_ERROR;
		}
	}

	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx = _ug_trackdelta[*trackdir].x;
		trdy = _ug_trackdelta[*trackdir].y;
		if (abs(dx) != abs(dy) && abs(dx) + abs(trdy) != abs(dy) + abs(trdx)) return CMD_ERROR;
	}

	return CommandCost();
}

/**
 * Build a stretch of underground rail: straight lines tile by tile, corner
 * tracks as the vanilla zig-zag. Existing pieces along the way are free
 * no-ops (CmdBuildUndergroundRail handles that).
 */
CommandCost CmdBuildUndergroundRailroadTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, RailType railtype, Track track, int16_t depth)
{
	if (!IsValidTile(start_tile) || !IsValidTile(end_tile)) return CMD_ERROR;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateUndergroundAutoDrag(&trackdir, start_tile, end_tile);
	if (ret.Failed()) return ret;

	/* Collect the dragged line first. Tiles where the level lies at or above
	 * the terrain are open ground: the tunnel ends with a portal on the last
	 * tile inside the hill and the line continues as ordinary surface track.
	 * A line ending on surface rail of its own course leaves for the surface
	 * there too, on flat ground as well. */
	struct DragTile {
		TileIndex tile;
		Trackdir trackdir;
		bool open;
	};
	std::vector<DragTile> line;
	{
		TileIndex tile = start_tile;
		Trackdir td = trackdir;
		for (;;) {
			line.push_back({tile, td, !IsValidMetroZ(tile, depth)});
			if (tile == end_tile) break;
			tile = AddTileIndexDiffCWrap(tile, _ug_trackdelta[td]);
			if (tile == INVALID_TILE) break;
			/* toggle railbit for the non-diagonal tracks */
			if (!IsDiagonalTrackdir(td)) ToggleBit(td, 0);
		}
	}
	if (line.size() > 1) {
		for (DragTile *end : {&line.front(), &line.back()}) {
			if (!end->open && IsPlainRailTile(end->tile) && HasTrack(end->tile, TrackdirToTrack(end->trackdir))) end->open = true;
		}
	}

	/* The mouth of a tunnel is the open tile next to an inside tile: on a hillside
	 * that is the incline whose foot is at the tunnel's level. */
	auto is_mouth = [&](size_t i) -> bool {
		if (!line[i].open) return false;
		bool inside_before = i > 0 && !line[i - 1].open;
		bool inside_after = i + 1 < line.size() && !line[i + 1].open;
		return inside_before != inside_after;
	};

	auto build_surface = [&](TileIndex tile, Track t, CommandCost &total) -> CommandCost {
		CommandCost r = Command<Commands::BuildRail>::Do(flags, tile, railtype, t, false);
		if (r.Failed()) {
			/* Pieces already there are free, as with vanilla rail dragging. */
			return r.GetErrorMessage() == STR_ERROR_ALREADY_BUILT ? CommandCost(EXPENSES_CONSTRUCTION) : r;
		}
		total.AddCost(r.GetCost());
		return CommandCost(EXPENSES_CONSTRUCTION);
	};

	/* A zig-zag line can never get a portal. Check every mouth before building
	 * anything, so the open tiles leading up to it (always sloped, being next to
	 * the hill) cannot hide this with a surface rail error. */
	for (size_t i = 0; i < line.size(); i++) {
		if (is_mouth(i) && !IsDiagonalTrack(TrackdirToTrack(line[i].trackdir))) return CommandCost(STR_ERROR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK);
	}

	/* Where each portal goes: on an incline rising into the hill the mouth itself
	 * is the portal. Anywhere else (flat ground, an incline the wrong way) the
	 * last tile inside becomes a portal diving into the ground, its open cut
	 * rising to the mouth, which then carries the surface track. */
	struct PortalPlan {
		size_t index;
		DiagDirection dir; ///< Into the tunnel.
	};
	std::vector<PortalPlan> portals;
	for (size_t i = 0; i < line.size(); i++) {
		if (!is_mouth(i)) continue;
		bool inside_before = i > 0 && !line[i - 1].open;
		size_t inside = inside_before ? i - 1 : i + 1;
		DiagDirection into_tunnel = DiagdirBetweenTiles(line[i].tile, line[inside].tile);
		if (_multilayer_map.FindSliceByKind(line[i].tile, SliceKind::Portal) != INVALID_SLICE_ID) continue;
		if (GetInclinedSlopeDirection(GetTileSlope(line[i].tile)) == into_tunnel) {
			portals.push_back({i, into_tunnel});
			continue;
		}
		if (!IsDiagonalTrack(TrackdirToTrack(line[inside].trackdir))) return CommandCost(STR_ERROR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK);
		portals.push_back({inside, into_tunnel});
	}

	CommandCost total(EXPENSES_CONSTRUCTION);
	for (size_t i = 0; i < line.size(); i++) {
		const DragTile &dt = line[i];
		Track t = TrackdirToTrack(dt.trackdir);

		auto portal = std::ranges::find(portals, i, &PortalPlan::index);
		if (portal != portals.end()) {
			if (_multilayer_map.FindSliceByKind(dt.tile, SliceKind::Portal) != INVALID_SLICE_ID) continue;
			ret = CmdBuildPortal(flags, dt.tile, railtype, portal->dir, depth);
			if (ret.Failed()) return ret;
			total.AddCost(ret.GetCost());
			continue;
		}
		if (is_mouth(i) && _multilayer_map.FindSliceByKind(dt.tile, SliceKind::Portal) != INVALID_SLICE_ID) continue;

		if (dt.open) {
			/* Open ground: ordinary surface track. */
			ret = build_surface(dt.tile, t, total);
			if (ret.Failed()) return ret;
			continue;
		}

		ret = CmdBuildUndergroundRail(flags, dt.tile, railtype, t, depth);
		if (ret.Failed()) return ret;
		total.AddCost(ret.GetCost());
	}

	return total;
}

/**
 * Place underground signals along a stretch of track every `density` tiles,
 * like CmdBuildSignalTrack: the stretch copies type, variant and direction of
 * a signal already on its first tile (entry and exit signals become block
 * signals), otherwise it gets path signals facing the drag direction or block
 * signals facing both ways.
 * @param flags      Command flags.
 * @param end_tile   Last tile of the stretch.
 * @param start_tile First tile of the stretch.
 * @param track      Track on the first tile.
 * @param depth      Underground depth.
 * @param density    Place a signal every this many tiles.
 * @param sigtype    Type of the signals.
 * @param sigvar     Variant of the signals.
 * @return Cost or error.
 */
CommandCost CmdBuildUndergroundSignalTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth, uint8_t density, SignalType sigtype, SignalVariant sigvar)
{
	if (!IsValidTile(start_tile) || !IsValidTile(end_tile)) return CMD_ERROR;
	if (sigtype > SIGTYPE_LAST || sigvar > SIG_SEMAPHORE) return CMD_ERROR;
	if (density == 0) density = 4;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateUndergroundAutoDrag(&trackdir, start_tile, end_tile);
	if (ret.Failed()) return ret;

	/* Copy the signal style of the first track piece if it has one. */
	uint8_t signals;
	bool first_has_signal = false;
	SliceID first_sid = _multilayer_map.FindSliceAt(start_tile, depth);
	if (first_sid != INVALID_SLICE_ID && HasUndergroundSignalOnTrack(_multilayer_map.GetSlice(first_sid), TrackdirToTrack(trackdir))) {
		const TileSlice &tp = _multilayer_map.GetSlice(first_sid);
		first_has_signal = true;
		signals = GetUndergroundSignalMask(tp) & SignalOnTrack(TrackdirToTrack(trackdir));
		sigvar = GetUndergroundSignalVariant(tp, TrackdirToTrack(trackdir));
		sigtype = GetUndergroundSignalType(tp, TrackdirToTrack(trackdir));
		if (sigtype == SIGTYPE_ENTRY || sigtype == SIGTYPE_EXIT) sigtype = SIGTYPE_BLOCK;
	} else {
		signals = IsPbsSignal(sigtype) ? SignalAlongTrackdir(trackdir) : SignalOnTrack(TrackdirToTrack(trackdir));
	}
	/* The direction relative to the drag, which turns with the track on diagonal stretches. */
	bool along = (signals & SignalAlongTrackdir(trackdir)) != 0;
	bool against = (signals & SignalAgainstTrackdir(trackdir)) != 0;

	CommandCost total(EXPENSES_CONSTRUCTION);
	TileIndex tile = start_tile;
	uint since_last = 0;
	bool placed_any = false;
	CommandCost last_error = CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	for (;;) {
		if (since_last == 0 && tile == start_tile && first_has_signal) {
			/* The signal the stretch copies stays as it is. */
			placed_any = true;
		} else if (since_last == 0) {
			uint8_t copy = (along ? SignalAlongTrackdir(trackdir) : 0) | (against ? SignalAgainstTrackdir(trackdir) : 0);
			CommandCost sig = CmdBuildUndergroundSignal(flags, tile, TrackdirToTrack(trackdir), depth, sigtype, sigvar, false, false, SIGTYPE_BLOCK, SIGTYPE_LAST, copy);
			/* Tiles that cannot carry a signal (stations, portals, duplicates)
			 * are skipped, matching surface signal dragging. */
			if (sig.Succeeded()) {
				total.AddCost(sig.GetCost());
				placed_any = true;
			} else {
				last_error = sig;
			}
		}
		since_last = (since_last + 1) % density;

		if (tile == end_tile) break;

		tile = AddTileIndexDiffCWrap(tile, _ug_trackdelta[trackdir]);
		if (tile == INVALID_TILE) break;
		if (!IsDiagonalTrackdir(trackdir)) ToggleBit(trackdir, 0);
	}

	if (!placed_any) return last_error;
	return total;
}

/**
 * Remove a single underground rail track at a given depth.
 * @param flags   Command flags.
 * @param tile    Surface tile position (x, y).
 * @param track   Track orientation to remove.
 * @param depth   Z depth (negative).
 * @return The cost of the operation or an error.
 */
CommandCost CmdRemoveUndergroundRail(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth)
{
	if (!IsValidTrack(track)) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	CommandCost depth_ret = CheckUndergroundExistingDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;

	TrackBits trackbit = TrackToTrackBits(track);

	/* Find the Track slice at this depth. */
	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (slice.kind != SliceKind::Track) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	/* Check if this track bit exists. */
	if (!(slice.track.tracks & trackbit)) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	/* Check ownership. */
	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;

	/* Refuse to remove track a train stands on (moving, stopped or loading;
	 * reserved or not) or that is reserved for one: trains hold TileRefs into
	 * the slice pool, and freeing such a slice lets its reused SliceID corrupt
	 * the train's position and PBS state. */
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	if (TrackOverlapsTracks(static_cast<TrackBits>(slice.track.reserved), track)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);

	/* Save railtype before potentially freeing the slice. */
	RailType rt = static_cast<RailType>(slice.track.railtype);
	uint16_t paid_soil = slice.track.paid_soil;

	if (flags.Test(DoCommandFlag::Execute)) {
		UndergroundSliceInfrastructure before = GetUndergroundSliceInfrastructure(slice);
		uint8_t owner = slice.owner;
		slice.track.tracks &= ~trackbit;
		/* The signals of the track go with it. */
		if (!TracksOverlap(static_cast<TrackBits>(slice.track.tracks) | trackbit)) slice.track.signal_mask &= ~SignalOnTrack(track);

		/* If no tracks remain, remove the slice entirely (its signals go with it). */
		if (slice.track.tracks == 0) {
			ApplyUndergroundInfrastructureDelta(owner, before, UndergroundSliceInfrastructure{});
			_multilayer_map.RemoveSliceFromColumn(tile, sid);
			_multilayer_map.FreeSlice(sid);
		} else {
			ApplyUndergroundInfrastructureDelta(owner, before, GetUndergroundSliceInfrastructure(slice));
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		MarkTileDirtyByTile(tile);
		RecomputePortalConnections();
	}
	return CommandCost(EXPENSES_CONSTRUCTION, -GetUndergroundRemoveRefund(UndergroundCostKind::Track, rt, tile, depth, paid_soil));
}

/**
 * Remove a stretch of underground rail (drag), like CmdRemoveRailroadTrack
 * does on the surface: straight lines tile by tile, corner tracks as the
 * zig-zag; tiles without the track are skipped.
 * @param flags      Command flags.
 * @param end_tile   Last tile of the stretch.
 * @param start_tile First tile of the stretch.
 * @param track      Track on the first tile.
 * @param depth      Underground depth.
 * @return Cost or error.
 */
CommandCost CmdRemoveUndergroundRailroadTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth)
{
	if (!IsValidTrack(track)) return CMD_ERROR;
	if (!IsValidTile(start_tile) || !IsValidTile(end_tile)) return CMD_ERROR;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateUndergroundAutoDrag(&trackdir, start_tile, end_tile);
	if (ret.Failed()) return ret;

	CommandCost total(EXPENSES_CONSTRUCTION);
	bool removed_any = false;
	CommandCost last_error = CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	TileIndex tile = start_tile;
	for (;;) {
		ret = CmdRemoveUndergroundRail(flags, tile, TrackdirToTrack(trackdir), depth);
		if (ret.Succeeded()) {
			total.AddCost(ret.GetCost());
			removed_any = true;
		} else if (ret.GetErrorMessage() != STR_ERROR_THERE_IS_NO_RAILROAD_TRACK) {
			/* Track that had to stay (a train, another owner) tells more than tiles without track. */
			last_error = ret;
		}

		if (tile == end_tile) break;

		tile = AddTileIndexDiffCWrap(tile, _ug_trackdelta[trackdir]);
		if (tile == INVALID_TILE) break;
		if (!IsDiagonalTrackdir(trackdir)) ToggleBit(trackdir, 0);
	}

	if (!removed_any) return last_error;
	return total;
}

/**
 * Build a portal (entry/exit between surface and underground).
 * Works like vanilla tunnel entry on sloped tiles, but also accepts flat tiles
 * when the caller provides an explicit direction.
 * @param flags    Command flags.
 * @param tile     Surface tile on a slope.
 * @param railtype Type of rail.
 * @param dir      Portal direction on flat tiles, otherwise auto-detected from slope.
 * @param depth    Underground depth the portal connects to.
 * @return The cost of the operation or an error.
 */
CommandCost CmdBuildPortal(DoCommandFlags flags, TileIndex tile, RailType railtype, DiagDirection dir, int16_t depth)
{
	if (railtype >= RAILTYPE_END) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;

	/* On inclined tiles the slope decides which way the portal faces (like a
	 * vanilla tunnel). A portal along the other axis would not join the line
	 * it was asked for, its track would run across it: refuse that instead of
	 * building a portal no train can ever come out of. Flat tiles use the
	 * caller-supplied direction. */
	Slope slope = GetTileSlope(tile);
	DiagDirection slope_dir = GetInclinedSlopeDirection(slope);
	if (slope_dir != INVALID_DIAGDIR) {
		if (IsValidDiagDirection(dir) && DiagDirToAxis(dir) != DiagDirToAxis(slope_dir)) return CommandCost(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		dir = slope_dir;
	} else if (slope != SLOPE_FLAT) {
		return CommandCost(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	} else if (dir >= DIAGDIR_END) {
		return CMD_ERROR;
	}

	/* A hillside mouth sits exactly at the foot of its incline (vanilla tunnel
	 * rule); a flat "dive" portal must be strictly below the surface. */
	bool hillside_mouth = slope_dir != INVALID_DIAGDIR && depth == GetMetroSurfaceZ(tile);
	if (!hillside_mouth) {
		CommandCost depth_ret = CheckUndergroundDepth(tile, depth);
		if (depth_ret.Failed()) return depth_ret;
	}
	if (IsBlockedByVanillaTunnel(tile, depth)) return CommandCost(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);

	/* Check if there's already a portal at this tile. */
	SliceID existing = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (existing != INVALID_SLICE_ID) return CommandCost(STR_ERROR_UNDERGROUND_PORTAL_ALREADY_HERE);

	/* The portal slice needs its depth level free; refuse in the test run too.
	 * Plain track along the portal's axis is replaced, as at the end of a line
	 * that is extended to the surface. */
	SliceID replaced = _multilayer_map.FindSliceAt(tile, depth);
	if (replaced != INVALID_SLICE_ID) {
		const TileSlice &ex = _multilayer_map.GetSlice(replaced);
		if (ex.kind != SliceKind::Track || ex.track.tracks != AxisToTrackBits(DiagDirToAxis(dir)) || ex.track.signal_mask != 0) {
			return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
		}
		CommandCost owner_ret = CheckUndergroundSliceOwnership(ex, tile);
		if (owner_ret.Failed()) return owner_ret;
		if (ex.track.reserved != 0 || UndergroundSliceInUse(tile, replaced)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	}
	CommandCost clearance = CheckUndergroundClearance(tile, depth);
	if (clearance.Failed()) return clearance;

	/* The surface rail laid on the mouth is paid with the portal: CmdRemovePortal refunds its removal. */
	Track mouth_track = (DiagDirToAxis(dir) == AXIS_X) ? TRACK_X : TRACK_Y;
	Money surface_rail = (IsPlainRailTile(tile) && HasTrack(tile, mouth_track)) ? Money(0) : RailBuildCost(railtype);

	if (flags.Test(DoCommandFlag::Execute) && replaced != INVALID_SLICE_ID) {
		UpdateUndergroundSliceInfrastructure(_multilayer_map.GetSlice(replaced), -1);
		_multilayer_map.RemoveSliceFromColumn(tile, replaced);
		_multilayer_map.FreeSlice(replaced);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Create Portal slice in multilayer map. */
		SliceID portal_sid = _multilayer_map.AllocateSlice();
		TileSlice &ps = _multilayer_map.GetSlice(portal_sid);
		ps.kind = SliceKind::Portal;
		ps.span = {static_cast<int16_t>(depth + 1), depth};
		ps.owner = _current_company.base();
		ps.portal.direction = static_cast<uint8_t>(dir);
		ps.portal.railtype = static_cast<uint8_t>(railtype);
		ps.portal.target_slice = INVALID_SLICE_ID;
		ps.portal.paid_soil = GetUndergroundPaidSoil(UndergroundCostKind::Portal, tile, depth);

		if (!_multilayer_map.InsertSlice(tile, portal_sid)) {
			_multilayer_map.FreeSlice(portal_sid);
			return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
		}
		/* The portal slice counts as one rail piece; the surface rail built below is counted by BuildRail. */
		UpdateUndergroundSliceInfrastructure(_multilayer_map.GetSlice(portal_sid), 1);

		/* Make surface tile passable: build rail track matching the slope direction.
		 * On slopes, only certain tracks are valid (X track on NE/SW slope, Y on NW/SE). */
			Track portal_track = (DiagDirToAxis(dir) == AXIS_X) ? TRACK_X : TRACK_Y;
			auto rail_result = Command<Commands::BuildRail>::Do(DoCommandFlags{DoCommandFlag::Execute}, tile, railtype, portal_track, false);
			if (!rail_result.Succeeded()) {
				/* Try the perpendicular track as fallback. */
				portal_track = (portal_track == TRACK_X) ? TRACK_Y : TRACK_X;
				Command<Commands::BuildRail>::Do(DoCommandFlags{DoCommandFlag::Execute}, tile, railtype, portal_track, false);
			}
		}

	if (flags.Test(DoCommandFlag::Execute)) {
		MarkTileDirtyByTile(tile);
		RecomputePortalConnections();
	}
	return CommandCost(EXPENSES_CONSTRUCTION, GetUndergroundBuildCost(UndergroundCostKind::Portal, railtype, tile, depth) + surface_rail);
}

/**
 * Build a single underground station platform tile.
 * @param flags      Command flags.
 * @param tile       Surface tile position.
 * @param railtype   Rail type for the platform.
 * @param depth      Underground depth.
 * @param station_id Station to attach to (NEW_UNDERGROUND_STATION_ID = create new).
 * @param axis       Platform direction (AXIS_X or AXIS_Y).
 * @param created_station_id Receives the id of the station this tile was attached to (on Execute).
 * @return Cost or error.
 */
static CommandCost BuildUndergroundStationTile(DoCommandFlags flags, TileIndex tile, RailType railtype, int16_t depth, uint16_t station_id, Axis axis, uint16_t *created_station_id)
{
	if (railtype >= RAILTYPE_END) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	CommandCost depth_ret = CheckUndergroundDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;
	if (depth < GetMetroDeepestStationZ()) {
		CommandCost too_deep(STR_ERROR_UNDERGROUND_STATION_TOO_DEEP);
		too_deep.SetEncodedMessage(GetEncodedString(STR_ERROR_UNDERGROUND_STATION_TOO_DEEP, GetMetroDeepestStationZ()));
		return too_deep;
	}
	if (IsBlockedByVanillaTunnel(tile, depth)) return CommandCost(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);

	/* Check for existing slice at this depth. Allow replacing Track with Station. */
	SliceID existing = _multilayer_map.FindSliceAt(tile, depth);
	/* A platform where there is none yet: not under a house or water that reaches down to here.
	 * (A platform already there or something else at this depth is reported as such below.) */
	if (existing == INVALID_SLICE_ID || _multilayer_map.GetSlice(existing).kind == SliceKind::Track) {
		CommandCost clearance = CheckUndergroundClearance(tile, depth);
		if (clearance.Failed()) return clearance;
	}
	if (existing != INVALID_SLICE_ID) {
		const TileSlice &ex_slice = _multilayer_map.GetSlice(existing);
		if (ex_slice.kind == SliceKind::Track) {
			/* Only the owner may turn its track into a platform. */
			CommandCost owner_ret = CheckUndergroundSliceOwnership(ex_slice, tile);
			if (owner_ret.Failed()) return owner_ret;
			/* Will replace Track with StationTile вЂ” inherit railtype if not specified. */
			if (railtype >= RAILTYPE_END) {
				railtype = static_cast<RailType>(ex_slice.track.railtype);
			}
			/* Guard against corrupted/uninitialised track slices. */
			if (railtype >= RAILTYPE_END) return CMD_ERROR;
			/* Don't replace a reserved track slice вЂ” a train is using it. */
			if (ex_slice.track.reserved != 0) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
			/* Replacing frees the track slice; refuse while a train stands on it. */
			if (UndergroundSliceInUse(tile, existing)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
			if (flags.Test(DoCommandFlag::Execute)) {
				UpdateUndergroundSliceInfrastructure(ex_slice, -1);
				_multilayer_map.RemoveSliceFromColumn(tile, existing);
				_multilayer_map.FreeSlice(existing);
			}
		} else if (ex_slice.kind == SliceKind::StationTile) {
			return CommandCost(STR_ERROR_ALREADY_BUILT);
		} else {
			return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY); /* Something else at this depth. */
		}
	}

	/* Validate existing station ids; the dedicated sentinel creates a new station. */
	if (station_id != NEW_UNDERGROUND_STATION_ID && !Station::IsValidID(StationID(station_id))) return CMD_ERROR;

	/* If station_id is the new-station sentinel, we need to create one. */
	if (station_id == NEW_UNDERGROUND_STATION_ID && !Station::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_STATIONS_LOADING);

	TrackBits platform_tracks = (axis == AXIS_X) ? TRACK_BIT_X : TRACK_BIT_Y;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Create new Station if needed. */
		if (station_id == NEW_UNDERGROUND_STATION_ID) {
			Station *st = Station::Create(tile);
			_station_kdtree.Insert(st->index);
			st->town = ClosestTownFromTile(tile, UINT_MAX);
			st->string_id = GenerateStationName(st, tile, StationNaming::Rail);
			st->owner = _current_company;
			st->rect.BeforeAddTile(tile, StationRect::ADD_FORCE);
			st->AddFacility(StationFacility::Train, tile);
			/* Set train_station to valid 1x1 area at surface tile above the platform.
			 * Not a real station tile, but prevents INVALID_TILE assertion crashes
			 * when station code iterates train_station. Surface code filters by
			 * IsTileType(Station) and will skip this tile safely. */
			st->train_station = TileArea(tile, 1, 1);
			if (Company::IsValidID(_current_company)) {
				st->town->have_ratings.Set(_current_company);
			}
			station_id = st->index.base();
			st->UpdateVirtCoord();
		} else {
			/* Extend existing station rect. */
			Station *st = Station::Get(StationID(station_id));
			st->rect.BeforeAddTile(tile, StationRect::ADD_FORCE);
			if (!st->facilities.Test(StationFacility::Train)) {
				st->AddFacility(StationFacility::Train, tile);
				st->train_station = TileArea(tile, 1, 1);
			} else if (st->train_station.tile == INVALID_TILE) {
				st->train_station = TileArea(tile, 1, 1);
			}
		}

		SliceID sid = _multilayer_map.AllocateSlice();
		TileSlice &s = _multilayer_map.GetSlice(sid);
		s.kind = SliceKind::StationTile;
		s.span = {static_cast<int16_t>(depth + 1), depth};
		s.owner = _current_company.base();
		s.station.station_id = station_id;
		s.station.role = 0; /* Platform. */
		s.station.tracks = static_cast<uint8_t>(platform_tracks);
		s.station.railtype = static_cast<uint8_t>(railtype);
		s.station.reserved = 0;
		s.station.paid_soil = GetUndergroundPaidSoil(UndergroundCostKind::Station, tile, depth);

		if (!_multilayer_map.InsertSlice(tile, sid)) {
			_multilayer_map.FreeSlice(sid);
			return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
		}
		UpdateUndergroundSliceInfrastructure(_multilayer_map.GetSlice(sid), 1);

		/* Register in StationComplex. */
		StationComplex &sc = GetOrCreateStationComplex(station_id);
		sc.station_id = station_id;
		StationComplexNode node;
		node.ref = TileRef{tile, sid};
		node.role = StationRole::Platform;
		node.station_id = station_id;
		node.depth = depth;
		sc.nodes.push_back(node);

		/* Platforms give no catchment of their own: passengers and cargo only reach the
		 * station through exits (pavilions, underpasses) the player builds. The station
		 * rect changed though, so refresh the catchment. */
		Station::Get(StationID(station_id))->RecomputeCatchment();
		MarkTileDirtyByTile(tile);

		/* Mark adjacent platform tiles dirty so their section visuals update. */
		TileIndexDiff delta = TileOffsByAxis(axis);
		if (IsValidTile(tile + delta)) MarkTileDirtyByTile(tile + delta);
		if (IsValidTile(tile - delta)) MarkTileDirtyByTile(tile - delta);

		if (created_station_id != nullptr) *created_station_id = station_id;
	}

	return CommandCost(EXPENSES_CONSTRUCTION, GetUndergroundBuildCost(UndergroundCostKind::Station, railtype, tile, depth));
}

/** Per-axis distance from a tile to a tile area (0 inside it). */
static std::pair<uint, uint> AxisDistanceToArea(TileArea area, TileIndex tile)
{
	auto axis_dist = [](uint v, uint lo, uint len) -> uint {
		if (v < lo) return lo - v;
		if (v >= lo + len) return v - (lo + len - 1);
		return 0;
	};
	return {axis_dist(TileX(tile), TileX(area.tile), area.w), axis_dist(TileY(tile), TileY(area.tile), area.h)};
}

/** Add a station id to a list unless it is there already. */
static void AddUniqueStation(std::vector<StationID> &list, StationID id)
{
	if (std::ranges::find(list, id) == list.end()) list.push_back(id);
}

/** Get the platform-owning station of a company for an underground station complex, or nullptr. */
static const Station *GetOwnUndergroundStation(uint16_t station_id, const StationComplex &sc, Owner owner)
{
	const Station *st = Station::GetIfValid(StationID(station_id));
	if (st == nullptr || st->owner != owner) return nullptr;
	if (sc.GetPlatforms().empty()) return nullptr;
	return st;
}

/**
 * Find the own underground stations a platform area would join automatically:
 * those with a platform on the same level directly next to the area
 * (DistanceManhattan 1 from an area tile).
 * @param area  Surface area of the new platforms.
 * @param depth Level of the new platforms.
 * @param owner Company building.
 * @return Distinct station ids, in the order they were found.
 */
std::vector<StationID> FindAdjacentUndergroundStations(TileArea area, int16_t depth, Owner owner)
{
	std::vector<StationID> result;
	for (const auto &[station_id, sc] : _station_complexes) {
		if (GetOwnUndergroundStation(station_id, sc, owner) == nullptr) continue;
		for (const StationComplexNode *n : sc.GetPlatforms()) {
			if (n->depth != depth || !IsValidTile(n->ref.tile)) continue;
			auto [dx, dy] = AxisDistanceToArea(area, n->ref.tile);
			if (dx + dy == 1) {
				AddUniqueStation(result, StationID(station_id));
				break;
			}
		}
	}
	std::ranges::sort(result);
	return result;
}

/**
 * Find the own underground stations with a platform on another level within
 * the columns of an area or the ring of columns around it.
 * @param area  Surface area of the new platforms.
 * @param depth Level of the new platforms (excluded from the search).
 * @param owner Company building.
 * @return Distinct station ids, sorted.
 */
std::vector<StationID> FindUndergroundStationsNearColumns(TileArea area, int16_t depth, Owner owner)
{
	std::vector<StationID> result;
	for (const auto &[station_id, sc] : _station_complexes) {
		if (GetOwnUndergroundStation(station_id, sc, owner) == nullptr) continue;
		for (const StationComplexNode *n : sc.GetPlatforms()) {
			if (n->depth == depth || !IsValidTile(n->ref.tile)) continue;
			auto [dx, dy] = AxisDistanceToArea(area, n->ref.tile);
			if (std::max(dx, dy) <= 1) {
				AddUniqueStation(result, StationID(station_id));
				break;
			}
		}
	}
	std::ranges::sort(result);
	return result;
}

/**
 * Find the own underground stations a platform area may be (distant) joined
 * to: stations with a platform on any level whose station rectangle can take
 * the area without exceeding the station spread.
 * @param area  Surface area of the new platforms.
 * @param owner Company building.
 * @return Station ids, nearest platform first.
 */
std::vector<StationID> FindUndergroundJoinCandidates(TileArea area, Owner owner)
{
	std::vector<std::pair<uint, StationID>> found;
	for (const auto &[station_id, sc] : _station_complexes) {
		const Station *st = GetOwnUndergroundStation(station_id, sc, owner);
		if (st == nullptr) continue;
		if (StationRect(st->rect).BeforeAddRect(area.tile, area.w, area.h, StationRect::ADD_TEST).Failed()) continue;

		uint best = UINT_MAX;
		for (const StationComplexNode *n : sc.GetPlatforms()) {
			if (!IsValidTile(n->ref.tile)) continue;
			auto [dx, dy] = AxisDistanceToArea(area, n->ref.tile);
			best = std::min(best, std::max(dx, dy));
		}
		if (best > _settings_game.station.station_spread) continue;
		found.emplace_back(best, StationID(station_id));
	}
	std::ranges::sort(found);

	std::vector<StationID> result;
	for (const auto &entry : found) result.push_back(entry.second);
	return result;
}

/**
 * Find the own underground stations a surface exit on a tile may belong to:
 * those with a platform within construction.max_station_exit_distance
 * (DistanceMax), as CmdBuildExitSurface enforces.
 * @param tile  Tile the exit is to be built on.
 * @param owner Company building.
 * @return Station ids, nearest platform first.
 */
std::vector<StationID> FindUndergroundStationsForExit(TileIndex tile, Owner owner)
{
	std::vector<std::pair<uint, StationID>> found;
	const uint limit = _settings_game.construction.max_station_exit_distance;
	for (const auto &[station_id, sc] : _station_complexes) {
		if (GetOwnUndergroundStation(station_id, sc, owner) == nullptr) continue;
		uint distance = UINT_MAX;
		for (const StationComplexNode *n : sc.GetPlatforms()) {
			if (IsValidTile(n->ref.tile)) distance = std::min(distance, DistanceMax(tile, n->ref.tile));
		}
		if (distance <= limit) found.emplace_back(distance, StationID(station_id));
	}
	std::ranges::sort(found);

	std::vector<StationID> result;
	for (const auto &entry : found) result.push_back(entry.second);
	return result;
}

/**
 * Build an underground station platform area at a given depth.
 * @param flags      Command flags.
 * @param tile       North-most surface tile of the platform area.
 * @param railtype   Rail type for the platforms.
 * @param depth      Underground depth.
 * @param station_id Station to join: an own station id (adjacent, or a distant join on any level when
 *                   station.distant_join_stations is on), UNDERGROUND_JOIN_ADJACENT_ID to join the one own
 *                   station with a platform next to the area on this level, or NEW_UNDERGROUND_STATION_ID
 *                   to build a separate station.
 * @param axis       Platform direction (AXIS_X or AXIS_Y).
 * @param numtracks  Number of parallel platforms (across the axis).
 * @param plat_len   Platform length (along the axis).
 * @return Cost or error.
 */
CommandCost CmdBuildUndergroundStation(DoCommandFlags flags, TileIndex tile, RailType railtype, int16_t depth, uint16_t station_id, Axis axis, uint8_t numtracks, uint8_t plat_len)
{
	if (numtracks == 0 || plat_len == 0) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	uint w = (axis == AXIS_X) ? plat_len : numtracks;
	uint h = (axis == AXIS_X) ? numtracks : plat_len;
	if (w > _settings_game.station.station_spread || h > _settings_game.station.station_spread) return CommandCost(STR_ERROR_STATION_TOO_SPREAD_OUT);

	/* Clip the area to the map, as the tile loop below does. */
	uint area_w = std::min<uint>(w, Map::SizeX() - TileX(tile));
	uint area_h = std::min<uint>(h, Map::SizeY() - TileY(tile));
	TileArea area(tile, area_w, area_h);

	/* Resolve which station the platforms join, like vanilla FindJoiningStation. */
	std::vector<StationID> adjacent = FindAdjacentUndergroundStations(area, depth, _current_company);
	if (station_id == UNDERGROUND_JOIN_ADJACENT_ID) {
		if (adjacent.size() > 1) return CommandCost(STR_ERROR_ADJOINS_MORE_THAN_ONE_EXISTING);
		station_id = adjacent.empty() ? NEW_UNDERGROUND_STATION_ID : adjacent.front().base();
	} else if (station_id != NEW_UNDERGROUND_STATION_ID) {
		const Station *st = Station::GetIfValid(StationID(station_id));
		if (st == nullptr) return CMD_ERROR;
		CommandCost ret = CheckOwnership(st->owner);
		if (ret.Failed()) return ret;
		/* Anything but a same-level neighbour is a distant join (other levels included). */
		bool is_adjacent = std::ranges::find(adjacent, st->index) != adjacent.end();
		if (!is_adjacent && !_settings_game.station.distant_join_stations) return CMD_ERROR;
	}

	/* Joining an existing station must keep it within the station spread. */
	if (station_id != NEW_UNDERGROUND_STATION_ID) {
		Station *st = Station::Get(StationID(station_id));
		CommandCost ret = st->rect.BeforeAddRect(area.tile, area.w, area.h, StationRect::ADD_TEST);
		if (ret.Failed()) return ret;
	}

	CommandCost total(EXPENSES_CONSTRUCTION);
	CommandCost last_error = CommandCost(STR_ERROR_OFF_EDGE_OF_MAP);
	bool built_any = false;

	for (uint dy = 0; dy < h; dy++) {
		for (uint dx = 0; dx < w; dx++) {
			TileIndex t = TileAddWrap(tile, dx, dy);
			if (t == INVALID_TILE) continue;

			uint16_t attached_id = NEW_UNDERGROUND_STATION_ID;
			CommandCost ret = BuildUndergroundStationTile(flags, t, railtype, depth, station_id, axis, &attached_id);
			if (ret.Failed()) {
				last_error = ret;
				continue;
			}
			built_any = true;
			total.AddCost(ret.GetCost());

			/* Join the remaining tiles to the station created by the first one. */
			if (station_id == NEW_UNDERGROUND_STATION_ID && attached_id != NEW_UNDERGROUND_STATION_ID) {
				station_id = attached_id;
			}
		}
	}

	if (!built_any) return last_error;
	return total;
}

/* Pavilion surface building helpers, implemented next to the vanilla road stop code in station_cmd.cpp. */
CommandCost BuildStationExitPavilion(DoCommandFlags flags, TileIndex tile, StationID station_id);
CommandCost RemoveStationExitPavilion(DoCommandFlags flags, TileIndex tile);

/**
 * Distance (DistanceMax) from a tile to the nearest platform of a station complex.
 * @param sc   The station complex.
 * @param tile Tile to measure from.
 * @return The distance, or UINT_MAX when the complex has no platform.
 */
static uint DistanceToNearestPlatform(const StationComplex &sc, TileIndex tile)
{
	uint best = UINT_MAX;
	for (const StationComplexNode &n : sc.nodes) {
		if (n.role != StationRole::Platform || !IsValidTile(n.ref.tile)) continue;
		best = std::min(best, DistanceMax(tile, n.ref.tile));
	}
	return best;
}

uint GetUndergroundExitDistanceToStation(uint16_t station_id, TileIndex tile)
{
	auto sc_it = _station_complexes.find(station_id);
	if (sc_it == _station_complexes.end()) return UINT_MAX;
	return DistanceToNearestPlatform(sc_it->second, tile);
}

/**
 * Check whether a tile is a road tile an underpass exit can be built on:
 * a plain road tile with one straight road piece (no junction, crossing,
 * depot or road stop); trams, if any, must follow the same axis.
 */
bool IsUnderpassRoadTile(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Road) || !IsNormalRoad(tile)) return false;
	RoadBits road = GetRoadBits(tile, RTT_ROAD);
	if (road != ROAD_X && road != ROAD_Y) return false;
	RoadBits tram = GetRoadBits(tile, RTT_TRAM);
	return tram == ROAD_NONE || tram == road;
}

/**
 * Build a surface exit for an underground station complex.
 * A pavilion turns a free surface tile into a bus station tile of the station;
 * an underpass is placed on a straight road tile that stays a road.
 * The exit must lie within construction.max_station_exit_distance tiles
 * (DistanceMax) of a platform of the station.
 * @param flags      Command flags.
 * @param tile       Surface tile for the exit.
 * @param station_id Station to attach to.
 * @param exit_type  StationExitType::Pavilion or StationExitType::Underpass.
 * @return Cost or error.
 */
CommandCost CmdBuildExitSurface(DoCommandFlags flags, TileIndex tile, uint16_t station_id, StationExitType exit_type)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (!IsExplicitStationExit(exit_type)) return CMD_ERROR;
	Station *st = Station::GetIfValid(StationID(station_id));
	if (st == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	auto sc_it = _station_complexes.find(station_id);
	if (sc_it == _station_complexes.end() || sc_it->second.GetPlatforms().empty()) return CommandCost(STR_ERROR_UNDERGROUND_NO_STATION_HERE);
	if (FindExplicitStationExitAt(tile) != nullptr) return CommandCost(STR_ERROR_UNDERGROUND_EXIT_ALREADY_HERE);

	if (DistanceToNearestPlatform(sc_it->second, tile) > _settings_game.construction.max_station_exit_distance) {
		return CommandCost(STR_ERROR_UNDERGROUND_EXIT_TOO_FAR_FROM_PLATFORM);
	}

	CommandCost cost(EXPENSES_CONSTRUCTION);
	if (exit_type == StationExitType::Pavilion) {
		ret = BuildStationExitPavilion(flags, tile, StationID(station_id));
		if (ret.Failed()) return ret;
		cost.AddCost(ret.GetCost());
	} else {
		if (!IsUnderpassRoadTile(tile)) return CommandCost(STR_ERROR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD);
		cost.AddCost(_price[Price::BuildStationBus]);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Register exit in StationComplex. */
		SliceID surface_sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Surface);
		assert(surface_sid != INVALID_SLICE_ID); /* Every valid tile must have a Surface slice. */
		StationComplex &sc = GetOrCreateStationComplex(station_id);
		StationComplexNode node;
		node.ref = TileRef{tile, surface_sid};
		node.role = StationRole::ExitSurface;
		node.station_id = station_id;
		node.depth = 0;
		node.exit_type = exit_type;
		sc.nodes.push_back(node);
		InvalidateStationExitCache();

		st->rect.BeforeAddTile(tile, StationRect::ADD_FORCE);
		st->RecomputeCatchment();
		MarkTileDirtyByTile(tile);
	}

	return cost;
}

/**
 * Remove a surface exit (pavilion or underpass) of an underground station,
 * together with the bus station tile of a pavilion.
 * @param flags Command flags.
 * @param tile  Surface tile of the exit.
 * @return Cost or error.
 */
CommandCost CmdRemoveExitSurface(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	const StationComplexNode *exit_node = FindExplicitStationExitAt(tile);
	if (exit_node == nullptr) return CommandCost(STR_ERROR_UNDERGROUND_NO_EXIT_HERE);

	uint16_t station_id = exit_node->station_id;
	StationExitType exit_type = exit_node->exit_type;
	Station *st = Station::GetIfValid(StationID(station_id));
	if (st != nullptr) {
		CommandCost ret = CheckOwnership(st->owner);
		if (ret.Failed()) return ret;
	}

	bool has_building = exit_type == StationExitType::Pavilion && IsStationExitPavilionTile(tile);
	CommandCost cost(EXPENSES_CONSTRUCTION);
	if (has_building) {
		/* Test only here: the node must be gone before the tile is cleared in execution. */
		CommandCost ret = RemoveStationExitPavilion(DoCommandFlags{flags}.Reset(DoCommandFlag::Execute), tile);
		if (ret.Failed()) return ret;
		cost.AddCost(ret.GetCost());
	} else {
		cost.AddCost(_price[Price::ClearStationBus]);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		ForgetExplicitStationExitAt(tile);
		if (has_building) {
			RemoveStationExitPavilion(flags, tile);
		} else if (st != nullptr) {
			st->RecomputeCatchment();
		}
		MarkTileDirtyByTile(tile);

		/* The last exit of a station whose platforms are gone takes the station with it. */
		auto sc_it = _station_complexes.find(station_id);
		if (sc_it == _station_complexes.end() || sc_it->second.GetPlatforms().empty()) UpdateStationAfterPlatformRemoval(station_id);
	}

	return cost;
}

/**
 * Build a ramp connecting two underground depth levels.
 * @param flags      Command flags.
 * @param tile       Surface tile position.
 * @param railtype   Rail type.
 * @param dir        Direction of travel from \a from_depth to \a to_depth: the ramp descends towards it when \a to_depth is the lower level, else rises towards it.
 * @param from_depth Entry level (e.g. -1).
 * @param to_depth   Exit level, from_depth - 1 or from_depth + 1.
 * @return Cost or error.
 */
CommandCost CmdBuildRamp(DoCommandFlags flags, TileIndex tile, RailType railtype, DiagDirection dir, int16_t from_depth, int16_t to_depth)
{
	if (railtype >= RAILTYPE_END) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (!IsValidDiagDirection(dir)) return CMD_ERROR;
	if (to_depth != from_depth - 1 && to_depth != from_depth + 1) return CommandCost(STR_ERROR_UNDERGROUND_RAMP_MUST_SPAN_ADJACENT_LEVELS);

	/* A ramp up to a level that is no longer underground leads out to the
	 * surface: on flat ground that is a portal, its open cut rising the last
	 * level, turned back into the tunnel the line comes from. */
	if (to_depth == from_depth + 1 && GetTileSlope(tile) == SLOPE_FLAT && to_depth >= GetMetroSurfaceZ(tile) && IsValidMetroZ(tile, from_depth)) {
		return Command<Commands::BuildPortal>::Do(flags, tile, railtype, ReverseDiagDir(dir), from_depth);
	}

	CommandCost depth_ret = CheckUndergroundDepth(tile, std::max(from_depth, to_depth));
	if (depth_ret.Succeeded()) depth_ret = CheckUndergroundDepth(tile, std::min(from_depth, to_depth));
	if (depth_ret.Failed()) return depth_ret;
	if (IsBlockedByVanillaTunnel(tile, from_depth) || IsBlockedByVanillaTunnel(tile, to_depth)) {
		return CommandCost(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);
	}

	/* Ramp span covers both depths. */
	int16_t z_bot = std::min(from_depth, to_depth);
	int16_t z_top = std::max(from_depth, to_depth) + 1;

	/* Check for existing slices in the ramp's ZSpan. */
	for (int16_t z = z_bot; z < z_top; z++) {
		SliceID existing = _multilayer_map.FindSliceAt(tile, z);
		if (existing != INVALID_SLICE_ID) return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
	}
	/* The upper end is the shallower one; it alone can meet a foundation or water. */
	CommandCost clearance = CheckUndergroundClearance(tile, std::max(from_depth, to_depth));
	if (clearance.Failed()) return clearance;

	if (flags.Test(DoCommandFlag::Execute)) {
		SliceID sid = _multilayer_map.AllocateSlice();
		TileSlice &s = _multilayer_map.GetSlice(sid);
		s.kind = SliceKind::Ramp;
		s.span = {z_top, z_bot};
		s.owner = _current_company.base();
		s.ramp.entry_depth = from_depth;
		s.ramp.exit_depth = to_depth;
		s.ramp.direction = static_cast<uint8_t>(dir);
		s.ramp.railtype = static_cast<uint8_t>(railtype);
		s.ramp.paid_soil = GetUndergroundPaidSoil(UndergroundCostKind::Ramp, tile, z_bot);

		if (!_multilayer_map.InsertSlice(tile, sid)) {
			_multilayer_map.FreeSlice(sid);
			return CommandCost(STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY);
		}
		UpdateUndergroundSliceInfrastructure(_multilayer_map.GetSlice(sid), 1);
		MarkTileDirtyByTile(tile);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, GetUndergroundBuildCost(UndergroundCostKind::Ramp, railtype, tile, z_bot));
}

/**
 * Build, turn round or convert a signal on an underground track, like
 * CmdBuildSingleSignal does on the surface: a new path signal faces one way,
 * any other new signal both ways; building again on a signal cycles its side
 * (both -> one -> the other for block signals, one -> the other for path
 * signals), Ctrl cycles its type between cycle_start and cycle_stop, and the
 * convert mode changes type and variant (Ctrl: only the variant).
 * @param flags   Command flags.
 * @param tile    Surface tile position.
 * @param track   Track to place the signal on.
 * @param depth   Underground depth.
 * @param sigtype Type of a new or converted signal.
 * @param sigvar  Variant (light or semaphore) of a new or converted signal.
 * @param convert_signal Convert the signal on the track instead of building or turning one.
 * @param ctrl_pressed Ctrl: build the other variant, cycle the type, or (converting) toggle the variant.
 * @param cycle_start First signal type Ctrl cycles through.
 * @param cycle_stop Last signal type Ctrl cycles through.
 * @param signals_copy Signal presence bits to set on the track (dragging); 0 to build or cycle normally.
 * @return Cost or error.
 */
CommandCost CmdBuildUndergroundSignal(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth, SignalType sigtype, SignalVariant sigvar, bool convert_signal, bool ctrl_pressed, SignalType cycle_start, SignalType cycle_stop, uint8_t signals_copy)
{
	if (!IsValidTrack(track)) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (sigtype > SIGTYPE_LAST || sigvar > SIG_SEMAPHORE) return CMD_ERROR;
	if (cycle_start > cycle_stop || cycle_stop > SIGTYPE_LAST) return CMD_ERROR;
	/* Protect against invalid signal copying. */
	if (signals_copy != 0 && (signals_copy & SignalOnTrack(track)) == 0) return CMD_ERROR;

	if (ctrl_pressed) sigvar = static_cast<SignalVariant>(sigvar ^ SIG_SEMAPHORE);
	CommandCost depth_ret = CheckUndergroundExistingDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;

	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	/* Plain track, or a ramp (its one track; either of its levels names it). */
	TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (slice.kind == SliceKind::Ramp) {
		if (track != GetRampTrack(slice.ramp)) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	} else if (slice.kind != SliceKind::Track || !(slice.track.tracks & TrackToTrackBits(track))) {
		return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}

	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;

	/* Like on the surface no signal goes under a train, and none into the
	 * middle of a reserved path: reservations end at signals, so one placed
	 * inside a path would cut it in two. */
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	if (IsUndergroundTrackReservedByOther(TileRef{tile, sid}, track, VehicleID::Invalid())) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);

	/* Like on the surface: only on track combinations without crossing pieces. */
	if (slice.kind == SliceKind::Track && TracksOverlap(static_cast<TrackBits>(slice.track.tracks))) return CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);

	uint8_t track_signal_mask = SignalOnTrack(track);
	bool has_signal = HasUndergroundSignalOnTrack(slice, track);

	/* You can not convert a signal if no signal is on track. */
	if (convert_signal && !has_signal) return CommandCost(STR_ERROR_THERE_ARE_NO_SIGNALS);

	/* Building costs, changing the variant costs as much; turning a signal round or changing its type is free. */
	bool variant_change = has_signal && (signals_copy != 0 || convert_signal) &&
			(ctrl_pressed || GetUndergroundSignalVariant(slice, track) != sigvar);
	CommandCost cost = (!has_signal || variant_change) ?
		CommandCost(EXPENSES_CONSTRUCTION, GetUndergroundBuildCost(UndergroundCostKind::Signal, INVALID_RAILTYPE, tile, depth)) :
		CommandCost(EXPENSES_CONSTRUCTION, static_cast<Money>(0));

	if (flags.Test(DoCommandFlag::Execute)) {
		UndergroundSliceInfrastructure before = GetUndergroundSliceInfrastructure(slice);
		/* The same on plain track and on a ramp. */
		auto apply = [&](auto &tp) {
			auto make_pbs_one_way = [&]() {
				if (IsPbsSignal(GetUndergroundSignalType(tp, track)) && (tp.signal_mask & track_signal_mask) == track_signal_mask) {
					tp.signal_mask = (tp.signal_mask & ~track_signal_mask) | KillFirstBit(track_signal_mask);
				}
			};

			if (signals_copy != 0) {
				/* Dragging: copy the direction of the first signal of the stretch. */
				tp.signal_mask = (tp.signal_mask & ~track_signal_mask) | (signals_copy & track_signal_mask);
				SetUndergroundSignalType(tp, track, sigtype);
				SetUndergroundSignalVariant(tp, track, sigvar);
			} else if (!has_signal) {
				/* A new path signal faces one way, any other signal both ways. */
				tp.signal_mask |= IsPbsSignal(sigtype) ? KillFirstBit(track_signal_mask) : track_signal_mask;
				SetUndergroundSignalType(tp, track, sigtype);
				SetUndergroundSignalVariant(tp, track, sigvar);
			} else if (convert_signal) {
				if (ctrl_pressed) {
					/* Toggle the variant only: light <-> semaphore. */
					SetUndergroundSignalVariant(tp, track, GetUndergroundSignalVariant(tp, track) == SIG_ELECTRIC ? SIG_SEMAPHORE : SIG_ELECTRIC);
				} else {
					SetUndergroundSignalType(tp, track, sigtype);
					SetUndergroundSignalVariant(tp, track, sigvar);
					make_pbs_one_way();
				}
			} else if (ctrl_pressed) {
				/* Cycle the type between cycle_start and cycle_stop. */
				SignalType next = static_cast<SignalType>(GetUndergroundSignalType(tp, track) + 1);
				if (next < cycle_start || next > cycle_stop) next = cycle_start;
				SetUndergroundSignalType(tp, track, next);
				make_pbs_one_way();
			} else {
				CycleUndergroundSignalSide(tp, track);
			}
		};
		if (slice.kind == SliceKind::Ramp) {
			apply(slice.ramp);
		} else {
			apply(slice.track);
		}

		ApplyUndergroundInfrastructureDelta(slice.owner, before, GetUndergroundSliceInfrastructure(slice));
		Debug(misc, 4, "Underground signal placed: tile={} track={} depth={} type={} variant={} mask=0x{:02X}", tile.base(), (int)track, depth,
			(int)GetUndergroundSignalType(slice, track), (int)GetUndergroundSignalVariant(slice, track), GetUndergroundSignalMask(slice));
		MarkTileDirtyByTile(tile);
		/* One-way signals decide which portals a train can run between. */
		RecomputePortalConnections();
	}

	return cost;
}

/**
 * Remove the signals from an underground track or ramp.
 * @param flags   Command flags.
 * @param tile    Surface tile position.
 * @param track   Track to remove the signal from.
 * @param depth   Underground depth.
 * @return Cost or error.
 */
CommandCost CmdRemoveUndergroundSignal(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth)
{
	if (!IsValidTrack(track)) return CMD_ERROR;
	if (!IsValidTile(tile)) return CMD_ERROR;
	CommandCost depth_ret = CheckUndergroundExistingDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;

	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (!CanSliceCarrySignals(slice)) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	/* The track must be there: the signal bits of crossing tracks (X and Y,
	 * a diagonal and the one across it) are the same, so a drag along one
	 * track would otherwise take the signal off the other. */
	bool has_track = slice.kind == SliceKind::Ramp ? track == GetRampTrack(slice.ramp) :
		(slice.track.tracks & TrackToTrackBits(track)) != 0;
	if (!has_track) return CommandCost(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);

	uint8_t track_signal_mask = SignalOnTrack(track);
	if (!(GetUndergroundSignalMask(slice) & track_signal_mask)) return CommandCost(STR_ERROR_THERE_ARE_NO_SIGNALS);

	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;

	/* As when building: not from under a train, and not from the middle of a
	 * reserved path, whose train relies on the signal to stop it there. */
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	if (IsUndergroundTrackReservedByOther(TileRef{tile, sid}, track, VehicleID::Invalid())) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);

	if (flags.Test(DoCommandFlag::Execute)) {
		UndergroundSliceInfrastructure before = GetUndergroundSliceInfrastructure(slice);
		if (slice.kind == SliceKind::Ramp) {
			slice.ramp.signal_mask &= ~track_signal_mask;
		} else {
			slice.track.signal_mask &= ~track_signal_mask;
		}
		ApplyUndergroundInfrastructureDelta(slice.owner, before, GetUndergroundSliceInfrastructure(slice));
		MarkTileDirtyByTile(tile);
		/* One-way signals decide which portals a train can run between. */
		RecomputePortalConnections();
	}

	return CommandCost(EXPENSES_CONSTRUCTION, -GetUndergroundRemoveRefund(UndergroundCostKind::Signal, INVALID_RAILTYPE, tile, depth));
}

/**
 * Remove the underground signals along a stretch of track (drag), like
 * CmdRemoveSignalTrack does on the surface: every signal on the line goes,
 * tiles without one are skipped.
 * @param flags      Command flags.
 * @param end_tile   Last tile of the stretch.
 * @param start_tile First tile of the stretch.
 * @param track      Track on the first tile.
 * @param depth      Underground depth.
 * @return Cost or error.
 */
CommandCost CmdRemoveUndergroundSignalTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth)
{
	if (!IsValidTrack(track)) return CMD_ERROR;
	if (!IsValidTile(start_tile) || !IsValidTile(end_tile)) return CMD_ERROR;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateUndergroundAutoDrag(&trackdir, start_tile, end_tile);
	if (ret.Failed()) return ret;

	CommandCost total(EXPENSES_CONSTRUCTION);
	bool removed_any = false;
	CommandCost last_error = CommandCost(STR_ERROR_THERE_ARE_NO_SIGNALS);
	TileIndex tile = start_tile;
	for (;;) {
		CommandCost sig = CmdRemoveUndergroundSignal(flags, tile, TrackdirToTrack(trackdir), depth);
		if (sig.Succeeded()) {
			total.AddCost(sig.GetCost());
			removed_any = true;
		} else {
			/* When nothing could be removed, a signal that had to stay (a train,
			 * another owner) tells more than tiles without signals or track. */
			auto nothing_here = [](StringID msg) { return msg == STR_ERROR_THERE_ARE_NO_SIGNALS || msg == STR_ERROR_THERE_IS_NO_RAILROAD_TRACK; };
			if (nothing_here(last_error.GetErrorMessage()) && !nothing_here(sig.GetErrorMessage())) last_error = sig;
		}

		if (tile == end_tile) break;

		tile = AddTileIndexDiffCWrap(tile, _ug_trackdelta[trackdir]);
		if (tile == INVALID_TILE) break;
		if (!IsDiagonalTrackdir(trackdir)) ToggleBit(trackdir, 0);
	}

	if (!removed_any) return last_error;
	return total;
}

/**
 * Remove an underground station platform.
 * @param flags Command flags.
 * @param tile  Surface tile position.
 * @param depth Underground depth.
 * @return Cost or error.
 */
CommandCost CmdRemoveUndergroundStation(DoCommandFlags flags, TileIndex tile, int16_t depth)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	CommandCost depth_ret = CheckUndergroundExistingDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;

	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_UNDERGROUND_NO_STATION_HERE);

	TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (slice.kind != SliceKind::StationTile) return CommandCost(STR_ERROR_UNDERGROUND_NO_STATION_HERE);

	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;

	/* Refuse to remove a reserved platform вЂ” a train is using or about to use it. */
	if (slice.station.reserved != 0) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);

	RailType railtype = static_cast<RailType>(slice.station.railtype);
	uint16_t paid_soil = slice.station.paid_soil;

	if (flags.Test(DoCommandFlag::Execute)) {
		uint16_t station_id = slice.station.station_id;
		UpdateUndergroundSliceInfrastructure(slice, -1);

		/* Remove from StationComplex. */
		std::vector<TileIndex> exits;
		bool last_platform = true;
		auto sc_it = _station_complexes.find(station_id);
		if (sc_it != _station_complexes.end()) {
			std::erase_if(sc_it->second.nodes, [&](const StationComplexNode &n) {
				return n.ref.tile == tile && n.role == StationRole::Platform && n.depth == depth;
			});
			InvalidateStationExitCache();
			/* The exits of a station go with its last platform. */
			last_platform = sc_it->second.GetPlatforms().empty();
			if (last_platform) {
				for (const StationComplexNode *n : sc_it->second.GetExits()) {
					if (n->IsExplicitExit()) exits.push_back(n->ref.tile);
				}
			}
		}

		/* Determine platform axis before freeing the slice. */
		TrackBits removed_tracks = static_cast<TrackBits>(slice.station.tracks);
		Axis removed_axis = (removed_tracks & TRACK_BIT_X) ? AXIS_X : AXIS_Y;

		_multilayer_map.RemoveSliceFromColumn(tile, sid);
		_multilayer_map.FreeSlice(sid);
		MarkTileDirtyByTile(tile);

		/* Mark adjacent platform tiles dirty so their section visuals update. */
		TileIndexDiff delta = TileOffsByAxis(removed_axis);
		if (IsValidTile(tile + delta)) MarkTileDirtyByTile(tile + delta);
		if (IsValidTile(tile - delta)) MarkTileDirtyByTile(tile - delta);

		/* The exits go free of charge: a test run of an area removal cannot know which
		 * platform is the last one, and its cost must match the execution. An exit that
		 * cannot go now (a bus on the pavilion) stays, and can be demolished later. */
		for (TileIndex exit_tile : exits) CmdRemoveExitSurface(flags, exit_tile);
		if (last_platform) UpdateStationAfterPlatformRemoval(station_id);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, -GetUndergroundRemoveRefund(UndergroundCostKind::Station, railtype, tile, depth, paid_soil));
}

/**
 * Check whether the portal on a tile may be removed by the current company.
 * @param tile Portal tile.
 * @param sid  The portal slice on it.
 * @return An error, or success.
 */
static CommandCost CheckPortalRemovable(TileIndex tile, SliceID sid)
{
	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;

	/* A train diving into or emerging from the portal holds its slice. */
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	/* A train that holds the tunnel behind the mouth (standing in it, or waiting
	 * on the surface with its underground path reserved) is on its way through. */
	{
		TileIndex inside = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(GetPortalDiveDirection(tile, slice)));
		SliceID inside_sid = inside == INVALID_TILE ? INVALID_SLICE_ID : _multilayer_map.FindSliceAt(inside, slice.span.z_bot);
		if (inside_sid != INVALID_SLICE_ID) {
			const TileSlice &is = _multilayer_map.GetSlice(inside_sid);
			if ((is.kind == SliceKind::Track && is.track.reserved != 0) || (is.kind == SliceKind::StationTile && is.station.reserved != 0)) {
				return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
			}
		}
	}
	return CommandCost();
}

/**
 * Free a portal slice whose surface rail has been removed, and refund part of its build cost.
 * @param flags Command flags.
 * @param tile  Portal tile.
 * @param sid   The portal slice on it.
 * @return The refund.
 */
static CommandCost FreePortalSlice(DoCommandFlags flags, TileIndex tile, SliceID sid)
{
	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	RailType railtype = static_cast<RailType>(slice.portal.railtype);
	int16_t level = slice.span.z_bot;
	uint16_t paid_soil = slice.portal.paid_soil;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* The surface rail was uncounted by RemoveRail; this is the portal slice itself. */
		UpdateUndergroundSliceInfrastructure(slice, -1);
		_multilayer_map.RemoveSliceFromColumn(tile, sid);
		_multilayer_map.FreeSlice(sid);
		MarkTileDirtyByTile(tile);
		RecomputePortalConnections();
	}

	return CommandCost(EXPENSES_CONSTRUCTION, -GetUndergroundRemoveRefund(UndergroundCostKind::Portal, railtype, tile, level, paid_soil));
}

/**
 * Remove a portal together with the surface rail CmdBuildPortal laid on its tile.
 * @param flags Command flags.
 * @param tile  Portal tile.
 * @return Cost (refund) or an error.
 */
CommandCost CmdRemovePortal(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (!_multilayer_map.IsAllocated()) return CommandCost(STR_ERROR_UNDERGROUND_NO_PORTAL_HERE);

	SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_UNDERGROUND_NO_PORTAL_HERE);

	CommandCost check = CheckPortalRemovable(tile, sid);
	if (check.Failed()) return check;
	DiagDirection dir = static_cast<DiagDirection>(_multilayer_map.GetSlice(sid).portal.direction);

	CommandCost total(EXPENSES_CONSTRUCTION);

	/* Remove the surface rail the portal created. Extra tracks a player added
	 * to the tile stay; if the portal fell back to the perpendicular track
	 * (see CmdBuildPortal), that one is the portal's track. */
	if (IsTileType(tile, TileType::Railway) && IsPlainRail(tile)) {
		TrackBits present = GetTrackBits(tile);
		CommandCost ret = EnsureNoTrainOnTrackBits(tile, present);
		if (ret.Failed()) return ret;

		TrackBits to_remove = present & AxisToTrackBits(DiagDirToAxis(dir));
		if (to_remove == TRACK_BIT_NONE) to_remove = present;

		TileIndex saved_guard = _portal_surface_rail_removal_tile;
		_portal_surface_rail_removal_tile = tile;
		for (Track track : SetTrackBitIterator(to_remove)) {
			ret = Command<Commands::RemoveRail>::Do(flags, tile, track);
			if (ret.Failed()) break;
			total.AddCost(ret.GetCost());
		}
		_portal_surface_rail_removal_tile = saved_guard;
		if (ret.Failed()) return ret;
	}

	total.AddCost(FreePortalSlice(flags, tile, sid).GetCost());
	return total;
}

CommandCost ClearPortalSurfaceTile(DoCommandFlags flags, TileIndex tile, const std::function<CommandCost()> &clear_rail)
{
	SliceID sid = _multilayer_map.IsAllocated() ? _multilayer_map.FindSliceByKind(tile, SliceKind::Portal) : INVALID_SLICE_ID;
	if (sid == INVALID_SLICE_ID || flags.Test(DoCommandFlag::Bankrupt)) return clear_rail();

	CommandCost check = CheckPortalRemovable(tile, sid);
	if (check.Failed()) return check;

	TileIndex saved_guard = _portal_surface_rail_removal_tile;
	_portal_surface_rail_removal_tile = tile;
	CommandCost total = clear_rail();
	_portal_surface_rail_removal_tile = saved_guard;
	if (total.Failed()) return total;

	total.AddCost(FreePortalSlice(flags, tile, sid).GetCost());
	return total;
}

/**
 * Remove a ramp between two underground depth levels.
 * @param flags Command flags.
 * @param tile  Surface tile position.
 * @param depth Either depth level the ramp spans.
 * @return Cost (refund) or an error.
 */
CommandCost CmdRemoveRamp(DoCommandFlags flags, TileIndex tile, int16_t depth)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (!_multilayer_map.IsAllocated()) return CommandCost(STR_ERROR_UNDERGROUND_NO_RAMP_HERE);
	CommandCost depth_ret = CheckUndergroundExistingDepth(tile, depth);
	if (depth_ret.Failed()) return depth_ret;

	SliceID sid = _multilayer_map.FindSliceAt(tile, depth);
	if (sid == INVALID_SLICE_ID) return CommandCost(STR_ERROR_UNDERGROUND_NO_RAMP_HERE);

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (slice.kind != SliceKind::Ramp) return CommandCost(STR_ERROR_UNDERGROUND_NO_RAMP_HERE);
	CommandCost owner_ret = CheckUndergroundSliceOwnership(slice, tile);
	if (owner_ret.Failed()) return owner_ret;
	if (UndergroundSliceInUse(tile, sid)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
	if (slice.ramp.reserved != 0) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);

	RailType rt = static_cast<RailType>(slice.ramp.railtype);
	if (rt >= RAILTYPE_END) rt = RAILTYPE_RAIL;
	int16_t z_bot = slice.span.z_bot;
	uint16_t paid_soil = slice.ramp.paid_soil;

	if (flags.Test(DoCommandFlag::Execute)) {
		UpdateUndergroundSliceInfrastructure(slice, -1);
		_multilayer_map.RemoveSliceFromColumn(tile, sid);
		_multilayer_map.FreeSlice(sid);
		MarkTileDirtyByTile(tile);
		RecomputePortalConnections();
	}
	return CommandCost(EXPENSES_CONSTRUCTION, -GetUndergroundRemoveRefund(UndergroundCostKind::Ramp, rt, tile, z_bot, paid_soil));
}

/**
 * Remove all underground constructions in an area at a given depth.
 * Handles rail, stations, signals (implicitly with their track), portals and ramps.
 * @param flags    Command flags.
 * @param tile     One corner of the area.
 * @param end_tile The opposite corner of the area.
 * @param depth    Underground depth.
 * @return Total (negative) cost or an error if nothing could be removed.
 */
CommandCost CmdRemoveUndergroundArea(DoCommandFlags flags, TileIndex tile, TileIndex end_tile, int16_t depth)
{
	if (!IsValidTile(tile) || !IsValidTile(end_tile)) return CMD_ERROR;

	CommandCost total(EXPENSES_CONSTRUCTION);
	CommandCost last_error = CommandCost(STR_ERROR_UNDERGROUND_NOTHING_TO_REMOVE);
	bool removed_any = false;

	TileArea area(tile, end_tile);
	for (TileIndex t : area) {
		/* Surface exits of underground stations (any depth is shown below them). */
		if (FindExplicitStationExitAt(t) != nullptr) {
			CommandCost ret = CmdRemoveExitSurface(flags, t);
			if (ret.Failed()) {
				last_error = ret;
			} else {
				removed_any = true;
				total.AddCost(ret.GetCost());
			}
		}

		/* Portals stand on the surface; they go at any depth too. */
		SliceID portal_sid = _multilayer_map.FindSliceByKind(t, SliceKind::Portal);
		if (portal_sid != INVALID_SLICE_ID && !_multilayer_map.GetSlice(portal_sid).span.Contains(depth)) {
			CommandCost ret = CmdRemovePortal(flags, t);
			if (ret.Failed()) {
				last_error = ret;
			} else {
				removed_any = true;
				total.AddCost(ret.GetCost());
			}
		}

		SliceID sid = _multilayer_map.FindSliceAt(t, depth);
		if (sid == INVALID_SLICE_ID) continue;

		/* Copy what we need before removal frees the slice. */
		SliceKind kind = _multilayer_map.GetSlice(sid).kind;

		CommandCost ret = CMD_ERROR;
		switch (kind) {
			case SliceKind::Track: {
				TrackBits tracks = static_cast<TrackBits>(_multilayer_map.GetSlice(sid).track.tracks);
				for (Track track : SetTrackBitIterator(tracks)) {
					ret = CmdRemoveUndergroundRail(flags, t, track, depth);
					if (ret.Failed()) {
						last_error = ret;
						continue;
					}
					removed_any = true;
					total.AddCost(ret.GetCost());
				}
				continue; /* Costs already accumulated per track bit. */
			}

			case SliceKind::StationTile:
				ret = CmdRemoveUndergroundStation(flags, t, depth);
				break;

			case SliceKind::Portal:
				ret = CmdRemovePortal(flags, t);
				break;

			case SliceKind::Ramp:
				ret = CmdRemoveRamp(flags, t, depth);
				break;

			default:
				continue;
		}

		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		removed_any = true;
		total.AddCost(ret.GetCost());
	}

	if (!removed_any) return last_error;
	return total;
}

/**
 * Convert the rail type of the underground construction in an area at one
 * level, like CmdConvertRail on the surface: track, station platforms, ramps
 * and portals (a portal with the surface rail of its tile, so the line keeps
 * one type through the mouth). Track a train stands on can only be converted
 * to a type the train has power on.
 * @param flags Operation to perform.
 * @param tile End tile of the area.
 * @param area_start Start tile of the area.
 * @param totype The rail type to convert to.
 * @param depth The level.
 * @return The cost of this operation or an error.
 */
CommandCost CmdConvertUndergroundRail(DoCommandFlags flags, TileIndex tile, TileIndex area_start, RailType totype, int16_t depth)
{
	if (!ValParamRailType(totype)) return CMD_ERROR;
	if (!IsValidTile(tile) || !IsValidTile(area_start)) return CMD_ERROR;
	if (!_multilayer_map.IsAllocated()) return CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost error = CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK); // by default, there is no track to convert.
	bool found = false;
	std::set<SliceID> converted;

	for (TileIndex t : TileArea(area_start, tile)) {
		SliceID sid = _multilayer_map.FindSliceAt(t, depth);
		if (sid == INVALID_SLICE_ID) continue;
		TileSlice &slice = _multilayer_map.GetSlice(sid);

		uint8_t *railtype;
		uint pieces = 1;
		UndergroundCostKind cost_kind;
		switch (slice.kind) {
			case SliceKind::Track: railtype = &slice.track.railtype; pieces = CountBits(slice.track.tracks); cost_kind = UndergroundCostKind::Track; break;
			case SliceKind::StationTile: railtype = &slice.station.railtype; cost_kind = UndergroundCostKind::Station; break;
			case SliceKind::Ramp: railtype = &slice.ramp.railtype; cost_kind = UndergroundCostKind::Ramp; break;
			case SliceKind::Portal: railtype = &slice.portal.railtype; cost_kind = UndergroundCostKind::Portal; break;
			default: continue;
		}
		RailType type = static_cast<RailType>(*railtype);
		if (type == totype) continue;

		CommandCost ret = CheckUndergroundSliceOwnership(slice, t);
		if (ret.Failed()) {
			error = ret;
			continue;
		}
		/* A train standing on the track must be able to run on the new type,
		 * else it would be stranded (one already stranded on the old type is
		 * freed by the conversion). */
		bool stranded = false;
		for (const Train *u : Train::Iterate()) {
			if (u->IsUnderground() && u->underground_slice == sid && !u->First()->compatible_railtypes.Test(totype)) {
				stranded = true;
				break;
			}
		}
		if (stranded) {
			error = CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
			continue;
		}

		/* Underground the new rail is laid at underground prices: the difference
		 * to the old type (if dearer) and an eighth of the new one for the work,
		 * like RailConvertCost on the surface. Refunds use the new type. */
		Money new_price = GetUndergroundBuildCost(cost_kind, totype, t, slice.span.z_bot);
		Money old_price = GetUndergroundBuildCost(cost_kind, type, t, slice.span.z_bot);
		Money convert_price = std::max<Money>(new_price - old_price, 0) + new_price / 8;
		CommandCost piece_cost(EXPENSES_CONSTRUCTION, convert_price * pieces);
		if (slice.kind == SliceKind::Portal && IsTileType(t, TileType::Railway) && GetRailType(t) != totype) {
			ret = Command<Commands::ConvertRail>::Do(flags, t, t, totype, false);
			if (ret.Failed()) {
				error = ret;
				continue;
			}
			piece_cost.AddCost(ret.GetCost());
		}

		found = true;
		cost.AddCost(piece_cost.GetCost());
		if (flags.Test(DoCommandFlag::Execute)) {
			UndergroundSliceInfrastructure before = GetUndergroundSliceInfrastructure(slice);
			*railtype = totype;
			ApplyUndergroundInfrastructureDelta(slice.owner, before, GetUndergroundSliceInfrastructure(slice));
			converted.insert(sid);
			MarkTileDirtyByTile(t);
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Rail type changed, update the trains on it as when entering different track. */
		for (Train *v : Train::Iterate()) {
			if (!v->IsFrontEngine()) continue;
			for (const Train *u = v; u != nullptr; u = u->Next()) {
				if (u->IsUnderground() && converted.contains(u->underground_slice)) {
					v->ConsistChanged(CCF_TRACK);
					break;
				}
			}
		}
	}

	return found ? cost : error;
}
