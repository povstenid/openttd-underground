/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pedestrian_path.cpp Road paths of the purely visual pedestrians. */

#include "stdafx.h"
#include "pedestrian_path.h"
#include "map_func.h"
#include "road_func.h"
#include "road_map.h"
#include "station_map.h"
#include "core/random_func.hpp"

#include "safeguards.h"

static constexpr int FIELD_RADIUS = 24; ///< Tiles around the goal a field covers.
static constexpr int FIELD_SIZE = 2 * FIELD_RADIUS + 1; ///< Width and height of a field in tiles.
static constexpr uint FIELD_SLOTS = 64; ///< Fields kept in the cache.
static constexpr uint32_t FIELD_MAX_AGE = 1024; ///< Ticks after which a field is rebuilt to see new or removed roads.
static constexpr uint8_t FIELD_UNREACHABLE = 0xFF; ///< Field distance of a tile the goal can't be reached from.

/** Distances along the roads to one goal tile, in tiles. */
struct PedestrianPathField {
	TileIndex goal = INVALID_TILE; ///< Goal tile, #INVALID_TILE for an unused slot.
	uint32_t built = 0;    ///< Clock when the field was built.
	uint32_t used = 0;     ///< Clock when the field was last asked for.
	uint x0 = 0;           ///< X of the field's north corner.
	uint y0 = 0;           ///< Y of the field's north corner.
	uint w = 0;            ///< Width of the field in tiles.
	uint h = 0;            ///< Height of the field in tiles.
	std::array<uint8_t, FIELD_SIZE * FIELD_SIZE> dist; ///< Distance of every tile, #FIELD_UNREACHABLE if none.

	/**
	 * Distance of a tile to the goal.
	 * @param tile The tile.
	 * @return The distance in tiles, #FIELD_UNREACHABLE outside the field.
	 */
	uint8_t Get(TileIndex tile) const
	{
		uint x = TileX(tile) - this->x0;
		uint y = TileY(tile) - this->y0;
		if (x >= this->w || y >= this->h) return FIELD_UNREACHABLE;
		return this->dist[y * this->w + x];
	}

	void Build(TileIndex goal, uint32_t now);
};

static std::array<PedestrianPathField, FIELD_SLOTS> _path_fields;
static uint32_t _path_clock = 0; ///< Ticks since the pedestrians started, ages the fields.

/**
 * Road bits a pedestrian can walk along: normal roads, level crossings and road stops.
 * Depots, tunnels and bridges are not walked through.
 * @param tile The tile.
 * @return The walkable road bits of the tile.
 */
RoadBits GetPedestrianRoadBits(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Road:
			if (IsRoadDepot(tile)) return ROAD_NONE;
			return GetAnyRoadBits(tile, RTT_ROAD);

		case TileType::Station:
			if (!IsAnyRoadStop(tile)) return ROAD_NONE;
			return GetAnyRoadBits(tile, RTT_ROAD);

		default:
			return ROAD_NONE;
	}
}

/**
 * Can a pedestrian walk from a tile to its neighbour?
 * @param bits Walkable road bits of the tile.
 * @param next_bits Walkable road bits of the neighbour.
 * @param dir Direction from the tile to the neighbour.
 * @return Whether both tiles have road towards each other.
 */
static bool IsPedestrianConnection(RoadBits bits, RoadBits next_bits, DiagDirection dir)
{
	return (bits & DiagDirToRoadBits(dir)) != ROAD_NONE && (next_bits & DiagDirToRoadBits(ReverseDiagDir(dir))) != ROAD_NONE;
}

/**
 * Build the field by a breadth first search from the road tiles touching the goal.
 * The goal tile itself is the only seed when it is walkable (a road stop), else
 * its four neighbours, else its diagonal neighbours.
 * @param goal The goal tile.
 * @param now The current clock.
 */
void PedestrianPathField::Build(TileIndex goal, uint32_t now)
{
	this->goal = goal;
	this->built = now;
	this->used = now;
	int gx = TileX(goal);
	int gy = TileY(goal);
	this->x0 = std::max(gx - FIELD_RADIUS, 0);
	this->y0 = std::max(gy - FIELD_RADIUS, 0);
	this->w = std::min<uint>(gx + FIELD_RADIUS, Map::MaxX()) + 1 - this->x0;
	this->h = std::min<uint>(gy + FIELD_RADIUS, Map::MaxY()) + 1 - this->y0;
	this->dist.fill(FIELD_UNREACHABLE);

	static std::array<RoadBits, FIELD_SIZE * FIELD_SIZE> bits;
	for (uint y = 0; y < this->h; y++) {
		for (uint x = 0; x < this->w; x++) {
			bits[y * this->w + x] = GetPedestrianRoadBits(TileXY(this->x0 + x, this->y0 + y));
		}
	}

	static std::array<uint16_t, FIELD_SIZE * FIELD_SIZE> queue;
	uint head = 0;
	uint tail = 0;
	auto seed = [&](int dx, int dy) {
		uint x = gx + dx - this->x0;
		uint y = gy + dy - this->y0;
		if (x >= this->w || y >= this->h) return;
		uint i = y * this->w + x;
		if (bits[i] == ROAD_NONE || this->dist[i] == 0) return;
		this->dist[i] = 0;
		queue[tail++] = static_cast<uint16_t>(i);
	};
	seed(0, 0);
	if (tail == 0) {
		seed(-1, 0); seed(1, 0); seed(0, -1); seed(0, 1);
	}
	if (tail == 0) {
		seed(-1, -1); seed(1, -1); seed(-1, 1); seed(1, 1);
	}

	while (head < tail) {
		uint i = queue[head++];
		uint8_t d = this->dist[i];
		if (d + 1 >= FIELD_UNREACHABLE) continue;
		uint x = i % this->w;
		uint y = i / this->w;
		for (DiagDirection dir = DIAGDIR_BEGIN; dir != DIAGDIR_END; dir++) {
			TileIndexDiffC off = TileIndexDiffCByDiagDir(dir);
			uint nx = x + off.x;
			uint ny = y + off.y;
			if (nx >= this->w || ny >= this->h) continue;
			uint n = ny * this->w + nx;
			if (this->dist[n] != FIELD_UNREACHABLE) continue;
			/* Walking from the neighbour towards this tile. */
			if (!IsPedestrianConnection(bits[n], bits[i], ReverseDiagDir(dir))) continue;
			this->dist[n] = d + 1;
			queue[tail++] = static_cast<uint16_t>(n);
		}
	}
}

/**
 * Get the up to date field of a goal, building it when it isn't cached or too old.
 * @param goal The goal tile.
 * @param[in,out] hint Cache slot the field was found in the last time.
 * @return The field.
 */
static PedestrianPathField &GetField(TileIndex goal, uint8_t &hint)
{
	PedestrianPathField *field = nullptr;
	if (hint < FIELD_SLOTS && _path_fields[hint].goal == goal) {
		field = &_path_fields[hint];
	} else {
		PedestrianPathField *oldest = &_path_fields[0];
		for (uint i = 0; i < FIELD_SLOTS; i++) {
			PedestrianPathField &f = _path_fields[i];
			if (f.goal == goal) {
				field = &f;
				break;
			}
			if (f.goal == INVALID_TILE || (oldest->goal != INVALID_TILE && f.used < oldest->used)) oldest = &f;
		}
		if (field == nullptr) {
			field = oldest;
			field->Build(goal, _path_clock);
		}
		hint = static_cast<uint8_t>(field - _path_fields.data());
	}

	if (_path_clock - field->built > FIELD_MAX_AGE) field->Build(goal, _path_clock);
	field->used = _path_clock;
	return *field;
}

/**
 * Distance along the roads from a tile to a goal.
 * @param goal The goal tile.
 * @param tile The road tile.
 * @param[in,out] hint Cache slot hint, see #GetField.
 * @return The distance in tiles, #PEDESTRIAN_NO_PATH when the goal can't be reached.
 */
uint GetPedestrianPathDistance(TileIndex goal, TileIndex tile, uint8_t &hint)
{
	uint8_t d = GetField(goal, hint).Get(tile);
	return d == FIELD_UNREACHABLE ? PEDESTRIAN_NO_PATH : d;
}

/**
 * Where to walk next from a road tile towards a goal. The roads are checked
 * on the way, the field is rebuilt when they changed under it.
 * @param goal The goal tile.
 * @param tile The road tile the pedestrian is on.
 * @param[in,out] hint Cache slot hint, see #GetField.
 * @param[out] dir With #PedestrianStep::Move, the direction to walk in.
 * @return What to do next.
 */
PedestrianStep GetPedestrianStep(TileIndex goal, TileIndex tile, uint8_t &hint, DiagDirection &dir)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		PedestrianPathField &field = GetField(goal, hint);
		uint8_t d = field.Get(tile);
		if (d == FIELD_UNREACHABLE) return PedestrianStep::NoPath;
		if (d == 0) return PedestrianStep::AtGoal;

		RoadBits bits = GetPedestrianRoadBits(tile);
		DiagDirection options[DIAGDIR_END];
		uint count = 0;
		for (DiagDirection dd = DIAGDIR_BEGIN; dd != DIAGDIR_END; dd++) {
			TileIndex next = TileAddByDiagDir(tile, dd);
			if (!IsValidTile(next) || field.Get(next) != d - 1) continue;
			if (!IsPedestrianConnection(bits, GetPedestrianRoadBits(next), dd)) continue;
			options[count++] = dd;
		}
		if (count > 0) {
			dir = options[count == 1 ? 0 : InteractiveRandomRange(count)];
			return PedestrianStep::Move;
		}

		/* The roads changed since the field was built. */
		field.Build(goal, _path_clock);
	}
	return PedestrianStep::NoPath;
}

/** Advance the clock that ages the fields by one tick. */
void PedestrianPathsTick()
{
	_path_clock++;
}

/** Forget all fields, e.g. when another map is loaded. */
void ResetPedestrianPaths()
{
	for (PedestrianPathField &f : _path_fields) f.goal = INVALID_TILE;
	_path_clock = 0;
}
