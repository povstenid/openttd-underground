/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pedestrians.cpp Purely visual pedestrians walking between houses and stations. */

#include "stdafx.h"
#include "pedestrians.h"
#include "pedestrian_crowd.h"
#include "pedestrian_path.h"
#include "cargotype.h"
#include "landscape.h"
#include "settings_type.h"
#include "sprite.h"
#include "station_base.h"
#include "transparency.h"
#include "viewport_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "core/math_func.hpp"
#include "core/random_func.hpp"
#include "multilayer/station_complex.h"
#include "network/network.h"

#include "table/sprites.h"

#include "safeguards.h"

static constexpr size_t MAX_PEDESTRIANS = 8000; ///< Hard limit of pedestrians alive at once.
static constexpr uint MAX_PEDESTRIANS_PER_EVENT = 6; ///< Limit for one batch of passengers leaving a house.
static constexpr int SPEED_MIN = 40; ///< Slowest walking speed, 1/256 world units per tick.
static constexpr int SPEED_MAX = 72; ///< Fastest walking speed, 1/256 world units per tick.
static constexpr int PAVEMENT_LANE_MIN = 5; ///< Nearest pavement lane to the road centre line, world units.
static constexpr int PAVEMENT_LANE_MAX = 6; ///< Farthest pavement lane from the road centre line, world units.
static constexpr uint CROSS_ROAD_CHANCE = 12; ///< One in this many road tiles a pedestrian crosses to the other pavement.
static constexpr uint8_t FRAME_TICKS = 6; ///< Ticks per animation frame.
static constexpr int SPAWN_MARGIN = 128; ///< Pedestrians appear this many screen pixels around the viewports.
static constexpr ZoomLevel MAX_PEDESTRIAN_ZOOM = ZoomLevel::Out2x; ///< Pedestrians are not drawn when zoomed out further.

/** What a pedestrian is walking to at the moment. */
enum class WalkPhase : uint8_t {
	ToRoad, ///< From the start onto the pavement of the first road tile.
	Corner, ///< To the pavement corner of a road tile where the road turns.
	Edge,   ///< To the pavement at the edge of a road tile, entering the next one.
	ToGoal, ///< Off the road straight to the goal point.
};

/** One pedestrian walking along the pavements, or in a straight line where no road leads, towards its goal. */
struct Pedestrian {
	int32_t fx;  ///< X position in 1/256 world units.
	int32_t fy;  ///< Y position in 1/256 world units.
	int32_t vx;  ///< X velocity in 1/256 world units per tick.
	int32_t vy;  ///< Y velocity in 1/256 world units per tick.
	int32_t wx;  ///< X of the waypoint walked to, world units.
	int32_t wy;  ///< Y of the waypoint walked to, world units.
	int32_t gx;  ///< X of the goal point, world units.
	int32_t gy;  ///< Y of the goal point, world units.
	Rect coord;  ///< Screen extent (virtual viewport coordinates) at the current position.
	TileIndex goal_tile; ///< Tile whose road paths are followed, #INVALID_TILE when walking straight.
	TileIndex tile;      ///< Road tile being walked along.
	uint16_t ticks_left; ///< Ticks until the waypoint is reached.
	int16_t z;   ///< Height of the ground at the current position.
	uint8_t variant;     ///< Sprite offset of the variant.
	uint8_t sprite;      ///< Sprite offset of variant and direction, without the frame.
	uint8_t frame;       ///< Animation frame.
	uint8_t frame_ticks; ///< Ticks spent in the current frame.
	uint8_t speed;       ///< Walking speed, 1/256 world units per tick.
	uint8_t path_hint;   ///< Cache slot hint of the road paths.
	int8_t lane;         ///< Distance of the pavement walked on to the right of the road centre line, negative for the left.
	DiagDirection dir;   ///< Direction of travel along the road.
	WalkPhase phase;     ///< What the pedestrian walks to at the moment.
	Colours colour;      ///< Clothes colour.
	PedestrianGoal goal; ///< What the pedestrian walks to.

	int X() const { return this->fx >> 8; }
	int Y() const { return this->fy >> 8; }
};

static std::vector<Pedestrian> _pedestrians;
static PedestrianStats _pedestrian_stats;
static TileIndex _pedestrian_source = INVALID_TILE;

PedestrianSourceScope::PedestrianSourceScope(TileIndex house)
{
	_pedestrian_source = house;
}

PedestrianSourceScope::~PedestrianSourceScope()
{
	_pedestrian_source = INVALID_TILE;
}

/** Screen extent of a pedestrian sprite (5x9 pixels, feet at the draw point) plus one pixel. */
static Rect GetPedestrianCoord(const Pedestrian &p)
{
	const int zb = static_cast<int>(ZOOM_BASE);
	Point pt = RemapCoords(p.X(), p.Y(), p.z);
	return {pt.x - 3 * zb, pt.y - 9 * zb, pt.x + 3 * zb, pt.y + 2 * zb};
}

static void MarkPedestrianDirty(const Rect &r)
{
	MarkAllViewportsDirty(r.left, r.top, r.right, r.bottom);
}

/**
 * Is the world position close enough to a viewport showing pedestrians to be worth one?
 * @param x X in world units.
 * @param y Y in world units.
 * @return Whether a pedestrian there might be seen.
 */
bool IsNearPedestrianViewport(int x, int y)
{
	if (IsInvisibilitySet(TO_PEDESTRIANS)) return false;

	Point pt = RemapCoords2(x, y);
	for (const Window *w : Window::Iterate()) {
		const Viewport *vp = w->viewport.get();
		if (vp == nullptr || vp->zoom > MAX_PEDESTRIAN_ZOOM) continue;
		int margin = ScaleByZoom(SPAWN_MARGIN, vp->zoom);
		if (pt.x >= vp->virtual_left - margin && pt.x < vp->virtual_left + vp->virtual_width + margin &&
				pt.y >= vp->virtual_top - margin && pt.y < vp->virtual_top + vp->virtual_height + margin) {
			return true;
		}
	}
	return false;
}

/**
 * Find the point of a station passengers walk to from a house: the next free
 * spot of the crowd waiting there, else the nearest point of the station's
 * passenger facilities, or of its surface exits for a station with
 * underground platforms.
 * @param st The station.
 * @param hx House X in world units.
 * @param hy House Y in world units.
 * @param[out] tx Target X in world units.
 * @param[out] ty Target Y in world units.
 * @return Whether the station has anything to walk to.
 */
bool FindPedestrianTarget(const Station *st, int hx, int hy, int &tx, int &ty)
{
	/* Walkers from houses stand for passengers just handed to the station:
	 * keep their spot free until they get there. */
	if (FindPedestrianCrowdTarget(st, hx, hy, _pedestrian_source != INVALID_TILE, tx, ty)) return true;

	int best = INT_MAX;
	auto consider = [&](const TileArea &ta, int inset) {
		if (ta.tile == INVALID_TILE) return;
		int x0 = TileX(ta.tile) * TILE_SIZE + inset;
		int y0 = TileY(ta.tile) * TILE_SIZE + inset;
		int x1 = (TileX(ta.tile) + ta.w) * TILE_SIZE - 1 - inset;
		int y1 = (TileY(ta.tile) + ta.h) * TILE_SIZE - 1 - inset;
		int cx = Clamp(hx, x0, x1);
		int cy = Clamp(hy, y0, y1);
		int dist = abs(cx - hx) + abs(cy - hy);
		if (dist < best) {
			best = dist;
			tx = cx;
			ty = cy;
		}
	};

	auto it = _station_complexes.find(st->index.base());
	if (it != _station_complexes.end() && it->second.HasSurfaceExit()) {
		/* The platforms are underground, only the exits are reachable on foot. */
		for (const StationComplexNode *n : it->second.GetExits()) consider(TileArea(n->ref.tile, 1, 1), 5);
	} else {
		consider(st->train_station, 3);
		consider(st->bus_station, 3);
		consider(st->airport, 3);
		consider(st->ship_station, 3);
	}
	return best != INT_MAX;
}

static constexpr int TILE = TILE_SIZE; ///< Size of a tile in world units, signed.
static constexpr int HALF_TILE = TILE / 2; ///< Distance from the centre of a tile to its edges.

/** X component of the unit vector of every direction of travel. */
static constexpr int DIR_DX[DIAGDIR_END] = {-1, 0, 1, 0};
/** Y component of the unit vector of every direction of travel. */
static constexpr int DIR_DY[DIAGDIR_END] = {0, 1, 0, -1};

/**
 * A point of a road tile's pavement, relative to the direction of travel.
 * @param tile The road tile.
 * @param dir Direction of travel.
 * @param along Distance from the tile centre in the direction of travel.
 * @param lane Distance from the road centre line to the right of travel.
 * @return The point in world units.
 */
static Point PavementPoint(TileIndex tile, DiagDirection dir, int along, int lane)
{
	int cx = TileX(tile) * TILE + HALF_TILE;
	int cy = TileY(tile) * TILE + HALF_TILE;
	/* The right of travel (ux, uy) is (uy, -ux). */
	return {cx + DIR_DX[dir] * along + DIR_DY[dir] * lane, cy + DIR_DY[dir] * along - DIR_DX[dir] * lane};
}

/**
 * The pavement corner of a road tile where the road turns: where the
 * pavements of both directions of travel meet.
 * @param tile The road tile.
 * @param from Direction of travel entering the tile.
 * @param to Direction of travel leaving the tile.
 * @param lane Distance from the road centre line to the right of travel.
 * @return The point in world units.
 */
static Point CornerPoint(TileIndex tile, DiagDirection from, DiagDirection to, int lane)
{
	int cx = TileX(tile) * TILE + HALF_TILE;
	int cy = TileY(tile) * TILE + HALF_TILE;
	return {cx + (DIR_DY[from] + DIR_DY[to]) * lane, cy - (DIR_DX[from] + DIR_DX[to]) * lane};
}

/**
 * Start walking in a straight line from the current position to a waypoint.
 * @param p The pedestrian.
 * @param x Waypoint X in world units.
 * @param y Waypoint Y in world units.
 */
static void WalkTo(Pedestrian &p, int x, int y)
{
	int dx = x - p.X();
	int dy = y - p.Y();
	int len = IntSqrt(dx * dx + dy * dy);
	int ticks = std::clamp(len * 256 / p.speed, 1, 0xFFFF);

	p.wx = x;
	p.wy = y;
	p.vx = ((x << 8) - p.fx) / ticks;
	p.vy = ((y << 8) - p.fy) / ticks;
	p.ticks_left = static_cast<uint16_t>(ticks);

	if (dx == 0 && dy == 0) return;
	DiagDirection dir;
	if (abs(dx) >= abs(dy)) {
		dir = dx < 0 ? DIAGDIR_NE : DIAGDIR_SW;
	} else {
		dir = dy < 0 ? DIAGDIR_NW : DIAGDIR_SE;
	}
	p.sprite = static_cast<uint8_t>(p.variant + dir * PEDESTRIAN_FRAMES);
}

/**
 * Decide where to walk on the road tile just entered: on along the
 * pavement, round a corner, or off the road to the goal.
 * @param p The pedestrian, at the edge of #Pedestrian::tile.
 */
static void PlanRoadTile(Pedestrian &p)
{
	DiagDirection dir;
	if (GetPedestrianStep(p.goal_tile, p.tile, p.path_hint, dir) != PedestrianStep::Move) {
		p.phase = WalkPhase::ToGoal;
		WalkTo(p, p.gx, p.gy);
		return;
	}

	/* Now and then cross the road to the other pavement. */
	if (InteractiveRandomRange(CROSS_ROAD_CHANCE) == 0) p.lane = -p.lane;

	if (dir != p.dir) {
		Point pt = CornerPoint(p.tile, p.dir, dir, p.lane);
		p.dir = dir;
		p.phase = WalkPhase::Corner;
		WalkTo(p, pt.x, pt.y);
	} else {
		Point pt = PavementPoint(p.tile, dir, HALF_TILE, p.lane);
		p.phase = WalkPhase::Edge;
		WalkTo(p, pt.x, pt.y);
	}
}

/**
 * The pedestrian reached its waypoint, walk to the next one.
 * @param p The pedestrian.
 * @return False when the goal is reached.
 */
static bool NextWaypoint(Pedestrian &p)
{
	switch (p.phase) {
		case WalkPhase::ToRoad:
		case WalkPhase::Corner: {
			Point pt = PavementPoint(p.tile, p.dir, HALF_TILE, p.lane);
			p.phase = WalkPhase::Edge;
			WalkTo(p, pt.x, pt.y);
			return true;
		}

		case WalkPhase::Edge:
			p.tile = TileAddByDiagDir(p.tile, p.dir);
			PlanRoadTile(p);
			return true;

		default:
			return false;
	}
}

/**
 * Put a new pedestrian on the pavement of the road tile next to its start
 * that is nearest to its goal along the roads.
 * @param p The pedestrian, at its start.
 * @param from_x Start X in world units.
 * @param from_y Start Y in world units.
 * @param to_x Goal X in world units.
 * @param to_y Goal Y in world units.
 * @return False when there is no road path, or the start is right next to the goal.
 */
static bool StartOnRoad(Pedestrian &p, int from_x, int from_y, int to_x, int to_y)
{
	const int max_x = static_cast<int>(Map::MaxX() * TILE);
	const int max_y = static_cast<int>(Map::MaxY() * TILE);
	if (from_x < TILE || from_y < TILE || to_x < 0 || to_y < 0 || from_x >= max_x || from_y >= max_y || to_x >= max_x || to_y >= max_y) return false;

	TileIndex goal_tile = TileVirtXY(to_x, to_y);
	int fx = from_x / TILE;
	int fy = from_y / TILE;
	uint8_t hint = UINT8_MAX;

	TileIndex best = INVALID_TILE;
	uint best_dist = PEDESTRIAN_NO_PATH;
	int best_len = INT_MAX;
	auto consider = [&](int dx, int dy) {
		TileIndex tile = TileXY(fx + dx, fy + dy);
		if (GetPedestrianRoadBits(tile) == ROAD_NONE) return;
		uint dist = GetPedestrianPathDistance(goal_tile, tile, hint);
		if (dist == PEDESTRIAN_NO_PATH) return;
		int len = abs(static_cast<int>(TileX(tile) * TILE + HALF_TILE) - from_x) + abs(static_cast<int>(TileY(tile) * TILE + HALF_TILE) - from_y);
		if (dist < best_dist || (dist == best_dist && len < best_len)) {
			best = tile;
			best_dist = dist;
			best_len = len;
		}
	};
	/* Houses touch their roads with a side; diagonal roads only when there is no other. */
	consider(0, 0);
	if (best == INVALID_TILE) {
		consider(-1, 0);
		consider(1, 0);
		consider(0, -1);
		consider(0, 1);
	}
	if (best == INVALID_TILE) {
		consider(-1, -1);
		consider(1, -1);
		consider(-1, 1);
		consider(1, 1);
	}
	if (best == INVALID_TILE) return false;

	DiagDirection dir;
	if (GetPedestrianStep(goal_tile, best, hint, dir) != PedestrianStep::Move) return false;

	/* Step onto the pavement on the side of the start, level with it. */
	int cx = TileX(best) * TILE + HALF_TILE;
	int cy = TileY(best) * TILE + HALF_TILE;
	int right = DIR_DY[dir] * (from_x - cx) - DIR_DX[dir] * (from_y - cy);
	int along = DIR_DX[dir] * (from_x - cx) + DIR_DY[dir] * (from_y - cy);
	int lane = PAVEMENT_LANE_MIN + static_cast<int>(InteractiveRandomRange(PAVEMENT_LANE_MAX - PAVEMENT_LANE_MIN + 1));
	if (right < 0 || (right == 0 && InteractiveRandomRange(2) == 0)) lane = -lane;
	Point pt = PavementPoint(best, dir, std::clamp(along, -HALF_TILE + 2, HALF_TILE - 2), lane);

	p.goal_tile = goal_tile;
	p.tile = best;
	p.dir = dir;
	p.lane = static_cast<int8_t>(lane);
	p.path_hint = hint;
	p.phase = WalkPhase::ToRoad;
	WalkTo(p, pt.x, pt.y);
	return true;
}

/**
 * Create a pedestrian walking from one point to another along the roads, or
 * in a straight line when no road leads there. It spreads out a little
 * sideways at the start and aims a bit off at the end.
 * @param from_x Start X in world units.
 * @param from_y Start Y in world units.
 * @param to_x Goal X in world units.
 * @param to_y Goal Y in world units.
 * @param goal What the pedestrian walks to.
 * @return Whether a pedestrian was created; false when the walk is too short or the limit is reached.
 */
bool SpawnPedestrian(int from_x, int from_y, int to_x, int to_y, PedestrianGoal goal)
{
	if (_pedestrians.size() >= std::min<size_t>(MAX_PEDESTRIANS, _settings_client.gui.pedestrian_limit)) {
		_pedestrian_stats.dropped++;
		return false;
	}

	int dx = to_x - from_x;
	int dy = to_y - from_y;
	int len = IntSqrt(dx * dx + dy * dy);
	if (len < 4) return false;

	int side = static_cast<int>(InteractiveRandomRange(7)) - 3;
	int sx = from_x - dy * side / len;
	int sy = from_y + dx * side / len;
	int tx = to_x + static_cast<int>(InteractiveRandomRange(5)) - 2;
	int ty = to_y + static_cast<int>(InteractiveRandomRange(5)) - 2;
	dx = tx - sx;
	dy = ty - sy;
	len = IntSqrt(dx * dx + dy * dy);
	if (len < 4) return false;

	Pedestrian p;
	p.fx = sx << 8;
	p.fy = sy << 8;
	p.gx = tx;
	p.gy = ty;
	p.goal_tile = INVALID_TILE;
	p.tile = INVALID_TILE;
	p.speed = static_cast<uint8_t>(SPEED_MIN + InteractiveRandomRange(SPEED_MAX - SPEED_MIN + 1));
	p.path_hint = UINT8_MAX;
	p.lane = 0;
	p.dir = INVALID_DIAGDIR;
	p.variant = static_cast<uint8_t>(InteractiveRandomRange(PEDESTRIAN_VARIANTS) * 4 * PEDESTRIAN_FRAMES);
	p.sprite = p.variant;
	p.frame = static_cast<uint8_t>(InteractiveRandomRange(PEDESTRIAN_FRAMES));
	p.frame_ticks = static_cast<uint8_t>(InteractiveRandomRange(FRAME_TICKS));
	p.colour = static_cast<Colours>(InteractiveRandomRange(COLOUR_END));
	p.goal = goal;

	if (!StartOnRoad(p, sx, sy, to_x, to_y)) {
		p.phase = WalkPhase::ToGoal;
		WalkTo(p, tx, ty);
	}

	p.z = GetSlopePixelZ(sx, sy);
	p.coord = GetPedestrianCoord(p);

	_pedestrians.push_back(p);
	_pedestrian_stats.spawned++;
	MarkPedestrianDirty(p.coord);
	return true;
}

/**
 * Passengers from the current #PedestrianSourceScope house were handed to a station:
 * send some pedestrians walking there.
 * @param st The station receiving the passengers.
 * @param cargo The cargo type.
 * @param amount Number of passengers.
 */
void OnPedestrianPassengersGenerated(const Station *st, CargoType cargo, uint amount)
{
	if (_pedestrian_source == INVALID_TILE || !_settings_client.gui.show_pedestrians || !_settings_client.gui.pedestrians_from_houses || _network_dedicated) return;
	if (!IsCargoInClass(cargo, CargoClass::Passengers)) return;

	_pedestrian_stats.passengers_seen += amount;

	int hx = TileX(_pedestrian_source) * TILE_SIZE + TILE_SIZE / 2;
	int hy = TileY(_pedestrian_source) * TILE_SIZE + TILE_SIZE / 2;
	if (!IsNearPedestrianViewport(hx, hy)) {
		_pedestrian_stats.passengers_offscreen += amount;
		return;
	}

	int tx, ty;
	if (!FindPedestrianTarget(st, hx, hy, tx, ty)) return;

	/* One pedestrian per 'ratio' passengers, the remainder by chance. */
	uint ratio = std::max<uint>(_settings_client.gui.pedestrian_ratio, 1);
	uint count = amount / ratio + (InteractiveRandomRange(ratio) < amount % ratio ? 1 : 0);
	count = std::min(count, MAX_PEDESTRIANS_PER_EVENT);

	/* Leave the house at its edge facing the station. */
	int dx = tx - hx;
	int dy = ty - hy;
	int len = IntSqrt(dx * dx + dy * dy);
	if (len < 8) return;
	int sx = hx + dx * 6 / len;
	int sy = hy + dy * 6 / len;

	for (uint i = 0; i < count; i++) {
		if (!SpawnPedestrian(sx, sy, tx, ty, PedestrianGoal::EnterStation)) return;
	}
}

/** Move and animate all pedestrians by one game tick. */
void PedestriansTick()
{
	PedestrianPathsTick();

	for (size_t i = 0; i < _pedestrians.size();) {
		Pedestrian &p = _pedestrians[i];

		int old_x = p.X();
		int old_y = p.Y();
		p.fx += p.vx;
		p.fy += p.vy;
		if (--p.ticks_left == 0) {
			p.fx = p.wx << 8;
			p.fy = p.wy << 8;
			if (!NextWaypoint(p)) {
				/* Arrived: disappear into the station or house. */
				MarkPedestrianDirty(p.coord);
				_pedestrian_stats.arrived++;
				p = _pedestrians.back();
				_pedestrians.pop_back();
				continue;
			}
		}
		bool moved = p.X() != old_x || p.Y() != old_y;

		bool animated = ++p.frame_ticks >= FRAME_TICKS;
		if (animated) {
			p.frame_ticks = 0;
			p.frame = (p.frame + 1) % PEDESTRIAN_FRAMES;
		}

		if (moved) p.z = GetSlopePixelZ(p.X(), p.Y());
		if (moved || animated) {
			Rect r = GetPedestrianCoord(p);
			MarkAllViewportsDirty(std::min(r.left, p.coord.left), std::min(r.top, p.coord.top),
					std::max(r.right, p.coord.right), std::max(r.bottom, p.coord.bottom));
			p.coord = r;
		}
		i++;
	}
}

/** Remove all pedestrians, e.g. when a game is started or loaded, or the setting is turned off. */
void ResetPedestrians()
{
	_pedestrians.clear();
	_pedestrian_stats = {};
	ResetPedestrianPaths();
	ResetPedestrianCrowds();
}

/**
 * Add the pedestrians within a viewport area to the sprites to draw.
 * @param dpi The area being drawn.
 */
void ViewportAddPedestrians(DrawPixelInfo *dpi)
{
	if (_pedestrians.empty() || dpi->zoom > MAX_PEDESTRIAN_ZOOM || IsInvisibilitySet(TO_PEDESTRIANS)) return;
	const bool transparent = IsTransparencySet(TO_PEDESTRIANS);

	const int l = dpi->left;
	const int r = dpi->left + dpi->width;
	const int t = dpi->top;
	const int b = dpi->top + dpi->height;

	/* A tiny box around the feet, so the pedestrian sorts by where it stands. */
	static constexpr SpriteBounds bounds{{-1, -1, 0}, {2, 2, 8}, {1, 1, 0}};
	for (const Pedestrian &p : _pedestrians) {
		if (p.coord.right < l || p.coord.left > r || p.coord.bottom < t || p.coord.top > b) continue;
		AddSortableSpriteToDraw(SPR_PEDESTRIAN_BASE + p.sprite + p.frame, GetColourPalette(p.colour), p.X(), p.Y(), p.z, bounds, transparent);
	}
}

size_t GetPedestrianCount()
{
	return _pedestrians.size();
}

const PedestrianStats &GetPedestrianStats()
{
	return _pedestrian_stats;
}
