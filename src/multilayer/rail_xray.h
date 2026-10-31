/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_xray.h Rail X-ray view: the railway network alone, every underground level at its own depth. */

#ifndef RAIL_XRAY_H
#define RAIL_XRAY_H

#include "../gfx_type.h"
#include "../tile_type.h"

struct Train;

/**
 * Whether the rail X-ray view is on. It replaces the drawing of the viewports
 * by a schematic of the railway network: track, stations, depots, signals and
 * trains, of the surface and of every underground level at once, each at its
 * true height, on a plain background. Nothing else is drawn. Everything stays
 * where the ordinary view has it, so trains and tiles can be clicked as usual.
 */
extern bool _rail_xray_view;

/** Check if the rail X-ray view is on. */
inline bool IsRailXRayView()
{
	return _rail_xray_view;
}

/** Which trains show an arrow to where their current order sends them. */
enum class RailXRayDestinations : uint8_t {
	Off,      ///< None.
	Selected, ///< The trains with their view, orders or timetable window open.
	All,      ///< Every train.
};

/** Which trains show an arrow to their destination in the rail X-ray view. */
extern RailXRayDestinations _rail_xray_destinations;

/** Switch the rail X-ray view on or off. */
void SetRailXRayView(bool on);

/** Toggle the rail X-ray view. */
inline void ToggleRailXRayView()
{
	SetRailXRayView(!_rail_xray_view);
}

struct TileSlice;

/**
 * Height (world pixels) of a point on a portal tile in the rail X-ray view:
 * one straight slope from the ground at the open side to the level of the
 * tunnel at the tunnel side, for the track and for the trains on it.
 * @param tile The portal tile.
 * @param portal The portal.
 * @param x World x of the point.
 * @param y World y of the point.
 */
int GetRailXRayPortalPixelZ(TileIndex tile, const TileSlice &portal, int x, int y);

/** Lowest height (world pixels) anything is drawn at in the rail X-ray view. */
int GetRailXRayLowestPixelZ();

/** Whether a train part is drawn (and can be clicked) in the rail X-ray view. */
bool IsRailXRayTrainPartDrawn(const Train *t);

/**
 * Draw the rail X-ray view of a part of a viewport.
 * @param vd_dpi The area being drawn, in viewport (zoomed) coordinates; _cur_dpi points to it.
 */
void ViewportDrawRailXRay(const DrawPixelInfo &vd_dpi);

/**
 * The tile under a viewport position in the rail X-ray view: the railway
 * nearest to it among the surface and the underground levels drawn there.
 * @param vx Viewport x (zoomed coordinates of the virtual viewport).
 * @param vy Viewport y.
 * @param[out] pt The world position picked.
 * @return False when there is no railway there (the caller picks the terrain as usual).
 */
bool RailXRayPickPoint(int vx, int vy, Point &pt);

#endif /* RAIL_XRAY_H */
