/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_view.h Underground view mode state and helpers. */

#ifndef UNDERGROUND_VIEW_H
#define UNDERGROUND_VIEW_H

#include "../openttd.h"
#include "../tile_type.h"
#include "../transparency.h"

struct Vehicle;
struct Train;
struct TileSlice;

/**
 * How the underground is shown in the viewports.
 *
 * Slice is the construction mode: the world is cut by a horizontal plane at
 * the viewed absolute level. Every tile whose surface lies above that plane is
 * drawn as a flat slab at the plane's height (rock, plus whatever underground
 * content exists on that level); nothing above the plane is drawn. Tiles whose
 * surface lies at or below the plane show the ordinary landscape.
 *
 * XRay keeps the ordinary landscape (made transparent) and overlays the viewed
 * level on top of it, so tunnels can be followed under the terrain.
 *
 * Below and Above are the slice view without any surface: every tile is cut at
 * the viewed level, and the levels below (or above) it are drawn in full at
 * their own depth, so the player sees the network there while building at the
 * viewed level.
 */
enum class UndergroundViewMode : uint8_t {
	Off,   ///< Ordinary surface view.
	Slice, ///< Cut-plane view at the viewed level (construction mode).
	XRay,  ///< Surface view with the viewed level overlaid.
	Below, ///< Cut-plane view with the levels below the viewed one, no surface.
	Above, ///< Cut-plane view with the levels above the viewed one, no surface.
};

/** The current underground view mode. */
extern UndergroundViewMode _underground_view_mode;

/** The current underground view depth as an absolute z level (TileHeight units; may be negative). */
extern int16_t _underground_view_depth;

/** Show ghosts of the levels above the viewed depth (and, in X-ray mode, the surface). */
extern bool _underground_show_above;

/** Show ghosts of the levels below the viewed depth. */
extern bool _underground_show_below;

/** Check if any underground view mode is active. */
inline bool IsUndergroundViewActive()
{
	return _underground_view_mode != UndergroundViewMode::Off;
}

/** Check if a cut-plane view is active: the slice view, or the views of the levels below or above it. */
inline bool IsUndergroundSliceMode()
{
	return _underground_view_mode == UndergroundViewMode::Slice || _underground_view_mode == UndergroundViewMode::Below || _underground_view_mode == UndergroundViewMode::Above;
}

/** Check if the X-ray overlay view is active. */
inline bool IsUndergroundXRayMode()
{
	return _underground_view_mode == UndergroundViewMode::XRay;
}

/** Check if the view of the levels below the viewed one is active. */
inline bool IsUndergroundBelowMode()
{
	return _underground_view_mode == UndergroundViewMode::Below;
}

/** Check if the view of the levels above the viewed one is active. */
inline bool IsUndergroundAboveMode()
{
	return _underground_view_mode == UndergroundViewMode::Above;
}

/** Switch the underground view mode (handles the cutaway transparency and viewport refresh). */
void SetUndergroundViewMode(UndergroundViewMode mode);

/**
 * Whether the levels below or above the viewed one are drawn in full at their
 * own depth (besides the viewed level, drawn as the cut plane).
 * @param depth A level other than the viewed one.
 */
bool IsUndergroundOtherLevelDrawn(int16_t depth);

/** Lowest height (world pixels) anything of the underground is drawn at in the current view. */
int GetUndergroundViewLowestPixelZ();

/** Toggle between the ordinary view and the slice view. */
void ToggleUndergroundView();

/** Toggle between the slice view and the X-ray view (enters the slice view when the underground view is off). */
void ToggleUndergroundXRay();

/** The transparency options as the player set them, without the ones the X-ray view adds (saved to the config). */
TransparencyOptionBits GetPlayerTransparencyOptions();

/** Set the underground view depth (refreshes vehicle viewport positions). */
void SetUndergroundViewDepth(int16_t depth);

/** Get the current underground view depth. */
inline int16_t GetUndergroundViewDepth()
{
	return _underground_view_depth;
}

/** Height (in world pixel units) of the cut plane of the slice view. */
inline int GetUndergroundSlicePixelZ()
{
	return _underground_view_depth * static_cast<int>(TILE_HEIGHT);
}

/**
 * Check whether the slice view cuts through a tile, i.e. the tile's surface
 * lies above the viewed level and the tile is drawn as a flat slab at the plane.
 * Uses the same north-corner rule as IsValidMetroZ.
 */
bool IsTileCutBySlice(TileIndex tile);

/**
 * Height (world pixels) at which the viewport draws the ground of a tile
 * column: the cut plane for tiles cut by the slice view, the terrain otherwise.
 * @param x Tile x, may be outside the map.
 * @param y Tile y, may be outside the map.
 */
int GetViewTilePixelHeight(int x, int y);

/**
 * World z at a map position as the viewport shows it: the cut plane for
 * positions on tiles cut by the slice view, the terrain slope otherwise.
 * Mirrors GetSlopePixelZ(x, y).
 */
int GetViewSlopePixelZ(int x, int y);

/** Lowest pixel z of a tile as the viewport shows it (GetTilePixelZ, or the cut plane). */
int GetViewTilePixelZ(TileIndex tile);

/** Highest pixel z of a tile as the viewport shows it (GetTileMaxPixelZ, or the cut plane). */
int GetViewTileMaxPixelZ(TileIndex tile);

/**
 * The z coordinate a vehicle is drawn at. Underground trains sit at their
 * level's plane in the slice view; everything else uses its real z_pos.
 */
int GetVehicleViewportZ(const Vehicle *v);
bool IsUndergroundTrainDepthShown(int16_t depth);

/**
 * The portal slice of a tile when the slice view shows the portal's own level
 * (such a tile is always cut and drawn by DrawUndergroundSliceTile), else nullptr.
 */
const TileSlice *GetSliceViewPortal(TileIndex tile);

/** Is the portal a hillside mouth: at the foot of an incline rising towards its direction? */
bool IsHillsidePortal(TileIndex tile, const TileSlice &portal);
const TileSlice *GetOpenCutPortal(const Train *t);
int GetOpenCutDropLevels(TileIndex tile, const TileSlice &portal);
int GetOpenCutFloorZ(const TileSlice &portal, TileIndex tile, int x, int y);

/**
 * Height above the cut plane of the slice-view rendering of a portal tile at the
 * portal's level: 0 for a hillside mouth (drawn flat), for a dive portal a ramp
 * rising one level from the tunnel side to the open side.
 * @param x World x within the map (only the position inside the tile matters).
 * @param y World y within the map.
 */
int GetSliceViewPortalRise(const TileSlice &portal, TileIndex tile, int x, int y);

#endif /* UNDERGROUND_VIEW_H */
