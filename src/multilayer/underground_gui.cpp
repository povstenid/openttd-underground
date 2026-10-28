/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_gui.cpp Underground tools of the railway construction toolbar. */

#include "../stdafx.h"
#include "underground_gui.h"
#include "underground_view.h"
#include "underground_cmd.h"
#include "underground_cost.h"
#include "multilayer_map.h"
#include "underground_signal.h"
#include "station_complex.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../command_func.h"
#include "../company_func.h"
#include "../company_base.h"
#include "../error.h"
#include "../station_base.h"
#include "../station_func.h"
#include "../station_gui.h"
#include "../picker_gui.h"
#include "../rail_gui.h"
#include "../rail.h"
#include "../rail_map.h"
#include "../sound_func.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "../tilehighlight_func.h"
#include "../toolbar_gui.h"
#include "../tile_map.h"
#include "../slope_func.h"
#include "../gfx_func.h"
#include "../newgrf_station.h"
#include "../signal_func.h"
#include "../track_func.h"
#include "../zoom_func.h"
#include "../widgets/rail_widget.h"
#include "../landscape_cmd.h"
#include "../terraform_cmd.h"
#include "../core/backup_type.hpp"

#include "../table/sprites.h"
#include "../table/strings.h"

#include "../safeguards.h"

static const WindowNumber WN_UNDERGROUND_STATION_BUILDER = 0x55475354; // 'UGST'
static Axis _underground_station_axis = AXIS_X;

static void ClampUndergroundStationSettings()
{
	uint8_t max_spread = static_cast<uint8_t>(std::max<int>(1, _settings_game.station.station_spread));
	_settings_client.gui.station_numtracks = Clamp<uint8_t>(_settings_client.gui.station_numtracks, 1, max_spread);
	_settings_client.gui.station_platlength = Clamp<uint8_t>(_settings_client.gui.station_platlength, 1, max_spread);
}

/**
 * Post the area-station command for a tile area.
 * Goes through the regular command pipeline (network-safe), unlike a
 * client-side loop of Command::Do calls.
 */
/** The ramp the ramp tools show under the cursor. */
static UndergroundRampGhost _ramp_ghost;
/** Direction of the ramp picked by the click that started the ramp tool's drag. */
static DiagDirection _ramp_click_dir = INVALID_DIAGDIR;

/**
 * The edge of a tile nearest to a point on it, i.e. the triangle of the tile
 * split by its diagonals that the point lies in.
 * @param pt World coordinates of the point; only the position within its tile counts.
 * @return The direction from the tile's centre towards that edge.
 */
static DiagDirection GetNearestTileEdge(Point pt)
{
	int fx = pt.x & TILE_UNIT_MASK;
	int fy = pt.y & TILE_UNIT_MASK;
	std::array<int, DIAGDIR_END> dist{};
	dist[DIAGDIR_NE] = fx;
	dist[DIAGDIR_SE] = TILE_UNIT_MASK - fy;
	dist[DIAGDIR_SW] = TILE_UNIT_MASK - fx;
	dist[DIAGDIR_NW] = fy;
	return static_cast<DiagDirection>(std::distance(dist.begin(), std::ranges::min_element(dist)));
}

/**
 * The ramp a ramp tool builds: from the viewed level to the one below (down
 * tool) or above (up tool) it, leading towards \a dir. So the down ramp
 * descends towards \a dir and the up ramp rises towards it.
 */
static UndergroundRampGhost MakeRampGhost(WidgetID tool, TileIndex tile, DiagDirection dir, RailType railtype)
{
	UndergroundRampGhost ghost;
	ghost.tile = tile;
	ghost.dir = dir;
	ghost.entry_depth = GetUndergroundViewDepth();
	ghost.exit_depth = static_cast<int16_t>(tool == WID_RAT_BUILD_RAMP_DOWN ? ghost.entry_depth - 1 : ghost.entry_depth + 1);
	ghost.railtype = railtype;
	return ghost;
}

/**
 * The portal the portal tool builds on \a tile when the player points
 * towards \a dir. On flat ground the portal dives into the ground towards
 * \a dir, like a ramp going down: two levels below the surface (one where the
 * map bottom leaves no room), or on the viewed level when that is deeper. On
 * a hillside the tunnel mouth is at the foot of the incline and the incline
 * decides the direction; a drag across it is refused by the command.
 * @param tile The portal's tile.
 * @param dir The direction pointed at: the tile edge nearest to the cursor, or the drag.
 * @param dragged The direction comes from a drag, not from a click.
 * @param railtype The track type built.
 */
static UndergroundPortalGhost MakePortalGhost(TileIndex tile, DiagDirection dir, bool dragged, RailType railtype)
{
	UndergroundPortalGhost ghost;
	ghost.tile = tile;
	ghost.railtype = railtype;

	int16_t below = static_cast<int16_t>(GetTileZ(tile) - 2);
	if (below < GetMetroGlobalMinZ()) below = static_cast<int16_t>(GetTileZ(tile) - 1);
	ghost.depth = GetUndergroundViewDepth();
	if (!IsUndergroundSliceMode() || ghost.depth > below) ghost.depth = below;
	ghost.dir = dir;
	ghost.command_dir = dir;

	if (DiagDirection slope_dir = GetInclinedSlopeDirection(GetTileSlope(tile)); slope_dir != INVALID_DIAGDIR) {
		ghost.hillside = true;
		ghost.depth = static_cast<int16_t>(GetTileZ(tile));
		ghost.dir = slope_dir;
		if (!dragged) ghost.command_dir = slope_dir;
	}
	return ghost;
}

/**
 * The tile and direction a ramp or portal tool points at: while dragged, from
 * the first tile towards the one under the cursor (or the edge clicked when
 * the drag has not left the tile); otherwise the tile under the cursor and
 * its edge nearest to the cursor.
 * @param[out] tile The tile.
 * @param[out] dir The direction.
 * @return Whether the direction comes from a drag to another tile.
 */
static bool GetRailToolGhostSpot(TileIndex &tile, DiagDirection &dir)
{
	tile = INVALID_TILE;
	dir = INVALID_DIAGDIR;
	if (_special_mouse_mode == WSM_SIZING) {
		tile = TileVirtXY(_thd.selstart.x, _thd.selstart.y);
		TileIndex end = TileVirtXY(_thd.selend.x, _thd.selend.y);
		if (end != tile) {
			dir = DiagdirBetweenTiles(tile, end);
			return true;
		}
		dir = _ramp_click_dir;
		return false;
	}
	Point pt = GetTileBelowCursor();
	if (pt.x != -1) {
		tile = TileVirtXY(pt.x, pt.y);
		dir = GetNearestTileEdge(pt);
	}
	return false;
}

/** The portal the portal tool shows under the cursor. */
static UndergroundPortalGhost _portal_ghost;

/** Mark the tile of a portal ghost and the tile behind its arch for redraw. */
static void MarkPortalGhostDirty(const UndergroundPortalGhost &ghost)
{
	if (ghost.tile == INVALID_TILE) return;
	MarkTileDirtyByTile(ghost.tile);
	TileIndex behind = TileAddByDiagDir(ghost.tile, ghost.dir);
	if (IsValidTile(behind)) MarkTileDirtyByTile(behind);
}

/**
 * Show the ramp a ramp tool, or the portal the portal tool, would build under
 * the cursor, or while it is dragged; hide it with any other tool.
 * Client-side only: the command is just tested, to show what cannot be built in red.
 * @param tool The active tool of the railway construction toolbar, or INVALID_WIDGET.
 * @param railtype The track type built.
 */
void UpdateUndergroundRailToolGhost(WidgetID tool, RailType railtype)
{
	bool placing = _thd.window_class == WC_BUILD_TOOLBAR && (_thd.place_mode & HT_DRAG_MASK) != HT_NONE;

	UndergroundRampGhost ghost;
	if ((tool == WID_RAT_BUILD_RAMP_DOWN || tool == WID_RAT_BUILD_RAMP_UP) && IsUndergroundSliceMode() && placing) {
		TileIndex tile;
		DiagDirection dir;
		GetRailToolGhostSpot(tile, dir);
		if (IsValidTile(tile) && IsValidDiagDirection(dir)) {
			ghost = MakeRampGhost(tool, tile, dir, railtype);
			Backup<CompanyID> cur_company(_current_company, _local_company);
			ghost.buildable = Command<Commands::BuildRamp>::Do(DoCommandFlag::Auto, tile, railtype, dir, ghost.entry_depth, ghost.exit_depth).Succeeded();
			cur_company.Restore();
		}
	}

	if (ghost != _ramp_ghost) {
		if (_ramp_ghost.tile != INVALID_TILE) MarkTileDirtyByTile(_ramp_ghost.tile);
		_ramp_ghost = ghost;
		if (_ramp_ghost.tile != INVALID_TILE) MarkTileDirtyByTile(_ramp_ghost.tile);
	}

	/* The portal tool works from the surface and from the views below ground. */
	UndergroundPortalGhost portal;
	if (tool == WID_RAT_BUILD_PORTAL && placing) {
		TileIndex tile;
		DiagDirection dir;
		bool dragged = GetRailToolGhostSpot(tile, dir);
		if (IsValidTile(tile) && IsValidDiagDirection(dir)) {
			portal = MakePortalGhost(tile, dir, dragged, railtype);
			Backup<CompanyID> cur_company(_current_company, _local_company);
			portal.buildable = Command<Commands::BuildPortal>::Do(DoCommandFlag::Auto, tile, railtype, portal.command_dir, portal.depth).Succeeded();
			cur_company.Restore();
		}
	}

	if (portal != _portal_ghost) {
		MarkPortalGhostDirty(_portal_ghost);
		_portal_ghost = portal;
		MarkPortalGhostDirty(_portal_ghost);
	}
}

/**
 * The portal the portal tool shows on a tile before it is built.
 * @param tile The tile.
 * @return The portal, or nullptr when none is shown on the tile.
 */
const UndergroundPortalGhost *GetUndergroundPortalGhost(TileIndex tile)
{
	if (_portal_ghost.tile != tile || _thd.window_class != WC_BUILD_TOOLBAR || (_thd.place_mode & HT_DRAG_MASK) == HT_NONE) return nullptr;
	return &_portal_ghost;
}

/**
 * The ramp a ramp tool shows on a tile before it is built.
 * @param tile The tile.
 * @return The ramp, or nullptr when none is shown on the tile.
 */
const UndergroundRampGhost *GetUndergroundRampGhost(TileIndex tile)
{
	if (_ramp_ghost.tile != tile || _thd.window_class != WC_BUILD_TOOLBAR || (_thd.place_mode & HT_DRAG_MASK) == HT_NONE) return nullptr;
	if (!IsUndergroundSliceMode() || _ramp_ghost.entry_depth != GetUndergroundViewDepth()) return nullptr;
	return &_ramp_ghost;
}

/**
 * Build an underground platform area, joining stations like vanilla rail stations do:
 * - a plain click joins the one own station with a platform next to the area on
 *   this level (or builds a new one when there is none);
 * - when several stations adjoin, or a station has platforms on another level in
 *   the same columns, the "Join station" window lets the player pick one or build
 *   a separate station (with distant joins enabled; otherwise the command decides);
 * - ctrl-click opens the window with every station in distant-join range, or
 *   builds a separate station when distant joins are disabled.
 */
static void PostBuildUndergroundStationArea(TileArea area, RailType railtype, int16_t depth, Axis axis)
{
	uint8_t numtracks = static_cast<uint8_t>((axis == AXIS_X) ? area.h : area.w);
	uint8_t plat_len = static_cast<uint8_t>((axis == AXIS_X) ? area.w : area.h);

	/* to_join: StationID::Invalid() = join adjacent, NEW_STATION = build separate, else the station. */
	auto proc = [=](bool test, StationID to_join) -> bool {
		uint16_t station_id = to_join == StationID::Invalid() ? UNDERGROUND_JOIN_ADJACENT_ID
			: to_join == NEW_STATION ? NEW_UNDERGROUND_STATION_ID : to_join.base();
		if (test) {
			return Command<Commands::BuildUndergroundStation>::Do(CommandFlagsToDCFlags(GetCommandFlags<Commands::BuildUndergroundStation>()),
				area.tile, railtype, depth, station_id, axis, numtracks, plat_len).Succeeded();
		}
		return Command<Commands::BuildUndergroundStation>::Post(STR_ERROR_CAN_T_BUILD_UNDERGROUND_STATION,
			area.tile, railtype, depth, station_id, axis, numtracks, plat_len);
	};

	const bool distant_join = _settings_game.station.distant_join_stations;
	std::vector<StationID> adjacent = FindAdjacentUndergroundStations(area, depth, _local_company);

	if (_ctrl_pressed) {
		/* Vanilla ctrl-click: pick from all joinable stations, or build separately. */
		if (!distant_join || !proc(true, NEW_STATION)) {
			proc(false, NEW_STATION);
			return;
		}
		ShowSelectStationFromCandidates(area, [area]() {
			std::vector<StationID> list{NEW_STATION};
			for (StationID id : FindUndergroundJoinCandidates(area, _local_company)) list.push_back(id);
			return list;
		}, proc);
		return;
	}

	std::vector<StationID> nearby = adjacent;
	for (StationID id : FindUndergroundStationsNearColumns(area, depth, _local_company)) {
		if (std::ranges::find(nearby, id) == nearby.end()) nearby.push_back(id);
	}

	/* Unambiguous: nothing around, or exactly one same-level neighbour. */
	bool ambiguous = adjacent.size() > 1 || nearby.size() > adjacent.size();
	if (!ambiguous) {
		proc(false, StationID::Invalid());
		return;
	}

	if (distant_join) {
		if (!proc(true, NEW_STATION)) {
			proc(false, StationID::Invalid());
			return;
		}
		ShowSelectStationFromCandidates(area, [area, nearby]() {
			std::vector<StationID> list{NEW_STATION};
			for (StationID id : FindUndergroundJoinCandidates(area, _local_company)) {
				if (std::ranges::find(nearby, id) != nearby.end()) list.push_back(id);
			}
			return list;
		}, proc);
	} else if (adjacent.size() > 1) {
		/* Only same-level neighbours can be joined without distant joins. */
		ShowSelectStationFromCandidates(area, [area, depth]() {
			std::vector<StationID> list{NEW_STATION};
			for (StationID id : FindAdjacentUndergroundStations(area, depth, _local_company)) list.push_back(id);
			return list;
		}, proc);
	} else {
		proc(false, StationID::Invalid());
	}
}

struct UndergroundStationBuilderWindow : public PickerWindowBase {
private:
	uint coverage_height = 0;
	RailType railtype = RAILTYPE_RAIL;

	void UpdateTileSelection() const
	{
		ClampUndergroundStationSettings();

		if (_settings_client.gui.station_dragdrop) {
			SetTileSelectSize(1, 1);
		} else {
			int width = _settings_client.gui.station_numtracks;
			int height = _settings_client.gui.station_platlength;
			if (_underground_station_axis == AXIS_X) std::swap(width, height);
			SetTileSelectSize(width, height);
		}

		if (_settings_client.gui.station_show_coverage) {
			int rad = (_settings_game.station.modified_catchment) ? CA_TRAIN : CA_UNMODIFIED;
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		}
	}

public:
	UndergroundStationBuilderWindow(WindowDesc &desc, WindowNumber window_number, Window *parent, RailType railtype) :
		PickerWindowBase(desc, parent), railtype(railtype)
	{
		this->coverage_height = 2 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;
		this->CreateNestedTree();
		this->FinishInitNested(window_number);
		this->OnInit();
	}

	void SetRailType(RailType new_railtype)
	{
		this->railtype = new_railtype;
		this->SetDirty();
	}

	void OnInit() override
	{
		ClampUndergroundStationSettings();
		this->LowerWidget(WID_BRAS_PLATFORM_DIR_X + _underground_station_axis);
		if (_settings_client.gui.station_dragdrop) {
			this->LowerWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);
		} else {
			this->LowerWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
			this->LowerWidget(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength);
		}
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);
	}

	void Close(int data = 0) override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindowBase::Close(data);
	}

	void OnPaint() override
	{
		this->UpdateTileSelection();

		for (uint bits = 0; bits < 7; bits++) {
			bool disable = bits >= _settings_game.station.station_spread;
			this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_NUM_1, disable);
			this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_LEN_1, disable);
		}

		this->DrawWidgets();

		if (this->IsShaded()) return;

		Rect r = this->GetWidget<NWidgetBase>(WID_BRAS_COVERAGE_TEXTS)->GetCurrentRect();
		const int bottom = r.bottom;
		r.bottom = INT_MAX;
		int rad = (_settings_game.station.modified_catchment) ? CA_TRAIN : CA_UNMODIFIED;
		r.top = DrawStationCoverageAreaText(r, SCT_ALL, rad, false) + WidgetDimensions::scaled.vsep_normal;
		r.top = DrawStationCoverageAreaText(r, SCT_ALL, rad, true);
		if (r.top > bottom) {
			this->coverage_height += r.top - bottom;
			this->ReInit();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
				size.width = ScaleGUITrad(PickerWindow::PREVIEW_WIDTH) + WidgetDimensions::scaled.fullbevel.Horizontal();
				size.height = ScaleGUITrad(PickerWindow::PREVIEW_HEIGHT) + WidgetDimensions::scaled.fullbevel.Vertical();
				break;

			case WID_BRAS_COVERAGE_TEXTS:
				size.height = this->coverage_height;
				break;

			default:
				this->PickerWindowBase::UpdateWidgetSize(widget, size, padding, fill, resize);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		DrawPixelInfo tmp_dpi;

		switch (widget) {
			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width() - ScaleSpriteTrad(PickerWindow::PREVIEW_WIDTH)) / 2 + ScaleSpriteTrad(PickerWindow::PREVIEW_LEFT);
					int y = (ir.Height() + ScaleSpriteTrad(PickerWindow::PREVIEW_HEIGHT)) / 2 - ScaleSpriteTrad(PickerWindow::PREVIEW_BOTTOM);
					Axis axis = (widget == WID_BRAS_PLATFORM_DIR_X) ? AXIS_X : AXIS_Y;
					if (!DrawStationTile(x, y, this->railtype, axis, STAT_CLASS_DFLT, 0)) {
						StationPickerDrawSprite(x, y, StationType::Rail, this->railtype, INVALID_ROADTYPE, 2 + axis);
					}
				}
				break;
			}

			default:
				this->PickerWindowBase::DrawWidget(r, widget);
				break;
		}
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
				this->RaiseWidget(WID_BRAS_PLATFORM_DIR_X + _underground_station_axis);
				_underground_station_axis = static_cast<Axis>(widget - WID_BRAS_PLATFORM_DIR_X);
				this->LowerWidget(WID_BRAS_PLATFORM_DIR_X + _underground_station_axis);
				SndClickBeep();
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_PLATFORM_NUM_1:
			case WID_BRAS_PLATFORM_NUM_2:
			case WID_BRAS_PLATFORM_NUM_3:
			case WID_BRAS_PLATFORM_NUM_4:
			case WID_BRAS_PLATFORM_NUM_5:
			case WID_BRAS_PLATFORM_NUM_6:
			case WID_BRAS_PLATFORM_NUM_7:
				this->RaiseWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);
				_settings_client.gui.station_numtracks = widget - WID_BRAS_PLATFORM_NUM_BEGIN;
				_settings_client.gui.station_dragdrop = false;
				this->LowerWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
				this->LowerWidget(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength);
				SndClickBeep();
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_PLATFORM_LEN_1:
			case WID_BRAS_PLATFORM_LEN_2:
			case WID_BRAS_PLATFORM_LEN_3:
			case WID_BRAS_PLATFORM_LEN_4:
			case WID_BRAS_PLATFORM_LEN_5:
			case WID_BRAS_PLATFORM_LEN_6:
			case WID_BRAS_PLATFORM_LEN_7:
				this->RaiseWidget(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);
				_settings_client.gui.station_platlength = widget - WID_BRAS_PLATFORM_LEN_BEGIN;
				_settings_client.gui.station_dragdrop = false;
				this->LowerWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
				this->LowerWidget(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength);
				SndClickBeep();
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_PLATFORM_DRAG_N_DROP:
				_settings_client.gui.station_dragdrop ^= true;
				this->ToggleWidgetLoweredState(WID_BRAS_PLATFORM_DRAG_N_DROP);
				this->SetWidgetLoweredState(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks, !_settings_client.gui.station_dragdrop);
				this->SetWidgetLoweredState(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength, !_settings_client.gui.station_dragdrop);
				SndClickBeep();
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_HIGHLIGHT_OFF:
			case WID_BRAS_HIGHLIGHT_ON:
				_settings_client.gui.station_show_coverage = (widget != WID_BRAS_HIGHLIGHT_OFF);
				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);
				SndClickBeep();
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			default:
				this->PickerWindowBase::OnClick(pt, widget, click_count);
				break;
		}
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_underground_station_builder_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetStringTip(STR_STATION_BUILD_RAIL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0), SetPadding(WidgetDimensions::unscaled.picker),
			NWidget(WWT_LABEL, INVALID_COLOUR), SetMinimalSize(144, 11), SetFill(1, 0), SetStringTip(STR_STATION_BUILD_ORIENTATION),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_X), SetFill(0, 0), SetToolTip(STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_Y), SetFill(0, 0), SetToolTip(STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(WWT_LABEL, INVALID_COLOUR), SetMinimalSize(144, 11), SetFill(1, 0), SetStringTip(STR_STATION_BUILD_NUMBER_OF_TRACKS),
			NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_1), SetAspect(1.25f), SetStringTip(STR_BLACK_1, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_2), SetAspect(1.25f), SetStringTip(STR_BLACK_2, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_3), SetAspect(1.25f), SetStringTip(STR_BLACK_3, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_4), SetAspect(1.25f), SetStringTip(STR_BLACK_4, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_5), SetAspect(1.25f), SetStringTip(STR_BLACK_5, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_6), SetAspect(1.25f), SetStringTip(STR_BLACK_6, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_7), SetAspect(1.25f), SetStringTip(STR_BLACK_7, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			EndContainer(),
			NWidget(WWT_LABEL, INVALID_COLOUR), SetMinimalSize(144, 11), SetFill(1, 0), SetStringTip(STR_STATION_BUILD_PLATFORM_LENGTH),
			NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_1), SetAspect(1.25f), SetStringTip(STR_BLACK_1, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_2), SetAspect(1.25f), SetStringTip(STR_BLACK_2, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_3), SetAspect(1.25f), SetStringTip(STR_BLACK_3, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_4), SetAspect(1.25f), SetStringTip(STR_BLACK_4, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_5), SetAspect(1.25f), SetStringTip(STR_BLACK_5, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_6), SetAspect(1.25f), SetStringTip(STR_BLACK_6, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_7), SetAspect(1.25f), SetStringTip(STR_BLACK_7, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_DRAG_N_DROP), SetMinimalSize(75, 12), SetStringTip(STR_STATION_BUILD_DRAG_DROP, STR_STATION_BUILD_DRAG_DROP_TOOLTIP),
			EndContainer(),
			NWidget(WWT_LABEL, INVALID_COLOUR), SetStringTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE), SetFill(1, 0),
			NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_OFF), SetMinimalSize(60, 12), SetStringTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_ON), SetMinimalSize(60, 12), SetStringTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			EndContainer(),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BRAS_COVERAGE_TEXTS), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, 0),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _underground_station_builder_desc(
	WDP_AUTO, "build_station_underground", 0, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WindowDefaultFlag::Construction,
	_nested_underground_station_builder_widgets
);

void ShowUndergroundStationBuilder(Window *parent, RailType railtype)
{
	auto *existing = dynamic_cast<UndergroundStationBuilderWindow *>(BringWindowToFrontById(WC_BUILD_STATION, WN_UNDERGROUND_STATION_BUILDER));
	if (existing != nullptr) {
		existing->parent = parent;
		existing->SetRailType(railtype);
		return;
	}

	new UndergroundStationBuilderWindow(_underground_station_builder_desc, WN_UNDERGROUND_STATION_BUILDER, parent, railtype);
}

void SetUndergroundStationBuilderRailType(RailType railtype)
{
	auto *w = dynamic_cast<UndergroundStationBuilderWindow *>(FindWindowById(WC_BUILD_STATION, WN_UNDERGROUND_STATION_BUILDER));
	if (w != nullptr) w->SetRailType(railtype);
}

void CloseUndergroundStationBuilder()
{
	CloseWindowById(WC_BUILD_STATION, WN_UNDERGROUND_STATION_BUILDER);
}

/**
 * Build a surface exit for one of the local company's underground stations.
 * The candidates are the stations with a platform within
 * construction.max_station_exit_distance; one candidate is built directly,
 * several open the "Join station" window (without "Build a separate station":
 * an exit always belongs to a station).
 * @param tile      Tile the exit is to be built on.
 * @param exit_type Pavilion or underpass.
 */
static void PlaceUndergroundStationExit(TileIndex tile, StationExitType exit_type)
{
	std::vector<StationID> candidates = FindUndergroundStationsForExit(tile, _local_company);
	if (candidates.empty()) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_BUILD_STATION_EXIT), GetEncodedString(STR_ERROR_UNDERGROUND_NO_STATION_HERE),
			WL_INFO, TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE);
		return;
	}

	auto proc = [tile, exit_type](bool test, StationID to_join) -> bool {
		if (!Station::IsValidID(to_join)) return false;
		if (test) {
			return Command<Commands::BuildExitSurface>::Do(CommandFlagsToDCFlags(GetCommandFlags<Commands::BuildExitSurface>()), tile, to_join.base(), exit_type).Succeeded();
		}
		return Command<Commands::BuildExitSurface>::Post(STR_ERROR_CAN_T_BUILD_STATION_EXIT, tile, to_join.base(), exit_type);
	};

	if (candidates.size() == 1) {
		proc(false, candidates.front());
		return;
	}

	ShowSelectStationFromCandidates(TileArea(tile, 1, 1), [tile]() {
		return FindUndergroundStationsForExit(tile, _local_company);
	}, proc);
}

/**
 * Build a portal with the portal tool of the railway construction toolbar,
 * the one its ghost showed (see MakePortalGhost): on flat ground it dives
 * into the ground towards the tile edge clicked, or towards the drag; on a
 * hillside the incline decides. The viewed level follows the portal's level.
 */
static void PlaceUndergroundPortal(TileIndex start_tile, TileIndex end_tile, RailType railtype)
{
	bool dragged = start_tile != end_tile;
	DiagDirection dir = dragged ? DiagdirBetweenTiles(start_tile, end_tile) : _ramp_click_dir;
	if (!IsValidDiagDirection(dir)) return;

	UndergroundPortalGhost portal = MakePortalGhost(start_tile, dir, dragged, railtype);
	SetUndergroundViewDepth(portal.depth);
	Command<Commands::BuildPortal>::Post(STR_ERROR_CAN_T_BUILD_PORTAL, start_tile, railtype, portal.command_dir, portal.depth);
}

/**
 * Place, convert or remove signals on the viewed level: a click places one
 * signal on the track piece under the cursor, a drag places signals along the
 * line every few tiles.
 */
static void PlaceUndergroundSignals(TileIndex start_tile, TileIndex end_tile, Track track, int16_t depth, bool remove)
{
	if (start_tile != end_tile) {
		if (remove) {
			Command<Commands::RemoveUndergroundSignalTrack>::Post(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM, end_tile, start_tile, track, depth);
		} else {
			SignalToolSelection sel = GetSignalToolSelection(std::nullopt);
			Command<Commands::BuildUndergroundSignalTrack>::Post(STR_ERROR_CAN_T_BUILD_SIGNALS_HERE,
				end_tile, start_tile, track, depth, static_cast<uint8_t>(_settings_client.gui.drag_signals_density), sel.type, sel.variant);
		}
		return;
	}

	SliceID sid = _multilayer_map.FindSliceAt(start_tile, depth);
	if (sid == INVALID_SLICE_ID) return;
	const TileSlice &slice = _multilayer_map.GetSlice(sid);
	if (!CanSliceCarrySignals(slice)) return;

	/* A ramp carries its one track. */
	TrackBits trackbits = slice.kind == SliceKind::Ramp ? TrackToTrackBits(GetRampTrack(slice.ramp)) : static_cast<TrackBits>(slice.track.tracks);
	if (trackbits & TRACK_BIT_VERT) {
		trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
	}
	if (trackbits & TRACK_BIT_HORZ) {
		trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
	}
	Track t = FindFirstTrack(trackbits);
	if (remove) {
		Command<Commands::RemoveUndergroundSignal>::Post(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM, start_tile, t, depth);
		return;
	}
	/* What to build, convert to or cycle through, as GenericPlaceSignals decides it. */
	std::optional<SignalType> on_tile;
	if (IsValidTrack(t) && HasUndergroundSignalOnTrack(slice, t)) on_tile = GetUndergroundSignalType(slice, t);
	SignalToolSelection sel = GetSignalToolSelection(on_tile);
	Command<Commands::BuildUndergroundSignal>::Post(sel.convert ? STR_ERROR_SIGNAL_CAN_T_CONVERT_SIGNALS_HERE : STR_ERROR_CAN_T_BUILD_SIGNALS_HERE,
		start_tile, t, depth, sel.type, sel.variant, sel.convert, _ctrl_pressed, sel.cycle_start, sel.cycle_end, 0);
}

/**
 * Start using a tool of the railway construction toolbar where it works
 * differently on the viewed underground level, or is an underground tool.
 * @param tool The tool's button.
 * @param tile The tile clicked.
 * @param railtype The track type built.
 * @return False if the tool works like on the surface.
 */
bool UndergroundRailToolPlaceObject(WidgetID tool, TileIndex tile, RailType railtype)
{
	switch (tool) {
		case WID_RAT_BUILD_EXIT_PAVILION:
		case WID_RAT_BUILD_EXIT_UNDERPASS:
			PlaceUndergroundStationExit(tile, tool == WID_RAT_BUILD_EXIT_PAVILION ? StationExitType::Pavilion : StationExitType::Underpass);
			return true;

		case WID_RAT_BUILD_PORTAL:
		case WID_RAT_BUILD_RAMP_DOWN:
		case WID_RAT_BUILD_RAMP_UP:
			/* Towards the tile edge nearest to the click, as the ghost showed it;
			 * a drag along a line leads it towards the drag instead. */
			_ramp_click_dir = GetNearestTileEdge(_tile_fract_coords);
			VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_RAIL);
			return true;

		default:
			break;
	}

	/* Only the cut-plane views build below ground: X-ray only shows it. */
	if (!IsUndergroundSliceMode()) return false;

	switch (tool) {
		case WID_RAT_BUILD_STATION:
			ClampUndergroundStationSettings();
			if (_settings_client.gui.station_dragdrop) {
				VpStartPlaceSizing(tile, _underground_station_axis == AXIS_X ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_STATION);
				VpSetPlaceSizingLimit(_settings_game.station.station_spread);
			} else {
				int width = _settings_client.gui.station_numtracks;
				int height = _settings_client.gui.station_platlength;
				if (_underground_station_axis == AXIS_X) std::swap(width, height);
				PostBuildUndergroundStationArea(TileArea(tile, width, height), railtype, GetUndergroundViewDepth(), _underground_station_axis);
			}
			return true;

		case WID_RAT_DEMOLISH:
			/* Ctrl demolishes a surface area like the landscaping dynamite;
			 * the X-by-Y method tells it apart on mouse up. */
			VpStartPlaceSizing(tile, _ctrl_pressed ? VPM_X_AND_Y : VPM_RAILDIRS, DDSP_DEMOLISH_AREA);
			return true;

		default:
			/* Track, signals and conversion are dragged like on the surface. */
			return false;
	}
}

/**
 * Finish using a tool of the railway construction toolbar where it works
 * differently on the viewed underground level, or is an underground tool.
 * @param tool The tool's button.
 * @param select_method The method of the drag.
 * @param select_proc What the drag was for.
 * @param start_tile The tile the drag started at.
 * @param end_tile The tile the drag ended at.
 * @param railtype The track type built.
 * @param remove The remove button is down.
 * @return False if the tool works like on the surface.
 */
bool UndergroundRailToolPlaceMouseUp(WidgetID tool, ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc,
	TileIndex start_tile, TileIndex end_tile, RailType railtype, bool remove)
{
	Track track = static_cast<Track>(_thd.drawstyle & HT_DIR_MASK);
	int16_t depth = GetUndergroundViewDepth();

	switch (tool) {
		case WID_RAT_BUILD_PORTAL:
			PlaceUndergroundPortal(start_tile, end_tile, railtype);
			return true;

		case WID_RAT_BUILD_RAMP_DOWN:
		case WID_RAT_BUILD_RAMP_UP: {
			/* From the viewed level to the one below or above it, on the first
			 * tile, leading towards the edge clicked or towards the second tile. */
			DiagDirection dir = (start_tile != end_tile) ? DiagdirBetweenTiles(start_tile, end_tile) : _ramp_click_dir;
			if (!IsValidDiagDirection(dir)) return true;
			UndergroundRampGhost ramp = MakeRampGhost(tool, start_tile, dir, railtype);
			Command<Commands::BuildRamp>::Post(STR_ERROR_CAN_T_BUILD_RAMP, ramp.tile, ramp.railtype, ramp.dir, ramp.entry_depth, ramp.exit_depth);
			return true;
		}

		default:
			break;
	}

	if (!IsUndergroundSliceMode()) return false;

	switch (select_proc) {
		case DDSP_PLACE_RAIL:
			/* Along the whole dragged line, vanilla style: straight runs tile by
			 * tile, corner tracks zig-zag. Existing pieces along the way are free no-ops. */
			if (remove) {
				Command<Commands::RemoveUndergroundRailroadTrack>::Post(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK, end_tile, start_tile, track, depth);
			} else {
				Command<Commands::BuildUndergroundRailroadTrack>::Post(STR_ERROR_CAN_T_BUILD_UNDERGROUND_RAIL, end_tile, start_tile, railtype, track, depth);
			}
			return true;

		case DDSP_BUILD_SIGNALS:
			PlaceUndergroundSignals(start_tile, end_tile, track, depth, remove);
			return true;

		case DDSP_CONVERT_RAIL:
			Command<Commands::ConvertUndergroundRail>::Post(STR_ERROR_CAN_T_CONVERT_RAIL, end_tile, start_tile, railtype, depth);
			return true;

		case DDSP_DEMOLISH_AREA:
			if (select_method == VPM_X_AND_Y) {
				Command<Commands::ClearArea>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, CcPlaySound_EXPLOSION, end_tile, start_tile, false);
			} else {
				Command<Commands::RemoveUndergroundArea>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, start_tile, end_tile, depth);
			}
			return true;

		case DDSP_BUILD_STATION:
			/* A full platform rectangle in the chosen station orientation. */
			PostBuildUndergroundStationArea(TileArea(start_tile, end_tile), railtype, depth, _underground_station_axis);
			return true;

		default:
			return false;
	}
}

void ShowUndergroundToolbar()
{
	Window *w = FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);
	if (w == nullptr) w = ShowBuildRailToolbar(_last_built_railtype);
	if (w == nullptr) return;

	/* The toolbar builds on the viewed level; go below the surface. */
	if (!IsUndergroundSliceMode()) SetUndergroundViewMode(UndergroundViewMode::Slice);
}

/** The view the first of the view buttons pressed in a row was pressed in: the ordinary or the slice view. */
static UndergroundViewMode _view_before_button = UndergroundViewMode::Off;

void ToggleUndergroundViewButton(UndergroundViewMode mode)
{
	if (_underground_view_mode != mode) {
		/* From one of these views to another, back still leads to where the first was pressed. */
		if (_underground_view_mode == UndergroundViewMode::Off || _underground_view_mode == UndergroundViewMode::Slice) {
			_view_before_button = _underground_view_mode;
		}
		SetUndergroundViewMode(mode);
		return;
	}
	/* The slice view is for building; without the railway toolbar, back to the surface. */
	bool building = FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL) != nullptr;
	SetUndergroundViewMode(building ? _view_before_button : UndergroundViewMode::Off);
}

/** The soil tooltip of the metro tools while it is shown; another tooltip (a drag's measurement) replaces it. */
static const Window *_soil_tooltip = nullptr;

void UpdateUndergroundSoilTooltip(TileIndex tile)
{
	auto hide = []() {
		/* Close only our own tooltip, never one that has replaced it since. */
		Window *w = FindWindowById(WC_TOOLTIPS, 0);
		if (w != nullptr && w == _soil_tooltip) w->Close();
		_soil_tooltip = nullptr;
	};

	bool tool = _settings_client.gui.measure_tooltip && IsUndergroundSliceMode() &&
			_thd.window_class == WC_BUILD_TOOLBAR && _thd.window_number == TRANSPORT_RAIL &&
			(_thd.place_mode & HT_DRAG_MASK) != HT_NONE;
	if (!tool || !IsValidTile(tile)) return hide();

	int16_t level = GetUndergroundViewDepth();
	if (level >= GetMetroSurfaceZ(tile)) return hide();

	SoilLayer soil = GetSoilLayer(tile, level);
	GuiShowTooltips(_thd.GetCallbackWnd(), GetEncodedString(STR_UNDERGROUND_SOIL_TOOLTIP, GetSoilLayerName(soil), GetDepthBelowSurface(tile, level),
			GetUndergroundSoilMultiplier(UndergroundCostKind::Track, soil), 1, GetUndergroundSoilMultiplier(UndergroundCostKind::Station, soil), 1), TCC_EXIT_VIEWPORT);
	_soil_tooltip = FindWindowById(WC_TOOLTIPS, 0);
}
