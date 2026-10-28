/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_cmd.h Command definitions for underground rail construction. */

#ifndef UNDERGROUND_CMD_H
#define UNDERGROUND_CMD_H

#include "../command_type.h"
#include "../settings_type.h"
#include "../track_type.h"
#include "../rail_type.h"
#include "../signal_type.h"
#include "../direction_type.h"
#include "../company_type.h"
#include "../tile_map.h"
#include "../tilearea_type.h"
#include "../station_type.h"
#include "station_complex.h"
#include "multilayer_map.h"
#include <vector>

/** station_id of CmdBuildUndergroundStation: always build a separate, new station. */
static constexpr uint16_t NEW_UNDERGROUND_STATION_ID = UINT16_MAX;
/**
 * station_id of CmdBuildUndergroundStation: join the own underground station
 * that has a platform next to the new area on the same level (vanilla
 * "join adjacent"), or build a new station when there is none.
 */
static constexpr uint16_t UNDERGROUND_JOIN_ADJACENT_ID = UINT16_MAX - 1;

/**
 * Height of the terrain above a column for the purposes of the underground:
 * the lowest corner of the tile, as for vanilla tunnels. A slice is valid
 * while it lies strictly below it; the one exception is a tunnel mouth on a
 * hillside, which sits exactly at the foot of its incline (see CmdBuildPortal).
 */
inline int16_t GetMetroSurfaceZ(TileIndex tile)
{
	return static_cast<int16_t>(GetTileZ(tile));
}

/**
 * Deepest absolute level of tunnels, ramps and portals (underground.deepest_tunnel_level).
 * @return The level, e.g. -3.
 */
inline int16_t GetMetroDeepestTunnelZ()
{
	/* 0 is in the setting's range only because a maximum can't be negative; a config file may still hold it. */
	return std::min<int16_t>(_settings_game.underground.deepest_tunnel_level, -1);
}

/**
 * Deepest absolute level of station platforms (underground.deepest_station_level), never below tunnels.
 * @return The level, e.g. -3.
 */
inline int16_t GetMetroDeepestStationZ()
{
	return std::clamp<int16_t>(_settings_game.underground.deepest_station_level, GetMetroDeepestTunnelZ(), -1);
}

inline int16_t GetMetroMinZ(TileIndex tile)
{
	int16_t surface_z = GetMetroSurfaceZ(tile);
	int16_t relative_floor = static_cast<int16_t>(surface_z - _settings_game.construction.max_underground_depth);
	return std::max<int16_t>(GetMetroDeepestTunnelZ(), relative_floor);
}

inline bool IsValidMetroZ(TileIndex tile, int16_t z)
{
	if (!IsValidTile(tile)) return false;
	int16_t surface_z = GetMetroSurfaceZ(tile);
	return z < surface_z && z >= GetMetroMinZ(tile);
}

/**
 * Lowest level the views, level buttons and scripts reach: the deepest tunnel
 * level, or deeper where something was built before the limit was raised.
 * New construction is limited by GetMetroMinZ.
 */
inline int16_t GetMetroGlobalMinZ()
{
	return std::min(GetMetroDeepestTunnelZ(), _multilayer_map.GetDeepestBuiltZ());
}

/**
 * Highest underground level anywhere: one below the highest land.
 * The highest land is cached until a tile height changes.
 */
int16_t GetMetroGlobalMaxZ();

struct TileSlice;

/* Company infrastructure accounting and ownership handling for underground slices. */
void UpdateUndergroundSliceInfrastructure(const TileSlice &s, int delta);
void AddUndergroundInfrastructureCounts();
void AfterLoadUndergroundPortalRailtypes();
void ChangeUndergroundOwnership(Owner old_owner, Owner new_owner);

CommandCost CmdBuildUndergroundRail(DoCommandFlags flags, TileIndex tile, RailType railtype, Track track, int16_t depth);
CommandCost CmdBuildUndergroundRailroadTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, RailType railtype, Track track, int16_t depth);
CommandCost CmdBuildUndergroundSignalTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth, uint8_t density, SignalType sigtype, SignalVariant sigvar);
CommandCost CmdRemoveUndergroundRail(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth);
CommandCost CmdRemoveUndergroundRailroadTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth);
CommandCost CmdBuildPortal(DoCommandFlags flags, TileIndex tile, RailType railtype, DiagDirection dir, int16_t depth);
CommandCost CmdBuildUndergroundStation(DoCommandFlags flags, TileIndex tile, RailType railtype, int16_t depth, uint16_t station_id, Axis axis, uint8_t numtracks, uint8_t plat_len);
CommandCost CmdBuildExitSurface(DoCommandFlags flags, TileIndex tile, uint16_t station_id, StationExitType exit_type);
CommandCost CmdRemoveExitSurface(DoCommandFlags flags, TileIndex tile);

/** Distance (DistanceMax) from a tile to the nearest platform of an underground station, UINT_MAX without platforms. */
uint GetUndergroundExitDistanceToStation(uint16_t station_id, TileIndex tile);
bool IsUnderpassRoadTile(TileIndex tile);

std::vector<StationID> FindAdjacentUndergroundStations(TileArea area, int16_t depth, Owner owner);
std::vector<StationID> FindUndergroundStationsNearColumns(TileArea area, int16_t depth, Owner owner);
std::vector<StationID> FindUndergroundJoinCandidates(TileArea area, Owner owner);
std::vector<StationID> FindUndergroundStationsForExit(TileIndex tile, Owner owner);
CommandCost CmdBuildRamp(DoCommandFlags flags, TileIndex tile, RailType railtype, DiagDirection dir, int16_t from_depth, int16_t to_depth);
CommandCost CmdBuildUndergroundSignal(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth, SignalType sigtype, SignalVariant sigvar, bool convert_signal, bool ctrl_pressed, SignalType cycle_start, SignalType cycle_stop, uint8_t signals_copy);
CommandCost CmdRemoveUndergroundSignal(DoCommandFlags flags, TileIndex tile, Track track, int16_t depth);
CommandCost CmdRemoveUndergroundSignalTrack(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, Track track, int16_t depth);
CommandCost CmdRemoveUndergroundStation(DoCommandFlags flags, TileIndex tile, int16_t depth);
CommandCost CmdRemoveUndergroundArea(DoCommandFlags flags, TileIndex tile, TileIndex end_tile, int16_t depth);
CommandCost CmdConvertUndergroundRail(DoCommandFlags flags, TileIndex tile, TileIndex area_start, RailType totype, int16_t depth);
CommandCost CmdRemoveRamp(DoCommandFlags flags, TileIndex tile, int16_t depth);
CommandCost CmdRemovePortal(DoCommandFlags flags, TileIndex tile);

/**
 * Check whether removing surface rail from a tile would orphan a metro portal.
 * The surface rail of a portal column may only be removed by CmdRemovePortal.
 * @param tile           Surface tile the rail is removed from.
 * @param removed_tracks Tracks about to be removed (TRACK_BIT_ALL when the whole tile is cleared or converted).
 * @param flags          Flags of the removing command; bankruptcy cleanup is never blocked.
 * @return STR_ERROR_MUST_REMOVE_PORTAL_FIRST when the removal must be refused, otherwise success.
 */
CommandCost CheckPortalSurfaceRailRemoval(TileIndex tile, TrackBits removed_tracks, DoCommandFlags flags);

/**
 * Check whether a surface track may be laid on a tile: a metro portal tile only
 * carries the portal's own axis track (built by CmdBuildPortal).
 * @param tile  Surface tile.
 * @param track Track about to be built.
 * @return STR_ERROR_UNDERGROUND_PORTAL_TRACK_ONLY when the track must be refused, otherwise success.
 */
CommandCost CheckPortalSurfaceRailBuild(TileIndex tile, Track track);

DEF_CMD_TRAIT(Commands::BuildUndergroundRail, CmdBuildUndergroundRail, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildUndergroundRailroadTrack, CmdBuildUndergroundRailroadTrack, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildUndergroundSignalTrack, CmdBuildUndergroundSignalTrack, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundRail, CmdRemoveUndergroundRail, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundRailroadTrack, CmdRemoveUndergroundRailroadTrack, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildPortal, CmdBuildPortal, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildUndergroundStation, CmdBuildUndergroundStation, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildExitSurface, CmdBuildExitSurface, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildRamp, CmdBuildRamp, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::BuildUndergroundSignal, CmdBuildUndergroundSignal, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundSignal, CmdRemoveUndergroundSignal, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundSignalTrack, CmdRemoveUndergroundSignalTrack, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundStation, CmdRemoveUndergroundStation, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveUndergroundArea, CmdRemoveUndergroundArea, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveRamp, CmdRemoveRamp, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemovePortal, CmdRemovePortal, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveExitSurface, CmdRemoveExitSurface, CommandFlag::Auto, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::ConvertUndergroundRail, CmdConvertUndergroundRail, CommandFlags({}), CommandType::LandscapeConstruction)

#endif /* UNDERGROUND_CMD_H */
