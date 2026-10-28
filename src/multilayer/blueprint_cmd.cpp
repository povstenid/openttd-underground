/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint_cmd.cpp Command for pasting metro blueprints. */

#include "../stdafx.h"
#include "blueprint_cmd.h"
#include "blueprint.h"
#include "multilayer_map.h"
#include "portal_registry.h"
#include "station_complex.h"
#include "underground_cmd.h"
#include "underground_cost.h"
#include "underground_signal.h"
#include "../command_func.h"
#include "../company_func.h"
#include "../economy_func.h"
#include "../map_func.h"
#include "../rail_cmd.h"
#include "../rail_map.h"
#include "../station_base.h"
#include "../track_func.h"
#include "../train.h"

#include "../table/strings.h"

#include <map>

#include "../safeguards.h"

/* Implemented next to the vanilla road stop code in station_cmd.cpp. */
CommandCost BuildStationExitPavilion(DoCommandFlags flags, TileIndex tile, StationID station_id);

/** What a paste knows of a station of the blueprint. */
struct PasteStation {
	std::optional<uint16_t> id; ///< The station in the game, once it is known.
	std::vector<std::pair<TileIndex, int>> platforms; ///< Test run: platforms (tile, level) the paste is to build.
	std::optional<StationRect> rect; ///< Test run: its station rect with those platforms and exits.
};

/** Stations of the blueprint, by their index in it. */
using PasteStations = std::map<uint8_t, PasteStation>;

/** The station of an own platform on a tile and level with the given axis and rail type, or nothing. */
static std::optional<uint16_t> GetOwnPlatformStation(TileIndex tile, int z, Axis axis, RailType rt)
{
	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(z));
	if (sid == INVALID_SLICE_ID) return std::nullopt;
	const TileSlice &s = _multilayer_map.GetSlice(sid);
	if (s.kind != SliceKind::StationTile || s.station.role != 0 || s.owner != _current_company.base()) return std::nullopt;
	if (s.station.tracks != AxisToTrackBits(axis) || s.station.railtype != rt) return std::nullopt;
	return s.station.station_id;
}

/** Whether a train stands on a column underground. */
static bool HasUndergroundTrainOn(TileIndex tile)
{
	for (const Train *t : Train::Iterate()) {
		if (t->IsUnderground() && t->underground_tile_raw == tile.base()) return true;
	}
	return false;
}

/** Whether a piece of the blueprint is already there, as it would be built. */
static bool IsAlreadyBuilt(const BlueprintItem &item, TileIndex tile, int z)
{
	switch (item.kind) {
		case BlueprintItemKind::Platform:
			return GetOwnPlatformStation(tile, z, static_cast<Axis>(item.dir), static_cast<RailType>(item.railtype)).has_value();
		case BlueprintItemKind::Ramp: {
			SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(z));
			if (sid == INVALID_SLICE_ID) return false;
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			return s.kind == SliceKind::Ramp && s.owner == _current_company.base() && s.ramp.direction == item.dir && s.ramp.entry_depth == z && s.ramp.exit_depth == z - item.z + item.z2;
		}
		case BlueprintItemKind::Portal: {
			SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
			if (sid == INVALID_SLICE_ID) return false;
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			return s.owner == _current_company.base() && s.span.z_bot == z && DiagDirToAxis(static_cast<DiagDirection>(s.portal.direction)) == DiagDirToAxis(static_cast<DiagDirection>(item.dir));
		}
		case BlueprintItemKind::Depot:
			return IsRailDepotTile(tile) && IsTileOwner(tile, _current_company) && GetRailDepotDirection(tile) == static_cast<DiagDirection>(item.dir);
		case BlueprintItemKind::Exit: {
			const StationComplexNode *exit = FindExplicitStationExitAt(tile);
			if (exit == nullptr || exit->exit_type != static_cast<StationExitType>(item.exit_type)) return false;
			const Station *st = Station::GetIfValid(StationID(exit->station_id));
			return st != nullptr && st->owner == _current_company;
		}
		default:
			return false;
	}
}

/** Check the values of a piece received in a command. */
static bool IsValidPayloadItem(const BlueprintItem &item)
{
	switch (item.kind) {
		case BlueprintItemKind::Track:
		case BlueprintItemKind::SurfaceRail:
			if (item.tracks == 0 || (item.tracks & ~TRACK_BIT_ALL) != 0 || (item.signal_mask & ~0xF) != 0) return false;
			/* Like on the surface, signals only stand on tracks that do not cross. */
			return item.signal_mask == 0 || !TracksOverlap(static_cast<TrackBits>(item.tracks));
		case BlueprintItemKind::Platform:
			return item.dir <= AXIS_Y;
		case BlueprintItemKind::Ramp:
			return item.dir < DIAGDIR_END && std::abs(item.z - item.z2) == 1 && (item.signal_mask & ~0xF) == 0;
		case BlueprintItemKind::Portal:
		case BlueprintItemKind::Depot:
			return item.dir < DIAGDIR_END;
		case BlueprintItemKind::Exit:
			return IsExplicitStationExit(static_cast<StationExitType>(item.exit_type));
	}
	return false;
}

/**
 * Build the signals of a track, ramp or surface rail piece, or test building them.
 * The test run checks what the signal command will check once the track is
 * there: tracks that do not cross, the owner, and no train on the tile.
 * @param flags Command flags.
 * @param item The piece.
 * @param tracks Its tracks.
 * @param tile Its tile.
 * @param z Its level, or std::nullopt on the surface.
 * @param[in,out] built Cost of what is built (or would be built in the test run).
 * @return Success, or why the signals cannot be built.
 */
static CommandCost BuildBlueprintSignals(DoCommandFlags flags, const BlueprintItem &item, TrackBits tracks, TileIndex tile, std::optional<int> z, CommandCost &built)
{
	if (item.signal_mask == 0) return CommandCost();
	const bool exec = flags.Test(DoCommandFlag::Execute);

	/* The tracks the tile will carry, and whether they all are there already. */
	TrackBits present = TRACK_BIT_NONE;
	const TileSlice *slice = nullptr;
	if (z.has_value()) {
		SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(*z));
		if (sid != INVALID_SLICE_ID && CanSliceCarrySignals(_multilayer_map.GetSlice(sid))) {
			slice = &_multilayer_map.GetSlice(sid);
			present = slice->kind == SliceKind::Ramp ? TrackToTrackBits(GetRampTrack(slice->ramp)) : static_cast<TrackBits>(slice->track.tracks);
		}
	} else if (IsPlainRailTile(tile)) {
		present = GetTrackBits(tile);
	}
	if (!exec && TracksOverlap(present | tracks)) return CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);
	const bool all_present = (tracks & ~present) == TRACK_BIT_NONE;

	for (Track track : SetTrackBitIterator(tracks)) {
		uint8_t copy = item.signal_mask & SignalOnTrack(track);
		if (copy == 0) continue;
		SignalType type = GetUndergroundSignalType(item, track);
		SignalVariant variant = GetUndergroundSignalVariant(item, track);
		if (type > SIGTYPE_LAST) return CMD_ERROR;

		/* The signals already there: any at all, and exactly these. A train
		 * standing on the track must not keep an identical paste from passing. */
		bool has_signal = false, same = false, other_variant = false;
		if (z.has_value()) {
			if (slice != nullptr && HasTrack(present, track)) {
				has_signal = HasUndergroundSignalOnTrack(*slice, track);
				other_variant = has_signal && GetUndergroundSignalVariant(*slice, track) != variant;
				same = has_signal && !other_variant && (GetUndergroundSignalMask(*slice) & SignalOnTrack(track)) == copy && GetUndergroundSignalType(*slice, track) == type;
			}
		} else if (HasTrack(present, track)) {
			has_signal = HasSignalOnTrack(tile, track);
			other_variant = has_signal && GetSignalVariant(tile, track) != variant;
			same = has_signal && !other_variant && (GetPresentSignals(tile) & SignalOnTrack(track)) == copy && GetSignalType(tile, track) == type;
		}
		if (same) continue;

		auto build = [&](DoCommandFlags f) {
			return z.has_value() ?
				Command<Commands::BuildUndergroundSignal>::Do(f, tile, track, static_cast<int16_t>(*z), type, variant, false, false, SIGTYPE_BLOCK, SIGTYPE_LAST, copy) :
				Command<Commands::BuildSignal>::Do(f, tile, track, type, variant, false, false, false, SIGTYPE_BLOCK, SIGTYPE_LAST, 0, copy);
		};

		if (!exec && !all_present) {
			/* The track is still to be built: check what the signal command will. */
			if (z.has_value() && slice != nullptr && HasUndergroundTrainOn(tile)) return CommandCost(STR_ERROR_TRAIN_IN_THE_WAY);
			/* As CmdBuildUndergroundSignal charges it, also for changing its variant. */
			Money price = z.has_value() ? GetUndergroundBuildCost(UndergroundCostKind::Signal, INVALID_RAILTYPE, tile, static_cast<int16_t>(*z)) : _price[Price::BuildSignals];
			if (!has_signal) built.AddCost(CommandCost(EXPENSES_CONSTRUCTION, price));
			if (other_variant) built.AddCost(CommandCost(EXPENSES_CONSTRUCTION, z.has_value() ? price : price + _price[Price::ClearSignals]));
			continue;
		}

		/* With its track there, the signal command itself tests or builds it. */
		CommandCost ret = build(flags);
		if (ret.Failed()) return ret;
		built.AddCost(ret.GetCost());
	}
	return CommandCost();
}

/**
 * Test, as the test run, the station side of a platform: the spread of the
 * station with the platforms of this paste, and (without distant joining)
 * that it lies next to a platform of its station.
 */
static CommandCost TestBlueprintPlatformStation(PasteStation &ps, TileIndex tile, int z)
{
	if (!ps.rect.has_value()) ps.rect = ps.id.has_value() ? Station::Get(StationID(*ps.id))->rect : StationRect();
	CommandCost ret = ps.rect->BeforeAddRect(tile, 1, 1, StationRect::ADD_TEST);
	if (ret.Failed()) return ret;

	bool joining = ps.id.has_value() || !ps.platforms.empty();
	if (joining && !_settings_game.station.distant_join_stations) {
		bool adjacent = std::ranges::any_of(ps.platforms, [&](const auto &p) { return p.second == z && DistanceManhattan(p.first, tile) == 1; });
		if (!adjacent && ps.id.has_value()) {
			std::vector<StationID> next = FindAdjacentUndergroundStations(TileArea(tile, 1, 1), static_cast<int16_t>(z), _current_company);
			adjacent = std::ranges::find(next, StationID(*ps.id)) != next.end();
		}
		if (!adjacent) return CMD_ERROR;
	}

	ps.rect->BeforeAddRect(tile, 1, 1, StationRect::ADD_FORCE);
	ps.platforms.emplace_back(tile, z);
	return CommandCost();
}

/**
 * Test, as the test run, an exit of a station of the blueprint that may still
 * be to be built: the tile, its distance to the station's platforms and the
 * spread of the station.
 * @param[in,out] built Cost the exit would have.
 */
static CommandCost TestBlueprintExit(DoCommandFlags flags, PasteStation &ps, const BlueprintItem &item, TileIndex tile, CommandCost &built)
{
	if (FindExplicitStationExitAt(tile) != nullptr) return CommandCost(STR_ERROR_UNDERGROUND_EXIT_ALREADY_HERE);

	uint distance = ps.id.has_value() ? GetUndergroundExitDistanceToStation(*ps.id, tile) : UINT_MAX;
	for (const auto &p : ps.platforms) distance = std::min(distance, DistanceMax(p.first, tile));
	if (distance == UINT_MAX) return CommandCost(STR_ERROR_UNDERGROUND_NO_STATION_HERE);
	if (distance > _settings_game.construction.max_station_exit_distance) return CommandCost(STR_ERROR_UNDERGROUND_EXIT_TOO_FAR_FROM_PLATFORM);

	if (!ps.rect.has_value()) ps.rect = ps.id.has_value() ? Station::Get(StationID(*ps.id))->rect : StationRect();
	if (static_cast<StationExitType>(item.exit_type) == StationExitType::Pavilion) {
		CommandCost ret = ps.rect->BeforeAddRect(tile, 1, 1, StationRect::ADD_TEST);
		if (ret.Failed()) return ret;
		ret = BuildStationExitPavilion(flags, tile, ps.id.has_value() ? StationID(*ps.id) : StationID::Invalid());
		if (ret.Failed()) return ret;
		built.AddCost(ret.GetCost());
	} else {
		if (!IsUnderpassRoadTile(tile)) return CommandCost(STR_ERROR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD);
		built.AddCost(CommandCost(EXPENSES_CONSTRUCTION, _price[Price::BuildStationBus]));
	}
	ps.rect->BeforeAddRect(tile, 1, 1, StationRect::ADD_FORCE);
	return CommandCost();
}

/**
 * Build one piece of a blueprint, or test building it.
 * Whatever a piece builds before one of its steps fails stays built; its cost
 * is in \a built either way, so it is paid for.
 * @param flags Command flags.
 * @param item The piece.
 * @param tile Its tile.
 * @param depth The level the anchor level of the blueprint goes to.
 * @param stations Stations of the blueprint; updated when a platform creates or (test run) extends one.
 * @param options BlueprintPasteOption bits.
 * @param[in,out] built Cost of what is built (or would be built in the test run).
 * @return Success, or why (the rest of) the piece cannot be built.
 */
static CommandCost BuildBlueprintItem(DoCommandFlags flags, const BlueprintItem &item, TileIndex tile, int depth, PasteStations &stations, uint8_t options, CommandCost &built)
{
	int z = depth + item.z;
	RailType rt = static_cast<RailType>(item.railtype);
	bool exec = flags.Test(DoCommandFlag::Execute);
	auto add = [&](const CommandCost &ret) {
		if (ret.Succeeded()) built.AddCost(ret.GetCost());
		return ret;
	};

	if (IsAlreadyBuilt(item, tile, z)) {
		if (item.kind == BlueprintItemKind::Platform) {
			PasteStation &ps = stations[item.station];
			if (!ps.id.has_value()) ps.id = GetOwnPlatformStation(tile, z, static_cast<Axis>(item.dir), rt);
		}
		return CommandCost();
	}

	switch (item.kind) {
		case BlueprintItemKind::Track: {
			for (Track track : SetTrackBitIterator(static_cast<TrackBits>(item.tracks))) {
				CommandCost ret = add(Command<Commands::BuildUndergroundRail>::Do(flags, tile, rt, track, static_cast<int16_t>(z)));
				if (ret.Failed()) return ret;
			}
			return BuildBlueprintSignals(flags, item, static_cast<TrackBits>(item.tracks), tile, z, built);
		}

		case BlueprintItemKind::Platform: {
			PasteStation &ps = stations[item.station];
			bool first = !ps.id.has_value() && ps.platforms.empty();
			/* The test run checks the joining itself, as the station may only be built by this paste. */
			uint16_t station_id = (exec && ps.id.has_value()) ? *ps.id :
					((first && (options & BPO_JOIN_ADJACENT) != 0) ? UNDERGROUND_JOIN_ADJACENT_ID : NEW_UNDERGROUND_STATION_ID);
			CommandCost ret = add(Command<Commands::BuildUndergroundStation>::Do(flags, tile, rt, static_cast<int16_t>(z), station_id, static_cast<Axis>(item.dir), 1, 1));
			if (ret.Failed()) return ret;
			if (exec) {
				if (!ps.id.has_value()) ps.id = GetOwnPlatformStation(tile, z, static_cast<Axis>(item.dir), rt);
				return CommandCost();
			}
			if (first && station_id == UNDERGROUND_JOIN_ADJACENT_ID) {
				std::vector<StationID> next = FindAdjacentUndergroundStations(TileArea(tile, 1, 1), static_cast<int16_t>(z), _current_company);
				if (next.size() == 1) ps.id = next.front().base();
			}
			return TestBlueprintPlatformStation(ps, tile, z);
		}

		case BlueprintItemKind::Ramp: {
			CommandCost ret = add(Command<Commands::BuildRamp>::Do(flags, tile, rt, static_cast<DiagDirection>(item.dir), static_cast<int16_t>(z), static_cast<int16_t>(depth + item.z2)));
			if (ret.Failed()) return ret;
			return BuildBlueprintSignals(flags, item, AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(item.dir))), tile, z, built);
		}

		case BlueprintItemKind::Portal:
			return add(Command<Commands::BuildPortal>::Do(flags, tile, rt, static_cast<DiagDirection>(item.dir), static_cast<int16_t>(z)));

		case BlueprintItemKind::Exit: {
			PasteStation &ps = stations[item.station];
			if (!exec) return TestBlueprintExit(flags, ps, item, tile, built);
			if (!ps.id.has_value()) return CommandCost(STR_ERROR_UNDERGROUND_NO_STATION_HERE);
			return add(Command<Commands::BuildExitSurface>::Do(flags, tile, *ps.id, static_cast<StationExitType>(item.exit_type)));
		}

		case BlueprintItemKind::SurfaceRail: {
			for (Track track : SetTrackBitIterator(static_cast<TrackBits>(item.tracks))) {
				if (IsPlainRailTile(tile) && HasTrack(tile, track)) continue;
				CommandCost ret = add(Command<Commands::BuildRail>::Do(flags, tile, rt, track, false));
				if (ret.Failed()) return ret;
			}
			return BuildBlueprintSignals(flags, item, static_cast<TrackBits>(item.tracks), tile, std::nullopt, built);
		}

		case BlueprintItemKind::Depot:
			return add(Command<Commands::BuildRailDepot>::Do(flags, tile, rt, static_cast<DiagDirection>(item.dir)));
	}
	NOT_REACHED();
}

/**
 * Build a part of a metro blueprint. The client splits a blueprint into parts
 * small enough for a network packet (see PlanBlueprintPaste); a station built
 * over several parts is named in the later ones by some of its platforms.
 * Every piece is tested first. Without BPO_BEST_EFFORT nothing of the part is
 * built when any piece fails the test or the company cannot pay for all of
 * them; with it, the pieces that fail are left out.
 * @param flags Command flags.
 * @param tile North corner of the blueprint's footprint.
 * @param depth Level the anchor level of the blueprint goes to.
 * @param options BlueprintPasteOption bits.
 * @param data The part, as written by PlanBlueprintPaste.
 * @return The cost, the number of pieces built (or already there) and the number left out.
 */
std::tuple<CommandCost, uint16_t, uint16_t> CmdPasteUndergroundBlueprint(DoCommandFlags flags, TileIndex tile, int16_t depth, uint8_t options, const std::string &data)
{
	if (!IsValidTile(tile) || !_multilayer_map.IsAllocated()) return {CMD_ERROR, 0, 0};
	if (std::abs(depth) > 256) return {CMD_ERROR, 0, 0};

	auto payload = DecodeBlueprintPayload(data);
	if (!payload.has_value() || payload->items.empty() || payload->items.size() > UINT16_MAX) return {CMD_ERROR, 0, 0};
	const size_t n = payload->items.size();

	/* Where every piece goes. */
	std::vector<TileIndex> tiles;
	for (const BlueprintItem &item : payload->items) {
		if (!IsValidPayloadItem(item)) return {CMD_ERROR, 0, 0};
		TileIndex t = TileAddWrap(tile, item.x, item.y);
		if (t == INVALID_TILE) return {CommandCost(STR_ERROR_OFF_EDGE_OF_MAP), 0, 0};
		tiles.push_back(t);
	}

	/* Stations of the blueprint built by earlier parts: the first of its platforms named that is there. */
	PasteStations stations;
	for (const auto &[index, ref] : payload->station_refs) {
		PasteStation &ps = stations[index];
		if (ps.id.has_value()) continue;
		TileIndex t = TileAddWrap(tile, ref.x, ref.y);
		if (t == INVALID_TILE) continue;
		ps.id = GetOwnPlatformStation(t, depth + ref.z, static_cast<Axis>(ref.dir), static_cast<RailType>(ref.railtype));
	}

	const bool best_effort = (options & BPO_BEST_EFFORT) != 0;
	PortalConnectionsBatch batch;

	/* Test every piece first. */
	DoCommandFlags test_flags = flags;
	test_flags.Reset(DoCommandFlag::Execute);
	PasteStations test_stations = stations;
	CommandCost total(EXPENSES_CONSTRUCTION);
	CommandCost last_error = CommandCost(STR_ERROR_BLUEPRINT_NOTHING_BUILT);
	std::vector<Money> piece_cost(n, 0);
	std::vector<bool> fits(n, false);
	uint16_t ok = 0;
	for (size_t i = 0; i < n; i++) {
		CommandCost cost(EXPENSES_CONSTRUCTION);
		CommandCost ret = BuildBlueprintItem(test_flags, payload->items[i], tiles[i], depth, test_stations, options, cost);
		if (ret.Failed()) {
			if (!best_effort) return {ret, 0, 0};
			last_error = ret;
			continue;
		}
		fits[i] = true;
		piece_cost[i] = cost.GetCost();
		total.AddCost(cost.GetCost());
		ok++;
	}
	if (ok == 0) return {last_error, 0, static_cast<uint16_t>(n)};

	Money money = GetAvailableMoneyForCommand();
	if (!best_effort && total.GetCost() > money) return {CommandCostWithParam(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY, total.GetCost()), 0, 0};
	if (!flags.Test(DoCommandFlag::Execute)) return {total, ok, static_cast<uint16_t>(n - ok)};

	/* Build what passed the test, as long as the money lasts. A piece can still
	 * fail now where the test could not foresee it; it is left out, and what
	 * it built before failing is paid for. */
	CommandCost built(EXPENSES_CONSTRUCTION);
	uint16_t done = 0, left_out = 0;
	for (size_t i = 0; i < n; i++) {
		if (!fits[i] || (piece_cost[i] > 0 && built.GetCost() + piece_cost[i] > money)) {
			left_out++;
			continue;
		}
		CommandCost cost(EXPENSES_CONSTRUCTION);
		CommandCost ret = BuildBlueprintItem(flags, payload->items[i], tiles[i], depth, stations, options, cost);
		built.AddCost(cost.GetCost());
		if (ret.Failed()) {
			last_error = ret;
			left_out++;
			continue;
		}
		done++;
	}
	if (done == 0 && built.GetCost() == 0) return {last_error, 0, left_out};
	return {built, done, left_out};
}
