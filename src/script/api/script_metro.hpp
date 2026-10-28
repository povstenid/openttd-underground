/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_metro.hpp Native metro / underground helpers for GameScripts. */

#ifndef SCRIPT_METRO_HPP
#define SCRIPT_METRO_HPP

#include "script_company.hpp"
#include "script_error.hpp"
#include "script_order.hpp"
#include "script_rail.hpp"
#include "script_station.hpp"
#include "script_vehicle.hpp"
#include "../../multilayer/blueprint.h"

/**
 * Class that handles metro / underground construction helpers for GameScripts.
 * @api game
 */
class ScriptMetro : public ScriptObject {
public:
	/**
	 * Looks of an underground signal.
	 */
	enum SignalVariant {
		/* Note: these values represent part of the in-game SignalVariant enum */
		SIGNAL_VARIANT_ELECTRIC  = ::SIG_ELECTRIC,  ///< Light signal.
		SIGNAL_VARIANT_SEMAPHORE = ::SIG_SEMAPHORE, ///< Semaphore signal.
		SIGNAL_VARIANT_INVALID   = 0xFF,            ///< No signal.
	};

	/**
	 * All metro / underground related error messages.
	 */
	enum ErrorMessages {
		/** Base for metro / underground related errors */
		ERR_METRO_BASE = ScriptError::ERR_CAT_METRO << ScriptError::ERR_CAT_BIT_SIZE,

		/** The depth is not below the surface of the tile */
		ERR_UNDERGROUND_DEPTH_INVALID,          // [STR_ERROR_UNDERGROUND_DEPTH_INVALID]

		/** The depth is deeper than allowed for the tile */
		ERR_UNDERGROUND_TOO_DEEP,               // [STR_ERROR_UNDERGROUND_TOO_DEEP]

		/** Another underground structure (track, station, portal, ramp) occupies this depth */
		ERR_UNDERGROUND_SOMETHING_IN_THE_WAY,   // [STR_ERROR_UNDERGROUND_SOMETHING_IN_THE_WAY]

		/** Existing underground track at this depth has a different rail type */
		ERR_UNDERGROUND_RAIL_TYPE_MISMATCH,     // [STR_ERROR_UNDERGROUND_RAIL_TYPE_MISMATCH]

		/** A ramp must connect two adjacent depth levels */
		ERR_UNDERGROUND_RAMP_NOT_ADJACENT,      // [STR_ERROR_UNDERGROUND_RAMP_MUST_SPAN_ADJACENT_LEVELS]

		/** There is already a portal on this tile */
		ERR_UNDERGROUND_PORTAL_ALREADY_BUILT,   // [STR_ERROR_UNDERGROUND_PORTAL_ALREADY_HERE]

		/** There is no portal on this tile */
		ERR_UNDERGROUND_NO_PORTAL,              // [STR_ERROR_UNDERGROUND_NO_PORTAL_HERE]

		/** There is no underground station platform at this depth */
		ERR_UNDERGROUND_NO_STATION,             // [STR_ERROR_UNDERGROUND_NO_STATION_HERE]

		/** There is no ramp at this depth */
		ERR_UNDERGROUND_NO_RAMP,                // [STR_ERROR_UNDERGROUND_NO_RAMP_HERE]

		/** Nothing underground to remove in the area at this depth */
		ERR_UNDERGROUND_NOTHING_TO_REMOVE,      // [STR_ERROR_UNDERGROUND_NOTHING_TO_REMOVE]

		/** Underground construction prevents this surface change (e.g. lowering land above it) */
		ERR_UNDERGROUND_IN_THE_WAY,             // [STR_ERROR_UNDERGROUND_IN_THE_WAY]

		/** The surface rail belongs to a metro portal; remove the portal first */
		ERR_MUST_REMOVE_PORTAL_FIRST,           // [STR_ERROR_MUST_REMOVE_PORTAL_FIRST]

		/** A tunnel mouth (portal) on a dragged line needs a straight track */
		ERR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK, // [STR_ERROR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK]

		/** The station exit is further from the station's platforms than construction.max_station_exit_distance */
		ERR_UNDERGROUND_EXIT_TOO_FAR,           // [STR_ERROR_UNDERGROUND_EXIT_TOO_FAR_FROM_PLATFORM]

		/** An underpass exit needs a plain straight road tile */
		ERR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD, // [STR_ERROR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD]

		/** There is already a station exit on this tile */
		ERR_UNDERGROUND_EXIT_ALREADY_BUILT,     // [STR_ERROR_UNDERGROUND_EXIT_ALREADY_HERE]

		/** There is no station exit on this tile */
		ERR_UNDERGROUND_NO_EXIT,                // [STR_ERROR_UNDERGROUND_NO_EXIT_HERE]

		/** The tile is the pavilion of a station exit; remove the exit instead */
		ERR_UNDERGROUND_MUST_REMOVE_EXIT_FIRST, // [STR_ERROR_UNDERGROUND_MUST_REMOVE_EXIT_FIRST]

		/** The level is deeper than underground.deepest_station_level allows for stations */
		ERR_UNDERGROUND_STATION_TOO_DEEP,       // [STR_ERROR_UNDERGROUND_STATION_TOO_DEEP]

		/** The foundation of the house on the tile reaches down to this depth, see GetFoundationDepth */
		ERR_UNDERGROUND_HOUSE_FOUNDATION,       // [STR_ERROR_UNDERGROUND_HOUSE_FOUNDATION]

		/** The water on the tile reaches down to this depth, see GetWaterDepth */
		ERR_UNDERGROUND_WATER_TOO_SHALLOW,      // [STR_ERROR_UNDERGROUND_WATER_TOO_SHALLOW]

		/** Nothing can be built underground below the sea */
		ERR_UNDERGROUND_UNDER_SEA,              // [STR_ERROR_UNDERGROUND_UNDER_SEA]
	};

	/**
	 * Soil around underground construction, by the depth below the surface of the tile.
	 */
	enum Soil {
		/* Note: these values represent the in-game SoilLayer enum */
		SOIL_EARTH = 0,          ///< Depth 1: earth.
		SOIL_CLAY_LIGHT = 1,     ///< Depth 2: light clay.
		SOIL_CLAY = 2,           ///< Depth 3: clay.
		SOIL_CLAY_DARK = 3,      ///< Depth 4: dark clay.
		SOIL_MARBLE_LIGHT = 4,   ///< Depth 5: light marble.
		SOIL_MARBLE_GREY = 5,    ///< Depth 6: grey marble.
		SOIL_GRANITE_PINK = 6,   ///< Depth 7: pink granite.
		SOIL_GRANITE_GREY = 7,   ///< Depth 8: grey granite.
		SOIL_GRANITE_DARK = 8,   ///< Depth 9: dark granite.
		SOIL_GRANITE_BLACK = 9,  ///< Depth 10: black granite.
		SOIL_OBSIDIAN = 10,      ///< Depth 11 and deeper: obsidian.
		SOIL_SAND_LIGHT = 11,    ///< Depth 1 below a river, canal or the sea: light sand.
		SOIL_SAND = 12,          ///< Depth 2 below water: sand.
		SOIL_SAND_DARK = 13,     ///< Depth 3 below water: dark sand (depth 4 is dark clay again).
		SOIL_INVALID = -1,       ///< Not underground (an invalid tile, or a level at or above the surface).
	};

	/**
	 * Types of underground station surface exits.
	 */
	enum ExitType {
		EXIT_PAVILION = 0,   ///< Station building on a free tile (a bus station tile of the station), catchment radius 3.
		EXIT_UNDERPASS = 1,  ///< Stairs on a straight road tile, the road stays usable, catchment radius 2.
		EXIT_INVALID = -1,   ///< No (explicit) exit.
	};

	/**
	 * Underground tile kinds as exposed to scripts.
	 */
	enum TileKind {
		TK_NONE = 0,     ///< No underground content at the requested depth.
		TK_TRACK,        ///< Underground track slice.
		TK_STATION,      ///< Underground station platform slice.
		TK_PORTAL,       ///< Portal slice.
		TK_RAMP,         ///< Ramp slice.
		TK_INVALID = -1, ///< Invalid kind.
	};

	/**
	 * Options of pasting a blueprint, to be OR-ed together.
	 */
	enum BlueprintOptions {
		BLUEPRINT_JOIN_ADJACENT = ::BPO_JOIN_ADJACENT, ///< Join the stations of the blueprint to an own station next to them.
		BLUEPRINT_BEST_EFFORT = ::BPO_BEST_EFFORT,     ///< Build what can be built; otherwise nothing of a part is built when any piece of it cannot be.
		BLUEPRINT_CURRENT_RAILTYPE = 0x80,             ///< Build every piece with the current rail type instead of the blueprint's own.
	};

	/**
	 * Diagonal directions used by portals and ramps.
	 */
	enum DiagDirection {
		DIAGDIR_NE = ::DIAGDIR_NE, ///< Northeast.
		DIAGDIR_SE = ::DIAGDIR_SE, ///< Southeast.
		DIAGDIR_SW = ::DIAGDIR_SW, ///< Southwest.
		DIAGDIR_NW = ::DIAGDIR_NW, ///< Northwest.
		DIAGDIR_INVALID = -1,      ///< Invalid direction.
	};

	/**
	 * Create a playable company for headless GS scenarios.
	 * @param company Optional preferred company slot or COMPANY_INVALID for first free slot.
	 * @pre ScriptCompanyMode::IsDeity().
	 * @return The created company id or COMPANY_INVALID on failure.
	 */
	static ScriptCompany::CompanyID CreateCompany(ScriptCompany::CompanyID company);

	/**
	 * Remove a company as if it went bankrupt (headless GS scenario helper).
	 * Runs the regular company deletion, which removes all its property.
	 * @param company The company to remove.
	 * @pre ScriptCompanyMode::IsDeity().
	 * @pre At least one other company exists.
	 * @return Whether the company was removed.
	 */
	static bool RemoveCompany(ScriptCompany::CompanyID company);

	/**
	 * Merge a company into another one (headless GS scenario helper).
	 * Runs the regular company acquisition, transferring all property.
	 * @param company The company to be taken over; it no longer exists afterwards.
	 * @param into The company that takes over.
	 * @pre ScriptCompanyMode::IsDeity().
	 * @return Whether the merger happened.
	 */
	static bool MergeCompany(ScriptCompany::CompanyID company, ScriptCompany::CompanyID into);

	/**
	 * Build a single underground rail track at an absolute underground z level.
	 * @param tile Tile above the underground slice.
	 * @param rail_track Single track bit from ScriptRail::RailTrack.
	 * @param depth Absolute z below the local surface, e.g. -1 at sea level or 7 under a height-8 mountain.
	 * @return Whether building succeeded.
	 */
	static bool BuildUndergroundRail(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Build a stretch of underground rail at an absolute z level, like dragging
	 * rail with the underground rail tool. Tiles whose terrain lies above the
	 * level get underground track; where the line leaves the terrain a portal is
	 * built automatically on the incline of the tunnel mouth, and open tiles
	 * beyond it get ordinary surface rail.
	 * @param start_tile First tile of the stretch.
	 * @param end_tile Last tile of the stretch.
	 * @param rail_track Single track bit from ScriptRail::RailTrack giving the orientation of the stretch.
	 * @param depth Absolute underground z level.
	 * @pre start_tile and end_tile lie on a line along rail_track.
	 * @exception ScriptError::ERR_LAND_SLOPED_WRONG
	 * @exception ScriptMetro::ERR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK
	 * @return Whether building succeeded.
	 */
	static bool BuildUndergroundRailTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Remove a single underground rail track at a depth.
	 */
	static bool RemoveUndergroundRail(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Remove a stretch of underground rail, like Ctrl+dragging the underground
	 * rail tool. Tiles without the track are skipped.
	 * @param start_tile First tile of the stretch.
	 * @param end_tile Last tile of the stretch.
	 * @param rail_track Single track bit from ScriptRail::RailTrack giving the orientation of the stretch.
	 * @param depth Absolute underground z level.
	 * @pre start_tile and end_tile lie on a line along rail_track.
	 * @exception ScriptError::ERR_VEHICLE_IN_THE_WAY
	 * @return Whether at least one piece of track was removed.
	 */
	static bool RemoveUndergroundRailTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Build a surface portal into underground at an absolute z level.
	 */
	static bool BuildPortal(TileIndex tile, DiagDirection direction, SQInteger depth);

	/**
	 * Remove a portal together with the surface rail it created.
	 * Fails while a train is on the portal (surface or underground side).
	 * @param tile The portal tile.
	 * @return Whether removing succeeded.
	 */
	static bool RemovePortal(TileIndex tile);

	/**
	 * Build an underground station platform.
	 * @param tile Tile above the underground platform.
	 * @param direction Platform direction.
	 * @param depth Absolute underground z level.
	 * @param station_id Station to join, ScriptStation::STATION_NEW for a separate new station, or
	 *        ScriptStation::STATION_JOIN_ADJACENT to join the own station that has a platform next to
	 *        this one on the same level (a new station when there is none). An existing station id
	 *        joins that station, also when its platforms are on another level (distant join).
	 * @pre station_id == ScriptStation::STATION_NEW || station_id == ScriptStation::STATION_JOIN_ADJACENT || ScriptStation::IsValidStation(station_id).
	 * @exception ScriptError::ERR_STATION_TOO_SPREAD_OUT
	 */
	static bool BuildUndergroundStation(TileIndex tile, ScriptRail::RailTrack direction, SQInteger depth, StationID station_id);

	/**
	 * Remove an underground station platform.
	 */
	static bool RemoveUndergroundStation(TileIndex tile, SQInteger depth);

	/**
	 * Build a surface exit for an underground station complex.
	 * A pavilion turns a free tile into a bus station tile of the station,
	 * demolishing a town house on the tile (subject to the town authority); an
	 * underpass is placed on a straight road tile which stays a road.
	 * @param tile Surface tile of the exit.
	 * @param station_id The underground station.
	 * @param exit_type EXIT_PAVILION or EXIT_UNDERPASS.
	 * @pre The exit is within construction.max_station_exit_distance tiles of a platform of the station.
	 * @exception ScriptMetro::ERR_UNDERGROUND_EXIT_TOO_FAR
	 * @exception ScriptMetro::ERR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD
	 * @exception ScriptMetro::ERR_UNDERGROUND_EXIT_ALREADY_BUILT
	 * @return Whether the exit was built.
	 */
	static bool BuildExitSurface(TileIndex tile, StationID station_id, ExitType exit_type);

	/**
	 * Remove a surface exit (pavilion or underpass) together with the station tile of a pavilion.
	 * @param tile Surface tile of the exit.
	 * @exception ScriptMetro::ERR_UNDERGROUND_NO_EXIT
	 * @return Whether the exit was removed.
	 */
	static bool RemoveExitSurface(TileIndex tile);

	/**
	 * Get the type of the explicit surface exit on a tile.
	 * @param tile Surface tile.
	 * @return The exit type, or EXIT_INVALID when there is no pavilion or underpass.
	 */
	static ExitType GetExitType(TileIndex tile);

	/**
	 * Build a ramp between two underground levels. A ramp one level up on flat
	 * ground whose upper level is already the surface is built as a portal
	 * instead, turned back into the tunnel, so the line leaves for the surface.
	 */
	static bool BuildRamp(TileIndex tile, DiagDirection direction, SQInteger from_depth, SQInteger to_depth);

	/**
	 * Remove a ramp between two underground levels.
	 * Fails while a train is on the ramp.
	 * @param tile Tile above the ramp.
	 * @param depth Either absolute underground z level the ramp spans.
	 * @return Whether removing succeeded.
	 */
	static bool RemoveRamp(TileIndex tile, SQInteger depth);

#ifndef DOXYGEN_API
	/**
	 * Internal representation of the BuildUndergroundSignal function, which has optional parameters.
	 */
	static SQInteger BuildUndergroundSignal(HSQUIRRELVM vm);
#else
	/**
	 * Build a signal on an underground track, or turn round or convert the one there.
	 * All signal types of the surface are available underground and behave the same.
	 * Like on the surface a new path signal faces one way and a new block,
	 * entry, exit or combo signal both ways. Building again on a signal without
	 * a signal_type, or with the type and variant it already has, turns it
	 * round: both ways -> one way -> the other way -> both ways for block
	 * signals, one way -> the other way for path signals. Building with another
	 * type or variant converts the signal and keeps its direction (a two-way
	 * path signal becomes one way).
	 * @param tile The tile above the track.
	 * @param rail_track The track to build the signal on.
	 * @param depth The underground level.
	 * @param signal_type Optional: the type of the signal, one of SIGNALTYPE_NORMAL,
	 *  SIGNALTYPE_ENTRY, SIGNALTYPE_EXIT, SIGNALTYPE_COMBO, SIGNALTYPE_PBS and
	 *  SIGNALTYPE_PBS_ONEWAY of ScriptRail::SignalType; default SIGNALTYPE_PBS_ONEWAY.
	 * @param signal_variant Optional: SIGNAL_VARIANT_ELECTRIC (default) or SIGNAL_VARIANT_SEMAPHORE.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @pre signal_type is one of the types above (without SIGNALTYPE_TWOWAY).
	 * @return Whether the signal was built, turned round or converted.
	 */
	static bool BuildUndergroundSignal(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth, ScriptRail::SignalType signal_type, SignalVariant signal_variant);
#endif /* DOXYGEN_API */

	/**
	 * Remove the signals (both directions) from an underground track, or from a
	 * ramp between levels (named by either of its levels). The track or ramp stays.
	 * @param tile Tile above the underground slice.
	 * @param rail_track Single track bit from ScriptRail::RailTrack.
	 * @param depth Absolute underground z level.
	 * @exception ScriptError::ERR_VEHICLE_IN_THE_WAY
	 * @return Whether removing succeeded.
	 */
	static bool RemoveUndergroundSignal(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Remove every underground signal along a stretch of track, like dragging
	 * the underground signal tool in remove mode. Tiles without signals are skipped.
	 * @param start_tile First tile of the stretch.
	 * @param end_tile Last tile of the stretch.
	 * @param rail_track Single track bit from ScriptRail::RailTrack giving the orientation of the stretch.
	 * @param depth Absolute underground z level.
	 * @pre start_tile and end_tile lie on a line along rail_track.
	 * @return Whether at least one signal was removed.
	 */
	static bool RemoveUndergroundSignalTrack(TileIndex start_tile, TileIndex end_tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Get the type of the signal on an underground track.
	 * @param tile The tile above the track.
	 * @param rail_track The track.
	 * @param depth The underground level.
	 * @return The signal type (without SIGNALTYPE_TWOWAY, see IsUndergroundSignalTwoWay),
	 *  or SIGNALTYPE_NONE when the track has no signal.
	 */
	static ScriptRail::SignalType GetUndergroundSignalType(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Get the variant (light or semaphore) of the signal on an underground track.
	 * @param tile The tile above the track.
	 * @param rail_track The track.
	 * @param depth The underground level.
	 * @return The variant, or SIGNAL_VARIANT_INVALID when the track has no signal.
	 */
	static SignalVariant GetUndergroundSignalVariant(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Check whether the signal on an underground track faces both ways.
	 * @param tile The tile above the track.
	 * @param rail_track The track.
	 * @param depth The underground level.
	 * @return True if there are signals for both directions of the track.
	 */
	static bool IsUndergroundSignalTwoWay(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Append a goto-station order using a station id.
	 * This is needed for underground stations, which do not have surface Station tiles.
	 */
	static bool AppendOrderByStationID(VehicleID vehicle_id, StationID station_id, ScriptOrder::ScriptOrderFlags order_flags);

	/**
	 * Check whether a tile has any underground content.
	 */
	static bool HasUnderground(TileIndex tile);

	/**
	 * Check whether a tile has underground content at the given depth.
	 */
	static bool IsUndergroundTile(TileIndex tile, SQInteger depth);

	/**
	 * Check whether a tile contains a portal.
	 */
	static bool IsPortalTile(TileIndex tile);

	/**
	 * Check whether a tile has traversable underground rail at the given depth.
	 */
	static bool HasUndergroundRail(TileIndex tile, SQInteger depth);

	/**
	 * Get the kind of underground content at the given depth.
	 */
	static TileKind GetUndergroundTileKind(TileIndex tile, SQInteger depth);

	/**
	 * Get the traversable underground track bits at the given depth.
	 */
	static SQInteger GetUndergroundTrackBits(TileIndex tile, SQInteger depth);

	/**
	 * Get the rail type of the underground track, station, ramp or portal at the given depth.
	 * @param tile Tile above the underground slice.
	 * @param depth Absolute underground z level.
	 * @return The rail type, or ScriptRail::RAILTYPE_INVALID when there is no rail there.
	 */
	static ScriptRail::RailType GetUndergroundRailType(TileIndex tile, SQInteger depth);

	/**
	 * Convert the rail type of the underground construction in a rectangle at
	 * one level to the current rail type (see ScriptRail::SetCurrentRailType),
	 * like ScriptRail::ConvertRailType on the surface. A portal is converted
	 * together with the surface rail of its tile.
	 * @param start_tile One corner of the rectangle.
	 * @param end_tile The opposite corner of the rectangle.
	 * @param depth Absolute underground z level.
	 * @return Whether anything was converted.
	 */
	static bool ConvertUndergroundRail(TileIndex start_tile, TileIndex end_tile, SQInteger depth);

	/**
	 * Check whether an underground track has a signal (of any type, in either direction).
	 */
	static bool HasUndergroundSignal(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth);

	/**
	 * Get the deepest underground level on this tile, or 0 when there is none.
	 */
	static SQInteger GetMetroDepth(TileIndex tile);

	/**
	 * Get how many levels below the surface the foundation of the house on a tile
	 * takes. Nothing underground can be built there at a depth below the surface
	 * of this many levels or fewer; towns and players do not build a house whose
	 * foundation would reach underground construction.
	 * @param tile The tile.
	 * @return 0 to 2; 0 when there is no house on the tile.
	 */
	static SQInteger GetFoundationDepth(TileIndex tile);

	/**
	 * Get how many levels below its surface the water on a tile takes: a river or
	 * canal 1 next to land (on one of its four sides) and 2 otherwise, the sea 1 to 3
	 * by the steps to the nearest land. Nothing underground can be built there at a
	 * depth below the surface of this many levels or fewer; deeper it can, below the
	 * sea too. Only with the stricter setting "sea tunnels forbidden" nothing at all
	 * can be built below the sea.
	 * @param tile The tile.
	 * @return 0 to 3; 0 for land, a shore included.
	 */
	static SQInteger GetWaterDepth(TileIndex tile);

	/**
	 * Get the number of house types of this game.
	 * @return House types are 0 to this minus 1.
	 */
	static SQInteger GetHouseTypeCount();

	/**
	 * Get the foundation depth of a house type, see GetFoundationDepth.
	 * @param house_type The house type.
	 * @return 0 to 2, or -1 when the house type is invalid or cannot be built.
	 */
	static SQInteger GetHouseTypeFoundationDepth(SQInteger house_type);

	/**
	 * Build canals on a rectangle of tiles in one go, like dragging the canal tool.
	 * Water must not come over underground construction it would reach, see GetWaterDepth.
	 * @param start_tile One corner of the rectangle.
	 * @param end_tile The other corner.
	 * @pre ScriptCompanyMode::IsValid().
	 * @return Whether the canals were built.
	 */
	static bool BuildCanalArea(TileIndex start_tile, TileIndex end_tile);

	/**
	 * Place a house of the town closest to the tile, like the scenario editor does.
	 * @param tile The tile, the north tile of a multi-tile house.
	 * @param house_type The house type.
	 * @pre ScriptCompanyMode::IsDeity().
	 * @pre The setting economy.place_houses is not "forbidden".
	 * @return Whether the house was placed.
	 */
	static bool PlaceHouse(TileIndex tile, SQInteger house_type);

	/**
	 * Get the station id of an underground platform tile.
	 */
	static StationID GetUndergroundStationID(TileIndex tile, SQInteger depth);

	/**
	 * Check whether a station complex has at least one surface exit.
	 */
	static bool HasSurfaceExit(StationID station_id);

	/**
	 * Count underground platform nodes in a station complex.
	 */
	static SQInteger GetStationComplexPlatformCount(StationID station_id);

	/**
	 * Count surface exits in a station complex.
	 */
	static SQInteger GetStationComplexExitCount(StationID station_id);

	/**
	 * Get the depth of a level below the surface of a tile: the height of the
	 * land (its lowest corner) minus the level. The soil and the price of
	 * underground construction depend on this depth.
	 * @param tile The tile.
	 * @param depth The absolute level.
	 * @pre ScriptMap::IsValidTile(tile).
	 * @return The depth below the surface (1 for the level right under the land), or 0 for an invalid tile.
	 */
	static SQInteger GetDepthBelowSurface(TileIndex tile, SQInteger depth);

	/**
	 * Get the soil of a tile at a level.
	 * @param tile The tile.
	 * @param depth The absolute level.
	 * @return The soil, or SOIL_INVALID for an invalid tile or a level that is not below the surface.
	 */
	static Soil GetSoil(TileIndex tile, SQInteger depth);

	/**
	 * Get the deepest platform level of a station complex.
	 */
	static SQInteger GetStationComplexDeepestLevel(StationID station_id);

	/**
	 * Check whether two portals are connected at a depth.
	 */
	static bool HasPortalConnection(TileIndex entry_tile, TileIndex exit_tile, SQInteger depth);

	/**
	 * Check whether a train is currently underground.
	 */
	static bool IsVehicleUnderground(VehicleID vehicle_id);

	/**
	 * Get the current underground depth of a train, or 0 when on surface.
	 */
	static SQInteger GetVehicleUndergroundDepth(VehicleID vehicle_id);

	/**
	 * Get the tile currently occupied by the underground train state.
	 */
	static TileIndex GetVehicleUndergroundTile(VehicleID vehicle_id);

	/**
	 * Copy the construction in an area into a blueprint: the underground levels
	 * from top to bottom and, when asked, the surface (rail, signals, depots and
	 * the exits of the stations copied) with every level above bottom. The top
	 * level becomes the level the blueprint is pasted on.
	 * @param start One corner of the area.
	 * @param end The other corner of the area.
	 * @param top Highest underground level copied.
	 * @param bottom Lowest underground level copied.
	 * @param surface Copy the surface too.
	 * @return The blueprint as JSON, or null when there is nothing to copy.
	 */
	static std::optional<std::string> CopyBlueprint(TileIndex start, TileIndex end, SQInteger top, SQInteger bottom, bool surface);

	/**
	 * Get into how many parts (commands) a blueprint is split when it is pasted.
	 * @param blueprint The blueprint: a share string or the JSON of a blueprint file.
	 * @param rotation Quarter turns, 0 to 3.
	 * @param mirror Mirror it first.
	 * @param options BlueprintOptions.
	 * @return The number of parts, or -1 when the blueprint is invalid or uses a rail type this game does not have.
	 */
	static SQInteger GetBlueprintPasteChunkCount(const std::string &blueprint, SQInteger rotation, bool mirror, SQInteger options);

	/**
	 * Build one part of a blueprint. Build the parts 0 to GetBlueprintPasteChunkCount() - 1 in order, with the same arguments.
	 * @param blueprint The blueprint: a share string or the JSON of a blueprint file.
	 * @param tile North corner of its footprint.
	 * @param depth Level its top copied level goes to.
	 * @param rotation Quarter turns, 0 to 3.
	 * @param mirror Mirror it first.
	 * @param options BlueprintOptions.
	 * @param chunk The part.
	 * @pre ScriptCompanyMode::IsValid().
	 * @return Whether (some of) the part was built.
	 */
	static bool PasteBlueprintChunk(const std::string &blueprint, TileIndex tile, SQInteger depth, SQInteger rotation, bool mirror, SQInteger options, SQInteger chunk);

private:
	/** BuildUndergroundSignal with its parameters parsed; signal_type is empty when the script gave none. */
	static bool BuildUndergroundSignalOfType(TileIndex tile, ScriptRail::RailTrack rail_track, SQInteger depth, std::optional<SQInteger> signal_type, SQInteger signal_variant);
};

#endif /* SCRIPT_METRO_HPP */
