/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_view.cpp Underground view mode state, cut-plane helpers and cutaway transparency. */

#include "../stdafx.h"
#include "underground_view.h"
#include "underground_cmd.h"
#include "../transparency.h"
#include "../tile_map.h"
#include "../landscape.h"
#include "../train.h"
#include "../gfx_func.h"
#include "../slope_func.h"
#include "../window_func.h"
#include "multilayer_map.h"

#include "../safeguards.h"

/** Current underground view mode. */
UndergroundViewMode _underground_view_mode = UndergroundViewMode::Off;

/** Current underground view depth (default: -1 = first level below sea level). */
int16_t _underground_view_depth = -1;

/** Show ghosts of the levels above the viewed depth. */
bool _underground_show_above = false;

/** Show ghosts of the levels below the viewed depth. */
bool _underground_show_below = true;

/** Transparency options the X-ray view turned on, that were off before. */
static TransparencyOptionBits _xray_transparency_added = 0;

/**
 * Vehicles cache their viewport rectangle from the z they are drawn at, and
 * that z depends on the view mode and depth. Recompute all of them.
 */
static void RefreshVehicleViewportPositions()
{
	for (Vehicle *v : Vehicle::Iterate()) {
		v->UpdateViewport(false);
	}
}

/** Enable the cutaway transparency used by the X-ray view. */
static void EnterXRayTransparency()
{
	if (_xray_transparency_added != 0) return;
	TransparencyOptionBits xray = 0;
	for (TransparencyOption to : {TO_TREES, TO_HOUSES, TO_INDUSTRIES, TO_BUILDINGS, TO_STRUCTURES, TO_BRIDGES}) SetBit(xray, to);
	_xray_transparency_added = xray & ~_transparency_opt;
	_transparency_opt |= xray;
}

/**
 * Undo the transparency of the X-ray view: only the options it turned on, so
 * changes the player made meanwhile stay.
 */
static void LeaveXRayTransparency()
{
	_transparency_opt &= ~_xray_transparency_added;
	_xray_transparency_added = 0;
}

TransparencyOptionBits GetPlayerTransparencyOptions()
{
	return _transparency_opt & ~_xray_transparency_added;
}

void SetUndergroundViewMode(UndergroundViewMode mode)
{
	if (mode == _underground_view_mode) return;
	_underground_view_mode = mode;

	/* Keep the legacy display-option bit in sync for code that only checks it. */
	extern uint8_t _display_opt;
	AssignBit(_display_opt, DO_UNDERGROUND_VIEW, mode != UndergroundViewMode::Off);

	if (mode != UndergroundViewMode::Off) {
		_underground_view_depth = Clamp<int16_t>(_underground_view_depth, GetMetroGlobalMinZ(), GetMetroGlobalMaxZ());
	}

	/* Per-mode defaults for the "above" toggle: the X-ray view is about seeing the
	 * level through the surface, the slice view cuts everything above away. */
	if (mode == UndergroundViewMode::XRay) {
		_underground_show_above = true;
		EnterXRayTransparency();
	} else {
		if (IsUndergroundSliceMode()) _underground_show_above = false;
		LeaveXRayTransparency();
	}

	RefreshVehicleViewportPositions();
	MarkWholeScreenDirty();

	/* The railway construction toolbar builds on the surface or the viewed level. */
	InvalidateWindowData(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);
}

void ToggleUndergroundView()
{
	SetUndergroundViewMode(IsUndergroundViewActive() ? UndergroundViewMode::Off : UndergroundViewMode::Slice);
}

void ToggleUndergroundXRay()
{
	SetUndergroundViewMode(IsUndergroundXRayMode() ? UndergroundViewMode::Slice : UndergroundViewMode::XRay);
}

void SetUndergroundViewDepth(int16_t depth)
{
	if (depth == _underground_view_depth) return;
	_underground_view_depth = depth;
	if (IsUndergroundViewActive()) {
		RefreshVehicleViewportPositions();
		MarkWholeScreenDirty();
	}
}

const TileSlice *GetSliceViewPortal(TileIndex tile)
{
	if (!IsUndergroundSliceMode() || !IsValidTile(tile) || !_multilayer_map.IsAllocated()) return nullptr;
	if (!_multilayer_map.GetPortalColumns().contains(tile.base())) return nullptr;
	SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return nullptr;
	const TileSlice &ps = _multilayer_map.GetSlice(sid);
	return ps.span.z_bot == _underground_view_depth ? &ps : nullptr;
}

bool IsHillsidePortal(TileIndex tile, const TileSlice &portal)
{
	return GetInclinedSlopeDirection(GetTileSlope(tile)) != INVALID_DIAGDIR && portal.span.z_bot == GetTileZ(tile);
}

/**
 * The portal a train part is crossing when it is in the open cut of a portal
 * diving into flat ground (on the surface layer of the portal tile, or on the
 * portal's column below it).
 * @param t The train part.
 * @return The portal, or nullptr.
 */
const TileSlice *GetOpenCutPortal(const Train *t)
{
	SliceID sid = _multilayer_map.FindSliceByKind(t->tile, SliceKind::Portal);
	if (sid == INVALID_SLICE_ID) return nullptr;
	if (t->IsUnderground() && t->underground_slice != sid) return nullptr;
	const TileSlice &ps = _multilayer_map.GetSlice(sid);
	return IsHillsidePortal(t->tile, ps) ? nullptr : &ps;
}

/**
 * Depth of a portal's open cut: from the flat ground down to the portal's
 * level, so the train comes out of the cut at the height of the tunnel. The
 * sprites exist for one and two levels.
 * @param tile The portal tile.
 * @param portal The portal.
 * @return Number of levels, 1 or 2.
 */
int GetOpenCutDropLevels(TileIndex tile, const TileSlice &portal)
{
	return Clamp(GetTileZ(tile) - portal.span.z_bot, 1, 2);
}

/**
 * Height of the floor of a portal's open cut below the surface: it descends
 * from the open edge to the tunnel mouth (see the open cut sprites).
 * @param portal The portal.
 * @param tile The portal tile.
 * @param x World x of the point.
 * @param y World y of the point.
 * @return Height relative to the surface, -drop * TILE_HEIGHT .. 0.
 */
int GetOpenCutFloorZ(const TileSlice &portal, TileIndex tile, int x, int y)
{
	int drop = GetOpenCutDropLevels(tile, portal) * static_cast<int>(TILE_HEIGHT);
	int lx = x & (TILE_SIZE - 1);
	int ly = y & (TILE_SIZE - 1);
	int u;
	switch (static_cast<DiagDirection>(portal.portal.direction)) {
		case DIAGDIR_NE: u = lx; break;
		case DIAGDIR_SW: u = TILE_SIZE - 1 - lx; break;
		case DIAGDIR_NW: u = ly; break;
		default:         u = TILE_SIZE - 1 - ly; break;
	}
	return -drop + u * drop / static_cast<int>(TILE_SIZE - 1);
}

int GetSliceViewPortalRise(const TileSlice &portal, TileIndex tile, int x, int y)
{
	if (IsHillsidePortal(tile, portal)) return 0;
	/* Distance from the tile edge on the tunnel side, 0..TILE_SIZE. */
	int lx = x & (TILE_SIZE - 1);
	int ly = y & (TILE_SIZE - 1);
	int u;
	switch (static_cast<DiagDirection>(portal.portal.direction)) {
		case DIAGDIR_NE: u = lx; break;
		case DIAGDIR_SW: u = TILE_SIZE - lx; break;
		case DIAGDIR_NW: u = ly; break;
		default:         u = TILE_SIZE - ly; break;
	}
	return u * static_cast<int>(TILE_HEIGHT) / static_cast<int>(TILE_SIZE);
}

bool IsTileCutBySlice(TileIndex tile)
{
	if (!IsUndergroundSliceMode()) return false;
	if (!IsValidTile(tile)) return false;
	/* The views of the levels below or above show no surface at all. */
	if (IsUndergroundBelowMode() || IsUndergroundAboveMode()) return true;
	/* A portal tile at the viewed level is drawn as the exit of that level, whatever its terrain. */
	if (GetSliceViewPortal(tile) != nullptr) return true;
	return GetTileZ(tile) > _underground_view_depth;
}

int GetViewTilePixelHeight(int x, int y)
{
	if (IsUndergroundSliceMode() && IsInsideBS(x, 0, Map::SizeX()) && IsInsideBS(y, 0, Map::SizeY())) {
		TileIndex tile = TileXY(x, y);
		if (IsTileCutBySlice(tile)) return GetUndergroundSlicePixelZ();
	}
	return static_cast<int>(TilePixelHeightOutsideMap(x, y));
}

int GetViewSlopePixelZ(int x, int y)
{
	if (IsUndergroundSliceMode()) {
		TileIndex tile = TileVirtXY(x, y);
		if (const TileSlice *portal = GetSliceViewPortal(tile); portal != nullptr) {
			return GetUndergroundSlicePixelZ() + GetSliceViewPortalRise(*portal, tile, x, y);
		}
		if (IsTileCutBySlice(tile)) return GetUndergroundSlicePixelZ();
	}
	return GetSlopePixelZ(x, y);
}

int GetViewTilePixelZ(TileIndex tile)
{
	if (IsTileCutBySlice(tile)) return GetUndergroundSlicePixelZ();
	return GetTilePixelZ(tile);
}

int GetViewTileMaxPixelZ(TileIndex tile)
{
	if (const TileSlice *portal = GetSliceViewPortal(tile); portal != nullptr && !IsHillsidePortal(tile, *portal)) {
		return GetUndergroundSlicePixelZ() + static_cast<int>(TILE_HEIGHT);
	}
	if (IsTileCutBySlice(tile)) return GetUndergroundSlicePixelZ();
	return GetTileMaxPixelZ(tile);
}

bool IsUndergroundOtherLevelDrawn(int16_t depth)
{
	if (IsUndergroundBelowMode()) return depth < GetUndergroundViewDepth();
	if (IsUndergroundAboveMode()) return depth > GetUndergroundViewDepth();
	return false;
}

int GetUndergroundViewLowestPixelZ()
{
	if (IsUndergroundXRayMode() || IsUndergroundBelowMode()) return GetMetroGlobalMinZ() * static_cast<int>(TILE_HEIGHT);
	return GetUndergroundSlicePixelZ();
}

/**
 * Whether underground train parts on \a depth are drawn: the slice view shows
 * the viewed level only, the X-ray view every underground train, so the
 * whole traffic below the surface can be followed at once, the views of the
 * levels below or above also the trains on those levels.
 * @param depth Level of the train part.
 * @return True if the part is drawn.
 */
bool IsUndergroundTrainDepthShown(int16_t depth)
{
	if (!IsUndergroundViewActive()) return false;
	return IsUndergroundXRayMode() || depth == GetUndergroundViewDepth() || IsUndergroundOtherLevelDrawn(depth);
}

int GetVehicleViewportZ(const Vehicle *v)
{
	if (v->type == VEH_TRAIN && !IsUndergroundSliceMode()) {
		/* In the open cut of a portal diving into flat ground the train runs
		 * down (or up) its floor, in the surface and in the X-ray view. */
		if (const TileSlice *portal = GetOpenCutPortal(Train::From(v)); portal != nullptr) {
			return GetTileZ(v->tile) * static_cast<int>(TILE_HEIGHT) + GetOpenCutFloorZ(*portal, v->tile, v->x_pos, v->y_pos);
		}
	}
	if (IsUndergroundViewActive() && v->type == VEH_TRAIN) {
		const Train *t = Train::From(v);
		if (t->IsUnderground()) return t->underground_depth * static_cast<int>(TILE_HEIGHT);
		/* On a portal tile the slice view shows at the portal's level, the train
		 * runs on the drawn exit (flat mouth or ramp), not on the surface rail. */
		if (IsUndergroundSliceMode()) {
			if (const TileSlice *portal = GetSliceViewPortal(v->tile); portal != nullptr) {
				return GetUndergroundSlicePixelZ() + GetSliceViewPortalRise(*portal, v->tile, v->x_pos, v->y_pos);
			}
		}
	}
	return v->z_pos;
}
