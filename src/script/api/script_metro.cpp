/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_metro.cpp Implementation of ScriptMetro. */

#include "../../stdafx.h"

#include "script_metro.hpp"
#include "script_companymode.hpp"
#include "script_error.hpp"
#include "../../company_base.h"
#include "../../company_cmd.h"
#include "../../command_func.h"
#include "../../core/backup_type.hpp"
#include "../../direction_type.h"
#include "../../economy_func.h"
#include "../../network/network_type.h"
#include "../../multilayer/blueprint.h"
#include "../../multilayer/blueprint_cmd.h"
#include "../../multilayer/multilayer_map.h"
#include "../../multilayer/multilayer_types.h"
#include "../../multilayer/portal_registry.h"
#include "../../multilayer/station_complex.h"
#include "../../multilayer/underground_cmd.h"
#include "../../multilayer/underground_rules.h"
#include "../../house.h"
#include "../../town_cmd.h"
#include "../../water_cmd.h"
#include "../../multilayer/underground_signal.h"
#include "../../multilayer/underground_soil.h"
#include "../../order_base.h"
#include "../../order_cmd.h"
#include "../../station_base.h"
#include "../../track_func.h"
#include "../../train.h"
#include "../../vehicle_base.h"

#include "../../safeguards.h"

extern Company *DoStartupNewCompany(bool is_ai, CompanyID company = CompanyID::Invalid());

static bool IsValidMetroDepth(SQInteger depth)
{
	return depth >= GetMetroGlobalMinZ() && depth <= GetMetroGlobalMaxZ();
}

/**
 * Enforce that a depth lies in the globally usable underground range,
 * reporting a metro specific error instead of ERR_PRECONDITION_FAILED.
 */
#define EnforceMetroDepth(depth) \
	EnforcePreconditionCustomError(false, (depth) >= GetMetroGlobalMinZ(), ScriptMetro::ERR_UNDERGROUND_TOO_DEEP); \
	EnforcePreconditionCustomError(false, (depth) <= GetMetroGlobalMaxZ(), ScriptMetro::ERR_UNDERGROUND_DEPTH_INVALID)

static bool IsValidMetroTrack(ScriptRail::RailTrack rail_track)
{
	if (rail_track == ScriptRail::RAILTRACK_INVALID || rail_track == 0) return false;
	uint bits = static_cast<uint>(rail_track);
	return (bits & ~static_cast<uint>(TRACK_BIT_ALL)) == 0 && KillFirstBit(bits) == 0;
}

static ::Track MetroScriptTrackToTrack(ScriptRail::RailTrack rail_track)
{
	return FindFirstTrack(static_cast<TrackBits>(rail_track));
}

static bool IsValidMetroDirection(ScriptMetro::DiagDirection direction)
{
	return direction >= ScriptMetro::DIAGDIR_NE && direction <= ScriptMetro::DIAGDIR_NW;
}

static ScriptMetro::TileKind MetroSliceKindToScript(SliceKind kind)
{
	switch (kind) {
		case SliceKind::Track: return ScriptMetro::TK_TRACK;
		case SliceKind::StationTile: return ScriptMetro::TK_STATION;
		case SliceKind::Portal: return ScriptMetro::TK_PORTAL;
		case SliceKind::Ramp: return ScriptMetro::TK_RAMP;
		default: return ScriptMetro::TK_NONE;
	}
}

static bool AreStationOrderFlagsValid(ScriptOrder::ScriptOrderFlags order_flags)
{
	return (order_flags & ~(ScriptOrder::OF_NON_STOP_FLAGS | ScriptOrder::OF_UNLOAD_FLAGS | ScriptOrder::OF_LOAD_FLAGS)) == 0 &&
			HasAtMostOneBit(order_flags & (ScriptOrder::OF_TRANSFER | ScriptOrder::OF_UNLOAD | ScriptOrder::OF_NO_UNLOAD)) &&
			HasAtMostOneBit(order_flags & (ScriptOrder::OF_NO_UNLOAD | ScriptOrder::OF_NO_LOAD)) &&
			HasAtMostOneBit(order_flags & (ScriptOrder::OF_FULL_LOAD | ScriptOrder::OF_NO_LOAD)) &&
			((order_flags & ScriptOrder::OF_FULL_LOAD_ANY) != (ScriptOrder::OF_FULL_LOAD_ANY & ~ScriptOrder::OF_FULL_LOAD));
}

/* static */ ScriptCompany::CompanyID ScriptMetro::CreateCompany(ScriptCompany::CompanyID company)
{
	EnforceDeityMode(ScriptCompany::COMPANY_INVALID);

	CompanyID preferred = CompanyID::Invalid();
	if (company != ScriptCompany::COMPANY_INVALID) {
		if (company < ScriptCompany::COMPANY_FIRST || company >= ScriptCompany::COMPANY_LAST) return ScriptCompany::COMPANY_INVALID;
		if (ScriptCompany::ResolveCompanyID(company) != ScriptCompany::COMPANY_INVALID) return ScriptCompany::COMPANY_INVALID;
		preferred = static_cast<CompanyID>(company);
	}

	Company *created = DoStartupNewCompany(false, preferred);
	return created == nullptr ? ScriptCompany::COMPANY_INVALID : ScriptCompany::ToScriptCompanyID(created->index);
}

/* static */ bool ScriptMetro::RemoveCompany(ScriptCompany::CompanyID company)
{
	EnforceDeityMode(false);
	company = ScriptCompany::ResolveCompanyID(company);
	EnforcePrecondition(false, company != ScriptCompany::COMPANY_INVALID);

	::CompanyID cid = ScriptCompany::FromScriptCompanyID(company);
	Backup<CompanyID> cur_company(_current_company, OWNER_DEITY);
	CommandCost res = ::Command<Commands::CompanyControl>::Do(DoCommandFlag::Execute, CompanyCtrlAction::Delete, cid, CompanyRemoveReason::Manual, INVALID_CLIENT_ID);
	cur_company.Restore();
	return res.Succeeded() && !::Company::IsValidID(cid);
}

/* static */ bool ScriptMetro::MergeCompany(ScriptCompany::CompanyID company, ScriptCompany::CompanyID into)
{
	EnforceDeityMode(false);
	company = ScriptCompany::ResolveCompanyID(company);
	into = ScriptCompany::ResolveCompanyID(into);
	EnforcePrecondition(false, company != ScriptCompany::COMPANY_INVALID);
	EnforcePrecondition(false, into != ScriptCompany::COMPANY_INVALID);
	EnforcePrecondition(false, company != into);

	::CompanyID cid = ScriptCompany::FromScriptCompanyID(company);
	::CompanyID into_id = ScriptCompany::FromScriptCompanyID(into);
	Backup<CompanyID> cur_company(_current_company, into_id);
	DoAcquireCompany(::Company::Get(cid), false);
	cur_company.Restore();
	return !::Company::IsValidID(cid);
}

/* static */ bool ScriptMetro::BuildUndergroundRail(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));

	return ScriptObject::Command<Commands::BuildUndergroundRail>::Do(tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::BuildUndergroundRailTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));

	return ScriptObject::Command<Commands::BuildUndergroundRailroadTrack>::Do(end_tile, start_tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::RemoveUndergroundRailTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveUndergroundRailroadTrack>::Do(end_tile, start_tile, MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::RemoveUndergroundRail(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveUndergroundRail>::Do(tile, MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::BuildPortal(TileIndex tile, DiagDirection direction, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroDirection(direction));
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));

	return ScriptObject::Command<Commands::BuildPortal>::Do(tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), static_cast<::DiagDirection>(direction), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::RemovePortal(TileIndex tile)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::Command<Commands::RemovePortal>::Do(tile);
}

/* static */ bool ScriptMetro::BuildUndergroundStation(TileIndex tile, ScriptRail::RailTrack direction, SQInteger depth, StationID station_id)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, direction == ScriptRail::RAILTRACK_NW_SE || direction == ScriptRail::RAILTRACK_NE_SW);
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));
	EnforcePrecondition(false, station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id));

	uint16_t internal_station_id = NEW_UNDERGROUND_STATION_ID;
	if (station_id == ScriptStation::STATION_JOIN_ADJACENT) {
		internal_station_id = UNDERGROUND_JOIN_ADJACENT_ID;
	} else if (station_id != ScriptStation::STATION_NEW) {
		internal_station_id = station_id.base();
	}
	Axis axis = direction == ScriptRail::RAILTRACK_NW_SE ? AXIS_Y : AXIS_X;

	return ScriptObject::Command<Commands::BuildUndergroundStation>::Do(tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), static_cast<int16_t>(depth), internal_station_id, axis, 1, 1);
}

/* static */ bool ScriptMetro::RemoveUndergroundStation(TileIndex tile, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveUndergroundStation>::Do(tile, static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::BuildExitSurface(TileIndex tile, StationID station_id, ExitType exit_type)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, ScriptStation::IsValidStation(station_id));
	EnforcePrecondition(false, exit_type == EXIT_PAVILION || exit_type == EXIT_UNDERPASS);

	::StationExitType type = exit_type == EXIT_PAVILION ? ::StationExitType::Pavilion : ::StationExitType::Underpass;
	return ScriptObject::Command<Commands::BuildExitSurface>::Do(tile, station_id.base(), type);
}

/* static */ bool ScriptMetro::RemoveExitSurface(TileIndex tile)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::Command<Commands::RemoveExitSurface>::Do(tile);
}

/* static */ ScriptMetro::ExitType ScriptMetro::GetExitType(TileIndex tile)
{
	if (!::IsValidTile(tile)) return EXIT_INVALID;
	const StationComplexNode *n = FindExplicitStationExitAt(tile);
	if (n == nullptr) return EXIT_INVALID;
	return n->exit_type == ::StationExitType::Pavilion ? EXIT_PAVILION : EXIT_UNDERPASS;
}

/* static */ bool ScriptMetro::BuildRamp(TileIndex tile, DiagDirection direction, SQInteger from_depth, SQInteger to_depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroDirection(direction));
	EnforceMetroDepth(from_depth);
	/* A ramp one level up may lead out to the surface (it is then built as a portal). */
	EnforceMetroDepth(to_depth == from_depth + 1 ? from_depth : to_depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));

	return ScriptObject::Command<Commands::BuildRamp>::Do(tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), static_cast<::DiagDirection>(direction), static_cast<int16_t>(from_depth), static_cast<int16_t>(to_depth));
}

/* static */ bool ScriptMetro::RemoveRamp(TileIndex tile, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveRamp>::Do(tile, static_cast<int16_t>(depth));
}

/**
 * Build, turn round or convert an underground signal; see ScriptMetro::BuildUndergroundSignal.
 * @param tile The tile above the track.
 * @param rail_track The track.
 * @param depth The underground level.
 * @param signal_type The type, or std::nullopt to only build the default or turn round.
 * @param signal_variant The variant.
 * @return Whether it worked.
 */
/* static */ bool ScriptMetro::BuildUndergroundSignalOfType(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth, std::optional<SQInteger> signal_type, SQInteger signal_variant)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, !signal_type.has_value() || (*signal_type >= ScriptRail::SIGNALTYPE_NORMAL && *signal_type <= ScriptRail::SIGNALTYPE_PBS_ONEWAY));
	EnforcePrecondition(false, signal_variant == ScriptMetro::SIGNAL_VARIANT_ELECTRIC || signal_variant == ScriptMetro::SIGNAL_VARIANT_SEMAPHORE);

	::Track track = MetroScriptTrackToTrack(rail_track);
	::SignalType type = signal_type.has_value() ? static_cast<::SignalType>(*signal_type) : ::SIGTYPE_PBS_ONEWAY;
	::SignalVariant variant = static_cast<::SignalVariant>(signal_variant);

	/* A signal of another type or variant is converted; otherwise building turns it round. */
	bool convert = false;
	if (signal_type.has_value() && _multilayer_map.IsAllocated()) {
		SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
		if (sid != INVALID_SLICE_ID) {
			const TileSlice &tp = _multilayer_map.GetSlice(sid);
			convert = HasUndergroundSignalOnTrack(tp, track) &&
					(::GetUndergroundSignalType(tp, track) != type || ::GetUndergroundSignalVariant(tp, track) != variant);
		}
	}

	return ScriptObject::Command<Commands::BuildUndergroundSignal>::Do(tile, track, static_cast<int16_t>(depth), type, variant, convert, false, ::SIGTYPE_BLOCK, ::SIGTYPE_LAST, 0);
}

/* static */ SQInteger ScriptMetro::BuildUndergroundSignal(HSQUIRRELVM vm)
{
	SQInteger nparam = sq_gettop(vm) - 1;
	if (nparam < 3 || nparam > 5) return sq_throwerror(vm, "wrong number of parameters: BuildUndergroundSignal(tile, rail_track, depth[, signal_type[, signal_variant]])");

	std::array<SQInteger, 5> args{0, 0, 0, ScriptRail::SIGNALTYPE_PBS_ONEWAY, SIGNAL_VARIANT_ELECTRIC};
	for (SQInteger i = 0; i < nparam; i++) {
		if (sq_gettype(vm, 2 + i) == OT_BOOL) {
			SQBool b;
			sq_getbool(vm, 2 + i, &b);
			args[i] = b ? 1 : 0;
		} else if (SQ_FAILED(sq_getinteger(vm, 2 + i, &args[i]))) {
			return sq_throwerror(vm, "parameters of BuildUndergroundSignal must be integers");
		}
	}

	std::optional<SQInteger> signal_type;
	if (nparam >= 4) signal_type = args[3];
	bool res = BuildUndergroundSignalOfType(TileIndex(static_cast<uint32_t>(args[0])), static_cast<ScriptRail::RailTrack>(args[1]), args[2], signal_type, args[4]);
	sq_pushbool(vm, res ? SQTrue : SQFalse);
	return 1;
}

/* static */ bool ScriptMetro::RemoveUndergroundSignal(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveUndergroundSignal>::Do(tile, MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::RemoveUndergroundSignalTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforcePrecondition(false, IsValidMetroTrack(rail_track));
	EnforceMetroDepth(depth);

	return ScriptObject::Command<Commands::RemoveUndergroundSignalTrack>::Do(end_tile, start_tile, MetroScriptTrackToTrack(rail_track), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::AppendOrderByStationID(VehicleID vehicle_id, StationID station_id, ScriptOrder::ScriptOrderFlags order_flags)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ScriptVehicle::IsPrimaryVehicle(vehicle_id));
	EnforcePrecondition(false, ScriptStation::IsValidStation(station_id));
	EnforcePrecondition(false, AreStationOrderFlagsValid(order_flags));

	Order order;
	order.MakeGoToStation(station_id);
	order.SetLoadType(static_cast<OrderLoadType>(GB(order_flags, 5, 3)));
	order.SetUnloadType(static_cast<OrderUnloadType>(GB(order_flags, 2, 3)));
	order.SetStopLocation(OrderStopLocation::FarEnd);
	order.SetNonStopType(static_cast<OrderNonStopFlags>(GB(order_flags, 0, 2)));

	/* CmdInsertOrder takes a real order index (implicit orders included);
	 * appending means after the last one, like ScriptOrder::AppendOrder. */
	return ScriptObject::Command<Commands::InsertOrder>::Do(0, vehicle_id, ::Vehicle::Get(vehicle_id)->GetNumOrders(), order);
}

/* static */ bool ScriptMetro::HasUnderground(TileIndex tile)
{
	if (!::IsValidTile(tile) || !_multilayer_map.IsAllocated()) return false;
	return _multilayer_map.GetColumn(tile).HasUnderground();
}

/* static */ bool ScriptMetro::IsUndergroundTile(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return false;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return false;

	return _multilayer_map.GetSlice(sid).kind != SliceKind::Surface;
}

/* static */ bool ScriptMetro::IsPortalTile(TileIndex tile)
{
	if (!::IsValidTile(tile) || !_multilayer_map.IsAllocated()) return false;
	return _multilayer_map.FindSliceByKind(tile, SliceKind::Portal) != INVALID_SLICE_ID;
}

/* static */ bool ScriptMetro::HasUndergroundRail(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return false;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return false;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	return slice.kind == SliceKind::Track || slice.kind == SliceKind::StationTile || slice.kind == SliceKind::Ramp;
}

/* static */ ScriptMetro::TileKind ScriptMetro::GetUndergroundTileKind(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return TK_NONE;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return TK_NONE;

	return MetroSliceKindToScript(_multilayer_map.GetSlice(sid).kind);
}

/* static */ SQInteger ScriptMetro::GetUndergroundTrackBits(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return ScriptRail::RAILTRACK_INVALID;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return ScriptRail::RAILTRACK_INVALID;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (slice.kind == SliceKind::Track) return slice.track.tracks;
	if (slice.kind == SliceKind::StationTile) return slice.station.tracks;
	return 0;
}

/* static */ ScriptRail::RailType ScriptMetro::GetUndergroundRailType(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return ScriptRail::RAILTYPE_INVALID;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return ScriptRail::RAILTYPE_INVALID;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	switch (slice.kind) {
		case SliceKind::Track: return static_cast<ScriptRail::RailType>(slice.track.railtype);
		case SliceKind::StationTile: return static_cast<ScriptRail::RailType>(slice.station.railtype);
		case SliceKind::Ramp: return static_cast<ScriptRail::RailType>(slice.ramp.railtype);
		case SliceKind::Portal: return static_cast<ScriptRail::RailType>(slice.portal.railtype);
		default: return ScriptRail::RAILTYPE_INVALID;
	}
}

/* static */ bool ScriptMetro::ConvertUndergroundRail(TileIndex start_tile, TileIndex end_tile, SQInteger depth)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));
	EnforceMetroDepth(depth);
	EnforcePrecondition(false, ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));

	return ScriptObject::Command<Commands::ConvertUndergroundRail>::Do(end_tile, start_tile, static_cast<::RailType>(ScriptRail::GetCurrentRailType()), static_cast<int16_t>(depth));
}

/* static */ bool ScriptMetro::HasUndergroundSignal(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroTrack(rail_track) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return false;

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return false;

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	Track track = MetroScriptTrackToTrack(rail_track);
	return HasUndergroundSignalOnTrack(slice, track);
}

/**
 * The underground track or ramp slice at a position carrying a track, if there is one.
 * @param tile The tile.
 * @param rail_track The track; it must exist on the slice (a ramp has the one of its axis).
 * @param depth The level (either level of a ramp).
 * @return The slice, or nullptr.
 */
static const TileSlice *GetMetroTrackPayload(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroTrack(rail_track) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return nullptr;
	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return nullptr;
	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	::Track track = MetroScriptTrackToTrack(rail_track);
	if (slice.kind == SliceKind::Ramp) return GetRampTrack(slice.ramp) == track ? &slice : nullptr;
	if (slice.kind != SliceKind::Track || (slice.track.tracks & TrackToTrackBits(track)) == 0) return nullptr;
	return &slice;
}

/* static */ ScriptRail::SignalType ScriptMetro::GetUndergroundSignalType(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	const TileSlice *tp = GetMetroTrackPayload(tile, rail_track, depth);
	::Track track = MetroScriptTrackToTrack(rail_track);
	if (tp == nullptr || !HasUndergroundSignalOnTrack(*tp, track)) return ScriptRail::SIGNALTYPE_NONE;
	return static_cast<ScriptRail::SignalType>(::GetUndergroundSignalType(*tp, track));
}

/* static */ ScriptMetro::SignalVariant ScriptMetro::GetUndergroundSignalVariant(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	const TileSlice *tp = GetMetroTrackPayload(tile, rail_track, depth);
	::Track track = MetroScriptTrackToTrack(rail_track);
	if (tp == nullptr || !HasUndergroundSignalOnTrack(*tp, track)) return SIGNAL_VARIANT_INVALID;
	return static_cast<SignalVariant>(::GetUndergroundSignalVariant(*tp, track));
}

/* static */ bool ScriptMetro::IsUndergroundSignalTwoWay(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth)
{
	const TileSlice *tp = GetMetroTrackPayload(tile, rail_track, depth);
	if (tp == nullptr) return false;
	uint8_t mask = SignalOnTrack(MetroScriptTrackToTrack(rail_track));
	return (GetUndergroundSignalMask(*tp) & mask) == mask;
}

/* static */ SQInteger ScriptMetro::GetMetroDepth(TileIndex tile)
{
	if (!::IsValidTile(tile) || !_multilayer_map.IsAllocated()) return 0;

	int16_t deepest = INT16_MAX;
	for (SliceID sid : _multilayer_map.GetColumn(tile).slices) {
		const TileSlice &slice = _multilayer_map.GetSlice(sid);
		if (slice.kind != SliceKind::Surface) deepest = std::min<int16_t>(deepest, slice.span.z_bot);
	}
	return deepest == INT16_MAX ? 0 : deepest;
}

/* static */ SQInteger ScriptMetro::GetFoundationDepth(TileIndex tile)
{
	if (!::IsValidTile(tile)) return 0;
	if (::IsTileType(tile, TileType::Industry)) return ::GetIndustryFoundationDepth(tile);
	return ::GetHouseFoundationDepth(tile);
}

/* static */ SQInteger ScriptMetro::GetWaterDepth(TileIndex tile)
{
	if (!::IsValidTile(tile)) return 0;
	return ::GetWaterDepth(tile);
}

/* static */ SQInteger ScriptMetro::GetHouseTypeCount()
{
	return static_cast<SQInteger>(HouseSpec::Specs().size());
}

/* static */ SQInteger ScriptMetro::GetHouseTypeFoundationDepth(SQInteger house_type)
{
	if (house_type < 0 || static_cast<size_t>(house_type) >= HouseSpec::Specs().size()) return -1;
	if (!HouseSpec::Get(static_cast<HouseID>(house_type))->enabled) return -1;
	return ::GetHouseTypeFoundationDepth(static_cast<HouseID>(house_type));
}

/* static */ bool ScriptMetro::BuildCanalArea(TileIndex start_tile, TileIndex end_tile)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(start_tile));
	EnforcePrecondition(false, ::IsValidTile(end_tile));

	return ScriptObject::Command<Commands::BuildCanal>::Do(end_tile, start_tile, WaterClass::Canal, false);
}

/* static */ bool ScriptMetro::PlaceHouse(TileIndex tile, SQInteger house_type)
{
	EnforceDeityMode(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, GetHouseTypeFoundationDepth(house_type) >= 0);

	return ScriptObject::Command<Commands::PlaceHouse>::Do(tile, static_cast<HouseID>(house_type), false, false);
}

/* static */ StationID ScriptMetro::GetUndergroundStationID(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile) || !IsValidMetroDepth(depth) || !_multilayer_map.IsAllocated()) return StationID::Invalid();

	SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(depth));
	if (sid == INVALID_SLICE_ID) return StationID::Invalid();

	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	return slice.kind == SliceKind::StationTile ? StationID(slice.station.station_id) : StationID::Invalid();
}

/* static */ bool ScriptMetro::HasSurfaceExit(StationID station_id)
{
	if (!ScriptStation::IsValidStation(station_id)) return false;

	auto it = _station_complexes.find(station_id.base());
	return it != _station_complexes.end() && it->second.HasSurfaceExit();
}

/* static */ SQInteger ScriptMetro::GetStationComplexPlatformCount(StationID station_id)
{
	if (!ScriptStation::IsValidStation(station_id)) return -1;

	auto it = _station_complexes.find(station_id.base());
	return it == _station_complexes.end() ? 0 : static_cast<SQInteger>(it->second.GetPlatforms().size());
}

/* static */ SQInteger ScriptMetro::GetStationComplexExitCount(StationID station_id)
{
	if (!ScriptStation::IsValidStation(station_id)) return -1;

	auto it = _station_complexes.find(station_id.base());
	return it == _station_complexes.end() ? 0 : static_cast<SQInteger>(it->second.GetExits().size());
}

/* static */ SQInteger ScriptMetro::GetDepthBelowSurface(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile)) return 0;
	return ::GetDepthBelowSurface(tile, static_cast<int16_t>(Clamp<SQInteger>(depth, INT16_MIN, INT16_MAX)));
}

/* static */ ScriptMetro::Soil ScriptMetro::GetSoil(TileIndex tile, SQInteger depth)
{
	if (!::IsValidTile(tile)) return SOIL_INVALID;
	int16_t level = static_cast<int16_t>(Clamp<SQInteger>(depth, INT16_MIN, INT16_MAX));
	if (::GetDepthBelowSurface(tile, level) < 1) return SOIL_INVALID;
	return static_cast<Soil>(::GetSoilLayer(tile, level));
}

/* static */ SQInteger ScriptMetro::GetStationComplexDeepestLevel(StationID station_id)
{
	if (!ScriptStation::IsValidStation(station_id)) return 0;

	auto it = _station_complexes.find(station_id.base());
	return it == _station_complexes.end() ? 0 : it->second.GetDeepestLevel();
}

/* static */ bool ScriptMetro::HasPortalConnection(TileIndex entry_tile, TileIndex exit_tile, SQInteger depth)
{
	if (!::IsValidTile(entry_tile) || !::IsValidTile(exit_tile) || !IsValidMetroDepth(depth)) return false;

	for (const PortalConnection &connection : GetPortalConnections(entry_tile)) {
		if (connection.exit_tile == exit_tile && connection.depth == depth) return true;
	}
	return false;
}

/* static */ bool ScriptMetro::IsVehicleUnderground(VehicleID vehicle_id)
{
	if (!ScriptVehicle::IsValidVehicle(vehicle_id)) return false;
	if (ScriptVehicle::GetVehicleType(vehicle_id) != ScriptVehicle::VT_RAIL) return false;

	const Train *train = Train::GetIfValid(vehicle_id);
	return train != nullptr && train->IsUnderground();
}

/* static */ SQInteger ScriptMetro::GetVehicleUndergroundDepth(VehicleID vehicle_id)
{
	if (!IsVehicleUnderground(vehicle_id)) return 0;

	const Train *train = Train::Get(vehicle_id);
	return train->underground_depth;
}

/* static */ TileIndex ScriptMetro::GetVehicleUndergroundTile(VehicleID vehicle_id)
{
	if (!IsVehicleUnderground(vehicle_id)) return INVALID_TILE;

	const Train *train = Train::Get(vehicle_id);
	return train->underground_tile_raw == 0 ? INVALID_TILE : TileIndex(train->underground_tile_raw);
}

/* static */ std::optional<std::string> ScriptMetro::CopyBlueprint(TileIndex start, TileIndex end, SQInteger top, SQInteger bottom, bool surface)
{
	if (!::IsValidTile(start) || !::IsValidTile(end)) return std::nullopt;
	BlueprintCaptureSpec spec;
	spec.start = start;
	spec.end = end;
	spec.z_top = static_cast<int16_t>(Clamp<SQInteger>(std::max(top, bottom), INT16_MIN, INT16_MAX));
	spec.z_bot = static_cast<int16_t>(Clamp<SQInteger>(std::min(top, bottom), INT16_MIN, INT16_MAX));
	spec.surface = surface;
	spec.anchor = spec.z_top;
	Blueprint bp = CaptureBlueprint(spec);
	if (bp.IsEmpty()) return std::nullopt;
	return BlueprintToJson(bp, false);
}

/** Split a blueprint of a script into the payloads of the paste command. */
static std::vector<std::string> PlanScriptBlueprint(const std::string &blueprint, SQInteger rotation, bool mirror, SQInteger options)
{
	auto bp = ParseBlueprintText(blueprint, nullptr);
	if (!bp.has_value()) return {};
	BlueprintPasteSpec spec;
	spec.transform.rotation = static_cast<uint8_t>(rotation & 3);
	spec.transform.mirror = mirror;
	spec.options = static_cast<uint8_t>(options & (BPO_JOIN_ADJACENT | BPO_BEST_EFFORT));
	if ((options & ScriptMetro::BLUEPRINT_CURRENT_RAILTYPE) != 0) spec.railtype = static_cast<::RailType>(ScriptRail::GetCurrentRailType());
	StringID error;
	return PlanBlueprintPaste(*bp, spec, &error);
}

/* static */ SQInteger ScriptMetro::GetBlueprintPasteChunkCount(const std::string &blueprint, SQInteger rotation, bool mirror, SQInteger options)
{
	std::vector<std::string> chunks = PlanScriptBlueprint(blueprint, rotation, mirror, options);
	return chunks.empty() ? -1 : static_cast<SQInteger>(chunks.size());
}

/* static */ bool ScriptMetro::PasteBlueprintChunk(const std::string &blueprint, TileIndex tile, SQInteger depth, SQInteger rotation, bool mirror, SQInteger options, SQInteger chunk)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, depth >= -256 && depth <= 256);
	std::vector<std::string> chunks = PlanScriptBlueprint(blueprint, rotation, mirror, options);
	EnforcePrecondition(false, chunk >= 0 && chunk < static_cast<SQInteger>(chunks.size()));

	return ScriptObject::Command<Commands::PasteUndergroundBlueprint>::Do(tile, static_cast<int16_t>(depth), static_cast<uint8_t>(options & (BPO_JOIN_ADJACENT | BPO_BEST_EFFORT)), chunks[chunk]);
}
