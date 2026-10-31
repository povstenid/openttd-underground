/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_xray_gui.cpp The window of the X-ray view: its layers and filters. The view is on while the window is open. */

#include "../stdafx.h"
#include "rail_xray.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../strings_func.h"
#include "../dropdown_type.h"
#include "../dropdown_func.h"
#include "../cargotype.h"
#include "../company_base.h"
#include "../gfx_func.h"
#include "../core/geometry_func.hpp"

#include "table/strings.h"

#include "../safeguards.h"

/** Widgets of the X-ray window. */
enum RailXRayWidgets : WidgetID {
	WID_XR_RAIL,           ///< Layer: railways.
	WID_XR_ROAD,           ///< Layer: roads and road vehicles.
	WID_XR_WATER,          ///< Layer: docks and ships.
	WID_XR_AIR,            ///< Layer: airports and aircraft.
	WID_XR_CARGO,          ///< Cargo and ratings at the stations.
	WID_XR_ORDER_COLOURS,  ///< Colour by shared orders.
	WID_XR_IMPLICIT,       ///< Implicit orders in the routes.
	WID_XR_DESTINATIONS,   ///< Dropdown: arrows to the destinations.
	WID_XR_ROUTES,         ///< Dropdown: routes of the order lists.
	WID_XR_CARGO_FILTER,   ///< Dropdown: cargo filter.
	WID_XR_COMPANY_FILTER, ///< Dropdown: company filter.
};

/** Names of the choices of the destination arrows, by #RailXRayDestinations. */
static const StringID _xray_destination_names[] = {STR_XRAY_DESTINATIONS_OFF, STR_XRAY_DESTINATIONS_SELECTED, STR_XRAY_DESTINATIONS_ALL};

/** Value of the "all companies" entry of the company filter dropdown. */
static constexpr int XRAY_ALL_COMPANIES = -1;

/** The X-ray window. */
struct RailXRayWindow : Window {
	RailXRayWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->OnInvalidateData();
	}

	void Close(int data = 0) override
	{
		RailXRayWindowClosed();
		this->Window::Close(data);
	}

	/** The setting a toggle button switches, or nullptr. */
	static bool *GetToggle(WidgetID widget)
	{
		switch (widget) {
			case WID_XR_RAIL: return &_xray_settings.rail;
			case WID_XR_ROAD: return &_xray_settings.road;
			case WID_XR_WATER: return &_xray_settings.water;
			case WID_XR_AIR: return &_xray_settings.air;
			case WID_XR_CARGO: return &_xray_settings.cargo;
			case WID_XR_ORDER_COLOURS: return &_xray_settings.order_colours;
			case WID_XR_IMPLICIT: return &_xray_settings.implicit_orders;
			default: return nullptr;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		/* A switched on toggle is lowered and green, so it shows at a glance. */
		for (WidgetID widget = WID_XR_RAIL; widget <= WID_XR_IMPLICIT; widget++) {
			bool on = *GetToggle(widget);
			this->SetWidgetLoweredState(widget, on);
			this->GetWidget<NWidgetCore>(widget)->colour = on ? COLOUR_GREEN : COLOUR_GREY;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_XR_DESTINATIONS:
				return GetString(_xray_destination_names[to_underlying(_rail_xray_destinations)]);

			case WID_XR_ROUTES:
				return GetString(_xray_destination_names[to_underlying(_xray_settings.routes)]);

			case WID_XR_CARGO_FILTER:
				if (!IsValidCargoType(_xray_settings.cargo_filter)) return GetString(STR_XRAY_FILTER_ALL);
				return GetString(CargoSpec::Get(_xray_settings.cargo_filter)->name);

			case WID_XR_COMPANY_FILTER:
				if (!Company::IsValidID(_xray_settings.company_filter)) return GetString(STR_XRAY_FILTER_ALL);
				return GetString(STR_COMPANY_NAME, _xray_settings.company_filter);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_XR_DESTINATIONS && widget != WID_XR_ROUTES && widget != WID_XR_CARGO_FILTER && widget != WID_XR_COMPANY_FILTER) return;
		Dimension d = GetStringBoundingBox(STR_XRAY_FILTER_ALL);
		for (StringID str : _xray_destination_names) d = maxdim(d, GetStringBoundingBox(str));
		for (const CargoSpec *cs : _sorted_standard_cargo_specs) d = maxdim(d, GetStringBoundingBox(cs->name));
		d.width += padding.width + NWidgetLeaf::dropdown_dimension.width;
		d.height += padding.height;
		size = maxdim(size, d);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (bool *toggle = GetToggle(widget); toggle != nullptr) {
			*toggle = !*toggle;
			this->OnInvalidateData();
			this->SetDirty();
			MarkWholeScreenDirty();
			return;
		}

		switch (widget) {
			case WID_XR_DESTINATIONS:
			case WID_XR_ROUTES: {
				DropDownList list;
				for (int i = 0; i < static_cast<int>(std::size(_xray_destination_names)); i++) {
					list.push_back(MakeDropDownListStringItem(_xray_destination_names[i], i));
				}
				RailXRayDestinations current = widget == WID_XR_ROUTES ? _xray_settings.routes : _rail_xray_destinations;
				ShowDropDownList(this, std::move(list), to_underlying(current), widget);
				break;
			}

			case WID_XR_CARGO_FILTER: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_XRAY_FILTER_ALL, INVALID_CARGO));
				for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
					list.push_back(MakeDropDownListStringItem(cs->name, cs->Index()));
				}
				ShowDropDownList(this, std::move(list), _xray_settings.cargo_filter, widget);
				break;
			}

			case WID_XR_COMPANY_FILTER: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_XRAY_FILTER_ALL, XRAY_ALL_COMPANIES));
				for (const Company *c : Company::Iterate()) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_COMPANY_NAME, c->index), c->index.base()));
				}
				int selected = Company::IsValidID(_xray_settings.company_filter) ? _xray_settings.company_filter.base() : XRAY_ALL_COMPANIES;
				ShowDropDownList(this, std::move(list), selected, widget);
				break;
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, [[maybe_unused]] int click_result) override
	{
		switch (widget) {
			case WID_XR_DESTINATIONS:
				_rail_xray_destinations = static_cast<RailXRayDestinations>(index);
				break;

			case WID_XR_ROUTES:
				_xray_settings.routes = static_cast<RailXRayDestinations>(index);
				break;

			case WID_XR_CARGO_FILTER:
				_xray_settings.cargo_filter = static_cast<CargoType>(index);
				break;

			case WID_XR_COMPANY_FILTER:
				_xray_settings.company_filter = index == XRAY_ALL_COMPANIES ? CompanyID::Invalid() : CompanyID(index);
				break;

			default: return;
		}
		this->SetDirty();
		MarkWholeScreenDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_rail_xray_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetStringTip(STR_XRAY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0), SetPadding(WidgetDimensions::unscaled.frametext),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_RAIL), SetFill(1, 0), SetStringTip(STR_XRAY_LAYER_RAIL, STR_XRAY_LAYER_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_ROAD), SetFill(1, 0), SetStringTip(STR_XRAY_LAYER_ROAD, STR_XRAY_LAYER_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_WATER), SetFill(1, 0), SetStringTip(STR_XRAY_LAYER_WATER, STR_XRAY_LAYER_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_AIR), SetFill(1, 0), SetStringTip(STR_XRAY_LAYER_AIR, STR_XRAY_LAYER_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_CARGO), SetFill(1, 0), SetStringTip(STR_XRAY_CARGO, STR_XRAY_CARGO_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_ORDER_COLOURS), SetFill(1, 0), SetStringTip(STR_XRAY_ORDER_COLOURS, STR_XRAY_ORDER_COLOURS_TOOLTIP),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXT, INVALID_COLOUR), SetFill(1, 0), SetStringTip(STR_XRAY_DESTINATIONS),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_XR_DESTINATIONS), SetToolTip(STR_XRAY_DESTINATIONS_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXT, INVALID_COLOUR), SetFill(1, 0), SetStringTip(STR_XRAY_ROUTES),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_XR_ROUTES), SetToolTip(STR_XRAY_ROUTES_TOOLTIP),
			EndContainer(),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_XR_IMPLICIT), SetFill(1, 0), SetStringTip(STR_XRAY_IMPLICIT, STR_XRAY_IMPLICIT_TOOLTIP),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXT, INVALID_COLOUR), SetFill(1, 0), SetStringTip(STR_XRAY_CARGO_FILTER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_XR_CARGO_FILTER), SetToolTip(STR_XRAY_CARGO_FILTER_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXT, INVALID_COLOUR), SetFill(1, 0), SetStringTip(STR_XRAY_COMPANY_FILTER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_XR_COMPANY_FILTER), SetToolTip(STR_XRAY_COMPANY_FILTER_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _rail_xray_desc(
	WDP_AUTO, "rail_xray", 0, 0,
	WC_RAIL_XRAY, WC_NONE,
	{},
	_nested_rail_xray_widgets
);

void ShowRailXRayWindow()
{
	AllocateWindowDescFront<RailXRayWindow>(_rail_xray_desc, 0);
}
