/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint_gui.cpp Window and console command for metro blueprints. */

#include "../stdafx.h"
#include "blueprint_gui.h"
#include "blueprint.h"
#include "underground_cmd.h"
#include "underground_view.h"
#include "../company_func.h"
#include "../console_func.h"
#include "../error.h"
#include "../gfx_func.h"
#include "../hotkeys.h"
#include "../map_func.h"
#include "../rail.h"
#include "../rail_gui.h"
#include "../toolbar_gui.h"
#include "../core/geometry_func.hpp"
#include "../string_func.h"
#include "../strings_func.h"
#include "../textbuf_gui.h"
#include "../tilehighlight_func.h"
#include "../viewport_func.h"
#include "../window_func.h"
#include "../window_gui.h"
#include "../core/string_consumer.hpp"

#include "../table/sprites.h"
#include "../table/strings.h"

#include "../safeguards.h"

std::optional<std::string> GetClipboardContents();
#ifdef _WIN32
bool SetClipboardContents(std::string_view text);
#else
/** The system clipboard can only be read on this platform. */
static bool SetClipboardContents(std::string_view) { return false; }
#endif

/** Rotation and mirroring the clipboard is pasted with. */
static BlueprintTransform _bp_transform;
/** Copy every level with construction in the area. */
static bool _bp_auto_levels = true;
/** Highest level copied when not automatic. */
static int16_t _bp_top = -1;
/** Lowest level copied when not automatic. */
static int16_t _bp_bottom = -1;
/** Copy the surface too. */
static bool _bp_surface = false;
/** Join pasted stations to an adjacent own station. */
static bool _bp_join = false;
/** Build what fits instead of all or nothing. */
static bool _bp_best_effort = false;
/** Build with the track type of the railway construction toolbar. */
static bool _bp_toolbar_railtype = false;

/** The clipboard as it is pasted: turned and mirrored, its pieces with the rail types they are built with, sorted by tile. */
static std::optional<Blueprint> _bp_preview;
/** First piece of each tile of the preview's footprint (row by row), and the end of the pieces. */
static std::vector<uint32_t> _bp_preview_cells;
/** The paste tool is in use. */
static bool _bp_pasting = false;

static void UpdateBlueprintPreview()
{
	const Blueprint *bp = GetBlueprintClipboard();
	if (bp == nullptr) {
		_bp_preview.reset();
		return;
	}
	_bp_preview = *bp;
	_bp_preview->Transform(_bp_transform);
	for (BlueprintItem &item : _bp_preview->items) {
		if (!item.HasRailType()) continue;
		RailType rt = _bp_toolbar_railtype ? _last_built_railtype : GetRailTypeByLabel(bp->railtypes[item.railtype]);
		item.railtype = static_cast<uint8_t>(rt == INVALID_RAILTYPE ? _last_built_railtype : rt);
	}

	/* The ghost looks the pieces of a tile up for every tile it draws. */
	const uint w = _bp_preview->w;
	auto cell = [w](const BlueprintItem &i) { return i.y * w + i.x; };
	std::ranges::stable_sort(_bp_preview->items, {}, cell);
	_bp_preview_cells.assign(w * _bp_preview->h + 1, 0);
	for (const BlueprintItem &i : _bp_preview->items) _bp_preview_cells[cell(i) + 1]++;
	for (size_t c = 1; c < _bp_preview_cells.size(); c++) _bp_preview_cells[c] += _bp_preview_cells[c - 1];
}

/** How the clipboard is pasted, as chosen in the window; Ctrl joins stations too. */
static BlueprintPasteSpec GetBlueprintPasteSpec()
{
	BlueprintPasteSpec spec;
	spec.transform = _bp_transform;
	if (_bp_toolbar_railtype) spec.railtype = _last_built_railtype;
	if (_bp_join || _ctrl_pressed) spec.options |= BPO_JOIN_ADJACENT;
	if (_bp_best_effort) spec.options |= BPO_BEST_EFFORT;
	return spec;
}

/** Turn the clipboard a quarter (left: north-east becomes north-west). */
static void RotateBlueprint(bool left)
{
	_bp_transform.rotation = (_bp_transform.rotation + (left ? 1 : 3)) % 4;
	UpdateBlueprintPreview();
}

/** Mirror the clipboard as it is pasted now. */
static void MirrorBlueprint()
{
	/* Mirroring after a turn is the opposite turn after mirroring. */
	_bp_transform.rotation = (4 - _bp_transform.rotation) % 4;
	_bp_transform.mirror = !_bp_transform.mirror;
	UpdateBlueprintPreview();
}

/**
 * Copy an area into the clipboard with the levels chosen in the window.
 * The viewed level becomes the anchor level when it is copied, else the top level.
 * @return Whether anything was copied.
 */
static bool CopyBlueprintArea(TileIndex start, TileIndex end)
{
	BlueprintCaptureSpec spec;
	spec.start = start;
	spec.end = end;
	spec.surface = _bp_surface;
	int16_t view = GetUndergroundViewDepth();

	if (_bp_auto_levels) {
		auto range = FindUndergroundLevelRange(start, end);
		if (range.has_value()) {
			spec.z_bot = range->first;
			spec.z_top = range->second;
		} else {
			spec.z_bot = spec.z_top = view;
		}
	} else {
		spec.z_top = std::max(_bp_top, _bp_bottom);
		spec.z_bot = std::min(_bp_top, _bp_bottom);
	}
	spec.anchor = (view >= spec.z_bot && view <= spec.z_top) ? view : spec.z_top;

	Blueprint bp = CaptureBlueprint(spec);
	if (bp.IsEmpty()) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_COPY_BLUEPRINT), GetEncodedString(STR_ERROR_BLUEPRINT_NOTHING_TO_COPY), WL_INFO);
		return false;
	}
	_bp_transform = {};
	SetBlueprintClipboard(std::move(bp));
	UpdateBlueprintPreview();
	return true;
}

/** Take a blueprint from text, e.g. a share string, into the clipboard. */
static bool ImportBlueprintText(std::string_view text)
{
	std::string error;
	auto bp = ParseBlueprintText(text, &error);
	if (!bp.has_value()) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_LOAD_BLUEPRINT), GetEncodedString(STR_ERROR_BLUEPRINT_INVALID_TEXT, error), WL_INFO);
		return false;
	}
	_bp_transform = {};
	SetBlueprintClipboard(std::move(*bp));
	UpdateBlueprintPreview();
	return true;
}

/** Hotkeys of the blueprints window; numbered apart from the widgets, which hotkeys may also name. */
enum MetroBlueprintHotkeys : int {
	MBHK_OPEN = 100,
	MBHK_ROTATE_LEFT,
	MBHK_ROTATE_RIGHT,
	MBHK_MIRROR,
};

/** The metro blueprints window: copy and paste tools and the blueprints on disk. */
struct MetroBlueprintWindow : Window {
	Scrollbar *vscroll = nullptr;
	int selected = -1; ///< Selected entry of the list.

	MetroBlueprintWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MBP_SCROLLBAR);
		this->FinishInitNested(number);
		UpdateBlueprintPreview();
		this->OnInvalidateData();
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		_bp_pasting = false;
		if (_thd.window_class == WC_METRO_BLUEPRINTS) ResetObjectToPlace();
		this->Window::Close();
	}

	bool IsPasting() const { return this->IsWidgetLowered(WID_MBP_PASTE); }

	/** Size the tile highlight of the paste tool like the blueprint. */
	void UpdatePasteSize()
	{
		if (!this->IsPasting() || !_bp_preview.has_value()) return;
		SetTileSelectSize(_bp_preview->w, _bp_preview->h);
		MarkWholeScreenDirty();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		UpdateBlueprintPreview();
		const auto &library = GetBlueprintLibrary();
		this->vscroll->SetCount(library.size());
		if (this->selected >= static_cast<int>(library.size())) this->selected = -1;

		bool has_bp = _bp_preview.has_value();
		this->SetWidgetsDisabledState(!has_bp, WID_MBP_PASTE, WID_MBP_ROTATE_LEFT, WID_MBP_ROTATE_RIGHT, WID_MBP_MIRROR, WID_MBP_SAVE, WID_MBP_EXPORT);
		this->SetWidgetDisabledState(WID_MBP_DELETE, this->selected < 0 || !IsBlueprintDeletable(library[this->selected]));
		this->SetWidgetsDisabledState(_bp_auto_levels, WID_MBP_TOP_DOWN, WID_MBP_TOP_UP, WID_MBP_BOTTOM_DOWN, WID_MBP_BOTTOM_UP);
		this->SetWidgetLoweredState(WID_MBP_AUTO_LEVELS, _bp_auto_levels);
		this->SetWidgetLoweredState(WID_MBP_SURFACE, _bp_surface);
		this->SetWidgetLoweredState(WID_MBP_JOIN, _bp_join);
		this->SetWidgetLoweredState(WID_MBP_BEST_EFFORT, _bp_best_effort);
		this->SetWidgetLoweredState(WID_MBP_TOOLBAR_RAILTYPE, _bp_toolbar_railtype);
		if (!has_bp && this->IsPasting()) ResetObjectToPlace();
		this->UpdatePasteSize();
		this->SetDirty();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_MBP_TOP_TEXT: return GetString(STR_BLUEPRINT_LEVEL_TOP, _bp_top);
			case WID_MBP_BOTTOM_TEXT: return GetString(STR_BLUEPRINT_LEVEL_BOTTOM, _bp_bottom);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_MBP_TOP_TEXT:
			case WID_MBP_BOTTOM_TEXT: {
				Dimension d = maxdim(GetStringBoundingBox(GetString(STR_BLUEPRINT_LEVEL_TOP, -88)), GetStringBoundingBox(GetString(STR_BLUEPRINT_LEVEL_BOTTOM, -88)));
				d.width += padding.width;
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}
			case WID_MBP_INFO:
				size.height = std::max<uint>(size.height, 4 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical());
				break;
			case WID_MBP_LIST:
				fill.height = resize.height = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical();
				size.height = std::max<uint>(size.height, 6 * resize.height);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_MBP_INFO: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				const Blueprint *bp = GetBlueprintClipboard();
				if (bp == nullptr || !_bp_preview.has_value()) {
					DrawStringMultiLine(ir, STR_BLUEPRINT_INFO_NONE, TC_BLACK);
					break;
				}
				auto [lo, hi] = bp->GetLevelRange();
				std::string name = bp->name.empty() ? GetString(STR_BLUEPRINT_UNNAMED) : bp->name;
				std::string text = GetString(STR_BLUEPRINT_INFO, name, _bp_preview->w, _bp_preview->h, bp->items.size(), bp->num_stations);
				if (bp->HasUndergroundItems()) text += "\n" + GetString(STR_BLUEPRINT_INFO_LEVELS, lo, hi);
				if (bp->HasSurfaceItems()) text += "\n" + GetString(STR_BLUEPRINT_INFO_SURFACE);
				text += "\n" + GetString(_bp_transform.mirror ? STR_BLUEPRINT_INFO_TRANSFORM_MIRRORED : STR_BLUEPRINT_INFO_TRANSFORM, _bp_transform.rotation * 90);
				DrawStringMultiLine(ir, text, TC_BLACK);
				break;
			}

			case WID_MBP_LIST: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.matrix);
				const auto &library = GetBlueprintLibrary();
				if (library.empty()) {
					DrawString(ir.left, ir.right, ir.top, STR_BLUEPRINT_LIST_EMPTY);
					break;
				}
				int line = this->resize.step_height;
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(library);
				for (auto it = first; it != last; ++it) {
					int index = static_cast<int>(it - library.begin());
					if (index == this->selected) GfxFillRect(r.left + 1, ir.top, r.right - 1, ir.top + line - 1, PC_DARK_GREY);
					DrawString(ir.left, ir.right, ir.top, GetString(STR_BLUEPRINT_LIST_ENTRY, it->bp.name, it->bp.w, it->bp.h), index == this->selected ? TC_WHITE : TC_BLACK);
					ir.top += line;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MBP_COPY:
				HandlePlacePushButton(this, WID_MBP_COPY, SPR_CURSOR_QUERY, HT_RECT);
				break;

			case WID_MBP_PASTE:
				this->StartPasting();
				break;

			case WID_MBP_ROTATE_LEFT:
			case WID_MBP_ROTATE_RIGHT:
				RotateBlueprint(widget == WID_MBP_ROTATE_LEFT);
				this->OnInvalidateData();
				break;

			case WID_MBP_MIRROR:
				MirrorBlueprint();
				this->OnInvalidateData();
				break;

			case WID_MBP_AUTO_LEVELS: _bp_auto_levels = !_bp_auto_levels; this->OnInvalidateData(); break;
			case WID_MBP_SURFACE: _bp_surface = !_bp_surface; this->OnInvalidateData(); break;
			case WID_MBP_JOIN: _bp_join = !_bp_join; this->OnInvalidateData(); break;
			case WID_MBP_BEST_EFFORT: _bp_best_effort = !_bp_best_effort; this->OnInvalidateData(); break;
			case WID_MBP_TOOLBAR_RAILTYPE: _bp_toolbar_railtype = !_bp_toolbar_railtype; this->OnInvalidateData(); break;

			case WID_MBP_TOP_DOWN:
			case WID_MBP_TOP_UP:
			case WID_MBP_BOTTOM_DOWN:
			case WID_MBP_BOTTOM_UP: {
				int16_t &level = (widget == WID_MBP_TOP_DOWN || widget == WID_MBP_TOP_UP) ? _bp_top : _bp_bottom;
				int step = (widget == WID_MBP_TOP_UP || widget == WID_MBP_BOTTOM_UP) ? 1 : -1;
				level = Clamp<int16_t>(level + step, GetMetroGlobalMinZ(), GetMetroGlobalMaxZ());
				if (_bp_bottom > _bp_top) {
					if (&level == &_bp_top) _bp_bottom = _bp_top; else _bp_top = _bp_bottom;
				}
				this->SetDirty();
				break;
			}

			case WID_MBP_LIST: {
				const auto &library = GetBlueprintLibrary();
				auto it = this->vscroll->GetScrolledItemFromWidget(library, pt.y, this, WID_MBP_LIST, WidgetDimensions::scaled.matrix.top);
				if (it == library.end()) return;
				this->selected = static_cast<int>(it - library.begin());
				_bp_transform = {};
				SetBlueprintClipboard(it->bp);
				if (click_count > 1) this->StartPasting();
				this->OnInvalidateData();
				break;
			}

			case WID_MBP_SAVE: {
				const Blueprint *bp = GetBlueprintClipboard();
				if (bp == nullptr) return;
				ShowQueryString(bp->name, STR_BLUEPRINT_SAVE_QUERY, 64, this, CS_ALPHANUMERAL, {QueryStringFlag::LengthIsInChars});
				break;
			}

			case WID_MBP_DELETE: {
				const auto &library = GetBlueprintLibrary();
				if (this->selected < 0 || this->selected >= static_cast<int>(library.size())) return;
				ShowQuery(GetEncodedString(STR_BLUEPRINT_DELETE_QUERY_CAPTION), GetEncodedString(STR_BLUEPRINT_DELETE_QUERY_TEXT, library[this->selected].bp.name), this,
					[](Window *w, bool confirmed) {
						if (!confirmed) return;
						MetroBlueprintWindow *bw = static_cast<MetroBlueprintWindow *>(w);
						const auto &library = GetBlueprintLibrary();
						if (bw->selected < 0 || bw->selected >= static_cast<int>(library.size())) return;
						DeleteBlueprintFromDisk(library[bw->selected]);
						bw->selected = -1;
						bw->OnInvalidateData();
					});
				break;
			}

			case WID_MBP_RESCAN:
				ScanBlueprintLibrary();
				break;

			case WID_MBP_EXPORT: {
				const Blueprint *bp = GetBlueprintClipboard();
				if (bp == nullptr) return;
				std::string text = BlueprintToShareString(*bp);
				if (SetClipboardContents(text)) {
					ShowErrorMessage(GetEncodedString(STR_BLUEPRINT_EXPORTED), {}, WL_INFO);
				} else {
					IConsolePrint(CC_INFO, "{}", text);
					ShowErrorMessage(GetEncodedString(STR_BLUEPRINT_EXPORTED_CONSOLE), {}, WL_INFO);
				}
				break;
			}

			case WID_MBP_IMPORT: {
				auto text = GetClipboardContents();
				if (!text.has_value()) {
					ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_LOAD_BLUEPRINT), GetEncodedString(STR_ERROR_BLUEPRINT_INVALID_TEXT, std::string()), WL_INFO);
					return;
				}
				ImportBlueprintText(*text);
				break;
			}
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value() || str->empty()) return;
		const Blueprint *current = GetBlueprintClipboard();
		if (current == nullptr) return;
		Blueprint bp = *current;
		bp.name = *str;
		std::string error;
		if (!SaveBlueprintToDisk(bp, &error)) {
			ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_SAVE_BLUEPRINT), GetEncodedString(STR_ERROR_BLUEPRINT_INVALID_TEXT, error), WL_ERROR);
			return;
		}
		SetBlueprintClipboard(std::move(bp));
		const auto &library = GetBlueprintLibrary();
		for (size_t i = 0; i < library.size(); i++) {
			if (StrEqualsIgnoreCase(library[i].bp.name, *str)) this->selected = static_cast<int>(i);
		}
		this->OnInvalidateData();
	}

	/** Pick up the paste tool. */
	void StartPasting()
	{
		if (!_bp_preview.has_value()) return;
		if (HandlePlacePushButton(this, WID_MBP_PASTE, SPR_CURSOR_RAIL_STATION, HT_RECT)) {
			_bp_pasting = true;
			this->UpdatePasteSize();
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->IsWidgetLowered(WID_MBP_COPY)) {
			VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_COPY_BLUEPRINT);
			return;
		}
		if (this->IsPasting()) {
			const Blueprint *bp = GetBlueprintClipboard();
			if (bp == nullptr) return;
			PostBlueprintPaste(*bp, GetBlueprintPasteSpec(), tile, GetUndergroundViewDepth(), _shift_pressed);
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1 || select_proc != DDSP_COPY_BLUEPRINT) return;
		if (CopyBlueprintArea(start_tile, end_tile)) {
			ResetObjectToPlace();
			this->StartPasting();
		}
	}

	void OnPlaceObjectAbort() override
	{
		_bp_pasting = false;
		this->RaiseWidget(WID_MBP_COPY);
		this->RaiseWidget(WID_MBP_PASTE);
		this->SetDirty();
		MarkWholeScreenDirty();
	}

	EventState OnHotkey(int hotkey) override
	{
		/* The keys turn and mirror only while pasting, and are free for other windows otherwise. */
		switch (hotkey) {
			case MBHK_OPEN:
				return ES_HANDLED;
			case MBHK_ROTATE_LEFT:
			case MBHK_ROTATE_RIGHT:
				if (!_bp_preview.has_value() || !this->IsPasting()) return ES_NOT_HANDLED;
				RotateBlueprint(hotkey == MBHK_ROTATE_LEFT);
				this->OnInvalidateData();
				return ES_HANDLED;
			case MBHK_MIRROR:
				if (!_bp_preview.has_value() || !this->IsPasting()) return ES_NOT_HANDLED;
				MirrorBlueprint();
				this->OnInvalidateData();
				return ES_HANDLED;
			default:
				return this->Window::OnHotkey(hotkey);
		}
	}

	static EventState GlobalHotkeys(int hotkey)
	{
		if (_game_mode == GM_MENU || hotkey != MBHK_OPEN) return ES_NOT_HANDLED;
		ShowMetroBlueprintWindow();
		return ES_HANDLED;
	}

	static inline HotkeyList hotkeys{"metro_blueprints", {
		Hotkey(WKC_CTRL | 'B', "open", MBHK_OPEN),
		Hotkey('R', "rotate_left", MBHK_ROTATE_LEFT),
		Hotkey(WKC_SHIFT | 'R', "rotate_right", MBHK_ROTATE_RIGHT),
		Hotkey('M', "mirror", MBHK_MIRROR),
	}, MetroBlueprintWindow::GlobalHotkeys};
};

static constexpr std::initializer_list<NWidgetPart> _nested_metro_blueprint_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_MBP_CAPTION), SetStringTip(STR_BLUEPRINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.picker), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_COPY), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_COPY, STR_BLUEPRINT_COPY_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_PASTE), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_PASTE, STR_BLUEPRINT_PASTE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_MBP_ROTATE_LEFT), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_ROTATE_LEFT, STR_BLUEPRINT_ROTATE_LEFT_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_MBP_ROTATE_RIGHT), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_ROTATE_RIGHT, STR_BLUEPRINT_ROTATE_RIGHT_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_MBP_MIRROR), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_MIRROR, STR_BLUEPRINT_MIRROR_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXT, INVALID_COLOUR), SetFill(0, 1), SetStringTip(STR_BLUEPRINT_LEVELS),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_AUTO_LEVELS), SetStringTip(STR_BLUEPRINT_LEVELS_AUTO, STR_BLUEPRINT_LEVELS_AUTO_TOOLTIP),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_MBP_TOP_DOWN), SetArrowWidgetTypeTip(AWV_DECREASE, STR_BLUEPRINT_LEVEL_TOOLTIP),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_MBP_TOP_TEXT), SetFill(0, 1), SetToolTip(STR_BLUEPRINT_LEVEL_TOOLTIP), SetAlignment(SA_CENTER),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_MBP_TOP_UP), SetArrowWidgetTypeTip(AWV_INCREASE, STR_BLUEPRINT_LEVEL_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_MBP_BOTTOM_DOWN), SetArrowWidgetTypeTip(AWV_DECREASE, STR_BLUEPRINT_LEVEL_TOOLTIP),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_MBP_BOTTOM_TEXT), SetFill(0, 1), SetToolTip(STR_BLUEPRINT_LEVEL_TOOLTIP), SetAlignment(SA_CENTER),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_MBP_BOTTOM_UP), SetArrowWidgetTypeTip(AWV_INCREASE, STR_BLUEPRINT_LEVEL_TOOLTIP),
				EndContainer(),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_SURFACE), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_SURFACE, STR_BLUEPRINT_SURFACE_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_JOIN), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_JOIN, STR_BLUEPRINT_JOIN_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_BEST_EFFORT), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_BEST_EFFORT, STR_BLUEPRINT_BEST_EFFORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MBP_TOOLBAR_RAILTYPE), SetFill(1, 0), SetStringTip(STR_BLUEPRINT_TOOLBAR_RAILTYPE, STR_BLUEPRINT_TOOLBAR_RAILTYPE_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_MBP_INFO), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_MBP_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MBP_SCROLLBAR), SetToolTip(STR_BLUEPRINT_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_MBP_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, COLOUR_DARK_GREEN, WID_MBP_SAVE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_BLUEPRINT_SAVE, STR_BLUEPRINT_SAVE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_DARK_GREEN, WID_MBP_DELETE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_BLUEPRINT_DELETE, STR_BLUEPRINT_DELETE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_DARK_GREEN, WID_MBP_RESCAN), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_BLUEPRINT_RESCAN, STR_BLUEPRINT_RESCAN_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_DARK_GREEN, WID_MBP_EXPORT), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_BLUEPRINT_EXPORT, STR_BLUEPRINT_EXPORT_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_DARK_GREEN, WID_MBP_IMPORT), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_BLUEPRINT_IMPORT, STR_BLUEPRINT_IMPORT_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
};

static WindowDesc _metro_blueprint_desc(
	WDP_AUTO, "metro_blueprints", 0, 0,
	WC_METRO_BLUEPRINTS, WC_NONE,
	{},
	_nested_metro_blueprint_widgets,
	&MetroBlueprintWindow::hotkeys
);

void ShowMetroBlueprintWindow()
{
	AllocateWindowDescFront<MetroBlueprintWindow>(_metro_blueprint_desc, 0);
}

/**
 * The pieces the paste tool shows on a tile, as they are pasted with the
 * footprint at the cursor.
 * @param tile The tile.
 * @return The pieces; none when the paste tool is not in use or the tile lies outside of the footprint.
 */
std::span<const BlueprintItem> GetBlueprintPastePreview(TileIndex tile)
{
	if (!_bp_pasting || !_bp_preview.has_value() || _thd.window_class != WC_METRO_BLUEPRINTS || _thd.drawstyle == HT_NONE) return {};
	if (_thd.pos.x < 0 || _thd.pos.y < 0) return {};
	TileIndex origin = TileVirtXY(_thd.pos.x, _thd.pos.y);
	int dx = static_cast<int>(TileX(tile)) - static_cast<int>(TileX(origin));
	int dy = static_cast<int>(TileY(tile)) - static_cast<int>(TileY(origin));
	if (dx < 0 || dy < 0 || dx >= _bp_preview->w || dy >= _bp_preview->h) return {};
	uint cell = dy * _bp_preview->w + dx;
	return std::span<const BlueprintItem>(_bp_preview->items).subspan(_bp_preview_cells[cell], _bp_preview_cells[cell + 1] - _bp_preview_cells[cell]);
}

/* === Console === */

static void PrintBlueprintHelp()
{
	IConsolePrint(CC_HELP, "Metro blueprints: copies of underground (and surface rail) construction.");
	IConsolePrint(CC_HELP, "  'blueprint gui [paste]' - Open the blueprints window (also Ctrl+B), with the paste tool");
	IConsolePrint(CC_HELP, "  'blueprint copy <x1> <y1> <x2> <y2> <top> <bottom> [surface] [anchor]' - Copy the levels top..bottom of an area into the clipboard");
	IConsolePrint(CC_HELP, "  'blueprint paste <x> <y> <depth> [rotation 0-3] [mirror 0|1] [best] [join]' - Paste the clipboard, its anchor level at depth");
	IConsolePrint(CC_HELP, "  'blueprint list' - List the blueprints on disk; 'blueprint rescan' reads them again");
	IConsolePrint(CC_HELP, "  'blueprint load <name>' - Take a blueprint from disk into the clipboard");
	IConsolePrint(CC_HELP, "  'blueprint save <name>' - Save the clipboard to disk");
	IConsolePrint(CC_HELP, "  'blueprint show' - Print the clipboard as JSON; 'blueprint export' as a share string");
	IConsolePrint(CC_HELP, "  'blueprint import [text]' - Take a blueprint from JSON or a share string, without text from the system clipboard");
}

/** Parse a signed number of a console argument. */
static std::optional<int> ParseConsoleInt(std::string_view arg)
{
	if (arg.starts_with('-')) {
		auto v = ParseInteger<int>(arg.substr(1));
		if (!v.has_value()) return std::nullopt;
		return -*v;
	}
	return ParseInteger<int>(arg);
}

bool ConBlueprint(std::span<std::string_view> argv)
{
	if (argv.size() < 2) {
		PrintBlueprintHelp();
		return true;
	}
	std::string_view cmd = argv[1];

	if (StrEqualsIgnoreCase(cmd, "gui")) {
		ShowMetroBlueprintWindow();
		if (argv.size() > 2 && StrEqualsIgnoreCase(argv[2], "paste")) {
			MetroBlueprintWindow *w = dynamic_cast<MetroBlueprintWindow *>(FindWindowById(WC_METRO_BLUEPRINTS, 0));
			if (w != nullptr && !w->IsPasting()) w->StartPasting();
		}
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "list")) {
		for (const BlueprintLibraryEntry &e : GetBlueprintLibrary()) {
			IConsolePrint(CC_DEFAULT, "{} ({}x{}, {} pieces) - {}", e.bp.name, e.bp.w, e.bp.h, e.bp.items.size(), e.filename);
		}
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "rescan")) {
		ScanBlueprintLibrary();
		IConsolePrint(CC_DEFAULT, "{} blueprints on disk", GetBlueprintLibrary().size());
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "copy")) {
		if (argv.size() < 8) {
			PrintBlueprintHelp();
			return true;
		}
		std::array<std::optional<int>, 6> v;
		for (size_t i = 0; i < 6; i++) v[i] = ParseConsoleInt(argv[2 + i]);
		if (std::ranges::any_of(v, [](const auto &o) { return !o.has_value(); }) || *v[0] < 0 || *v[1] < 0 || *v[2] < 0 || *v[3] < 0 ||
				static_cast<uint>(*v[0]) >= Map::SizeX() || static_cast<uint>(*v[2]) >= Map::SizeX() || static_cast<uint>(*v[1]) >= Map::SizeY() || static_cast<uint>(*v[3]) >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid coordinates or levels.");
			return true;
		}
		BlueprintCaptureSpec spec;
		spec.start = TileXY(*v[0], *v[1]);
		spec.end = TileXY(*v[2], *v[3]);
		spec.z_top = static_cast<int16_t>(std::max(*v[4], *v[5]));
		spec.z_bot = static_cast<int16_t>(std::min(*v[4], *v[5]));
		spec.anchor = spec.z_top;
		for (size_t i = 8; i < argv.size(); i++) {
			if (StrEqualsIgnoreCase(argv[i], "surface")) {
				spec.surface = true;
			} else if (auto a = ParseConsoleInt(argv[i]); a.has_value()) {
				spec.anchor = static_cast<int16_t>(*a);
			}
		}
		Blueprint bp = CaptureBlueprint(spec);
		if (bp.IsEmpty()) {
			IConsolePrint(CC_ERROR, "Nothing to copy there.");
			return true;
		}
		IConsolePrint(CC_DEFAULT, "Copied {} pieces ({}x{}, {} stations).", bp.items.size(), bp.w, bp.h, bp.num_stations);
		_bp_transform = {};
		SetBlueprintClipboard(std::move(bp));
		UpdateBlueprintPreview();
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "paste")) {
		const Blueprint *bp = GetBlueprintClipboard();
		if (bp == nullptr) {
			IConsolePrint(CC_ERROR, "The clipboard is empty.");
			return true;
		}
		if (argv.size() < 5) {
			PrintBlueprintHelp();
			return true;
		}
		auto x = ParseConsoleInt(argv[2]), y = ParseConsoleInt(argv[3]), depth = ParseConsoleInt(argv[4]);
		if (!x.has_value() || !y.has_value() || !depth.has_value() || *x < 0 || *y < 0 || static_cast<uint>(*x) >= Map::SizeX() || static_cast<uint>(*y) >= Map::SizeY()) {
			IConsolePrint(CC_ERROR, "Invalid tile or depth.");
			return true;
		}
		BlueprintPasteSpec spec;
		if (argv.size() > 5) spec.transform.rotation = static_cast<uint8_t>(ParseConsoleInt(argv[5]).value_or(0) & 3);
		if (argv.size() > 6) spec.transform.mirror = ParseConsoleInt(argv[6]).value_or(0) != 0;
		for (size_t i = 7; i < argv.size(); i++) {
			if (StrEqualsIgnoreCase(argv[i], "best")) spec.options |= BPO_BEST_EFFORT;
			if (StrEqualsIgnoreCase(argv[i], "join")) spec.options |= BPO_JOIN_ADJACENT;
		}
		bool ok = PostBlueprintPaste(*bp, spec, TileXY(*x, *y), static_cast<int16_t>(*depth), false);
		IConsolePrint(ok ? CC_DEFAULT : CC_ERROR, ok ? "Blueprint pasted." : "Blueprint not pasted.");
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "load")) {
		if (argv.size() < 3) {
			PrintBlueprintHelp();
			return true;
		}
		const Blueprint *bp = FindBlueprint(argv[2]);
		if (bp == nullptr) {
			IConsolePrint(CC_ERROR, "No blueprint '{}' on disk.", argv[2]);
			return true;
		}
		_bp_transform = {};
		SetBlueprintClipboard(*bp);
		UpdateBlueprintPreview();
		IConsolePrint(CC_DEFAULT, "Blueprint '{}' in the clipboard.", argv[2]);
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "save")) {
		const Blueprint *current = GetBlueprintClipboard();
		if (current == nullptr || argv.size() < 3) {
			IConsolePrint(CC_ERROR, current == nullptr ? "The clipboard is empty." : "Give the blueprint a name.");
			return true;
		}
		Blueprint bp = *current;
		bp.name = argv[2];
		std::string error;
		if (!SaveBlueprintToDisk(bp, &error)) {
			IConsolePrint(CC_ERROR, "Blueprint not saved: {}", error);
			return true;
		}
		SetBlueprintClipboard(std::move(bp));
		IConsolePrint(CC_DEFAULT, "Blueprint '{}' saved.", argv[2]);
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "show") || StrEqualsIgnoreCase(cmd, "export")) {
		const Blueprint *bp = GetBlueprintClipboard();
		if (bp == nullptr) {
			IConsolePrint(CC_ERROR, "The clipboard is empty.");
			return true;
		}
		if (StrEqualsIgnoreCase(cmd, "show")) {
			std::string json = BlueprintToJson(*bp, true);
			std::string_view rest = json;
			while (!rest.empty()) {
				auto nl = rest.find('\n');
				IConsolePrint(CC_DEFAULT, "{}", rest.substr(0, nl));
				if (nl == std::string_view::npos) break;
				rest.remove_prefix(nl + 1);
			}
		} else {
			std::string text = BlueprintToShareString(*bp);
			IConsolePrint(CC_DEFAULT, "{}", text);
			if (SetClipboardContents(text)) IConsolePrint(CC_INFO, "Also on the system clipboard.");
		}
		return true;
	}

	if (StrEqualsIgnoreCase(cmd, "import")) {
		/* A console line is short; without text the blueprint comes from the system clipboard. */
		std::string text;
		for (size_t i = 2; i < argv.size(); i++) {
			if (i > 2) text += ' ';
			text += argv[i];
		}
		if (text.empty()) {
			auto contents = GetClipboardContents();
			if (!contents.has_value()) {
				IConsolePrint(CC_ERROR, "The system clipboard holds no text.");
				return true;
			}
			text = std::move(*contents);
		}
		std::string error;
		auto bp = ParseBlueprintText(text, &error);
		if (!bp.has_value()) {
			IConsolePrint(CC_ERROR, "Not a blueprint: {}", error);
			return true;
		}
		IConsolePrint(CC_DEFAULT, "Blueprint with {} pieces in the clipboard.", bp->items.size());
		_bp_transform = {};
		SetBlueprintClipboard(std::move(*bp));
		UpdateBlueprintPreview();
		return true;
	}

	PrintBlueprintHelp();
	return true;
}
