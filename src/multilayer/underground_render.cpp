/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_render.cpp Drawing underground tiles in the viewport. */

#include "../stdafx.h"
#include "underground_render.h"
#include "underground_signal.h"
#include "underground_view.h"
#include "multilayer_map.h"
#include "../viewport_func.h"
#include "../rail.h"
#include "../newgrf_railtype.h"
#include "../signal_func.h"
#include "../signal_type.h"
#include "../sprite.h"
#include "../spritecache.h"
#include "../town.h"
#include "../town_map.h"
#include "../industry_map.h"
#include "../slope_func.h"
#include "../table/sprites.h"
#include "../track_func.h"
#include "../pbs.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../map_func.h"
#include "../road_map.h"
#include "../station_base.h"
#include "../transparency.h"
#include "../elrail_func.h"
#include "station_complex.h"
#include "blueprint.h"
#include "blueprint_gui.h"
#include "underground_cmd.h"
#include "underground_rules.h"
#include "underground_gui.h"
#include "underground_soil.h"

#include "../safeguards.h"

/* One recolour per land soil layer in each of the two runs of soil recolours, then the floors
 * of house foundations, the water column and the forbidden sea bed; the sand below water
 * (slab variants and its own two runs of recolours), then the tunnel art closing the metro block. */
static_assert(SPR_METRO_SOIL_XRAY_PAL_BASE == SPR_METRO_SOIL_PAL_BASE + SOIL_LAND_LAYER_COUNT);
static_assert(SPR_METRO_SOIL_XRAY_PAL_BASE + SOIL_LAND_LAYER_COUNT == SPR_METRO_BASEMENT_FLOOR);
static_assert(SPR_METRO_SAND_PAL_BASE == SPR_METRO_SAND_SLAB_BASE + 2);
static_assert(SPR_METRO_SAND_XRAY_PAL_BASE == SPR_METRO_SAND_PAL_BASE + SOIL_SAND_LAYER_COUNT);
static_assert(SPR_METRO_SAND_XRAY_PAL_BASE + SOIL_SAND_LAYER_COUNT == SPR_METRO_TUNNEL_ART_BASE);
static_assert(SPR_METRO_TUNNEL_CABLES_DIAG_RIGHT + 1 == SPR_METRO_TUNNEL_ART_BASE + METRO_TUNNEL_ART_SPRITE_COUNT);
static_assert(SPR_METRO_PARKING_BASE == SPR_METRO_TUNNEL_ART_BASE + METRO_TUNNEL_ART_SPRITE_COUNT);
static_assert(SPR_METRO_INDUSTRY_BASEMENT_BASE == SPR_METRO_PARKING_BASE + METRO_PARKING_SPRITE_COUNT);
static_assert(SPR_METRO_INDUSTRY_BASEMENT_BASE + METRO_INDUSTRY_BASEMENT_SPRITE_COUNT == SPR_METRO_ICON_BLUEPRINTS);
static_assert(SPR_METRO_ICON_BLUEPRINTS + METRO_MORE_ICON_SPRITE_COUNT == SPR_METRO_STATION_BASE + METRO_STATION_SPRITE_COUNT);

/**
 * Whether the base set has the soil sprites; an older openttd.grf has a shorter
 * metro block, then the floor falls back to the plain slab and grey backdrop.
 * @return True if the last sprite of the metro block exists.
 */
static bool HasSoilSprites()
{
	return SpriteExists(SPR_METRO_STATION_BASE + METRO_STATION_SPRITE_COUNT - 1);
}

/**
 * Recolour of the slab of the slice view for a soil layer.
 * @param layer The layer.
 * @return The recolour sprite.
 */
static PaletteID GetSoilSlabPalette(SoilLayer layer)
{
	if (IsSandSoilLayer(layer)) return SPR_METRO_SAND_PAL_BASE + to_underlying(layer) - to_underlying(SoilLayer::SandLight);
	return SPR_METRO_SOIL_PAL_BASE + to_underlying(layer);
}

/**
 * Recolour of the X-ray backdrop for a soil layer.
 * @param layer The layer.
 * @return The recolour sprite.
 */
static PaletteID GetSoilXrayPalette(SoilLayer layer)
{
	if (IsSandSoilLayer(layer)) return SPR_METRO_SAND_XRAY_PAL_BASE + to_underlying(layer) - to_underlying(SoilLayer::SandLight);
	return SPR_METRO_SOIL_XRAY_PAL_BASE + to_underlying(layer);
}

/**
 * Get a recolour palette for the bare terrain backdrop of the X-ray view: the
 * soil of the viewed level, muted towards grey so the surface stays the main
 * picture. Where the level is not below the ground of the tile it is plain grey
 * (PALETTE_NEWSPAPER, the only true greyscale recolour in the base palette).
 * @param tile Tile of the backdrop.
 * @param depth Viewed level.
 * @return The recolour palette.
 */
static PaletteID GetDepthPalette(TileIndex tile, int16_t depth)
{
	if (!HasSoilSprites() || GetDepthBelowSurface(tile, depth) < 1) return PALETTE_NEWSPAPER;
	return GetSoilXrayPalette(GetSoilLayer(tile, depth));
}

/** Texture of the slab of each soil layer: SPR_METRO_SLAB or a pair of variants from SPR_METRO_SOIL_SLAB_BASE. */
static constexpr std::array<SpriteID, SOIL_LAYER_COUNT> SOIL_SLAB_TEXTURE = {
	SPR_METRO_SOIL_SLAB_BASE + 0, // Earth: roots.
	SPR_METRO_SLAB,               // Light clay.
	SPR_METRO_SLAB,               // Clay.
	SPR_METRO_SLAB,               // Dark clay.
	SPR_METRO_SOIL_SLAB_BASE + 2, // Light marble: veins.
	SPR_METRO_SOIL_SLAB_BASE + 2, // Grey marble: veins.
	SPR_METRO_SOIL_SLAB_BASE + 4, // Pink granite: grain.
	SPR_METRO_SOIL_SLAB_BASE + 4, // Grey granite: grain.
	SPR_METRO_SOIL_SLAB_BASE + 4, // Dark granite: grain.
	SPR_METRO_SOIL_SLAB_BASE + 6, // Black granite: sparkles.
	SPR_METRO_SOIL_SLAB_BASE + 8, // Obsidian: glassy sheen.
	SPR_METRO_SAND_SLAB_BASE,     // Light sand below water: grain and ripples.
	SPR_METRO_SAND_SLAB_BASE,     // Sand.
	SPR_METRO_SAND_SLAB_BASE,     // Dark sand.
};

/**
 * Draw the underground parking where a house foundation reaches the viewed level.
 * The garage of a house has its walls on the two back edges of the house; the back
 * corner tile (the north tile of the house) holds the ramp to the street, the tiles
 * along the walls the parked cars, the rest is the driving aisle. Cars are recoloured
 * per tile.
 * @param ti Tile being drawn; a house tile.
 */
static void DrawUndergroundParking(const TileInfo *ti)
{
	HouseID house = GetHouseType(ti->tile);
	TileIndex north = ti->tile + GetHouseNorthPart(house);
	bool back_x = TileX(ti->tile) == TileX(north); ///< On the north-east (x) edge of the house.
	bool back_y = TileY(ti->tile) == TileY(north); ///< On the north-west (y) edge of the house.
	uint role = back_x ? (back_y ? 0 : 2) : (back_y ? 1 : 3);
	uint32_t h = (TileX(ti->tile) * 0x9E3779B1U) ^ (TileY(ti->tile) * 0x85EBCA6BU);
	PaletteID cars = GetColourPalette(static_cast<Colours>(((h * 0x27D4EB2DU) >> 28) % COLOUR_END));
	DrawGroundSprite(SPR_METRO_PARKING_BASE + role, cars);
}

/**
 * Draw the basement of an industry where its foundation reaches the viewed level:
 * a store room packed with goods. Its walls, lined with pallet racks, stand on the
 * back edges of the industry, where the tile next to it on the north-east or
 * north-west side is not part of the same industry; the rest of the floor holds
 * pallets of cartons, crates and drums, in one of two variants per tile.
 * @param ti Tile being drawn; an industry tile.
 */
static void DrawUndergroundIndustryBasement(const TileInfo *ti)
{
	const IndustryID ind = GetIndustryIndex(ti->tile);
	auto same_industry = [ind](TileIndex t) { return t != INVALID_TILE && IsTileType(t, TileType::Industry) && GetIndustryIndex(t) == ind; };
	bool back_x = !same_industry(TileAddWrap(ti->tile, -1, 0)); ///< Wall on the north-east (x) edge.
	bool back_y = !same_industry(TileAddWrap(ti->tile, 0, -1)); ///< Wall on the north-west (y) edge.
	uint role = back_x ? (back_y ? 0 : 2) : (back_y ? 1 : 3);
	uint32_t h = (TileX(ti->tile) * 0x9E3779B1U) ^ (TileY(ti->tile) * 0x85EBCA6BU);
	uint variant = (h * 0x27D4EB2DU) >> 31;
	DrawGroundSprite(SPR_METRO_INDUSTRY_BASEMENT_BASE + role * 2 + variant, PAL_NONE);
}

/**
 * Draw the floor of the slice view at the viewed level: light concrete where the
 * level has something built; the parking of a house foundation, the store room of
 * an industry foundation or the water column of a river, canal or the sea where
 * they reach down to the level,
 * and dark hatched rock below the sea where building is forbidden; otherwise the
 * cut soil of the tile at that depth. Where the level is not below the ground
 * (views of the levels below or above), the plain grey slab.
 * @param ti Tile being drawn; the floor is at ti->z.
 * @param depth Viewed level.
 * @param built The viewed level has a track, station, ramp or portal on the tile.
 */
static void DrawUndergroundFloor(const TileInfo *ti, int16_t depth, bool built)
{
	if (!HasSoilSprites()) {
		DrawGroundSprite(SPR_METRO_SLAB, PAL_NONE);
		return;
	}
	if (built) {
		DrawGroundSprite(SPR_METRO_CONCRETE_FLOOR, PAL_NONE);
		return;
	}
	if (GetDepthBelowSurface(ti->tile, depth) < 1) {
		DrawGroundSprite(SPR_METRO_SLAB, PAL_NONE);
		return;
	}
	switch (GetUndergroundObstacle(ti->tile, depth)) {
		case UndergroundObstacle::HouseFoundation:
			DrawUndergroundParking(ti);
			return;
		case UndergroundObstacle::IndustryFoundation:
			DrawUndergroundIndustryBasement(ti);
			return;
		case UndergroundObstacle::Water: {
			/* Darker the deeper below the bed: the first, second, third level of the column. */
			int in_water = std::clamp(GetDepthBelowSurface(ti->tile, depth), 1, 3);
			DrawGroundSprite(SPR_METRO_WATER_FLOOR_BASE + in_water - 1, PAL_NONE);
			return;
		}
		case UndergroundObstacle::SeaForbidden:
			DrawGroundSprite(SPR_METRO_SEA_FORBIDDEN, PAL_NONE);
			return;
		case UndergroundObstacle::None:
			break;
	}

	SoilLayer layer = GetSoilLayer(ti->tile, depth);
	SpriteID image = SOIL_SLAB_TEXTURE[to_underlying(layer)];
	/* The textured layers have two variants; mix them over the map. Hash both
	 * coordinates together, else whole rows and columns flip at once. */
	if (image != SPR_METRO_SLAB) {
		uint32_t h = (TileX(ti->tile) * 0x9E3779B1U) ^ (TileY(ti->tile) * 0x85EBCA6BU);
		image += (h * 0x27D4EB2DU) >> 31;
	}
	DrawGroundSprite(image, GetSoilSlabPalette(layer));
}

/**
 * Bounding box of a flat, tile-sized sprite drawn as a sortable sprite (X-ray view).
 * It lies just below the floor, so it always sorts behind what stands on the floor:
 * a box at the floor's height would overlap the train parts on it, and the sorter
 * then put the track of a neighbouring tile over the front of a train.
 */
static constexpr SpriteBounds FLAT_TILE_BOUNDS = {{0, 0, -1}, {TILE_SIZE, TILE_SIZE, 1}, {0, 0, 1}};

/**
 * Where and how the content of an underground level is drawn.
 *
 * Slice view: the level is the ground the viewport draws, so its sprites are
 * ordinary ground sprites relative to ti->z (the plane).
 *
 * X-ray view: the surface tile is the ground; the level lies below it at its
 * true depth. Its sprites are added as sortable sprites at that depth so they
 * appear through the (transparent) surface at the right place on screen.
 */
struct LevelDrawContext {
	const TileInfo *ti;
	bool slice_mode;
	int z;          ///< Height of the level's floor relative to ti->z.

	LevelDrawContext(const TileInfo *ti, int16_t depth, bool slice_mode, int extra_z = 0) : ti(ti), slice_mode(slice_mode)
	{
		this->z = (slice_mode ? 0 : depth * static_cast<int>(TILE_HEIGHT) - ti->z) + extra_z;
	}

	/** Draw a flat, tile-sized sprite on the level's floor. */
	void Flat(SpriteID image, PaletteID pal, int x = 0, int y = 0, int dz = 0) const
	{
		if (this->slice_mode) {
			DrawGroundSpriteAt(image, pal, x, y, this->z + dz);
		} else {
			AddSortableSpriteToDraw(image, pal, this->ti->x + x, this->ti->y + y, this->ti->z + this->z + dz, FLAT_TILE_BOUNDS, false);
		}
	}

	/** Draw a sprite with its own bounding box on the level's floor. */
	void Box(SpriteID image, PaletteID pal, const SpriteBounds &bounds, bool transparent = false) const
	{
		AddSortableSpriteToDraw(image, pal, this->ti->x, this->ti->y, this->ti->z + this->z, bounds, transparent);
	}
};

/** Platforms up to this many tiles long are drawn in the metro style. */
static constexpr uint METRO_STYLE_MAX_PLATFORM_LENGTH = 4;

/**
 * Measure the run of platform tiles of the same station, level and axis that
 * contains \a tile.
 * @param tile Tile of the platform.
 * @param depth Level of the platform.
 * @param slice The platform's slice.
 * @param is_x Axis of the platform.
 * @param[out] open_start The platform ends at the north side of \a tile (lower coordinate).
 * @param[out] open_end The platform ends at the south side of \a tile (higher coordinate).
 * @return Length of the run in tiles (counting stops at a few tiles past the metro limit).
 */
static uint GetUndergroundPlatformRun(TileIndex tile, int16_t depth, const TileSlice &slice, bool is_x, bool &open_start, bool &open_end)
{
	auto same_platform = [&](int dist) {
		int x = TileX(tile) + (is_x ? dist : 0);
		int y = TileY(tile) + (is_x ? 0 : dist);
		if (x < 0 || y < 0 || x >= static_cast<int>(Map::SizeX()) || y >= static_cast<int>(Map::SizeY())) return false;
		SliceID sid = _multilayer_map.FindSliceAt(TileXY(x, y), depth);
		if (sid == INVALID_SLICE_ID) return false;
		const TileSlice &s = _multilayer_map.GetSlice(sid);
		if (s.kind != SliceKind::StationTile || s.station.station_id != slice.station.station_id) return false;
		return ((static_cast<TrackBits>(s.station.tracks) & TRACK_BIT_X) != 0) == is_x;
	};

	constexpr int LIMIT = METRO_STYLE_MAX_PLATFORM_LENGTH + 1;
	uint length = 1;
	int d = 1;
	while (d <= LIMIT && same_platform(-d)) d++;
	open_start = d == 1;
	length += d - 1;
	d = 1;
	while (d <= LIMIT && same_platform(d)) d++;
	open_end = d == 1;
	length += d - 1;
	return length;
}

enum UndergroundSignalOffsets {
	UG_SIGNAL_TO_SOUTHWEST,
	UG_SIGNAL_TO_NORTHEAST,
	UG_SIGNAL_TO_SOUTHEAST,
	UG_SIGNAL_TO_NORTHWEST,
	UG_SIGNAL_TO_EAST,
	UG_SIGNAL_TO_WEST,
	UG_SIGNAL_TO_SOUTH,
	UG_SIGNAL_TO_NORTH,
};

static Trackdir FindUndergroundSignalTrackdir(Track track, uint8_t signal_bit)
{
	for (Trackdir td = TRACKDIR_BEGIN; td < TRACKDIR_END; td++) {
		if (!IsValidTrackdir(td)) continue;
		if (TrackdirToTrack(td) != track) continue;
		if (SignalAlongTrackdir(td) == signal_bit) return td;
	}
	return INVALID_TRACKDIR;
}

static DiagDirection GetRampDescentDirection(const TileSlice &slice);

/**
 * Height of a point of a ramp above its lower floor: the ramp rises one level
 * over the tile against the direction it descends towards.
 * @param slice The ramp.
 * @param x X position within the tile.
 * @param y Y position within the tile.
 * @return The height in pixels.
 */
static int GetRampPixelZ(const TileSlice &slice, int x, int y)
{
	switch (GetRampDescentDirection(slice)) {
		case DIAGDIR_NE: return x * TILE_HEIGHT / TILE_SIZE;
		case DIAGDIR_SW: return (TILE_SIZE - x) * TILE_HEIGHT / TILE_SIZE;
		case DIAGDIR_NW: return y * TILE_HEIGHT / TILE_SIZE;
		case DIAGDIR_SE: return (TILE_SIZE - y) * TILE_HEIGHT / TILE_SIZE;
		default: NOT_REACHED();
	}
}

static void DrawUndergroundSignalSprite(const LevelDrawContext &ctx, const TileRef &ref, const RailTypeInfo *rti, Track track, uint8_t signal_bit, UndergroundSignalOffsets image, uint pos)
{
	const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
	if ((GetUndergroundSignalMask(slice) & signal_bit) == 0) return;

	Trackdir td = FindUndergroundSignalTrackdir(track, signal_bit);
	SignalState state = td == INVALID_TRACKDIR ? SIGNAL_STATE_RED : GetUndergroundSignalState(ref, td);

	bool side;
	switch (_settings_game.construction.train_signal_side) {
		case 0:  side = false;                                 break;
		case 2:  side = true;                                  break;
		default: side = _settings_game.vehicle.road_side != 0; break;
	}

	static const Point kSignalPositions[2][12] = {
		{
			{ 8,  5}, {14,  1}, { 1, 14}, { 9, 11}, { 1,  0}, { 3, 10},
			{11,  4}, {14, 14}, {11,  3}, { 4, 13}, { 3,  4}, {11, 13}
		}, {
			{14,  1}, {12, 10}, { 4,  6}, { 1, 14}, {10,  4}, { 0,  1},
			{14, 14}, { 5, 12}, {11, 13}, { 4,  3}, {13,  4}, { 3, 11}
		}
	};

	SignalType type = GetUndergroundSignalType(slice, track);
	SignalVariant variant = GetUndergroundSignalVariant(slice, track);

	SpriteID signal_sprite = GetCustomSignalSprite(rti, ref.tile, type, variant, state);
	if (signal_sprite != 0) {
		signal_sprite += image;
	} else {
		/* Mirror DrawSingleSignal in rail_cmd.cpp: normal electric signals are
		 * stored in a different sprite block than all other signals. */
		signal_sprite = (type == SIGTYPE_BLOCK && variant == SIG_ELECTRIC) ? SPR_ORIGINAL_SIGNALS_BASE : SPR_SIGNALS_BASE - 16;
		signal_sprite += type * 16 + variant * 64 + image * 2 + state + (type > SIGTYPE_LAST_NOPBS ? 64 : 0);
	}
	/* Sortable like a surface signal, so it sorts correctly against the tunnel ribs. */
	const Point &p = kSignalPositions[side][pos];
	/* On a ramp the signal stands on the incline, like a surface signal on a slope. */
	int8_t z = slice.kind == SliceKind::Ramp ? static_cast<int8_t>(GetRampPixelZ(slice, p.x, p.y)) : 0;
	ctx.Box(signal_sprite, PAL_NONE, SpriteBounds{{static_cast<int8_t>(p.x), static_cast<int8_t>(p.y), z}, {1, 1, BB_HEIGHT_UNDER_BRIDGE}, {0, 0, 0}});
}

/**
 * Check if a tile has underground content at any depth.
 */
bool HasUndergroundContent(TileIndex tile)
{
	if (!_multilayer_map.IsAllocated()) return false;
	return _multilayer_map.GetColumn(tile).HasUnderground();
}

/**
 * Check if a tile has a portal (for surface rendering — portals should
 * be visible even when NOT in underground view).
 */
bool HasPortalOnTile(TileIndex tile)
{
	if (!_multilayer_map.IsAllocated()) return false;
	return _multilayer_map.FindSliceByKind(tile, SliceKind::Portal) != INVALID_SLICE_ID;
}

/**
 * Position of a surface signal on the tile of a dive portal. The floor of the
 * open cut drops away from its surface edge, so a signal at the usual place in
 * the middle of the tile would hang in the air over the descent: it stands at
 * the surface end of the cut instead, on its own side of the track.
 * @param tile The tile of the signal.
 * @param axis Axis of the signal's track.
 * @param[in,out] x Position of the signal within the tile.
 * @param[in,out] y Position of the signal within the tile.
 * @return Whether the tile is a dive portal along \a axis, so the position was
 *         moved and the signal is to be drawn over the walls of the portal.
 */
bool PlaceDivePortalSignal(TileIndex tile, Axis axis, uint &x, uint &y)
{
	if (!_multilayer_map.IsAllocated()) return false;
	SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return false;

	const TileSlice &ps = _multilayer_map.GetSlice(sid);
	if (IsHillsidePortal(tile, ps)) return false;
	DiagDirection dir = static_cast<DiagDirection>(ps.portal.direction);
	if (DiagDirToAxis(dir) != axis) return false;

	/* The surface edge of the cut is the edge away from the tunnel. */
	uint along = (dir == DIAGDIR_NE || dir == DIAGDIR_NW) ? TILE_SIZE - 2 : 1;
	(axis == AXIS_X ? x : y) = along;
	return true;
}

/** Same bounding boxes as vanilla DrawTile_TunnelBridge tunnel roofs, per tunnel direction. */
static constexpr SpriteBounds _portal_roof_bounds[DIAGDIR_END] = {
	{{0, 1, BB_Z_SEPARATOR}, {TILE_SIZE, TILE_SIZE - 1, 1}, {TILE_SIZE - 1, TILE_SIZE - 2, -BB_Z_SEPARATOR}}, // NE
	{{1, 0, BB_Z_SEPARATOR}, {TILE_SIZE - 1, TILE_SIZE, 1}, {TILE_SIZE - 2, TILE_SIZE - 1, -BB_Z_SEPARATOR}}, // SE
	{{0, 1, BB_Z_SEPARATOR}, {TILE_SIZE, TILE_SIZE - 1, 1}, {TILE_SIZE - 1, TILE_SIZE - 2, -BB_Z_SEPARATOR}}, // SW
	{{1, 0, BB_Z_SEPARATOR}, {TILE_SIZE - 1, TILE_SIZE, 1}, {TILE_SIZE - 2, TILE_SIZE - 1, -BB_Z_SEPARATOR}}, // NW
};

/** Company colour palette for the 'M' marking of a portal. */
static PaletteID GetPortalPalette(const TileSlice &ps)
{
	return ps.owner < MAX_COMPANIES ? GetCompanyPalette(static_cast<CompanyID>(ps.owner)) : PALETTE_TO_GREY;
}

/**
 * Draw the metro portal on the surface (always visible, not just in underground view).
 * Like a vanilla tunnel head: the dark tunnel void as a ground sprite plus the
 * concrete portal as a sortable sprite with the tunnel roof bounds. A hillside
 * mouth is a headwall at the top of its incline, a dive portal (below a flat
 * tile) an entrance block on the ground; both carry an 'M' in company colour.
 */
void DrawPortalOverlay(const TileInfo *ti)
{
	SliceID sid = _multilayer_map.FindSliceByKind(ti->tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return;

	const TileSlice &ps = _multilayer_map.GetSlice(sid);
	DiagDirection dir = static_cast<DiagDirection>(ps.portal.direction);
	PaletteID pal = GetPortalPalette(ps);

	/* Sprite pair: rear at +dir*2, front at +dir*2+1. */
	SpriteID base = SPR_METRO_PORTAL_HILL_BASE;
	if (!IsHillsidePortal(ti->tile, ps)) {
		base = GetOpenCutDropLevels(ti->tile, ps) == 1 ? SPR_METRO_PORTAL_CUT_SHALLOW_BASE : SPR_METRO_PORTAL_DIVE_BASE;
	}
	SpriteID image = base + dir * 2;
	DrawGroundSprite(image, pal);
	AddSortableSpriteToDraw(image + 1, pal, *ti, _portal_roof_bounds[dir], false);
}

bool HasStationExitOverlay(TileIndex tile)
{
	const StationComplexNode *n = FindExplicitStationExitAt(tile);
	return n != nullptr && n->exit_type == StationExitType::Underpass;
}

/**
 * Draw an underpass exit on its road tile: a stairwell with railings and an
 * 'M' post in the pavement on the road's north side, in the station owner's
 * colour. Pavilions need nothing here: they are bus station tiles that draw
 * the metro entrance building themselves (see DrawTile_Station).
 */
void DrawStationExitOverlay(const TileInfo *ti)
{
	const StationComplexNode *n = FindExplicitStationExitAt(ti->tile);
	if (n == nullptr || n->exit_type != StationExitType::Underpass) return;
	if (!IsTileType(ti->tile, TileType::Road) || !IsNormalRoad(ti->tile)) return;

	const Station *st = Station::GetIfValid(StationID(n->station_id));
	PaletteID pal = (st != nullptr && Company::IsValidID(st->owner)) ? GetCompanyPalette(st->owner) : PALETTE_TO_GREY;

	bool axis_x = (GetAllRoadBits(ti->tile) & ROAD_X) != ROAD_NONE;
	static constexpr SpriteBounds bounds_x = {{0, 0, 0}, {TILE_SIZE, 3, 16}, {0, 0, 0}};
	static constexpr SpriteBounds bounds_y = {{0, 0, 0}, {3, TILE_SIZE, 16}, {0, 0, 0}};
	SpriteID image = axis_x ? SPR_METRO_UNDERPASS_X : SPR_METRO_UNDERPASS_Y;
	int z = ti->z + GetSlopeMaxPixelZ(ti->tileh);
	AddSortableSpriteToDraw(image, pal, ti->x, ti->y, z, axis_x ? bounds_x : bounds_y, IsTransparencySet(TO_BUILDINGS));
}

/** High-side slope of an incline that DESCENDS towards the given direction. */
static const Slope _metro_descent_slope[DIAGDIR_END] = {SLOPE_SW, SLOPE_NW, SLOPE_NE, SLOPE_SE};

/** Same slope-to-sprite mapping the surface rail renderer uses for single sloped track. */
static const uint8_t _metro_track_sloped_sprites[14] = {
	14, 15, 22, 13,
	 0, 21, 17, 12,
	23,  0, 18, 20,
	19, 16
};

/**
 * The direction a ramp descends towards. ramp.direction is the direction of
 * travel from the ramp's entry level to its exit level (the pathfinder's view),
 * so it descends that way only when the exit is the lower level.
 */
static DiagDirection GetRampDescentDirection(const TileSlice &slice)
{
	DiagDirection dir = static_cast<DiagDirection>(slice.ramp.direction);
	return slice.ramp.exit_depth < slice.ramp.entry_depth ? dir : ReverseDiagDir(dir);
}

/** Single inclined rail sprite for an incline descending towards descent_dir. */
static SpriteID GetInclinedRailSprite(DiagDirection descent_dir, RailType rt)
{
	const RailTypeInfo *rti = GetRailTypeInfo(rt);
	if (rti->UsesOverlay()) {
		/* NewGRF track: its own sloped piece, as on a vanilla slope. */
		static const RailTrackOffset slope_offset[DIAGDIR_END] = {RTO_SLOPE_SW, RTO_SLOPE_NW, RTO_SLOPE_NE, RTO_SLOPE_SE};
		return GetCustomRailSprite(rti, INVALID_TILE, RTSG_GROUND) + slope_offset[descent_dir];
	}
	if (rt >= RAILTYPE_END) rt = RAILTYPE_RAIL;
	Slope s = _metro_descent_slope[descent_dir];
	return _metro_track_sloped_sprites[s - 1] + GetRailTypeInfo(rt)->base_sprites.single_sloped - 20;
}

/** Draw the single-piece rail sprites for a set of track bits. */
/**
 * Draw the track of an underground level the way the surface draws it, so every
 * track type looks like itself below ground too: NewGRF track types (high speed,
 * third rail, ...) with their own ground and overlay sprites, see
 * DrawTrackBitsOverlay. Recoloured drawings (reservation, ghosts) show the rails only.
 */
static void DrawTrackPieces(const LevelDrawContext &ctx, TrackBits tracks, const RailTypeInfo *rti, PaletteID pal)
{
	if (rti->UsesOverlay()) {
		SpriteID overlay = GetCustomRailSprite(rti, INVALID_TILE, RTSG_OVERLAY);
		auto draw_rails = [&](SpriteID base, PaletteID p) {
			if (tracks & TRACK_BIT_X)     ctx.Flat(base + RTO_X, p);
			if (tracks & TRACK_BIT_Y)     ctx.Flat(base + RTO_Y, p);
			if (tracks & TRACK_BIT_UPPER) ctx.Flat(base + RTO_N, p);
			if (tracks & TRACK_BIT_LOWER) ctx.Flat(base + RTO_S, p);
			if (tracks & TRACK_BIT_RIGHT) ctx.Flat(base + RTO_E, p);
			if (tracks & TRACK_BIT_LEFT)  ctx.Flat(base + RTO_W, p);
		};
		if (pal != PAL_NONE) {
			draw_rails(overlay, pal);
			return;
		}
		SpriteID ground = GetCustomRailSprite(rti, INVALID_TILE, RTSG_GROUND);
		switch (tracks) {
			/* Single pieces and pairs that do not overlap: the ground sprites alone. */
			case TRACK_BIT_X: case TRACK_BIT_Y: case TRACK_BIT_UPPER: case TRACK_BIT_LOWER:
			case TRACK_BIT_RIGHT: case TRACK_BIT_LEFT: case TRACK_BIT_HORZ: case TRACK_BIT_VERT:
				draw_rails(ground, PAL_NONE);
				return;
			case TRACK_BIT_CROSS:
				ctx.Flat(ground + RTO_CROSSING_XY, PAL_NONE);
				return;
			default:
				break;
		}
		/* A junction: its ground, then the rails of every piece. */
		if ((tracks & TRACK_BIT_3WAY_NE) == 0) {
			ctx.Flat(ground + RTO_JUNCTION_SW, PAL_NONE);
		} else if ((tracks & TRACK_BIT_3WAY_SW) == 0) {
			ctx.Flat(ground + RTO_JUNCTION_NE, PAL_NONE);
		} else if ((tracks & TRACK_BIT_3WAY_NW) == 0) {
			ctx.Flat(ground + RTO_JUNCTION_SE, PAL_NONE);
		} else if ((tracks & TRACK_BIT_3WAY_SE) == 0) {
			ctx.Flat(ground + RTO_JUNCTION_NW, PAL_NONE);
		} else {
			ctx.Flat(ground + RTO_JUNCTION_NSEW, PAL_NONE);
		}
		draw_rails(overlay, PAL_NONE);
		return;
	}
	if (tracks & TRACK_BIT_X)     ctx.Flat(rti->base_sprites.single_x, pal);
	if (tracks & TRACK_BIT_Y)     ctx.Flat(rti->base_sprites.single_y, pal);
	if (tracks & TRACK_BIT_UPPER) ctx.Flat(rti->base_sprites.single_n, pal);
	if (tracks & TRACK_BIT_LOWER) ctx.Flat(rti->base_sprites.single_s, pal);
	if (tracks & TRACK_BIT_LEFT)  ctx.Flat(rti->base_sprites.single_w, pal);
	if (tracks & TRACK_BIT_RIGHT) ctx.Flat(rti->base_sprites.single_e, pal);
}

static const RailTypeInfo *GetRailTypeInfoSafe(uint8_t rt)
{
	return GetRailTypeInfo(rt < RAILTYPE_END ? static_cast<RailType>(rt) : RAILTYPE_RAIL);
}

/**
 * Pale ghosts of content on levels other than the viewed one, toggleable per
 * direction via the toolbar (_underground_show_above / _underground_show_below).
 * In the slice view the ghosts lie on the plane; in the X-ray view at their own depth.
 */
static void DrawUndergroundLevelContent(const TileInfo *ti, int16_t depth, SliceID sid, bool slice_mode);

static void DrawUndergroundGhosts(const TileInfo *ti, int16_t depth, bool slice_mode)
{
	const TileColumn &col = _multilayer_map.GetColumn(ti->tile);
	for (SliceID other_sid : col.slices) {
		const TileSlice &other = _multilayer_map.GetSlice(other_sid);
		if (other.kind == SliceKind::Surface || other.kind == SliceKind::End) continue;
		if (other.span.Contains(depth)) continue; /* The viewed level is drawn in full. */

		bool above = other.span.z_bot > depth;
		if (above ? !_underground_show_above : !_underground_show_below) continue;
		/* The views of the levels below or above draw those levels in full. */
		if (IsUndergroundOtherLevelDrawn(other.span.z_bot)) continue;

		/* On the construction plane only the connections between levels are hinted;
		 * plain track of other levels would just clutter the slab. */
		if (slice_mode && other.kind != SliceKind::Ramp && other.kind != SliceKind::Portal) continue;

		/* In the X-ray view the open cut of a portal diving into flat ground already
		 * shows its track running down to the tunnel (see DrawPortalOverlay); a flat
		 * ghost at the portal's level would lie across the cut. */
		if (!slice_mode && other.kind == SliceKind::Portal && !IsHillsidePortal(ti->tile, other)) continue;

		/* In the X-ray view stations of every level are drawn in full at their own
		 * depth, so the player sees where they are without picking their level. */
		if (!slice_mode && other.kind == SliceKind::StationTile) {
			DrawUndergroundLevelContent(ti, other.span.z_bot, other_sid, false);
			continue;
		}

		/* Ghosts of the slice view sit a hair above the plane so they are not hidden by its ground. */
		LevelDrawContext ctx(ti, slice_mode ? depth : other.span.z_bot, slice_mode, slice_mode ? 7 : 0);

		switch (other.kind) {
			case SliceKind::Track:
				DrawTrackPieces(ctx, static_cast<TrackBits>(other.track.tracks), GetRailTypeInfoSafe(other.track.railtype), PALETTE_TO_TRANSPARENT);
				break;
			case SliceKind::Portal: {
				DiagDirection pd = static_cast<DiagDirection>(other.portal.direction);
				ctx.Flat(GetRailTypeInfoSafe(other.portal.railtype)->base_sprites.single_x + (DiagDirToAxis(pd) == AXIS_X ? 0 : 1), PALETTE_TO_TRANSPARENT);
				break;
			}
			case SliceKind::Ramp: {
				ctx.Flat(GetInclinedRailSprite(GetRampDescentDirection(other), static_cast<RailType>(other.ramp.railtype)), PALETTE_TO_TRANSPARENT);
				break;
			}
			default: break;
		}
	}
}

/**
 * Draw the content of the slice at the viewed level in full detail.
 * @param ti         Tile info; in the slice view its z is the plane the level is drawn at.
 * @param depth      The viewed level.
 * @param sid        The slice on that level.
 * @param slice_mode True in the cut-plane view (content is the ground), false in the
 *                   X-ray view (content is drawn at its true depth through the surface).
 */
static void DrawUndergroundLevelContent(const TileInfo *ti, int16_t depth, SliceID sid, bool slice_mode)
{
	TileIndex tile = ti->tile;
	const TileSlice &slice = _multilayer_map.GetSlice(sid);

	/* A ramp seen from its upper level descends into the plane: base it one level lower. */
	int extra_z = 0;
	if (slice.kind == SliceKind::Ramp && depth == std::max(slice.ramp.entry_depth, slice.ramp.exit_depth)) {
		extra_z = -static_cast<int>(TILE_HEIGHT);
	}
	LevelDrawContext ctx(ti, depth, slice_mode, extra_z);

	switch (slice.kind) {
		case SliceKind::Track: {
			TrackBits tracks = static_cast<TrackBits>(slice.track.tracks);
			const RailTypeInfo *rti = GetRailTypeInfoSafe(slice.track.railtype);

			DrawTrackPieces(ctx, tracks, rti, PAL_NONE);
			DrawUndergroundCatenary(ti, tracks, static_cast<RailType>(slice.track.railtype), ti->z + ctx.z);

			/* PBS reservation overlay. */
			if (slice.track.reserved != 0) {
				DrawTrackPieces(ctx, static_cast<TrackBits>(slice.track.reserved), rti, PALETTE_CRASH);
			}

			/* Signals, of every type, in both directions. */
			if (slice.track.signal_mask != 0) {
				TileRef ref{tile, sid};
				auto maybe_draw_signal = [&](uint8_t signal_bit, UndergroundSignalOffsets image, uint pos, Track track) {
					DrawUndergroundSignalSprite(ctx, ref, rti, track, signal_bit, image, pos);
				};

				if (!(tracks & TRACK_BIT_Y)) {
					if (!(tracks & TRACK_BIT_X)) {
						if (tracks & TRACK_BIT_LEFT) {
							maybe_draw_signal(1U << 2, UG_SIGNAL_TO_NORTH, 0, TRACK_LEFT);
							maybe_draw_signal(1U << 3, UG_SIGNAL_TO_SOUTH, 1, TRACK_LEFT);
						}
						if (tracks & TRACK_BIT_RIGHT) {
							maybe_draw_signal(1U << 0, UG_SIGNAL_TO_NORTH, 2, TRACK_RIGHT);
							maybe_draw_signal(1U << 1, UG_SIGNAL_TO_SOUTH, 3, TRACK_RIGHT);
						}
						if (tracks & TRACK_BIT_UPPER) {
							maybe_draw_signal(1U << 3, UG_SIGNAL_TO_WEST, 4, TRACK_UPPER);
							maybe_draw_signal(1U << 2, UG_SIGNAL_TO_EAST, 5, TRACK_UPPER);
						}
						if (tracks & TRACK_BIT_LOWER) {
							maybe_draw_signal(1U << 1, UG_SIGNAL_TO_WEST, 6, TRACK_LOWER);
							maybe_draw_signal(1U << 0, UG_SIGNAL_TO_EAST, 7, TRACK_LOWER);
						}
					} else {
						maybe_draw_signal(1U << 3, UG_SIGNAL_TO_SOUTHWEST, 8, TRACK_X);
						maybe_draw_signal(1U << 2, UG_SIGNAL_TO_NORTHEAST, 9, TRACK_X);
					}
				} else {
					maybe_draw_signal(1U << 3, UG_SIGNAL_TO_SOUTHEAST, 10, TRACK_Y);
					maybe_draw_signal(1U << 2, UG_SIGNAL_TO_NORTHWEST, 11, TRACK_Y);
				}
			}

			/* A concrete rib over a straight track, so a line of them reads as a
			 * tunnel. Its vault springs above the height of a signal. It stands
			 * near the edge of the tile, but on a tile with signals in the middle,
			 * between the two signal positions (4 and 11 along the track), so it
			 * never stands on a signal. */
			bool has_signals = GetUndergroundSignalMask(slice) != 0;
			int8_t rib = has_signals ? 7 : 2;
			/* Along the tunnel run an evacuation walkway on the front side (a ground
			 * sprite: nothing on the tile stands in front of it) and a cable tray on
			 * the back side, sorted before the rib (its box is the smaller one), so
			 * the rib's leg covers the cables where they pass through it. Both are
			 * left out on junctions, where they would cross the other track. */
			if (tracks == TRACK_BIT_X) {
				ctx.Flat(SPR_METRO_TUNNEL_WALKWAY_X, PAL_NONE);
				ctx.Box(has_signals ? SPR_METRO_TUNNEL_CABLES_SIGNAL_X : SPR_METRO_TUNNEL_CABLES_X, PAL_NONE, SpriteBounds{{0, 0, 0}, {TILE_SIZE, 1, 1}, {0, 0, 0}});
				ctx.Box(SPR_METRO_TUNNEL_RIB_X, PAL_NONE, SpriteBounds{{rib, 0, 0}, {1, TILE_SIZE, 18}, {0, 0, 0}});
			} else if (tracks == TRACK_BIT_Y) {
				ctx.Flat(SPR_METRO_TUNNEL_WALKWAY_Y, PAL_NONE);
				ctx.Box(has_signals ? SPR_METRO_TUNNEL_CABLES_SIGNAL_Y : SPR_METRO_TUNNEL_CABLES_Y, PAL_NONE, SpriteBounds{{0, 0, 0}, {1, TILE_SIZE, 1}, {0, 0, 0}});
				ctx.Box(SPR_METRO_TUNNEL_RIB_Y, PAL_NONE, SpriteBounds{{0, rib, 0}, {TILE_SIZE, 1, 18}, {0, 0, 0}});
			} else if ((tracks & (TRACK_BIT_X | TRACK_BIT_Y)) == TRACK_BIT_NONE) {
				/* Walkway and cables of diagonal pieces lie on the lines of the straight
				 * tiles they connect to; only parallel pieces get them. */
				if ((tracks & TRACK_BIT_HORZ) == tracks || (tracks & TRACK_BIT_VERT) == tracks) {
					static constexpr Track pieces[] = {TRACK_UPPER, TRACK_LOWER, TRACK_LEFT, TRACK_RIGHT};
					for (uint i = 0; i < std::size(pieces); i++) {
						if (HasBit(tracks, pieces[i])) ctx.Flat(SPR_METRO_TUNNEL_WALKWAY_DIAG_BASE + i, PAL_NONE);
					}
					static constexpr SpriteBounds cable_bounds{{0, 0, 0}, {TILE_SIZE, TILE_SIZE, 1}, {0, 0, 0}};
					if (tracks & TRACK_BIT_LOWER) ctx.Box(SPR_METRO_TUNNEL_CABLES_DIAG_LOWER, PAL_NONE, cable_bounds);
					if (tracks & TRACK_BIT_RIGHT) ctx.Box(SPR_METRO_TUNNEL_CABLES_DIAG_RIGHT, PAL_NONE, cable_bounds);
				}
				/* Diagonal pieces: a rib across the middle of each piece. */
				if (tracks & TRACK_BIT_UPPER) ctx.Box(SPR_METRO_TUNNEL_RIB_DIAG_A, PAL_NONE, SpriteBounds{{0, 0, 0}, {8, 8, 18}, {0, 0, 0}});
				if (tracks & TRACK_BIT_LOWER) ctx.Box(SPR_METRO_TUNNEL_RIB_DIAG_A, PAL_NONE, SpriteBounds{{8, 8, 0}, {8, 8, 18}, {0, 0, 0}});
				if (tracks & TRACK_BIT_RIGHT) ctx.Box(SPR_METRO_TUNNEL_RIB_DIAG_B, PAL_NONE, SpriteBounds{{0, 8, 0}, {8, 8, 18}, {0, 0, 0}});
				if (tracks & TRACK_BIT_LEFT)  ctx.Box(SPR_METRO_TUNNEL_RIB_DIAG_B, PAL_NONE, SpriteBounds{{8, 0, 0}, {8, 8, 18}, {0, 0, 0}});
			}
			break;
		}

		case SliceKind::Portal: {
			/* The exit of the level towards the surface; the portal tile is always
			 * cut at its own level (see IsTileCutBySlice) and trains on it are drawn
			 * on what is drawn here (see GetVehicleViewportZ).
			 * In the X-ray view the surface tile already shows the portal; the views
			 * of the levels below or above show no surface, so draw it at its depth. */
			if (IsUndergroundXRayMode()) break;
			DiagDirection dir = static_cast<DiagDirection>(slice.portal.direction);
			const RailTypeInfo *rti = GetRailTypeInfoSafe(slice.portal.railtype);
			if (IsHillsidePortal(tile, slice)) {
				/* Hillside mouth at the foot level: the floor runs flat out of a block
				 * of rock with a concrete headwall (rear: the dark tunnel void as a
				 * ground sprite, front: the block with the vanilla tunnel roof bounds
				 * so trains pass under it). */
				DrawTrackPieces(ctx, AxisToTrackBits(DiagDirToAxis(dir)), rti, PAL_NONE);
				SpriteID image = SPR_METRO_TUNNEL_BASE + dir * 2;
				ctx.Flat(image, PAL_NONE);
				ctx.Box(image + 1, PAL_NONE, _portal_roof_bounds[dir]);
			} else {
				/* Dive portal: the slab continues as a concrete ramp rising one level
				 * from the tunnel side to the open side, with the inclined rail and a
				 * gate on the open edge where the line leaves for the surface. */
				static constexpr SpriteBounds gate_bounds[DIAGDIR_END] = {
					{{TILE_SIZE - 1, 0, 0}, {1, TILE_SIZE, 20}, {0, 0, 0}}, // NE: open edge south-west
					{{0, 0, 0}, {TILE_SIZE, 1, 20}, {0, 0, 0}},             // SE: open edge north-west
					{{0, 0, 0}, {1, TILE_SIZE, 20}, {0, 0, 0}},             // SW: open edge north-east
					{{0, TILE_SIZE - 1, 0}, {TILE_SIZE, 1, 20}, {0, 0, 0}}, // NW: open edge south-east
				};
				ctx.Flat(SPR_METRO_PORTAL_RAMP_BASE + dir, PAL_NONE);
				ctx.Flat(GetInclinedRailSprite(dir, static_cast<RailType>(slice.portal.railtype)), PAL_NONE);
				ctx.Box(SPR_METRO_PORTAL_GATE_BASE + dir, GetPortalPalette(slice), gate_bounds[dir]);
			}
			break;
		}

		case SliceKind::StationTile: {
			TrackBits st_tracks = static_cast<TrackBits>(slice.station.tracks);
			bool is_x = (st_tracks & TRACK_BIT_X) != 0;
			const RailTypeInfo *rti = GetRailTypeInfoSafe(slice.station.railtype);

			/* Station floor, the track, and the platforms on both sides (same
			 * boxes as the vanilla rail platforms so trains sort correctly between
			 * them). The rear platform alternately carries a pillar and a name board. */
			ctx.Flat(is_x ? SPR_METRO_PLATFORM_FLOOR_X : SPR_METRO_PLATFORM_FLOOR_Y, PAL_NONE);
			DrawTrackPieces(ctx, is_x ? TRACK_BIT_X : TRACK_BIT_Y, rti, PAL_NONE);
			if (slice.station.reserved != 0) DrawTrackPieces(ctx, is_x ? TRACK_BIT_X : TRACK_BIT_Y, rti, PALETTE_CRASH);
			DrawUndergroundCatenary(ti, is_x ? TRACK_BIT_X : TRACK_BIT_Y, static_cast<RailType>(slice.station.railtype), ti->z + ctx.z);
			PaletteID plat_pal = slice.owner < MAX_COMPANIES ? GetCompanyPalette(static_cast<CompanyID>(slice.owner)) : PAL_NONE;
			bool pillar = ((is_x ? TileX(tile) : TileY(tile)) & 1) == 0;

			/* Short platforms get the metro style: two pillars per tile on the rear
			 * platform, short boards between them and an arched end wall at each
			 * end of the platform. Long platforms keep the railway style. */
			bool open_start, open_end;
			uint length = GetUndergroundPlatformRun(tile, depth, slice, is_x, open_start, open_end);
			if (length <= METRO_STYLE_MAX_PLATFORM_LENGTH) {
				/* Box coordinates given along / across the platform axis. */
				auto at = [is_x](int along, int across, int z) {
					int8_t a = static_cast<int8_t>(along), c = static_cast<int8_t>(across);
					return is_x ? Coord3D<int8_t>(a, c, static_cast<int8_t>(z)) : Coord3D<int8_t>(c, a, static_cast<int8_t>(z));
				};
				auto size = [is_x](int along, int across, int z) {
					uint8_t a = static_cast<uint8_t>(along), c = static_cast<uint8_t>(across);
					return is_x ? Coord3D<uint8_t>(a, c, static_cast<uint8_t>(z)) : Coord3D<uint8_t>(c, a, static_cast<uint8_t>(z));
				};
				if (is_x) {
					ctx.Box(SPR_METRO_PLATFORM_X_REAR,  plat_pal, SpriteBounds{{0,  0, 0}, {TILE_SIZE, 5, 2}, {0, 0, 0}});
					ctx.Box(SPR_METRO_PLATFORM_X_FRONT, plat_pal, SpriteBounds{{0, 11, 0}, {TILE_SIZE, 5, 2}, {0, 0, 0}});
				} else {
					ctx.Box(SPR_METRO_PLATFORM_Y_REAR,  plat_pal, SpriteBounds{{ 0, 0, 0}, {5, TILE_SIZE, 2}, {0, 0, 0}});
					ctx.Box(SPR_METRO_PLATFORM_Y_FRONT, plat_pal, SpriteBounds{{11, 0, 0}, {5, TILE_SIZE, 2}, {0, 0, 0}});
				}
				/* The big pillar sprite is drawn relative to its box, so moving the box
				 * along the platform moves the pillar. Pillars every 8 units. */
				SpriteID big = is_x ? SPR_METRO_PILLAR_X : SPR_METRO_PILLAR_Y;
				for (int a : {2, 10}) {
					ctx.Box(big, plat_pal, SpriteBounds{at(a, 1, 3), size(2, 2, 22), {0, 0, 0}});
				}
				if (!pillar) {
					ctx.Box(is_x ? SPR_METRO_BOARD_SHORT_X : SPR_METRO_BOARD_SHORT_Y, plat_pal, SpriteBounds{at(5, 1, 3), size(5, 2, 10), {0, 0, 0}});
				}
				SpriteID portal = is_x ? SPR_METRO_STATION_PORTAL_X : SPR_METRO_STATION_PORTAL_Y;
				bool transparent = IsTransparencySet(TO_BUILDINGS);
				if (open_start) ctx.Box(portal, plat_pal, SpriteBounds{at(0, 0, 0), size(1, TILE_SIZE, 20), {0, 0, 0}}, transparent);
				if (open_end) ctx.Box(portal, plat_pal, SpriteBounds{at(TILE_SIZE - 1, 0, 0), size(1, TILE_SIZE, 20), {0, 0, 0}}, transparent);
				break;
			}

			if (is_x) {
				ctx.Box(SPR_METRO_PLATFORM_X_REAR,  plat_pal, SpriteBounds{{0,  0, 0}, {TILE_SIZE, 5, 2}, {0, 0, 0}});
				ctx.Box(SPR_METRO_PLATFORM_X_FRONT, plat_pal, SpriteBounds{{0, 11, 0}, {TILE_SIZE, 5, 2}, {0, 0, 0}});
				if (pillar) {
					ctx.Box(SPR_METRO_PILLAR_X, plat_pal, SpriteBounds{{7, 1, 3}, {2, 2, 22}, {0, 0, 0}});
				} else {
					ctx.Box(SPR_METRO_BOARD_X, plat_pal, SpriteBounds{{5, 1, 3}, {7, 2, 10}, {0, 0, 0}});
				}
			} else {
				ctx.Box(SPR_METRO_PLATFORM_Y_REAR,  plat_pal, SpriteBounds{{ 0, 0, 0}, {5, TILE_SIZE, 2}, {0, 0, 0}});
				ctx.Box(SPR_METRO_PLATFORM_Y_FRONT, plat_pal, SpriteBounds{{11, 0, 0}, {5, TILE_SIZE, 2}, {0, 0, 0}});
				if (pillar) {
					ctx.Box(SPR_METRO_PILLAR_Y, plat_pal, SpriteBounds{{1, 7, 3}, {2, 2, 22}, {0, 0, 0}});
				} else {
					ctx.Box(SPR_METRO_BOARD_Y, plat_pal, SpriteBounds{{1, 5, 3}, {2, 7, 10}, {0, 0, 0}});
				}
			}
			break;
		}

		case SliceKind::Ramp: {
			/* Concrete incline plus an inclined rail, descending towards the lower end. */
			DiagDirection ramp_dir = GetRampDescentDirection(slice);
			ctx.Flat(SPR_METRO_RAMP_BASE + ramp_dir, PAL_NONE);
			ctx.Flat(GetInclinedRailSprite(ramp_dir, static_cast<RailType>(slice.ramp.railtype)), PAL_NONE);

			/* Arrow showing where the ramp leads from the viewed level:
			 * down = continues deeper, up = continues shallower. */
			bool descends = slice.ramp.exit_depth < slice.ramp.entry_depth;
			bool at_entry = (depth == slice.ramp.entry_depth);
			SpriteID arrow = (at_entry == descends) ? SPR_METRO_RAMP_ARROW_DOWN : SPR_METRO_RAMP_ARROW_UP;
			ctx.Flat(arrow, PAL_NONE, 8, 4, 7);

			/* Signals on its one track, in both directions, as on plain track. */
			if (slice.ramp.signal_mask != 0) {
				TileRef ref{tile, sid};
				const RailTypeInfo *rti = GetRailTypeInfoSafe(slice.ramp.railtype);
				if (GetRampTrack(slice.ramp) == TRACK_X) {
					DrawUndergroundSignalSprite(ctx, ref, rti, TRACK_X, 1U << 3, UG_SIGNAL_TO_SOUTHWEST, 8);
					DrawUndergroundSignalSprite(ctx, ref, rti, TRACK_X, 1U << 2, UG_SIGNAL_TO_NORTHEAST, 9);
				} else {
					DrawUndergroundSignalSprite(ctx, ref, rti, TRACK_Y, 1U << 3, UG_SIGNAL_TO_SOUTHEAST, 10);
					DrawUndergroundSignalSprite(ctx, ref, rti, TRACK_Y, 1U << 2, UG_SIGNAL_TO_NORTHWEST, 11);
				}
			}
			break;
		}

		default:
			break;
	}
}

/**
 * Whether a piece of a blueprint being pasted meets something in its way on
 * the viewed level, the level the anchor of the blueprint goes to: a level it
 * may not be built on there, or something the paste neither joins nor replaces
 * (the same rules as CmdPasteUndergroundBlueprint).
 */
static bool IsBlueprintGhostBlocked(const BlueprintItem &item, TileIndex tile, int16_t depth)
{
	int z1 = depth + item.z;
	int z2 = item.kind == BlueprintItemKind::Ramp ? depth + item.z2 : z1;
	for (int z = std::min(z1, z2); z <= std::max(z1, z2); z++) {
		bool hillside = item.kind == BlueprintItemKind::Portal && GetInclinedSlopeDirection(GetTileSlope(tile)) != INVALID_DIAGDIR && z == GetMetroSurfaceZ(tile);
		if (!hillside && !IsValidMetroZ(tile, static_cast<int16_t>(z))) return true;
		SliceID sid = _multilayer_map.FindSliceAt(tile, static_cast<int16_t>(z));
		if (sid == INVALID_SLICE_ID) {
			/* A house foundation or water reaching down to this level. */
			if (CheckUndergroundClearance(tile, static_cast<int16_t>(z)).Failed()) return true;
			continue;
		}
		const TileSlice &s = _multilayer_map.GetSlice(sid);
		bool own = s.owner == _local_company.base();
		/* New construction where something is already, like more track or a platform on track. */
		bool cleared = CheckUndergroundClearance(tile, static_cast<int16_t>(z)).Succeeded();
		switch (item.kind) {
			case BlueprintItemKind::Track: {
				/* More track joins track of its type; pieces already there are left alone. */
				TrackBits have = TRACK_BIT_NONE;
				if (s.kind == SliceKind::Track) {
					if (own && s.track.railtype == item.railtype) {
						if ((item.tracks & ~static_cast<TrackBits>(s.track.tracks)) != 0 && !cleared) return true;
						continue;
					}
					have = static_cast<TrackBits>(s.track.tracks);
				} else if (s.kind == SliceKind::StationTile) {
					have = static_cast<TrackBits>(s.station.tracks);
				} else if (s.kind == SliceKind::Ramp) {
					have = AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(s.ramp.direction)));
				} else if (s.kind == SliceKind::Portal) {
					have = AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(s.portal.direction)));
				}
				if ((item.tracks & ~have) == 0) continue;
				return true;
			}
			case BlueprintItemKind::Platform:
				/* A platform replaces own plain track, and an identical one is there already. */
				if (own && s.kind == SliceKind::Track) {
					if (!cleared) return true;
					continue;
				}
				if (own && s.kind == SliceKind::StationTile && s.station.tracks == AxisToTrackBits(static_cast<Axis>(item.dir))) continue;
				return true;
			case BlueprintItemKind::Ramp:
				if (own && s.kind == SliceKind::Ramp && s.ramp.direction == item.dir && s.ramp.entry_depth == z1 && s.ramp.exit_depth == z2) continue;
				return true;
			case BlueprintItemKind::Portal:
				if (own && s.kind == SliceKind::Portal && DiagDirToAxis(static_cast<DiagDirection>(s.portal.direction)) == DiagDirToAxis(static_cast<DiagDirection>(item.dir))) continue;
				return true;
			default:
				return true;
		}
	}
	return false;
}

/**
 * Ghost of a blueprint being pasted: its pieces on the viewed level, pale
 * where they fit and red where something is in the way.
 */
static void DrawBlueprintGhost(const TileInfo *ti, int16_t depth, bool slice_mode)
{
	for (const BlueprintItem &item : GetBlueprintPastePreview(ti->tile)) {
		if (item.IsSurface()) continue;
		if (item.z != 0 && !(item.kind == BlueprintItemKind::Ramp && item.z2 == 0)) continue;

		bool blocked = IsBlueprintGhostBlocked(item, ti->tile, depth);
		PaletteID pal = blocked ? PALETTE_SEL_TILE_RED : PALETTE_TO_TRANSPARENT;
		LevelDrawContext ctx(ti, depth, slice_mode, slice_mode ? 7 : 0);
		if (blocked) ctx.Flat(SPR_SELECT_TILE, PALETTE_SEL_TILE_RED);
		const RailTypeInfo *rti = GetRailTypeInfoSafe(item.railtype);
		switch (item.kind) {
			case BlueprintItemKind::Track:
				DrawTrackPieces(ctx, static_cast<TrackBits>(item.tracks), rti, pal);
				break;
			case BlueprintItemKind::Platform: {
				bool is_x = static_cast<Axis>(item.dir) == AXIS_X;
				ctx.Flat(is_x ? SPR_METRO_PLATFORM_FLOOR_X : SPR_METRO_PLATFORM_FLOOR_Y, pal);
				DrawTrackPieces(ctx, is_x ? TRACK_BIT_X : TRACK_BIT_Y, rti, pal);
				break;
			}
			case BlueprintItemKind::Ramp: {
				DiagDirection dir = static_cast<DiagDirection>(item.dir);
				DiagDirection descent = item.z2 < item.z ? dir : ReverseDiagDir(dir);
				ctx.Flat(SPR_METRO_RAMP_BASE + descent, pal);
				break;
			}
			case BlueprintItemKind::Portal:
				ctx.Flat(rti->base_sprites.single_x + (DiagDirToAxis(static_cast<DiagDirection>(item.dir)) == AXIS_X ? 0 : 1), pal);
				break;
			default:
				break;
		}
	}
}

/**
 * X-ray view: the surface tile is drawn by the caller (pale, or hidden); the
 * viewed level is drawn at its true depth through it.
 */
static void DrawUndergroundTileContent(const TileInfo *ti)
{
	int16_t depth = GetUndergroundViewDepth();

	if (_underground_show_above) {
		/* Dim the (already drawn) surface with a checkerboard "ceiling". */
		DrawGroundSpriteAt(SPR_METRO_CEILING, PAL_NONE, 0, 0, 7);
		/* The surface arch of a portal still belongs to the surface. */
		if (HasPortalOnTile(ti->tile)) DrawPortalOverlay(ti);
	} else {
		/* World above hidden: draw plain grey terrain as the backdrop. */
		DrawGroundSprite(SPR_FLAT_BARE_LAND + SlopeToSpriteOffset(ti->tileh), GetDepthPalette(ti->tile, depth));
	}

	if (!_multilayer_map.GetColumn(ti->tile).HasUnderground()) return;
	DrawUndergroundGhosts(ti, depth, false);

	SliceID sid = _multilayer_map.FindSliceAt(ti->tile, depth);
	if (sid == INVALID_SLICE_ID) return;
	DrawUndergroundLevelContent(ti, depth, sid, false);
}

/**
 * Slice view: the column is cut by the plane at the viewed level. ti->z is the
 * plane's height and ti->tileh is flat. Draw the rock slab and, on top of it,
 * the viewed level's content; nothing above the plane is drawn.
 */
static void DrawUndergroundSliceTileContent(const TileInfo *ti)
{
	int16_t depth = GetUndergroundViewDepth();

	/* The cut rock at the plane: the soil, or concrete under what is built on this level. */
	SliceID sid = _multilayer_map.FindSliceAt(ti->tile, depth);
	DrawUndergroundFloor(ti, depth, sid != INVALID_SLICE_ID && _multilayer_map.GetSlice(sid).kind != SliceKind::Surface);

	/* Column of a station exit (pavilion or underpass): mark it on every level so the
	 * player sees where the exits are relative to the platforms. */
	if (FindExplicitStationExitAt(ti->tile) != nullptr) {
		DrawGroundSpriteAt(SPR_METRO_EXIT_MARKER, PAL_NONE, TILE_SIZE / 2, TILE_SIZE / 2, 0);
	}

	if (!_multilayer_map.GetColumn(ti->tile).HasUnderground()) return;
	DrawUndergroundGhosts(ti, depth, true);

	/* The views of the levels below or above: those levels in full at their own
	 * depth, like the X-ray view draws its level, seen through the plane. */
	for (SliceID other_sid : _multilayer_map.GetColumn(ti->tile).slices) {
		const TileSlice &other = _multilayer_map.GetSlice(other_sid);
		if (other.kind == SliceKind::Surface || other.kind == SliceKind::End) continue;
		if (other.span.Contains(depth) || !IsUndergroundOtherLevelDrawn(other.span.z_bot)) continue;
		DrawUndergroundLevelContent(ti, other.span.z_bot, other_sid, false);
	}

	if (sid == INVALID_SLICE_ID) return;
	DrawUndergroundLevelContent(ti, depth, sid, true);
}

/**
 * Ghost of the ramp a ramp tool would build on the viewed level, drawn like a
 * built ramp with its arrow: pale where it can be built, red where it cannot.
 */
static void DrawRampGhost(const TileInfo *ti, int16_t depth, bool slice_mode)
{
	const UndergroundRampGhost *ghost = GetUndergroundRampGhost(ti->tile);
	if (ghost == nullptr) return;

	PaletteID pal = ghost->buildable ? PALETTE_TO_TRANSPARENT : PALETTE_SEL_TILE_RED;
	/* Placed like a built ramp: one going down from the viewed level is based a level lower. */
	bool descends = ghost->exit_depth < ghost->entry_depth;
	LevelDrawContext ctx(ti, depth, slice_mode, descends ? -static_cast<int>(TILE_HEIGHT) : 0);
	if (!ghost->buildable) LevelDrawContext(ti, depth, slice_mode).Flat(SPR_SELECT_TILE, PALETTE_SEL_TILE_RED);
	DiagDirection descent = descends ? ghost->dir : ReverseDiagDir(ghost->dir);
	ctx.Flat(SPR_METRO_RAMP_BASE + descent, pal);
	ctx.Flat(GetInclinedRailSprite(descent, ghost->railtype), pal);
	ctx.Flat(descends ? SPR_METRO_RAMP_ARROW_DOWN : SPR_METRO_RAMP_ARROW_UP, pal, 8, 4, 7);

	/* Which way it leads, with the one-way road arrow lying on the incline. */
	static const uint8_t oneway_arrow[DIAGDIR_END] = {1, 4, 0, 3};
	bool raised_north = (descent == DIAGDIR_SE || descent == DIAGDIR_SW);
	SpriteID arrow = SPR_ONEWAY_BASE + oneway_arrow[ghost->dir] + (raised_north ? ONEWAY_SLOPE_N_OFFSET : ONEWAY_SLOPE_S_OFFSET);
	ctx.Flat(arrow, ghost->buildable ? PAL_NONE : PALETTE_SEL_TILE_RED, 8, 8, TILE_HEIGHT / 2);
}

void DrawUndergroundTile(const TileInfo *ti)
{
	DrawUndergroundTileContent(ti);
	DrawBlueprintGhost(ti, GetUndergroundViewDepth(), false);
	DrawRampGhost(ti, GetUndergroundViewDepth(), false);
}

void DrawUndergroundSliceTile(const TileInfo *ti)
{
	DrawUndergroundSliceTileContent(ti);
	DrawBlueprintGhost(ti, GetUndergroundViewDepth(), true);
	DrawRampGhost(ti, GetUndergroundViewDepth(), true);
}
