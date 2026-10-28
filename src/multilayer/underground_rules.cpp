/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_rules.cpp House and industry foundations and water depth above the underground. */

#include "../stdafx.h"
#include "underground_rules.h"
#include "multilayer_map.h"
#include "underground_cmd.h"
#include "../command_func.h"
#include "../house.h"
#include "../industry_map.h"
#include "../map_func.h"
#include "../settings_type.h"
#include "../tile_map.h"
#include "../town_map.h"
#include "../water_map.h"
#include "../table/strings.h"
#include <algorithm>
#include <vector>

#include "../safeguards.h"

/**
 * Share of the largest population per tile from which a house has the deepest foundation.
 * @return Percentage, 60 by default.
 */
uint GetFoundationThresholdHighPercent()
{
	return _settings_game.underground.foundation_high_percent;
}

/**
 * Share of the largest population per tile from which a house has a foundation of one level,
 * never above the one of the deepest foundation (a config file may hold any pair).
 * @return Percentage, 25 by default.
 */
uint GetFoundationThresholdMidPercent()
{
	return std::min(_settings_game.underground.foundation_medium_percent, _settings_game.underground.foundation_high_percent);
}

/**
 * Whether the depth of the sea decides where one can build below it ("by sea depth"),
 * instead of nothing at all being built below the sea ("forbidden"). "By sea depth" is the default.
 * @return True for "by sea depth".
 */
bool AreSeaTunnelsAllowedByDepth()
{
	return _settings_game.underground.sea_tunnels == UST_BY_SEA_DEPTH;
}

/** Foundation depth of every house type, filled on first use. */
static std::vector<uint8_t> _house_foundation_depths;
static bool _house_foundation_depths_valid = false;
/** What the table was computed for; a change of any of these recomputes it. */
static LandscapeType _house_foundation_landscape{};
static uint _house_foundation_high = 0;
static uint _house_foundation_mid = 0;

/** The set of houses changed (NewGRFs loaded for a new or a loaded game): compute the foundations anew. */
void InvalidateHouseFoundationDepths()
{
	_house_foundation_depths_valid = false;
}

/**
 * Number of tiles of a house, given the spec of its north tile.
 * @param hs House spec.
 * @return 1, 2 or 4; 0 for the other tiles of a multi-tile house.
 */
static uint GetHouseTileCount(const HouseSpec &hs)
{
	if (hs.building_flags.Test(BuildingFlag::Size2x2)) return 4;
	if (hs.building_flags.Any(BUILDING_HAS_2_TILES)) return 2;
	if (hs.building_flags.Test(BuildingFlag::Size1x1)) return 1;
	return 0;
}

/**
 * Compute the foundation depth of every house type (spec section 3.2): the
 * population per tile p of a house against the largest population per tile P
 * of the houses this game can build; p >= high % of P gives 2 levels,
 * p >= mid % of P gives 1, else 0. A church has none and a stadium one,
 * whatever their population. The other tiles of a multi-tile house share the
 * foundation of its north tile.
 */
static void ComputeHouseFoundationDepths()
{
	const std::vector<HouseSpec> &specs = HouseSpec::Specs();
	const HouseZones climate = GetClimateMaskForLandscape();
	const uint high = GetFoundationThresholdHighPercent();
	const uint mid = GetFoundationThresholdMidPercent();

	auto population = [&specs](size_t first, uint tiles) {
		uint64_t sum = 0;
		for (size_t i = first; i < first + tiles && i < specs.size(); i++) sum += specs[i].population;
		return sum;
	};

	/* The largest population per tile, as the fraction best_pop / best_tiles. */
	uint64_t best_pop = 0;
	uint64_t best_tiles = 1;
	for (size_t i = 0; i < specs.size(); i++) {
		const HouseSpec &hs = specs[i];
		uint tiles = GetHouseTileCount(hs);
		if (tiles == 0) continue;
		if (!hs.enabled || hs.grf_prop.override_id != INVALID_HOUSE_ID || !hs.building_availability.Any(climate)) continue;
		if (hs.building_flags.Any({BuildingFlag::IsChurch, BuildingFlag::IsStadium})) continue;
		uint64_t pop = population(i, tiles);
		if (pop * best_tiles > best_pop * tiles) {
			best_pop = pop;
			best_tiles = tiles;
		}
	}

	_house_foundation_depths.assign(specs.size(), 0);
	for (size_t i = 0; i < specs.size(); i++) {
		const HouseSpec &hs = specs[i];
		uint tiles = GetHouseTileCount(hs);
		if (tiles == 0) continue;

		uint8_t f = 0;
		if (hs.building_flags.Test(BuildingFlag::IsChurch)) {
			f = 0;
		} else if (hs.building_flags.Test(BuildingFlag::IsStadium)) {
			f = 1;
		} else if (best_pop > 0) {
			/* p / P >= percent / 100, with p = pop / tiles and P = best_pop / best_tiles. */
			uint64_t lhs = population(i, tiles) * 100 * best_tiles;
			if (lhs >= high * best_pop * tiles) {
				f = 2;
			} else if (lhs >= mid * best_pop * tiles) {
				f = 1;
			}
		}
		for (size_t j = i; j < i + tiles && j < specs.size(); j++) _house_foundation_depths[j] = f;
	}

	_house_foundation_landscape = _settings_game.game_creation.landscape;
	_house_foundation_high = high;
	_house_foundation_mid = mid;
	_house_foundation_depths_valid = true;
}

/**
 * Foundation depth of a house type.
 * @param house House type.
 * @return Levels below the surface its foundation takes, 0 to MAX_HOUSE_FOUNDATION_DEPTH.
 */
uint8_t GetHouseTypeFoundationDepth(HouseID house)
{
	if (!_house_foundation_depths_valid || _house_foundation_depths.size() != HouseSpec::Specs().size() ||
			_house_foundation_landscape != _settings_game.game_creation.landscape ||
			_house_foundation_high != GetFoundationThresholdHighPercent() || _house_foundation_mid != GetFoundationThresholdMidPercent()) {
		ComputeHouseFoundationDepths();
	}
	return house < _house_foundation_depths.size() ? _house_foundation_depths[house] : 0;
}

/**
 * Foundation depth of the house on a tile, from the first stage of its construction.
 * @param tile Tile.
 * @return Levels below the surface its foundation takes; 0 when there is no house.
 */
uint8_t GetHouseFoundationDepth(TileIndex tile)
{
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::House)) return 0;
	return GetHouseTypeFoundationDepth(GetHouseType(tile));
}

/**
 * Whether underground construction of a column reaches a range of depths below its surface.
 * @param tile Column.
 * @param min_depth Smallest depth below the surface, at least 1.
 * @param max_depth Largest depth below the surface.
 * @return True if a slice lies at a depth g with min_depth <= g <= max_depth.
 */
static bool HasUndergroundInDepthRange(TileIndex tile, uint min_depth, uint max_depth)
{
	if (min_depth > max_depth || !_multilayer_map.IsAllocated()) return false;
	const int surface = GetMetroSurfaceZ(tile);
	for (SliceID sid : _multilayer_map.GetColumn(tile).slices) {
		if (!_multilayer_map.IsValidSlice(sid)) continue;
		const TileSlice &s = _multilayer_map.GetSlice(sid);
		if (s.kind == SliceKind::Surface) continue;
		/* The slice takes the levels z_bot .. z_top - 1. */
		int shallowest = std::max<int>(surface - (s.span.z_top - 1), min_depth);
		int deepest = std::min<int>(surface - s.span.z_bot, max_depth);
		if (shallowest <= deepest) return true;
	}
	return false;
}

/**
 * Whether underground construction of a column lies within a depth below its surface.
 * @param tile Column.
 * @param depth Depth below the surface, e.g. a foundation depth.
 * @return True if a slice lies at a depth g with 1 <= g <= depth.
 */
bool HasUndergroundWithinDepth(TileIndex tile, uint depth)
{
	return HasUndergroundInDepthRange(tile, 1, depth);
}

/**
 * Whether the foundation of a house would be clear of the underground on every tile of the house.
 * @param house House type.
 * @param tile North tile of the house.
 * @return True if the house may be built there.
 */
bool CanHouseFoundationBeBuilt(HouseID house, TileIndex tile)
{
	uint f = GetHouseTypeFoundationDepth(house);
	if (f == 0) return true;
	const HouseSpec *hs = HouseSpec::Get(house);
	uint w = hs->building_flags.Any(BUILDING_2_TILES_X) ? 2 : 1;
	uint h = hs->building_flags.Any(BUILDING_2_TILES_Y) ? 2 : 1;
	for (uint dx = 0; dx < w; dx++) {
		for (uint dy = 0; dy < h; dy++) {
			TileIndex t = TileAddWrap(tile, dx, dy);
			if (t != INVALID_TILE && HasUndergroundWithinDepth(t, f)) return false;
		}
	}
	return true;
}

/**
 * Foundation depth of every industry (setting underground.industry_foundation_depth).
 * @return Levels below the surface, 0 to MAX_INDUSTRY_FOUNDATION_DEPTH.
 */
uint8_t GetIndustryFoundationDepthSetting()
{
	return std::min(_settings_game.underground.industry_foundation_depth, MAX_INDUSTRY_FOUNDATION_DEPTH);
}

/**
 * Foundation depth of the industry on a tile: its basement, the same for every tile of every industry.
 * @param tile Tile.
 * @return Levels below the surface its foundation takes; 0 when there is no industry.
 */
uint8_t GetIndustryFoundationDepth(TileIndex tile)
{
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::Industry)) return 0;
	return GetIndustryFoundationDepthSetting();
}

/**
 * Whether a tile counts as water for the depth rules: sea, river or canal,
 * also with a lock, a ship depot, a buoy, a dock or an oil rig on it.
 * A shore (land sloping to the water) is land.
 * @param tile Tile.
 * @return True for water.
 */
bool IsUndergroundWaterTile(TileIndex tile)
{
	if (!IsValidTile(tile) || !HasTileWaterClass(tile)) return false;
	if (IsCoastTile(tile)) return false;
	return GetWaterClass(tile) != WaterClass::Invalid;
}

/** Tiles that are about to become water (a canal being built, a tile being flooded). */
struct NewWater {
	std::vector<TileIndex> sorted; ///< The tiles, sorted for the lookup.

	NewWater() = default;
	explicit NewWater(std::span<const TileIndex> tiles) : sorted(tiles.begin(), tiles.end()) { std::sort(this->sorted.begin(), this->sorted.end()); }

	bool Contains(TileIndex t) const { return std::binary_search(this->sorted.begin(), this->sorted.end(), t); }
};

/**
 * The depth down to which water keeps the underground out: its depth, below
 * the sea too (the normal rule); all the way down below the sea only with the
 * stricter setting "sea tunnels forbidden".
 */
static uint GetWaterBlockedDepth(bool sea, uint w)
{
	if (sea && !AreSeaTunnelsAllowedByDepth()) return UINT8_MAX;
	return w;
}

/** Whether a column has any underground construction. */
static bool HasUndergroundColumn(TileIndex tile)
{
	return tile != INVALID_TILE && IsValidTile(tile) && _multilayer_map.GetColumn(tile).HasUnderground();
}

/**
 * Whether a tile is land for the depth of the water next to it. A tile off
 * the map or at its edge is no land.
 */
static bool IsLandForWaterDepth(TileIndex tile, const NewWater &extra)
{
	if (tile == INVALID_TILE || !IsValidTile(tile)) return false;
	return !IsUndergroundWaterTile(tile) && !extra.Contains(tile);
}

/**
 * Depth of the water on a tile (spec section 4.1), counting the given new water.
 * @param tile Water tile.
 * @param sea The tile is sea.
 * @param extra New water.
 * @return 1 or 2 for a river or canal (land on one of its four sides or not), 1 to 3 for the sea (distance to the nearest land).
 */
static uint8_t GetWaterDepthWith(TileIndex tile, bool sea, const NewWater &extra)
{
	if (!sea) {
		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
			TileIndexDiffC off = TileIndexDiffCByDiagDir(d);
			if (IsLandForWaterDepth(TileAddWrap(tile, off.x, off.y), extra)) return 1;
		}
		return 2;
	}
	/* Steps to the nearest land, along the sides of the tiles, searched up to 2 steps away. */
	for (int dist = 1; dist <= 2; dist++) {
		for (int dx = -dist; dx <= dist; dx++) {
			int rest = dist - std::abs(dx);
			for (int dy : {-rest, rest}) {
				if (IsLandForWaterDepth(TileAddWrap(tile, dx, dy), extra)) return dist;
				if (rest == 0) break;
			}
		}
	}
	return 3;
}

/**
 * Depth of the water on a tile: how many levels below its surface the water takes.
 * For the sea it grows with the distance from the shore: 1, 2, then 3 for the open sea.
 * Below it building is allowed, unless the stricter setting forbids the sea altogether.
 * @param tile Tile.
 * @return 0 for land, else 1 to 3.
 */
uint8_t GetWaterDepth(TileIndex tile)
{
	if (!IsUndergroundWaterTile(tile)) return 0;
	return GetWaterDepthWith(tile, GetWaterClass(tile) == WaterClass::Sea, NewWater{});
}

/**
 * What the surface takes of the ground at a level of a column: a house or
 * industry foundation, the water column, or the forbidden ground below the sea.
 * It follows the same rules as CheckUndergroundClearance.
 * @param tile Column.
 * @param level Absolute level.
 * @param[out] depth If not nullptr: the depth of the foundation or the water, in levels below the surface.
 * @return The obstacle, UndergroundObstacle::None for free soil.
 */
UndergroundObstacle GetUndergroundObstacle(TileIndex tile, int16_t level, uint *depth)
{
	if (!IsValidTile(tile)) return UndergroundObstacle::None;
	int g = GetMetroSurfaceZ(tile) - level;
	if (g < 1) return UndergroundObstacle::None;
	uint f = GetHouseFoundationDepth(tile);
	if (static_cast<uint>(g) <= f) {
		if (depth != nullptr) *depth = f;
		return UndergroundObstacle::HouseFoundation;
	}
	f = GetIndustryFoundationDepth(tile);
	if (static_cast<uint>(g) <= f) {
		if (depth != nullptr) *depth = f;
		return UndergroundObstacle::IndustryFoundation;
	}
	if (!IsUndergroundWaterTile(tile)) return UndergroundObstacle::None;
	bool sea = GetWaterClass(tile) == WaterClass::Sea;
	uint w = GetWaterDepthWith(tile, sea, NewWater{});
	if (depth != nullptr) *depth = w;
	if (static_cast<uint>(g) <= w) return UndergroundObstacle::Water;
	return GetWaterBlockedDepth(sea, w) == UINT8_MAX ? UndergroundObstacle::SeaForbidden : UndergroundObstacle::None;
}

/**
 * Check that the surface of a column leaves room for underground construction
 * at a level: no house or industry foundation and no water reach down to it. Every
 * underground build command uses this, so the test runs of the GUI (cost
 * estimate, blueprint paste) refuse the same.
 * @param tile Column.
 * @param level Absolute level of the construction.
 * @return Success, or why the level cannot be used here.
 */
CommandCost CheckUndergroundClearance(TileIndex tile, int16_t level)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	int g = GetMetroSurfaceZ(tile) - level;
	/* A tunnel mouth on a hillside at the foot of its incline is not below the surface. */
	if (g < 1) return CommandCost();

	uint f = GetHouseFoundationDepth(tile);
	if (static_cast<uint>(g) <= f) return CommandCostWithParam(STR_ERROR_UNDERGROUND_HOUSE_FOUNDATION, f);
	f = GetIndustryFoundationDepth(tile);
	if (static_cast<uint>(g) <= f) return CommandCostWithParam(STR_ERROR_UNDERGROUND_INDUSTRY_FOUNDATION, f);

	if (IsUndergroundWaterTile(tile)) {
		bool sea = GetWaterClass(tile) == WaterClass::Sea;
		uint w = GetWaterDepthWith(tile, sea, NewWater{});
		if (GetWaterBlockedDepth(sea, w) == UINT8_MAX) return CommandCost(STR_ERROR_UNDERGROUND_UNDER_SEA);
		if (static_cast<uint>(g) <= w) return CommandCostWithParam(STR_ERROR_UNDERGROUND_WATER_TOO_SHALLOW, w);
	}
	return CommandCost();
}

/**
 * Check that land turning into water does not put water onto underground
 * construction: neither the new water itself nor the water next to it, which
 * gets deeper when its land goes, may reach down to a slice that it did not
 * reach before. Construction that is there already under old water stays.
 * Only columns with underground construction are looked at, so a large area
 * of new water costs little where there is no metro.
 * @param tiles Tiles becoming water; checked all at once, so the test run of a dragged canal sees what its execution builds.
 * @param sea They become sea where they lie at sea level (TileHeight 0), else canal; false: all canal or river.
 * @return Success, or STR_ERROR_UNDERGROUND_IN_THE_WAY.
 */
CommandCost CheckNewWaterOverUnderground(std::span<const TileIndex> tiles, bool sea)
{
	if (!_multilayer_map.IsAllocated()) return CommandCost();
	const NewWater extra(tiles);

	for (TileIndex t : tiles) {
		if (!IsValidTile(t) || IsUndergroundWaterTile(t)) continue;
		/* Like CmdBuildCanal: the sea tool makes a canal above sea level. */
		bool t_sea = sea && TileHeight(t) == 0;
		if (HasUndergroundColumn(t) && HasUndergroundWithinDepth(t, GetWaterBlockedDepth(t_sea, GetWaterDepthWith(t, t_sea, extra)))) {
			return CommandCost(STR_ERROR_UNDERGROUND_IN_THE_WAY);
		}

		/* Water around it loses land: up to two steps away for the sea, one for rivers and canals. */
		for (int dx = -2; dx <= 2; dx++) {
			for (int dy = -2; dy <= 2; dy++) {
				if (std::abs(dx) + std::abs(dy) > 2 || (dx == 0 && dy == 0)) continue;
				TileIndex u = TileAddWrap(t, dx, dy);
				if (!HasUndergroundColumn(u) || !IsUndergroundWaterTile(u)) continue;
				bool u_sea = GetWaterClass(u) == WaterClass::Sea;
				uint before = GetWaterBlockedDepth(u_sea, GetWaterDepthWith(u, u_sea, NewWater{}));
				uint after = GetWaterBlockedDepth(u_sea, GetWaterDepthWith(u, u_sea, extra));
				if (HasUndergroundInDepthRange(u, before + 1, after)) return CommandCost(STR_ERROR_UNDERGROUND_IN_THE_WAY);
			}
		}
	}
	return CommandCost();
}
