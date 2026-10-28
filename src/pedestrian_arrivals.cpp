/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pedestrian_arrivals.cpp Purely visual crowds leaving vehicles that delivered passengers. */

#include "stdafx.h"
#include "pedestrian_arrivals.h"
#include "pedestrians.h"
#include "cargotype.h"
#include "settings_type.h"
#include "station_base.h"
#include "tile_cmd.h"
#include "train.h"
#include "core/math_func.hpp"
#include "core/random_func.hpp"
#include "multilayer/station_complex.h"
#include "network/network.h"
#include "timer/timer_game_tick.h"

#include "safeguards.h"

static constexpr uint MAX_WALKERS_PER_PART = 6; ///< Limit for the pedestrians leaving one wagon at once.
static constexpr uint MAX_WALKERS_PER_VEHICLE = 20; ///< Limit for the pedestrians leaving one vehicle at once.
static constexpr TimerGameTick::TickCounter HOUSE_LIST_TTL = 30 * Ticks::DAY_TICKS; ///< Rebuild the house list of a station this often.

/** Houses around a station passengers walk to, weighted by their passenger acceptance. */
struct StationHouses {
	Rect catchment{};    ///< Catchment rectangle the list was built for.
	TimerGameTick::TickCounter built = 0; ///< Tick the list was built at.
	std::vector<TileIndex> tiles;  ///< House tiles accepting passengers.
	std::vector<uint> cumulative;  ///< Running sum of the acceptance (in 1/8) of the tiles.
};

static std::unordered_map<uint, StationHouses> _station_houses;
static PedestrianArrivalStats _arrival_stats;

/* The vehicle currently unloading, to limit the crowd of a whole vehicle. */
static uint _arrival_front = UINT_MAX;
static TimerGameTick::TickCounter _arrival_tick = 0;
static uint _arrival_count = 0;

static bool SameRect(const Rect &a, const Rect &b)
{
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

/**
 * Get the passenger accepting houses around a station, rebuilding the cached list when needed.
 * @param st The station.
 * @return The houses.
 */
static const StationHouses &GetStationHouses(const Station *st)
{
	StationHouses &h = _station_houses[st->index.base()];
	Rect catchment = st->GetCatchmentRect();
	if (h.built != 0 && SameRect(h.catchment, catchment) && h.built <= TimerGameTick::counter &&
			TimerGameTick::counter - h.built < HOUSE_LIST_TTL) {
		return h;
	}

	h.catchment = catchment;
	h.built = std::max<TimerGameTick::TickCounter>(TimerGameTick::counter, 1);
	h.tiles.clear();
	h.cumulative.clear();

	uint total = 0;
	BitmapTileIterator it(st->catchment_tiles);
	for (TileIndex tile = it; tile != INVALID_TILE; tile = ++it) {
		if (!IsTileType(tile, TileType::House)) continue;

		CargoArray acceptance{};
		CargoTypes always_accepted{};
		AddAcceptedCargo(tile, acceptance, always_accepted);
		uint weight = 0;
		for (const CargoSpec *cs : CargoSpec::Iterate()) {
			if (cs->classes.Test(CargoClass::Passengers)) weight += acceptance[cs->Index()];
		}
		if (weight == 0) continue;

		total += weight;
		h.tiles.push_back(tile);
		h.cumulative.push_back(total);
	}
	return h;
}

/**
 * Pick a house of the station for one walker, by passenger acceptance.
 * @param h The houses of the station.
 * @return The house tile, or #INVALID_TILE when there is none.
 */
static TileIndex PickHouse(const StationHouses &h)
{
	if (h.tiles.empty()) return INVALID_TILE;
	uint r = InteractiveRandomRange(h.cumulative.back());
	size_t i = std::upper_bound(h.cumulative.begin(), h.cumulative.end(), r) - h.cumulative.begin();
	TileIndex tile = h.tiles[std::min(i, h.tiles.size() - 1)];
	/* The town may have rebuilt it since the list was made. */
	return IsTileType(tile, TileType::House) ? tile : INVALID_TILE;
}

/**
 * Is a vehicle part travelling through the underground layers?
 * @param part The vehicle part.
 * @return Whether its passengers have to come up through the exits.
 */
static bool IsUndergroundPart(const Vehicle *part)
{
	if (part->type != VEH_TRAIN) return false;
	return Train::From(part)->IsUnderground() || Train::From(part->First())->IsUnderground();
}

/**
 * Find where the passengers of a vehicle part step out.
 * @param st The station.
 * @param part The vehicle part.
 * @param[out] x X in world units.
 * @param[out] y Y in world units.
 * @return Whether there is such a place.
 */
static bool FindArrivalStart(const Station *st, const Vehicle *part, int &x, int &y)
{
	if (IsUndergroundPart(part)) {
		/* Underground platforms: come up through a random surface exit. */
		auto it = _station_complexes.find(st->index.base());
		if (it == _station_complexes.end()) return false;
		std::vector<const StationComplexNode *> exits = it->second.GetExits();
		if (exits.empty()) return false;
		TileIndex tile = exits[InteractiveRandomRange(static_cast<uint>(exits.size()))]->ref.tile;
		x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2 + static_cast<int>(InteractiveRandomRange(7)) - 3;
		y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2 + static_cast<int>(InteractiveRandomRange(7)) - 3;
		return true;
	}

	/* World unit steps for every direction a vehicle can face. */
	static constexpr int8_t DIR_DX[DIR_END] = {-1, -1, -1,  0,  1,  1,  1,  0};
	static constexpr int8_t DIR_DY[DIR_END] = {-1,  0,  1,  1,  1,  0, -1, -1};
	int ax = DIR_DX[part->direction];
	int ay = DIR_DY[part->direction];

	/* Somewhere along the wagon, then off it sideways onto the platform or kerb. */
	int along = static_cast<int>(InteractiveRandomRange(7)) - 3;
	int side = (InteractiveRandomRange(2) == 0 ? -1 : 1) * (part->type == VEH_TRAIN ? 6 : 4);
	x = part->x_pos + ax * along - ay * side;
	y = part->y_pos + ay * along + ax * side;
	return true;
}

/**
 * A vehicle part delivered passengers to a station: send some of them walking
 * from the vehicle, or the surface exits of underground platforms, to the
 * houses around the station.
 * @param st The station the passengers were delivered to.
 * @param part The vehicle part that carried them.
 * @param amount Number of passengers delivered (not transferred).
 */
void OnPedestrianPassengersDelivered(const Station *st, const Vehicle *part, uint amount)
{
	if (amount == 0 || !_settings_client.gui.show_pedestrians || !_settings_client.gui.pedestrian_arrivals || _network_dedicated) return;
	if (!IsCargoInClass(part->cargo_type, CargoClass::Passengers)) return;

	_arrival_stats.passengers_delivered += amount;

	int sx, sy;
	if (!FindArrivalStart(st, part, sx, sy) || !IsNearPedestrianViewport(sx, sy)) {
		_arrival_stats.passengers_offscreen += amount;
		return;
	}

	const StationHouses &houses = GetStationHouses(st);
	if (houses.tiles.empty()) {
		_arrival_stats.no_house += amount;
		return;
	}

	/* One pedestrian per 'ratio' passengers, the remainder by chance. */
	uint ratio = std::max<uint>(_settings_client.gui.pedestrian_ratio, 1);
	uint count = amount / ratio + (InteractiveRandomRange(ratio) < amount % ratio ? 1 : 0);
	count = std::min(count, MAX_WALKERS_PER_PART);

	/* The whole vehicle unloads in the same tick, limit its crowd. */
	uint front = part->First()->index.base();
	if (front != _arrival_front || _arrival_tick != TimerGameTick::counter) {
		_arrival_front = front;
		_arrival_tick = TimerGameTick::counter;
		_arrival_count = 0;
	}
	count = std::min(count, MAX_WALKERS_PER_VEHICLE - _arrival_count);

	for (uint i = 0; i < count; i++) {
		TileIndex house = PickHouse(houses);
		if (house == INVALID_TILE) continue;

		/* End the walk at the edge of the house facing the walker. */
		int hx = TileX(house) * TILE_SIZE + TILE_SIZE / 2;
		int hy = TileY(house) * TILE_SIZE + TILE_SIZE / 2;
		int dx = sx - hx;
		int dy = sy - hy;
		int len = IntSqrt(dx * dx + dy * dy);
		if (len > 6) {
			hx += dx * 6 / len;
			hy += dy * 6 / len;
		}

		/* Each walker steps out at its own spot of the wagon. */
		int wx = sx, wy = sy;
		if (i > 0) FindArrivalStart(st, part, wx, wy);
		if (SpawnPedestrian(wx, wy, hx, hy, PedestrianGoal::EnterHouse)) {
			_arrival_stats.spawned++;
			_arrival_count++;
		}
	}
}

/** Forget the cached house lists and counters, e.g. when a game is started or loaded. */
void ResetPedestrianArrivals()
{
	_station_houses.clear();
	_arrival_stats = {};
	_arrival_front = UINT_MAX;
	_arrival_count = 0;
}

const PedestrianArrivalStats &GetPedestrianArrivalStats()
{
	return _arrival_stats;
}
