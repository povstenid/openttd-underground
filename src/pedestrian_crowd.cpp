/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file pedestrian_crowd.cpp Purely visual crowds of passengers waiting at stations.
 *
 * Every station near a viewport gets a crowd sized by the passengers waiting
 * there, spread over its bus and tram stops and platforms, or over the surface
 * pavilions of an underground station. Each tile has a fixed list of standing
 * spots in a fixed order, so a growing crowd adds people and a shrinking one
 * loses the last comers. A walker heading for the station reserves the next
 * free spot; the waiting passenger it stands for only appears there when the
 * walker should have arrived.
 *
 * Nothing here is saved or touches the game state, and the only randomness is
 * a hash of the tile, the spot and the time.
 */

#include "stdafx.h"
#include "pedestrian_crowd.h"
#include "cargotype.h"
#include "gfx_func.h"
#include "landscape.h"
#include "newgrf_roadstop.h"
#include "roadstop_base.h"
#include "settings_type.h"
#include "station_base.h"
#include "station_map.h"
#include "tile_map.h"
#include "transparency.h"
#include "viewport_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "multilayer/station_complex.h"
#include "network/network.h"
#include "timer/timer.h"
#include "timer/timer_game_tick.h"
#include "timer/timer_window.h"

#include "table/sprites.h"

#include "safeguards.h"

static constexpr uint MAX_CROWD_SPOTS = 10; ///< Most people standing on one tile.
static constexpr uint MAX_CROWD_PER_STATION = 80; ///< Most people standing at one station.
static constexpr uint MAX_CROWD_RESERVATIONS = 16; ///< Most walkers per station holding back a waiting passenger.
static constexpr uint CROWD_SHIFT_TICKS = 96; ///< Ticks between two moments a tile's crowd may shift its poses.
static constexpr int CROWD_WALK_SPEED = 48; ///< Walking speed assumed for the arrival of a walker, 1/256 world units per tick.
static constexpr int CROWD_VISIBLE_MARGIN = 64; ///< Stations this many screen pixels around the viewports get a crowd.
static constexpr ZoomLevel MAX_CROWD_ZOOM = ZoomLevel::Out2x; ///< Crowds are not drawn when zoomed out further.

/** A place one waiting passenger stands on, relative to the north corner of the tile. */
struct CrowdSpot {
	int8_t x;  ///< X within the tile.
	int8_t y;  ///< Y within the tile.
	int8_t z;  ///< Height above the tile, e.g. on a platform.
	int8_t box_x; ///< Offset of the bounding box in X, to sort in front of a building.
	int8_t box_y; ///< Offset of the bounding box in Y, to sort in front of a building.
	DiagDirection facing; ///< Direction the passenger looks to when idle.
};

/** The crowd on one tile. */
struct CrowdTile {
	TileIndex tile;    ///< The tile.
	uint8_t cap;       ///< Number of spots of the tile.
	uint8_t count;     ///< People standing there.
	uint8_t reserved;  ///< Spots after the people kept free for walkers.
	uint64_t epoch;    ///< Pose period the tile was last drawn with.
};

/** A walker on its way to a spot. */
struct CrowdReservation {
	TileIndex tile;    ///< Tile the walker joins the crowd on.
	TimerGameTick::TickCounter due;   ///< When it should have arrived.
};

/** The crowd of one station. */
struct CrowdStation {
	std::vector<CrowdTile> tiles; ///< Tiles passengers wait on.
	std::vector<CrowdReservation> reservations; ///< Walkers on their way.
	uint32_t generation = 0; ///< Update pass that last saw the station near a viewport.
};

static std::unordered_map<uint16_t, CrowdStation> _crowd_stations;
static uint32_t _crowd_generation = 0;

/** Mix a few numbers into a well spread hash. */
static uint32_t CrowdHash(uint32_t a, uint32_t b = 0, uint32_t c = 0)
{
	uint32_t h = a * 0x9E3779B1U ^ b * 0x85EBCA77U ^ c * 0xC2B2AE3DU;
	h ^= h >> 15;
	h *= 0x2C1B3C6DU;
	h ^= h >> 12;
	h *= 0x297A2D39U;
	h ^= h >> 15;
	return h;
}

/**
 * Get the standing spots of a station tile, in the order they fill.
 * @param tile The station tile.
 * @param[out] spots The spots.
 * @return Number of spots; 0 when nobody waits on this tile.
 */
static uint GetCrowdSpots(TileIndex tile, std::array<CrowdSpot, MAX_CROWD_SPOTS> &spots)
{
	uint n = 0;
	const uint32_t h = CrowdHash(tile.base(), 0x5107);
	auto add = [&](int x, int y, int z, DiagDirection facing, int box_x = 0, int box_y = 0) {
		spots[n++] = {static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z), static_cast<int8_t>(box_x), static_cast<int8_t>(box_y), facing};
	};
	/* Position along the waiting strip with a pixel of jitter. */
	auto jitter = [&](uint i) { return static_cast<int>(GB(h, i, 1)); };

	if (IsRailStationTile(tile)) {
		/* On the platforms either side of the track, facing it; not where the
		 * default graphics have a building or pillars. */
		Axis axis = GetRailStationAxis(tile);
		StationGfx gfx = GetStationGfx(tile);
		bool custom = IsCustomStationSpecIndex(tile);
		bool rear = custom || gfx < 2 || gfx >= 6;
		bool front = custom || gfx < 6;
		for (uint i = 0; i < 5; i++) {
			int l = 1 + 3 * static_cast<int>(i) + jitter(i);
			if (rear) {
				int d = 2 + static_cast<int>((i + h) & 1);
				if (axis == AXIS_X) {
					add(l, d, 2, DIAGDIR_SE);
				} else {
					add(d, l, 2, DIAGDIR_SW);
				}
			}
			if (front) {
				int d = 13 - static_cast<int>((i + h) & 1);
				if (axis == AXIS_X) {
					add(l, d, 2, DIAGDIR_NW);
				} else {
					add(d, l, 2, DIAGDIR_NE);
				}
			}
		}
	} else if (IsAnyRoadStop(tile) && IsBusStop(tile)) {
		if (IsDriveThroughStopTile(tile)) {
			/* At the kerb in front of the shelters on both sides of the road. */
			Axis axis = GetDriveThroughStopAxis(tile);
			for (uint i = 0; i < 4; i++) {
				int l = 2 + 3 * static_cast<int>(i) + jitter(i);
				if (axis == AXIS_X) {
					add(l, 3, 0, DIAGDIR_SE);
					add(l, 12, 0, DIAGDIR_NW);
				} else {
					add(3, l, 0, DIAGDIR_SW);
					add(12, l, 0, DIAGDIR_NE);
				}
			}
		} else if (GetRoadStopSpec(tile) == nullptr && IsStationExitPavilionTile(tile)) {
			/* On the paving in front of the door side of the metro pavilion,
			 * sorted in front of the building which fills the whole tile. */
			Axis axis = DiagDirToAxis(GetBayRoadStopDir(tile));
			for (uint i = 0; i < 8; i++) {
				int l = 2 + static_cast<int>(i) * 3 / 2 + jitter(i);
				int d = 14 + static_cast<int>(i & 1);
				if (axis == AXIS_X) {
					add(d, l, 0, DIAGDIR_SW, TILE_SIZE - d, 0);
				} else {
					add(l, d, 0, DIAGDIR_SE, 0, TILE_SIZE - d);
				}
			}
		} else {
			/* In the bay in front of the shelter at the back, facing the entrance. */
			DiagDirection dir = GetBayRoadStopDir(tile);
			for (uint i = 0; i < 6; i++) {
				int l = 3 + 3 * static_cast<int>(i % 3) + jitter(i);
				int d = i < 3 ? 12 : 10;
				switch (dir) {
					case DIAGDIR_NE: add(d, l, 0, dir); break;
					case DIAGDIR_SW: add(TILE_SIZE - 1 - d, l, 0, dir); break;
					case DIAGDIR_NW: add(l, d, 0, dir); break;
					case DIAGDIR_SE: add(l, TILE_SIZE - 1 - d, 0, dir); break;
					default: NOT_REACHED();
				}
			}
		}
	}

	/* Fill in a fixed but scattered order. */
	for (uint i = n; i > 1; i--) {
		uint j = CrowdHash(tile.base(), i) % i;
		std::swap(spots[i - 1], spots[j]);
	}
	return n;
}

/**
 * Get the tiles passengers of a station wait on.
 * @param st The station.
 * @param[out] tiles The tiles.
 */
static void GetCrowdTiles(const Station *st, std::vector<TileIndex> &tiles)
{
	auto it = _station_complexes.find(st->index.base());
	if (it != _station_complexes.end() && it->second.HasSurfaceExit()) {
		/* The platforms are underground, people wait at the pavilions. */
		for (const StationComplexNode *n : it->second.GetExits()) {
			TileIndex t = n->ref.tile;
			if (IsTileType(t, TileType::Station) && GetStationIndex(t) == st->index) tiles.push_back(t);
		}
		return;
	}

	for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) tiles.push_back(rs->xy);
	for (TileIndex t : st->train_station) {
		if (st->TileBelongsToRailStation(t)) tiles.push_back(t);
	}
}

/** Is a station close enough to a viewport showing pedestrians to be worth a crowd? */
static bool IsCrowdStationVisible(const Station *st)
{
	if (st->rect.IsEmpty()) return false;

	int x0 = st->rect.left * TILE_SIZE;
	int y0 = st->rect.top * TILE_SIZE;
	int x1 = (st->rect.right + 1) * TILE_SIZE - 1;
	int y1 = (st->rect.bottom + 1) * TILE_SIZE - 1;
	Point corners[] = {RemapCoords2(x0, y0), RemapCoords2(x1, y0), RemapCoords2(x0, y1), RemapCoords2(x1, y1)};
	Rect r{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
	for (const Point &pt : corners) {
		r.left = std::min(r.left, pt.x);
		r.right = std::max(r.right, pt.x);
		r.top = std::min(r.top, pt.y);
		r.bottom = std::max(r.bottom, pt.y);
	}

	for (const Window *w : Window::Iterate()) {
		const Viewport *vp = w->viewport.get();
		if (vp == nullptr || vp->zoom > MAX_CROWD_ZOOM) continue;
		int margin = ScaleByZoom(CROWD_VISIBLE_MARGIN, vp->zoom);
		if (r.right >= vp->virtual_left - margin && r.left < vp->virtual_left + vp->virtual_width + margin &&
				r.bottom >= vp->virtual_top - margin && r.top < vp->virtual_top + vp->virtual_height + margin) {
			return true;
		}
	}
	return false;
}

/** Add one person to the tile with the most room, the first one when equal. */
static bool AddToLeastFilled(CrowdStation &cs)
{
	CrowdTile *best = nullptr;
	for (CrowdTile &ct : cs.tiles) {
		if (ct.count + ct.reserved >= ct.cap) continue;
		if (best == nullptr || ct.count * best->cap < best->count * ct.cap) best = &ct;
	}
	if (best == nullptr) return false;
	best->count++;
	return true;
}

/** Remove one person from the fullest tile, the last one when equal. */
static void RemoveFromMostFilled(CrowdStation &cs)
{
	CrowdTile *best = nullptr;
	for (CrowdTile &ct : cs.tiles) {
		if (ct.count == 0) continue;
		if (best == nullptr || ct.count * best->cap >= best->count * ct.cap) best = &ct;
	}
	if (best != nullptr) best->count--;
}

/** Pose period of a tile; the tiles shift at different moments. */
static uint64_t GetCrowdEpoch(TileIndex tile)
{
	return (TimerGameTick::counter + CrowdHash(tile.base(), 0xE90C) % CROWD_SHIFT_TICKS) / CROWD_SHIFT_TICKS;
}

/**
 * Bring the crowd of a station near a viewport up to date and redraw the tiles that changed.
 * @param st The station.
 * @param cs Its crowd.
 */
static void UpdateCrowdStation(const Station *st, CrowdStation &cs)
{
	static std::vector<TileIndex> tile_list;
	tile_list.clear();
	GetCrowdTiles(st, tile_list);

	/* Rebuild the tile list, keeping the crowds of the tiles that remain. */
	std::vector<CrowdTile> old_tiles = std::move(cs.tiles);
	cs.tiles.clear();
	uint capacity = 0;
	std::array<CrowdSpot, MAX_CROWD_SPOTS> spots;
	for (TileIndex t : tile_list) {
		uint cap = GetCrowdSpots(t, spots);
		if (cap == 0) continue;
		auto old = std::ranges::find(old_tiles, t, &CrowdTile::tile);
		CrowdTile ct{t, static_cast<uint8_t>(cap), 0, 0, GetCrowdEpoch(t)};
		if (old != old_tiles.end()) {
			ct.count = std::min<uint8_t>(old->count, ct.cap);
			ct.epoch = old->epoch;
			old->tile = INVALID_TILE;
		}
		cs.tiles.push_back(ct);
		capacity += cap;
	}
	for (const CrowdTile &ct : old_tiles) {
		if (ct.tile != INVALID_TILE && ct.count > 0 && IsValidTile(ct.tile)) MarkTileDirtyByTile(ct.tile);
	}

	std::vector<uint8_t> before;
	before.reserve(cs.tiles.size());
	uint total = 0;
	for (const CrowdTile &ct : cs.tiles) {
		before.push_back(ct.count);
		total += ct.count;
	}

	uint waiting = 0;
	for (const CargoSpec *spec : CargoSpec::Iterate()) {
		if (IsCargoInClass(spec->Index(), CargoClass::Passengers)) waiting += st->goods[spec->Index()].TotalCount();
	}
	uint ratio = std::max<uint>(_settings_client.gui.pedestrian_ratio, 1);
	uint wanted = std::min({waiting / ratio, capacity, MAX_CROWD_PER_STATION});

	/* Walkers that arrived join the crowd where they were heading. */
	const TimerGameTick::TickCounter now = TimerGameTick::counter;
	for (auto it = cs.reservations.begin(); it != cs.reservations.end();) {
		auto ct = std::ranges::find(cs.tiles, it->tile, &CrowdTile::tile);
		bool lost = ct == cs.tiles.end();
		if (!lost && it->due > now) {
			ct->reserved++;
			++it;
			continue;
		}
		if (!lost && total < wanted && ct->count < ct->cap) {
			ct->count++;
			total++;
		}
		it = cs.reservations.erase(it);
	}

	/* The passengers of walkers still on their way are not shown yet. */
	uint held = static_cast<uint>(cs.reservations.size());
	uint shown = wanted > held ? wanted - held : 0;
	while (total < shown && AddToLeastFilled(cs)) total++;
	for (; total > wanted; total--) RemoveFromMostFilled(cs);

	for (size_t i = 0; i < cs.tiles.size(); i++) {
		CrowdTile &ct = cs.tiles[i];
		uint64_t epoch = GetCrowdEpoch(ct.tile);
		if (ct.count != before[i] || (ct.count > 0 && epoch != ct.epoch)) MarkTileDirtyByTile(ct.tile);
		ct.epoch = epoch;
	}
}

/** Update the crowds of all stations near the viewports. */
static void UpdatePedestrianCrowds()
{
	if (!_settings_client.gui.show_pedestrians || !_settings_client.gui.pedestrian_crowds || IsInvisibilitySet(TO_PEDESTRIANS) || _network_dedicated) {
		ResetPedestrianCrowds();
		return;
	}

	_crowd_generation++;
	for (const Station *st : Station::Iterate()) {
		if (!IsCrowdStationVisible(st)) continue;
		CrowdStation &cs = _crowd_stations[st->index.base()];
		cs.generation = _crowd_generation;
		UpdateCrowdStation(st, cs);
	}

	/* Forget stations out of sight; they get a fresh crowd when they come back. */
	for (auto it = _crowd_stations.begin(); it != _crowd_stations.end();) {
		if (it->second.generation != _crowd_generation) {
			it = _crowd_stations.erase(it);
		} else {
			++it;
		}
	}
}

static const IntervalTimer<TimerWindow> _crowd_update_interval(std::chrono::milliseconds(250), [](auto) {
	UpdatePedestrianCrowds();
});

/**
 * Draw the people waiting on a station tile.
 * @param ti The tile being drawn, after its foundation.
 */
void DrawPedestrianCrowd(const TileInfo *ti)
{
	if (_crowd_stations.empty() || _cur_dpi->zoom > MAX_CROWD_ZOOM) return;

	auto it = _crowd_stations.find(GetStationIndex(ti->tile).base());
	if (it == _crowd_stations.end()) return;
	auto ct = std::ranges::find(it->second.tiles, ti->tile, &CrowdTile::tile);
	if (ct == it->second.tiles.end() || ct->count == 0) return;

	std::array<CrowdSpot, MAX_CROWD_SPOTS> spots;
	uint count = std::min<uint>(ct->count, GetCrowdSpots(ti->tile, spots));
	int tx = TileX(ti->tile) * TILE_SIZE;
	int ty = TileY(ti->tile) * TILE_SIZE;
	for (uint slot = 0; slot < count; slot++) {
		const CrowdSpot &s = spots[slot];

		/* Who stands there is fixed, how they stand changes now and then. */
		uint32_t person = CrowdHash(ti->tile.base(), slot, 0x9E05);
		uint32_t mood = CrowdHash(ti->tile.base(), slot, static_cast<uint32_t>(ct->epoch));
		uint variant = person % PEDESTRIAN_VARIANTS;
		Colours colour = static_cast<Colours>((person >> 8) % COLOUR_END);
		uint pose = GB(mood, 0, 3) < 5 ? 0 : (GB(mood, 0, 3) < 7 ? 1 : 2);
		DiagDirection facing = s.facing;
		switch (GB(mood, 3, 3)) {
			case 0: facing = ChangeDiagDir(facing, DIAGDIRDIFF_90LEFT); break; // Looking out for the next vehicle.
			case 1: facing = ChangeDiagDir(facing, DIAGDIRDIFF_90RIGHT); break;
			default: break;
		}

		SpriteID image = SPR_PEDESTRIAN_STANDING_BASE + (variant * DIAGDIR_END + facing) * PEDESTRIAN_STANDING_POSES + pose;
		SpriteBounds bounds{{s.box_x, s.box_y, 0}, {1, 1, 8}, {static_cast<int8_t>(-s.box_x), static_cast<int8_t>(-s.box_y), 0}};
		AddSortableSpriteToDraw(image, GetColourPalette(colour), tx + s.x, ty + s.y, ti->z + s.z, bounds, IsTransparencySet(TO_PEDESTRIANS));
	}
}

/**
 * Find the spot a walker from a house joins the crowd of a station at: the
 * next free spot of the nearest tile with room left.
 * @param st The station.
 * @param hx House X in world units.
 * @param hy House Y in world units.
 * @param reserve Whether the walker stands for passengers just handed to the
 *                station, which are not shown in the crowd until it arrives.
 * @param[out] tx Target X in world units.
 * @param[out] ty Target Y in world units.
 * @return Whether the station has a crowd to join.
 */
bool FindPedestrianCrowdTarget(const Station *st, int hx, int hy, bool reserve, int &tx, int &ty)
{
	auto it = _crowd_stations.find(st->index.base());
	if (it == _crowd_stations.end() || it->second.tiles.empty()) return false;
	CrowdStation &cs = it->second;

	auto distance = [&](const CrowdTile &ct) {
		return abs(static_cast<int>(TileX(ct.tile) * TILE_SIZE + TILE_SIZE / 2) - hx) + abs(static_cast<int>(TileY(ct.tile) * TILE_SIZE + TILE_SIZE / 2) - hy);
	};
	CrowdTile *best = nullptr;
	CrowdTile *nearest = nullptr;
	for (CrowdTile &ct : cs.tiles) {
		if (nearest == nullptr || distance(ct) < distance(*nearest)) nearest = &ct;
		if (ct.count + ct.reserved >= ct.cap) continue;
		if (best == nullptr || distance(ct) < distance(*best)) best = &ct;
	}

	/* With no room left anywhere, just walk into the back of the nearest crowd. */
	CrowdTile &target = best != nullptr ? *best : *nearest;
	std::array<CrowdSpot, MAX_CROWD_SPOTS> spots;
	uint cap = GetCrowdSpots(target.tile, spots);
	if (cap == 0) return false;
	const CrowdSpot &s = spots[std::min<uint>(target.count + target.reserved, cap - 1)];
	tx = TileX(target.tile) * TILE_SIZE + s.x;
	ty = TileY(target.tile) * TILE_SIZE + s.y;

	if (reserve && best != nullptr && cs.reservations.size() < MAX_CROWD_RESERVATIONS) {
		int walk = abs(tx - hx) + abs(ty - hy);
		cs.reservations.push_back({target.tile, TimerGameTick::counter + walk * 256 / CROWD_WALK_SPEED});
		target.reserved++;
	}
	return true;
}

/** Forget all crowds, e.g. when a game is started or loaded, or the pedestrians are turned off. */
void ResetPedestrianCrowds()
{
	_crowd_stations.clear();
}
