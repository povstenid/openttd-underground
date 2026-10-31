/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file rail_xray.cpp Rail X-ray view: the railway network alone, every underground level at its own depth.
 *
 * The view is drawn as plain lines and polygons instead of sprites, at the
 * true world position of everything, so it lines up with the ordinary view:
 * the vehicle bounding boxes used for clicking stay valid, and the tile under
 * the cursor is picked among the drawn levels (RailXRayPickPoint).
 *
 * Track has the colour of its rail type, darker on deeper levels; a reserved
 * path is drawn over it in orange. Signals are squares beside their track,
 * filled with their state and framed with the colour of their type. Trains
 * are thick bars per vehicle, coloured by what the train does: white moving,
 * blue loading, red standing, pink when it cannot find a path, grey stopped.
 *
 * Reservations and the state of underground signals change without marking
 * their tiles dirty when the ordinary view does not show them, so the view
 * is repainted a few times a second while it is on.
 */

#include "../stdafx.h"
#include "rail_xray.h"
#include "multilayer_map.h"
#include "station_complex.h"
#include "underground_cmd.h"
#include "underground_signal.h"
#include "underground_view.h"
#include "../bridge_map.h"
#include "../core/backup_type.hpp"
#include "../core/math_func.hpp"
#include "../gfx_func.h"
#include "../landscape.h"
#include "../palette_func.h"
#include "../pbs.h"
#include "../rail_map.h"
#include "../road_map.h"
#include "../settings_type.h"
#include "../station_map.h"
#include "../depot_map.h"
#include "../strings_func.h"
#include "../tile_cmd.h"
#include "../tilehighlight_func.h"
#include "../timer/timer.h"
#include "../timer/timer_window.h"
#include "../train.h"
#include "../tunnel_map.h"
#include "../tunnelbridge_map.h"
#include "../viewport_func.h"
#include "../window_func.h"
#include "../zoom_func.h"

#include "table/strings.h"

#include "../safeguards.h"

bool _rail_xray_view = false;
RailXRayDestinations _rail_xray_destinations = RailXRayDestinations::Selected;

/** Background of the view. */
static constexpr PixelColour XRAY_BACKGROUND = GREY_SCALE(3);

/** Rail tunnels by their two ends, the lower tile index first: nothing on the tiles between the ends tells about them. */
static std::vector<std::pair<TileIndex, TileIndex>> _xray_tunnels;
/** Whether #_xray_tunnels is up to date. */
static bool _xray_tunnels_valid = false;

/** Level of the last underground pick, or INT16_MIN for the surface; the tile selection is drawn on it. */
static int16_t _xray_pick_z = INT16_MIN;

/** Middle of each tile edge, in world units within the tile, by DiagDirection. */
static const Point _xray_edge_mid[DIAGDIR_END] = {{0, 8}, {8, 16}, {16, 8}, {8, 0}};

/** The two tile edges each track piece joins. */
static const DiagDirection _xray_track_ends[TRACK_END][2] = {
	{DIAGDIR_NE, DIAGDIR_SW}, // TRACK_X
	{DIAGDIR_NW, DIAGDIR_SE}, // TRACK_Y
	{DIAGDIR_NE, DIAGDIR_NW}, // TRACK_UPPER
	{DIAGDIR_SW, DIAGDIR_SE}, // TRACK_LOWER
	{DIAGDIR_NW, DIAGDIR_SW}, // TRACK_LEFT
	{DIAGDIR_NE, DIAGDIR_SE}, // TRACK_RIGHT
};

/** World x and y step of each Direction. */
static const int8_t _xray_dir_dx[DIR_END] = {-1, -1, -1, 0, 1, 1, 1, 0};
static const int8_t _xray_dir_dy[DIR_END] = {-1, 0, 1, 1, 1, 0, -1, -1};

/** The passes over the tiles: what lies below is drawn first. */
enum class XRayStage : uint8_t {
	Base,    ///< Platforms, depots and track.
	Overlay, ///< Reservations and signals.
};

/** How a level of the network is drawn. */
struct XRayLevel {
	bool underground;  ///< Below the surface: the track has a dark casing.
	ColourShade shade; ///< Shade of the track colours.
	bool dimmed;       ///< Not the level the underground view looks at.
};

/** The surface level; dimmed while the underground view picks a level to look at. */
static XRayLevel GetXRaySurfaceLevel()
{
	if (IsUndergroundViewActive()) return {false, SHADE_DARK, true};
	return {false, SHADE_LIGHTER, false};
}

/**
 * An underground level: darker the deeper it lies, so the levels can be told
 * apart where they cross on the screen, but the level the underground view
 * looks at is bright and the others dimmed.
 * @param z The level.
 */
static XRayLevel GetXRayUndergroundLevel(int16_t z)
{
	if (IsUndergroundViewActive()) {
		if (z == GetUndergroundViewDepth()) return {true, SHADE_LIGHTER, false};
		return {true, SHADE_DARKER, true};
	}
	int below = Clamp(-1 - z, 0, 2);
	return {true, static_cast<ColourShade>(SHADE_LIGHT - below), false};
}

/**
 * Colour of the track of a rail type. Red, green, yellow and orange are left
 * to signals, reservations and trains.
 * @param rt The rail type.
 */
static Colours GetXRayRailTypeColour(RailType rt)
{
	static const Colours colours[] = {
		COLOUR_GREY, COLOUR_LIGHT_BLUE, COLOUR_PURPLE, COLOUR_CREAM, COLOUR_MAUVE,
		COLOUR_BLUE, COLOUR_BROWN, COLOUR_PINK, COLOUR_PALE_GREEN, COLOUR_DARK_BLUE,
	};
	if (rt >= RAILTYPE_END) return COLOUR_GREY;
	return colours[to_underlying(rt) % std::size(colours)];
}

/** Drawing helpers for one area of a viewport. */
struct XRayDrawer {
	ZoomLevel zoom; ///< Zoom level of the viewport.
	Rect clip;      ///< Area being drawn, in unzoomed screen coordinates.

	/** Screen position of a world position. */
	Point Screen(int x, int y, int z) const
	{
		Point p = RemapCoords(x, y, z);
		return {UnScaleByZoomLower(p.x, this->zoom), UnScaleByZoomLower(p.y, this->zoom)};
	}

	/** A size in screen pixels given for the normal zoom level, at least one pixel. */
	int Px(int normal) const
	{
		return std::max(1, UnScaleByZoomLower(normal * ZOOM_BASE, this->zoom));
	}

	/** Whether the box of two points, grown by a margin, overlaps the area being drawn. */
	bool Visible(Point a, Point b, int margin) const
	{
		return std::max(a.x, b.x) + margin >= this->clip.left && std::min(a.x, b.x) - margin <= this->clip.right &&
				std::max(a.y, b.y) + margin >= this->clip.top && std::min(a.y, b.y) - margin <= this->clip.bottom;
	}

	void Line(Point a, Point b, PixelColour colour, int width, int dash = 0) const
	{
		if (!this->Visible(a, b, width)) return;
		GfxDrawLine(a.x, a.y, b.x, b.y, colour, width, dash);
	}

	/** A name on a dark plate, centred above a point. */
	void Label(Point p, std::string_view text, TextColour colour) const
	{
		Dimension dim = GetStringBoundingBox(text, FS_SMALL);
		int half = static_cast<int>(dim.width) / 2 + 2;
		Rect r{p.x - half, p.y - static_cast<int>(dim.height) - 2, p.x + half, p.y};
		if (r.right < this->clip.left || r.left > this->clip.right || r.bottom < this->clip.top || r.top > this->clip.bottom) return;
		GfxFillRect(r.left, r.top, r.right, r.bottom, PC_BLACK);
		DrawString(r.left, r.right, r.top + 1, text, colour, SA_HOR_CENTER, false, FS_SMALL);
	}

	/** Whether names of trains and depots are written: not when zoomed out far. */
	bool ShowsLabels() const
	{
		return this->zoom <= ZoomLevel::Out2x;
	}

	/** A square centred on a point, with a frame. */
	void Square(Point p, int half, PixelColour fill, PixelColour frame, int frame_width) const
	{
		GfxFillRect(p.x - half - frame_width, p.y - half - frame_width, p.x + half + frame_width, p.y + half + frame_width, frame);
		GfxFillRect(p.x - half, p.y - half, p.x + half, p.y + half, fill);
	}
};

/** Height of the drawn things across a tile. */
struct XRayGeom {
	enum class Kind : uint8_t {
		Surface, ///< On the ground (and its foundations).
		Flat,    ///< At a fixed height.
		Ramp,    ///< From a height at one edge to another at the opposite edge.
		Portal,  ///< Down a dive portal, straight from the ground to the level of its tunnel.
	};

	TileIndex tile;
	Kind kind = Kind::Surface;
	int z = 0;                         ///< Flat: the height; Ramp: the height at the edge opposite #dir.
	int z_exit = 0;                    ///< Ramp: the height at the edge #dir points to.
	DiagDirection dir = INVALID_DIAGDIR; ///< Ramp: towards the #z_exit edge.
	const TileSlice *portal = nullptr; ///< Portal: the portal.

	/**
	 * Height at a position within the tile.
	 * @param lx X within the tile, 0..TILE_SIZE.
	 * @param ly Y within the tile, 0..TILE_SIZE.
	 */
	int Z(int lx, int ly) const
	{
		int wx = TileX(this->tile) * TILE_SIZE + std::min<int>(lx, TILE_SIZE - 1);
		int wy = TileY(this->tile) * TILE_SIZE + std::min<int>(ly, TILE_SIZE - 1);
		switch (this->kind) {
			case Kind::Surface: return GetSlopePixelZ(wx, wy);
			case Kind::Flat: return this->z;
			case Kind::Ramp: {
				int u;
				switch (this->dir) {
					case DIAGDIR_NE: u = TILE_SIZE - lx; break;
					case DIAGDIR_SW: u = lx; break;
					case DIAGDIR_NW: u = TILE_SIZE - ly; break;
					default:         u = ly; break;
				}
				return this->z + (this->z_exit - this->z) * u / static_cast<int>(TILE_SIZE);
			}
			case Kind::Portal: return GetRailXRayPortalPixelZ(this->tile, *this->portal, wx, wy);
			default: NOT_REACHED();
		}
	}

	/** Screen position of a position within the tile. */
	Point P(const XRayDrawer &d, int lx, int ly) const
	{
		return d.Screen(TileX(this->tile) * TILE_SIZE + lx, TileY(this->tile) * TILE_SIZE + ly, this->Z(lx, ly));
	}

	Point P(const XRayDrawer &d, Point l) const
	{
		return this->P(d, l.x, l.y);
	}
};

/** Draw track pieces in their rail type colour. */
static void DrawXRayTracks(const XRayDrawer &d, const XRayGeom &g, TrackBits tracks, RailType rt, const XRayLevel &level)
{
	PixelColour colour = GetColourGradient(GetXRayRailTypeColour(rt), level.shade);
	for (Track t : SetTrackBitIterator(tracks)) {
		Point a = g.P(d, _xray_edge_mid[_xray_track_ends[t][0]]);
		Point b = g.P(d, _xray_edge_mid[_xray_track_ends[t][1]]);
		if (level.underground) d.Line(a, b, PC_BLACK, std::max(3, d.Px(6)));
		d.Line(a, b, colour, d.Px(2));
	}
}

/** Draw the reserved ones of some track pieces. */
static void DrawXRayReservation(const XRayDrawer &d, const XRayGeom &g, TrackBits reserved, const XRayLevel &level)
{
	PixelColour colour = GetColourGradient(COLOUR_ORANGE, level.dimmed ? SHADE_DARK : SHADE_LIGHTER);
	for (Track t : SetTrackBitIterator(reserved)) {
		Point a = g.P(d, _xray_edge_mid[_xray_track_ends[t][0]]);
		Point b = g.P(d, _xray_edge_mid[_xray_track_ends[t][1]]);
		d.Line(a, b, colour, d.Px(4));
	}
}

/** Draw a platform (or the smaller one of a waypoint) under the track of a station tile. */
static void DrawXRayPlatform(const XRayDrawer &d, const XRayGeom &g, Axis axis, int half_width, PixelColour colour)
{
	int lo = TILE_SIZE / 2 - half_width;
	int hi = TILE_SIZE / 2 + half_width;
	std::array<Point, 4> shape;
	if (axis == AXIS_X) {
		shape = {g.P(d, 0, lo), g.P(d, TILE_SIZE, lo), g.P(d, TILE_SIZE, hi), g.P(d, 0, hi)};
	} else {
		shape = {g.P(d, lo, 0), g.P(d, lo, TILE_SIZE), g.P(d, hi, TILE_SIZE), g.P(d, hi, 0)};
	}
	if (!d.Visible(shape[0], shape[2], 0) && !d.Visible(shape[1], shape[3], 0)) return;
	GfxFillPolygon(shape, colour);
}

/** Whether signals stand on the right of the track, seen in the direction they face. */
static bool AreXRaySignalsOnRight()
{
	switch (_settings_game.construction.train_signal_side) {
		case 0:  return false;
		case 2:  return true;
		default: return _settings_game.vehicle.road_side != 0;
	}
}

/** Frame colour of a signal type: none (black) for a block signal. */
static PixelColour GetXRaySignalFrame(SignalType type)
{
	switch (type) {
		case SIGTYPE_ENTRY:      return PC_YELLOW;
		case SIGTYPE_EXIT:       return GetColourGradient(COLOUR_LIGHT_BLUE, SHADE_LIGHTEST);
		case SIGTYPE_COMBO:      return GetColourGradient(COLOUR_PINK, SHADE_LIGHTER);
		case SIGTYPE_PBS:
		case SIGTYPE_PBS_ONEWAY: return PC_WHITE;
		default:                 return PC_BLACK;
	}
}

/** Short name of a signal type, written beside the signal when the view is zoomed in. */
static StringID GetXRaySignalLabel(SignalType type)
{
	switch (type) {
		case SIGTYPE_ENTRY:      return STR_RAIL_XRAY_SIGNAL_ENTRY;
		case SIGTYPE_EXIT:       return STR_RAIL_XRAY_SIGNAL_EXIT;
		case SIGTYPE_COMBO:      return STR_RAIL_XRAY_SIGNAL_COMBO;
		case SIGTYPE_PBS:        return STR_RAIL_XRAY_SIGNAL_PBS;
		case SIGTYPE_PBS_ONEWAY: return STR_RAIL_XRAY_SIGNAL_PBS_ONEWAY;
		default:                 return STR_RAIL_XRAY_SIGNAL_BLOCK;
	}
}

/**
 * Draw a signal: an arrow beside its track pointing the way of the trains it
 * stops, three quarters of the way along the tile in that direction, on the
 * side signals stand on. It is filled with its state and framed with the
 * colour of its type; a one-way path signal has a bar across its back.
 */
static void DrawXRaySignal(const XRayDrawer &d, const XRayGeom &g, Trackdir td, SignalType type, SignalState state, const XRayLevel &level)
{
	Track track = TrackdirToTrack(td);
	DiagDirection exit = TrackdirToExitdir(td);
	DiagDirection entry = _xray_track_ends[track][0] == exit ? _xray_track_ends[track][1] : _xray_track_ends[track][0];
	Point from = _xray_edge_mid[entry];
	Point to = _xray_edge_mid[exit];

	Point a = g.P(d, from);
	Point b = g.P(d, to);
	Point p = g.P(d, from.x + (to.x - from.x) * 3 / 4, from.y + (to.y - from.y) * 3 / 4);

	int dx = b.x - a.x;
	int dy = b.y - a.y;
	int len = std::max<int>(1, IntSqrt(dx * dx + dy * dy));
	int off = std::max(5, d.Px(9)) * (AreXRaySignalsOnRight() ? 1 : -1);
	p.x += -dy * off / len;
	p.y += dx * off / len;

	int size = std::max(3, d.Px(5));
	int frame = std::max(1, d.Px(2));
	if (!d.Visible(p, p, size + 2 * frame)) return;
	PixelColour fill = state == SIGNAL_STATE_GREEN ? PC_GREEN : PC_RED;
	if (level.dimmed) fill = state == SIGNAL_STATE_GREEN ? GetColourGradient(COLOUR_DARK_GREEN, SHADE_DARK) : PC_DARK_RED;
	PixelColour frame_colour = GetXRaySignalFrame(type);

	/* Unit vector along the travel direction, scaled by 256. */
	int ux = dx * 256 / len;
	int uy = dy * 256 / len;
	/* A slim arrow: the tip well ahead of the point, the back a little behind it. */
	auto arrow = [&](int r) -> std::array<Point, 3> {
		int tip = r * 3 / 2;
		int back = r / 2;
		int w = r * 3 / 5;
		return {{
			{p.x + ux * tip / 256, p.y + uy * tip / 256},
			{p.x - ux * back / 256 - uy * w / 256, p.y - uy * back / 256 + ux * w / 256},
			{p.x - ux * back / 256 + uy * w / 256, p.y - uy * back / 256 - ux * w / 256},
		}};
	};
	GfxFillPolygon(arrow(size + 2 * frame), frame_colour);
	GfxFillPolygon(arrow(size), fill);
	if (type == SIGTYPE_PBS_ONEWAY) {
		int back = (size + 2 * frame) / 2;
		Point c{p.x - ux * back / 256, p.y - uy * back / 256};
		int w = size + frame;
		d.Line({c.x - uy * w / 256, c.y + ux * w / 256}, {c.x + uy * w / 256, c.y - ux * w / 256}, frame_colour, frame);
	}

	/* The type in words, further out from the track, when there is room for it. */
	if (d.zoom <= ZoomLevel::Normal) {
		int out = size * 2 + frame;
		int sign = AreXRaySignalsOnRight() ? 1 : -1;
		Point q{p.x - uy * out * sign / 256, p.y + ux * out * sign / 256};
		int h = GetCharacterHeight(FS_SMALL);
		DrawString(q.x - 30, q.x + 30, q.y - h / 2, GetXRaySignalLabel(type), level.dimmed ? TC_GREY : TC_WHITE, SA_HOR_CENTER, false, FS_SMALL);
	}
}

/** Draw a big red cross at the end of a track that leads nowhere. */
static void DrawXRayDeadEnd(const XRayDrawer &d, const XRayGeom &g, DiagDirection edge)
{
	Point p = g.P(d, _xray_edge_mid[edge]);
	int r = std::max(4, d.Px(8));
	if (!d.Visible(p, p, r + 4)) return;
	int w = std::max(2, d.Px(3));
	for (int pass = 0; pass < 2; pass++) {
		PixelColour c = pass == 0 ? PC_BLACK : PC_RED;
		int ww = pass == 0 ? w + 2 : w;
		d.Line({p.x - r, p.y - r}, {p.x + r, p.y + r}, c, ww);
		d.Line({p.x - r, p.y + r}, {p.x + r, p.y - r}, c, ww);
	}
}

/** The track pieces that end at an edge of a tile. */
static TrackBits GetXRayTracksAtEdge(TrackBits tracks, DiagDirection edge)
{
	return tracks & DiagdirReachesTracks(ReverseDiagDir(edge));
}

/** The tile across an edge of a tile, or INVALID_TILE at the map border. */
static TileIndex GetXRayNeighbour(TileIndex tile, DiagDirection edge)
{
	return AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(edge));
}

static TrackBits GetXRaySliceTracks(const TileSlice &s);

/** Whether surface track ending at an edge of a tile continues across it. */
static bool IsXRaySurfaceEdgeOpen(TileIndex tile, DiagDirection edge)
{
	/* Into a bridge or tunnel, or down a portal. */
	if (IsTileType(tile, TileType::TunnelBridge) && GetTunnelBridgeDirection(tile) == edge) return true;
	if (_multilayer_map.IsAllocated()) {
		SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
		if (sid != INVALID_SLICE_ID && static_cast<DiagDirection>(_multilayer_map.GetSlice(sid).portal.direction) == edge) return true;
	}
	TileIndex n = GetXRayNeighbour(tile, edge);
	if (n == INVALID_TILE) return false;
	TrackBits t = TrackStatusToTrackBits(GetTileTrackStatus(n, TRANSPORT_RAIL, 0, ReverseDiagDir(edge)));
	return (t & DiagdirReachesTracks(edge)) != TRACK_BIT_NONE;
}

/** Whether underground track of a slice ending at an edge of its tile continues across it, at the level it leaves at. */
static bool IsXRayUndergroundEdgeOpen(TileIndex tile, const TileSlice &s, DiagDirection edge)
{
	TileIndex n = GetXRayNeighbour(tile, edge);
	if (n == INVALID_TILE) return false;
	int16_t depth = GetUndergroundLeaveDepth(s, edge);
	for (SliceID sid : _multilayer_map.GetColumn(n).slices) {
		const TileSlice &o = _multilayer_map.GetSlice(sid);
		if (o.kind == SliceKind::Surface) continue;
		if ((GetXRaySliceTracks(o) & DiagdirReachesTracks(edge)) == TRACK_BIT_NONE) continue;
		if (GetUndergroundLeaveDepth(o, ReverseDiagDir(edge)) == depth) return true;
	}
	return false;
}

/** Draw the dead ends of some surface track pieces. */
static void DrawXRaySurfaceDeadEnds(const XRayDrawer &d, const XRayGeom &g, TrackBits tracks)
{
	for (DiagDirection edge = DIAGDIR_BEGIN; edge < DIAGDIR_END; edge++) {
		if (GetXRayTracksAtEdge(tracks, edge) == TRACK_BIT_NONE) continue;
		if (!IsXRaySurfaceEdgeOpen(g.tile, edge)) DrawXRayDeadEnd(d, g, edge);
	}
}

/** Draw the dead ends of an underground slice. */
static void DrawXRayUndergroundDeadEnds(const XRayDrawer &d, const XRayGeom &g, const TileSlice &s, TrackBits tracks)
{
	for (DiagDirection edge = DIAGDIR_BEGIN; edge < DIAGDIR_END; edge++) {
		if (GetXRayTracksAtEdge(tracks, edge) == TRACK_BIT_NONE) continue;
		if (!IsXRayUndergroundEdgeOpen(g.tile, s, edge)) DrawXRayDeadEnd(d, g, edge);
	}
}

/** The portal on a tile, if there is one. */
static const TileSlice *GetXRayPortal(TileIndex tile)
{
	if (!_multilayer_map.IsAllocated() || !_multilayer_map.GetPortalColumns().contains(tile.base())) return nullptr;
	SliceID sid = _multilayer_map.FindSliceByKind(tile, SliceKind::Portal);
	return sid == INVALID_SLICE_ID ? nullptr : &_multilayer_map.GetSlice(sid);
}

/** Draw the railway on the surface of a tile, and a rail bridge over it. */
static void DrawXRaySurfaceTile(const XRayDrawer &d, TileIndex tile, XRayStage stage)
{
	XRayLevel level = GetXRaySurfaceLevel();
	XRayGeom g{tile};

	switch (GetTileType(tile)) {
		case TileType::Railway: {
			RailType rt = GetRailType(tile);
			if (IsRailDepot(tile)) {
				DiagDirection dir = GetRailDepotDirection(tile);
				Point stub_a = g.P(d, TILE_SIZE / 2, TILE_SIZE / 2);
				Point stub_b = g.P(d, _xray_edge_mid[dir]);
				if (stage == XRayStage::Base) {
					std::array<Point, 4> shape = {g.P(d, 3, 3), g.P(d, 13, 3), g.P(d, 13, 13), g.P(d, 3, 13)};
					if (d.Visible(shape[0], shape[2], 0) || d.Visible(shape[1], shape[3], 0)) {
						GfxFillPolygon(shape, GetColourGradient(COLOUR_BROWN, level.dimmed ? SHADE_DARK : SHADE_LIGHT));
					}
					d.Line(stub_a, stub_b, GetColourGradient(GetXRayRailTypeColour(rt), level.shade), d.Px(2));
				} else if (HasDepotReservation(tile)) {
					d.Line(stub_a, stub_b, GetColourGradient(COLOUR_ORANGE, SHADE_LIGHTER), d.Px(4));
				}
				break;
			}
			TrackBits tracks = GetTrackBits(tile);
			/* The rail along a portal goes down it, in one slope to the tunnel. */
			XRayGeom slope{tile, XRayGeom::Kind::Portal};
			TrackBits down = TRACK_BIT_NONE;
			if (const TileSlice *portal = GetXRayPortal(tile); portal != nullptr) {
				slope.portal = portal;
				down = tracks & DiagDirToDiagTrackBits(static_cast<DiagDirection>(portal->portal.direction));
			}
			if (stage == XRayStage::Base) {
				DrawXRayTracks(d, g, tracks & ~down, rt, level);
				if (down != TRACK_BIT_NONE) DrawXRayTracks(d, slope, down, rt, level);
				break;
			}
			TrackBits reserved = GetRailReservationTrackBits(tile);
			DrawXRayReservation(d, g, reserved & ~down, level);
			if (down != TRACK_BIT_NONE) DrawXRayReservation(d, slope, reserved & down, level);
			DrawXRaySurfaceDeadEnds(d, g, tracks);
			if (HasSignals(tile)) {
				for (Track t : SetTrackBitIterator(tracks)) {
					for (Trackdir td : {TrackToTrackdir(t), ReverseTrackdir(TrackToTrackdir(t))}) {
						if (HasSignalOnTrackdir(tile, td)) DrawXRaySignal(d, g, td, GetSignalType(tile, t), GetSignalStateByTrackdir(tile, td), level);
					}
				}
			}
			break;
		}

		case TileType::Road:
			if (!IsLevelCrossing(tile)) break;
			if (stage == XRayStage::Base) {
				DrawXRayTracks(d, g, GetCrossingRailBits(tile), GetRailType(tile), level);
			} else {
				if (HasCrossingReservation(tile)) DrawXRayReservation(d, g, GetCrossingRailBits(tile), level);
				DrawXRaySurfaceDeadEnds(d, g, GetCrossingRailBits(tile));
			}
			break;

		case TileType::Station: {
			if (!HasStationRail(tile)) break;
			Axis axis = GetRailStationAxis(tile);
			TrackBits track = AxisToTrackBits(axis);
			if (stage == XRayStage::Base) {
				if (IsRailWaypoint(tile)) {
					DrawXRayPlatform(d, g, axis, 3, GetColourGradient(COLOUR_DARK_BLUE, level.dimmed ? SHADE_DARK : SHADE_LIGHTER));
				} else {
					DrawXRayPlatform(d, g, axis, 6, GetColourGradient(COLOUR_DARK_BLUE, level.dimmed ? SHADE_DARKER : SHADE_NORMAL));
				}
				if (HasStationTileRail(tile)) DrawXRayTracks(d, g, track, GetRailType(tile), level);
			} else {
				if (HasStationReservation(tile)) DrawXRayReservation(d, g, track, level);
				if (HasStationTileRail(tile)) DrawXRaySurfaceDeadEnds(d, g, track);
			}
			break;
		}

		case TileType::TunnelBridge: {
			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) break;
			TrackBits track = DiagDirToDiagTrackBits(GetTunnelBridgeDirection(tile));
			if (stage == XRayStage::Base) {
				DrawXRayTracks(d, g, track, GetRailType(tile), level);
			} else {
				if (HasTunnelBridgeReservation(tile)) DrawXRayReservation(d, g, track, level);
				DrawXRaySurfaceDeadEnds(d, g, track);
			}
			break;
		}

		default: break;
	}

	if (IsBridgeAbove(tile)) {
		TileIndex north = GetNorthernBridgeEnd(tile);
		if (GetTunnelBridgeTransportType(north) != TRANSPORT_RAIL) return;
		XRayGeom deck{tile, XRayGeom::Kind::Flat, GetBridgePixelHeight(north)};
		TrackBits track = AxisToTrackBits(GetBridgeAxis(tile));
		if (stage == XRayStage::Base) {
			DrawXRayTracks(d, deck, track, GetRailType(north), level);
		} else if (HasTunnelBridgeReservation(north) || HasTunnelBridgeReservation(GetSouthernBridgeEnd(tile))) {
			DrawXRayReservation(d, deck, track, level);
		}
	}
}

/** Draw the signals of an underground track or ramp. */
static void DrawXRayUndergroundSignals(const XRayDrawer &d, const XRayGeom &g, TileIndex tile, SliceID sid, TrackBits tracks, const XRayLevel &level)
{
	const TileSlice &s = _multilayer_map.GetSlice(sid);
	if (GetUndergroundSignalMask(s) == 0) return;
	for (Track t : SetTrackBitIterator(tracks)) {
		for (Trackdir td : {TrackToTrackdir(t), ReverseTrackdir(TrackToTrackdir(t))}) {
			if (!HasUndergroundSignalOnTrackdir(s, td)) continue;
			DrawXRaySignal(d, g, td, GetUndergroundSignalType(s, t), GetUndergroundSignalDisplayState(TileRef{tile, sid}, td), level);
		}
	}
}

/** Draw the underground railway of a tile, every level at its own depth. */
static void DrawXRayUndergroundTile(const XRayDrawer &d, TileIndex tile, XRayStage stage)
{
	const TileColumn &col = _multilayer_map.GetColumn(tile);
	if (!col.HasUnderground()) return;

	for (SliceID sid : col.slices) {
		const TileSlice &s = _multilayer_map.GetSlice(sid);
		XRayLevel level = GetXRayUndergroundLevel(s.span.z_bot);
		XRayGeom g{tile, XRayGeom::Kind::Flat, s.span.z_bot * static_cast<int>(TILE_HEIGHT)};

		switch (s.kind) {
			case SliceKind::Track: {
				TrackBits tracks = static_cast<TrackBits>(s.track.tracks);
				RailType rt = static_cast<RailType>(s.track.railtype);
				if (stage == XRayStage::Base) {
					DrawXRayTracks(d, g, tracks, rt, level);
				} else {
					DrawXRayReservation(d, g, static_cast<TrackBits>(s.track.reserved), level);
					DrawXRayUndergroundDeadEnds(d, g, s, tracks);
					DrawXRayUndergroundSignals(d, g, tile, sid, tracks, level);
				}
				break;
			}

			case SliceKind::StationTile: {
				if (s.station.role != 0) break;
				TrackBits tracks = static_cast<TrackBits>(s.station.tracks);
				Axis axis = (tracks & TRACK_BIT_Y) != 0 ? AXIS_Y : AXIS_X;
				if (stage == XRayStage::Base) {
					DrawXRayPlatform(d, g, axis, 6, GetColourGradient(COLOUR_DARK_BLUE, level.dimmed ? SHADE_DARKEST : SHADE_DARK));
					DrawXRayTracks(d, g, AxisToTrackBits(axis), static_cast<RailType>(s.station.railtype), level);
				} else {
					DrawXRayReservation(d, g, static_cast<TrackBits>(s.station.reserved), level);
					DrawXRayUndergroundDeadEnds(d, g, s, AxisToTrackBits(axis));
				}
				break;
			}

			case SliceKind::Ramp: {
				g.kind = XRayGeom::Kind::Ramp;
				g.dir = static_cast<DiagDirection>(s.ramp.direction);
				g.z = s.ramp.entry_depth * static_cast<int>(TILE_HEIGHT);
				g.z_exit = s.ramp.exit_depth * static_cast<int>(TILE_HEIGHT);
				TrackBits track = DiagDirToDiagTrackBits(g.dir);
				if (stage == XRayStage::Base) {
					DrawXRayTracks(d, g, track, static_cast<RailType>(s.ramp.railtype), level);
				} else {
					DrawXRayReservation(d, g, s.ramp.reserved != 0 ? track : TRACK_BIT_NONE, level);
					DrawXRayUndergroundDeadEnds(d, g, s, track);
					DrawXRayUndergroundSignals(d, g, tile, sid, track, level);
				}
				break;
			}

			case SliceKind::Portal: {
				if (stage != XRayStage::Base) break;
				/* The track down to the tunnel is the surface rail of the tile (see
				 * DrawXRaySurfaceTile); mark the mouth of the tunnel at its foot. */
				g.kind = XRayGeom::Kind::Portal;
				g.portal = &s;
				DiagDirection dir = static_cast<DiagDirection>(s.portal.direction);
				RailType rt = static_cast<RailType>(s.portal.railtype);
				d.Square(g.P(d, _xray_edge_mid[dir]), d.Px(2), PC_BLACK, GetColourGradient(GetXRayRailTypeColour(rt), level.shade), 1);
				break;
			}

			default: break;
		}
	}
}

/** Collect the rail tunnels of the map. */
static void RebuildXRayTunnels()
{
	_xray_tunnels.clear();
	for (const auto tile : Map::Iterate()) {
		if (!IsTileType(tile, TileType::TunnelBridge) || !IsTunnel(tile) || GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) continue;
		TileIndex other = GetOtherTunnelEnd(tile);
		if (TileIndex(tile) < other) _xray_tunnels.emplace_back(tile, other);
	}
	_xray_tunnels_valid = true;
}

/** Draw the rail tunnels: dashed between their ends, at the height of the trains in them. */
static void DrawXRayTunnels(const XRayDrawer &d)
{
	if (!_xray_tunnels_valid) RebuildXRayTunnels();
	XRayLevel level = GetXRaySurfaceLevel();
	for (const auto &[a, b] : _xray_tunnels) {
		if (!IsTileType(a, TileType::TunnelBridge) || !IsTunnel(a) || !IsTileType(b, TileType::TunnelBridge)) continue;
		int z = GetTilePixelZ(a);
		Point pa = d.Screen(TileX(a) * TILE_SIZE + TILE_SIZE / 2, TileY(a) * TILE_SIZE + TILE_SIZE / 2, z);
		Point pb = d.Screen(TileX(b) * TILE_SIZE + TILE_SIZE / 2, TileY(b) * TILE_SIZE + TILE_SIZE / 2, z);
		bool reserved = HasTunnelBridgeReservation(a) || HasTunnelBridgeReservation(b);
		PixelColour colour = reserved ? GetColourGradient(COLOUR_ORANGE, SHADE_LIGHTER) : GetColourGradient(GetXRayRailTypeColour(GetRailType(a)), level.shade);
		d.Line(pa, pb, colour, d.Px(2), d.Px(4));
	}
}

bool IsRailXRayTrainPartDrawn(const Train *t)
{
	if (t->IsUnderground()) return true;
	/* Hidden on the surface: in a depot, or in a tunnel, which the view shows. */
	return !t->vehstatus.Test(VehState::Hidden) || t->track == TRACK_BIT_WORMHOLE;
}

/** Colour of a train by what it does. */
static PixelColour GetXRayTrainColour(const Train *front)
{
	if (front->vehstatus.Test(VehState::Crashed)) return PC_VERY_DARK_RED;
	if (front->vehstatus.Test(VehState::Stopped)) return PC_GREY;
	if (front->current_order.IsType(OT_LOADING)) return GetColourGradient(COLOUR_BLUE, SHADE_LIGHTER);
	if (front->cur_speed == 0) return front->flags.Test(VehicleRailFlag::Stuck) ? GetColourGradient(COLOUR_PINK, SHADE_LIGHTER) : PC_RED;
	return PC_WHITE;
}

/**
 * Draw the trains: a bar per vehicle, at the height the ordinary view draws
 * it, with a yellow dot at the head of the train.
 * @param underground Draw the parts below the surface, else the others.
 */
static void DrawXRayTrains(const XRayDrawer &d, bool underground)
{
	for (const Train *t : Train::Iterate()) {
		if (t->IsUnderground() != underground || !IsRailXRayTrainPartDrawn(t)) continue;

		int z = GetVehicleViewportZ(t);
		int dx = _xray_dir_dx[t->direction];
		int dy = _xray_dir_dy[t->direction];
		int len = t->gcache.cached_veh_length;
		int half = std::max(1, (dx != 0 && dy != 0 ? len * 3 / 8 : len / 2) - 1);
		Point a = d.Screen(t->x_pos - dx * half, t->y_pos - dy * half, z);
		Point b = d.Screen(t->x_pos + dx * half, t->y_pos + dy * half, z);
		int width = std::max(3, d.Px(6));
		if (!d.Visible(a, b, width + 2)) continue;

		d.Line(a, b, PC_BLACK, width + 2);
		d.Line(a, b, GetXRayTrainColour(t->First()), width);
		if (t == t->First()) d.Square(b, d.Px(2), PC_YELLOW, PC_BLACK, 1);
	}
}

/** Whether a train shows the arrow to its destination. */
static bool IsXRayDestinationShown(const Train *t)
{
	switch (_rail_xray_destinations) {
		case RailXRayDestinations::All: return true;
		case RailXRayDestinations::Selected:
			return FindWindowById(WC_VEHICLE_VIEW, t->index) != nullptr || FindWindowById(WC_VEHICLE_ORDERS, t->index) != nullptr ||
					FindWindowById(WC_VEHICLE_TIMETABLE, t->index) != nullptr;
		default: return false;
	}
}

/**
 * The world position a train heads for by its current order: the tile the
 * pathfinder aims at, at its height; for an underground station the platform
 * of it nearest to the train, at its level.
 * @param t The front of the train.
 * @param[out] target World x, y and height.
 * @return False when the train goes nowhere in particular.
 */
static bool GetXRayTrainTarget(const Train *t, std::array<int, 3> &target)
{
	if (!t->current_order.IsGotoOrder()) return false;

	if (t->current_order.IsType(OT_GOTO_STATION) || t->current_order.IsType(OT_GOTO_WAYPOINT)) {
		auto it = _station_complexes.find(t->current_order.GetDestination().base());
		if (it != _station_complexes.end()) {
			const StationComplexNode *best = nullptr;
			uint best_dist = UINT_MAX;
			for (const StationComplexNode &n : it->second.nodes) {
				if (n.role != StationRole::Platform || !_multilayer_map.IsValidSlice(n.ref.slice)) continue;
				uint dist = DistanceManhattan(n.ref.tile, t->tile);
				if (dist < best_dist) {
					best_dist = dist;
					best = &n;
				}
			}
			if (best != nullptr) {
				target = {static_cast<int>(TileX(best->ref.tile) * TILE_SIZE + TILE_SIZE / 2), static_cast<int>(TileY(best->ref.tile) * TILE_SIZE + TILE_SIZE / 2),
						_multilayer_map.GetSlice(best->ref.slice).span.z_bot * static_cast<int>(TILE_HEIGHT)};
				return true;
			}
		}
	}

	TileIndex tile = t->dest_tile;
	if (!IsValidTile(tile)) return false;
	int x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
	int y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
	target = {x, y, GetSlopePixelZ(x, y)};
	return true;
}

/** Draw a dashed arrow from the head of each train that shows it to where its current order sends it. */
static void DrawXRayDestinations(const XRayDrawer &d)
{
	if (_rail_xray_destinations == RailXRayDestinations::Off) return;
	for (const Train *t : Train::Iterate()) {
		if (t != t->First() || !IsRailXRayTrainPartDrawn(t) || !IsXRayDestinationShown(t)) continue;
		std::array<int, 3> target;
		if (!GetXRayTrainTarget(t, target)) continue;

		Point a = d.Screen(t->x_pos, t->y_pos, GetVehicleViewportZ(t));
		Point b = d.Screen(target[0], target[1], target[2]);
		int width = std::max(2, d.Px(2));
		if (!d.Visible(a, b, width + 16)) continue;
		d.Line(a, b, PC_BLACK, width + 2);
		d.Line(a, b, PC_YELLOW, width, std::max(3, d.Px(6)));

		/* The head of the arrow, at the destination. */
		int dx = b.x - a.x;
		int dy = b.y - a.y;
		int len = std::max<int>(1, IntSqrt(dx * dx + dy * dy));
		int ux = dx * 256 / len;
		int uy = dy * 256 / len;
		int r = std::max(6, d.Px(8));
		std::array<Point, 3> head = {{
			b,
			{b.x - ux * r / 256 - uy * r / 2 / 256, b.y - uy * r / 256 + ux * r / 2 / 256},
			{b.x - ux * r / 256 + uy * r / 2 / 256, b.y - uy * r / 256 - ux * r / 2 / 256},
		}};
		GfxFillPolygon(head, PC_YELLOW);
	}
}

/** Write the name of every train above its head. */
static void DrawXRayTrainLabels(const XRayDrawer &d)
{
	for (const Train *t : Train::Iterate()) {
		if (t != t->First() || !IsRailXRayTrainPartDrawn(t)) continue;
		Point head = d.Screen(t->x_pos, t->y_pos, GetVehicleViewportZ(t));
		head.y -= std::max(4, d.Px(6));
		/* Rough cull before formatting the name. */
		if (!d.Visible(head, head, 120)) continue;
		d.Label(head, GetString(STR_VEHICLE_NAME, t->index), TC_WHITE);
	}
}

/** Draw the outline of the tile selection, on the level it was picked on. */
static void DrawXRayTileSelection(const XRayDrawer &d)
{
	if ((_thd.drawstyle & HT_DRAG_MASK) == HT_NONE || _thd.size.x <= 0 || _thd.size.y <= 0) return;
	if (!IsInsideBS(_thd.pos.x / TILE_SIZE, 0, Map::SizeX()) || !IsInsideBS(_thd.pos.y / TILE_SIZE, 0, Map::SizeY())) return;
	int z = _xray_pick_z != INT16_MIN ? _xray_pick_z * static_cast<int>(TILE_HEIGHT) : GetTilePixelZ(TileVirtXY(_thd.pos.x, _thd.pos.y));
	int x0 = _thd.pos.x;
	int y0 = _thd.pos.y;
	int x1 = x0 + _thd.size.x;
	int y1 = y0 + _thd.size.y;
	std::array<Point, 4> c = {d.Screen(x0, y0, z), d.Screen(x1, y0, z), d.Screen(x1, y1, z), d.Screen(x0, y1, z)};
	for (size_t i = 0; i < c.size(); i++) d.Line(c[i], c[(i + 1) % c.size()], PC_WHITE, 1);
}

int GetRailXRayPortalPixelZ(TileIndex tile, const TileSlice &portal, int x, int y)
{
	int ground = GetTileZ(tile) * static_cast<int>(TILE_HEIGHT);
	int tunnel = portal.span.z_bot * static_cast<int>(TILE_HEIGHT);
	/* Distance from the tile edge on the tunnel side, 0..TILE_SIZE - 1. */
	int lx = x & (TILE_SIZE - 1);
	int ly = y & (TILE_SIZE - 1);
	int u;
	switch (static_cast<DiagDirection>(portal.portal.direction)) {
		case DIAGDIR_NE: u = lx; break;
		case DIAGDIR_SW: u = TILE_SIZE - 1 - lx; break;
		case DIAGDIR_NW: u = ly; break;
		default:         u = TILE_SIZE - 1 - ly; break;
	}
	return tunnel + (ground - tunnel) * u / static_cast<int>(TILE_SIZE - 1);
}

int GetRailXRayLowestPixelZ()
{
	if (!_multilayer_map.IsAllocated()) return 0;
	return std::min<int>(0, GetMetroGlobalMinZ()) * static_cast<int>(TILE_HEIGHT);
}

void ViewportDrawRailXRay(const DrawPixelInfo &vd_dpi)
{
	ZoomLevel zoom = vd_dpi.zoom;
	DrawPixelInfo dp = vd_dpi;
	dp.zoom = ZoomLevel::Min;
	dp.width = UnScaleByZoom(dp.width, zoom);
	dp.height = UnScaleByZoom(dp.height, zoom);
	dp.left = UnScaleByZoomLower(vd_dpi.left, zoom);
	dp.top = UnScaleByZoomLower(vd_dpi.top, zoom);
	AutoRestoreBackup cur_dpi(_cur_dpi, &dp);

	XRayDrawer d{zoom, {dp.left, dp.top, dp.left + dp.width - 1, dp.top + dp.height - 1}};
	GfxFillRect(d.clip.left, d.clip.top, d.clip.right, d.clip.bottom, XRAY_BACKGROUND);

	/* The edge of the map, at sea level. */
	int mx = Map::SizeX() * TILE_SIZE;
	int my = Map::SizeY() * TILE_SIZE;
	std::array<Point, 4> edge = {d.Screen(0, 0, 0), d.Screen(mx, 0, 0), d.Screen(mx, my, 0), d.Screen(0, my, 0)};
	for (size_t i = 0; i < edge.size(); i++) d.Line(edge[i], edge[(i + 1) % edge.size()], PC_BLACK, 1);

	/* The tiles that can have something in the area, from the deepest level to the highest bridge. */
	int z_lo = GetRailXRayLowestPixelZ();
	int z_hi = (_settings_game.construction.map_height_limit + _settings_game.construction.max_bridge_height + 1) * static_cast<int>(TILE_HEIGHT);
	int l = vd_dpi.left;
	int r = vd_dpi.left + vd_dpi.width;
	int t = vd_dpi.top;
	int b = vd_dpi.top + vd_dpi.height;
	int zb_lo = z_lo * ZOOM_BASE;
	int zb_hi = z_hi * ZOOM_BASE;
	/* Inverse of RemapCoords: x = (2 * (sy + z) - sx) / 4, y = (2 * (sy + z) + sx) / 4, in zoom base units. */
	int div = 4 * ZOOM_BASE * TILE_SIZE;
	int tx_lo = std::max(0, (2 * (t + zb_lo) - r) / div - 2);
	int tx_hi = std::min<int>(Map::MaxX(), (2 * (b + zb_hi) - l) / div + 2);
	int ty_lo = std::max(0, (2 * (t + zb_lo) + l) / div - 2);
	int ty_hi = std::min<int>(Map::MaxY(), (2 * (b + zb_hi) + r) / div + 2);

	std::vector<TileIndex> tiles;
	int half_width = 2 * ZOOM_BASE * TILE_SIZE;
	for (int ty = ty_lo; ty <= ty_hi; ty++) {
		for (int tx = tx_lo; tx <= tx_hi; tx++) {
			Point c = RemapCoords(tx * TILE_SIZE + TILE_SIZE / 2, ty * TILE_SIZE + TILE_SIZE / 2, 0);
			if (c.x + half_width < l || c.x - half_width > r) continue;
			int row = (tx + ty) * TILE_SIZE * ZOOM_BASE;
			if (row + 2 * TILE_SIZE * ZOOM_BASE - zb_lo < t || row - zb_hi > b) continue;
			tiles.push_back(TileXY(tx, ty));
		}
	}

	if (_multilayer_map.IsAllocated()) {
		for (XRayStage stage : {XRayStage::Base, XRayStage::Overlay}) {
			for (TileIndex tile : tiles) DrawXRayUndergroundTile(d, tile, stage);
		}
		DrawXRayTrains(d, true);
	}

	DrawXRayTunnels(d);
	for (XRayStage stage : {XRayStage::Base, XRayStage::Overlay}) {
		for (TileIndex tile : tiles) DrawXRaySurfaceTile(d, tile, stage);
	}
	DrawXRayTrains(d, false);
	DrawXRayDestinations(d);

	/* Names on top of everything: every train and every depot (stations and
	 * waypoints have their signs, see ViewportAddRailXRaySigns). */
	if (d.ShowsLabels()) {
		for (TileIndex tile : tiles) {
			if (!IsRailDepotTile(tile)) continue;
			XRayGeom g{tile};
			d.Label(g.P(d, TILE_SIZE / 2, TILE_SIZE / 2), GetString(STR_DEPOT_NAME, VEH_TRAIN, GetDepotIndex(tile)), TC_ORANGE);
		}
		DrawXRayTrainLabels(d);
	}

	DrawXRayTileSelection(d);
}

/** Squared distance from a point to a segment, in world units within a tile. */
static int XRaySegmentDistanceSq(Point a, Point b, int x, int y)
{
	int vx = b.x - a.x;
	int vy = b.y - a.y;
	int wx = x - a.x;
	int wy = y - a.y;
	int len = vx * vx + vy * vy;
	int dot = Clamp(wx * vx + wy * vy, 0, len);
	/* Position of the nearest point, scaled by len. */
	int px = a.x * len + vx * dot - x * len;
	int py = a.y * len + vy * dot - y * len;
	return (px * px + py * py) / (len * len);
}

/** Squared distance from a point in a tile to the nearest of some track pieces. */
static int XRayTrackDistanceSq(TrackBits tracks, int lx, int ly)
{
	int best = INT_MAX;
	for (Track t : SetTrackBitIterator(tracks)) {
		best = std::min(best, XRaySegmentDistanceSq(_xray_edge_mid[_xray_track_ends[t][0]], _xray_edge_mid[_xray_track_ends[t][1]], lx, ly));
	}
	return best;
}

/** The track pieces an underground slice has, for picking. */
static TrackBits GetXRaySliceTracks(const TileSlice &s)
{
	switch (s.kind) {
		case SliceKind::Track: return static_cast<TrackBits>(s.track.tracks);
		case SliceKind::StationTile: return s.station.role == 0 ? static_cast<TrackBits>(s.station.tracks) : TRACK_BIT_NONE;
		case SliceKind::Ramp: return DiagDirToDiagTrackBits(static_cast<DiagDirection>(s.ramp.direction));
		case SliceKind::Portal: return DiagDirToDiagTrackBits(static_cast<DiagDirection>(s.portal.direction));
		default: return TRACK_BIT_NONE;
	}
}

bool RailXRayPickPoint(int vx, int vy, Point &pt)
{
	int best = INT_MAX;
	bool found = false;

	/* The surface first, so it wins a tie. */
	Point sp = InverseRemapCoords2(vx, vy);
	if (IsInsideBS(sp.x / static_cast<int>(TILE_SIZE), 0, Map::SizeX()) && IsInsideBS(sp.y / static_cast<int>(TILE_SIZE), 0, Map::SizeY()) && sp.x >= 0 && sp.y >= 0) {
		TileIndex tile = TileVirtXY(sp.x, sp.y);
		TrackBits tracks = TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));
		if (tracks != TRACK_BIT_NONE) {
			best = XRayTrackDistanceSq(tracks, sp.x & TILE_UNIT_MASK, sp.y & TILE_UNIT_MASK);
			pt = sp;
			found = true;
			_xray_pick_z = INT16_MIN;
		}
	}

	if (_multilayer_map.IsAllocated()) {
		for (int z = GetMetroGlobalMaxZ(); z >= GetMetroGlobalMinZ(); z--) {
			Point wp = InverseRemapCoords(vx, vy + z * static_cast<int>(TILE_HEIGHT) * ZOOM_BASE);
			if (wp.x < 0 || wp.y < 0 || wp.x >= static_cast<int>(Map::SizeX() * TILE_SIZE) || wp.y >= static_cast<int>(Map::SizeY() * TILE_SIZE)) continue;
			TileIndex tile = TileVirtXY(wp.x, wp.y);
			for (SliceID sid : _multilayer_map.GetColumn(tile).slices) {
				const TileSlice &s = _multilayer_map.GetSlice(sid);
				if (s.kind == SliceKind::Surface || s.span.z_bot != z) continue;
				TrackBits tracks = GetXRaySliceTracks(s);
				if (tracks == TRACK_BIT_NONE) continue;
				int dist = XRayTrackDistanceSq(tracks, wp.x & TILE_UNIT_MASK, wp.y & TILE_UNIT_MASK);
				if (dist < best) {
					best = dist;
					pt = wp;
					found = true;
					_xray_pick_z = static_cast<int16_t>(z);
				}
			}
		}
	}

	if (!found) _xray_pick_z = INT16_MIN;
	return found;
}

void SetRailXRayView(bool on)
{
	if (on == _rail_xray_view) return;
	_rail_xray_view = on;
	_xray_tunnels_valid = false;
	_xray_pick_z = INT16_MIN;

	/* Underground trains are drawn at their own depth: their clickable boxes move with it. */
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->type == VEH_TRAIN) v->UpdateViewport(false);
	}
	MarkWholeScreenDirty();
	InvalidateWindowClassesData(WC_MAIN_TOOLBAR);
}

/** Repaint the view now and then: reservations and signals change without marking it dirty. */
static const IntervalTimer<TimerWindow> _rail_xray_refresh(std::chrono::milliseconds(500), [](auto) {
	if (!_rail_xray_view) return;
	static uint count = 0;
	/* Tunnels are built and removed rarely; look for them again every few seconds. */
	if (++count % 10 == 0) _xray_tunnels_valid = false;
	MarkAllViewportsDirty(INT_MIN / 4, INT_MIN / 4, INT_MAX / 4, INT_MAX / 4);
});
