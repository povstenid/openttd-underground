/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint_gui.h Window and console command for metro blueprints. */

#ifndef BLUEPRINT_GUI_H
#define BLUEPRINT_GUI_H

#include "../tile_type.h"
#include "../window_type.h"

#include <span>
#include <string_view>

struct BlueprintItem;

/** Widgets of the metro blueprints window. */
enum MetroBlueprintWidgets : WidgetID {
	WID_MBP_CAPTION,          ///< Caption.
	WID_MBP_COPY,             ///< Copy tool.
	WID_MBP_PASTE,            ///< Paste tool.
	WID_MBP_ROTATE_LEFT,      ///< Turn the blueprint a quarter left.
	WID_MBP_ROTATE_RIGHT,     ///< Turn the blueprint a quarter right.
	WID_MBP_MIRROR,           ///< Mirror the blueprint.
	WID_MBP_AUTO_LEVELS,      ///< Copy every level with construction.
	WID_MBP_TOP_DOWN,         ///< Top level of the copy one lower.
	WID_MBP_TOP_TEXT,         ///< Top level of the copy.
	WID_MBP_TOP_UP,           ///< Top level of the copy one higher.
	WID_MBP_BOTTOM_DOWN,      ///< Bottom level of the copy one lower.
	WID_MBP_BOTTOM_TEXT,      ///< Bottom level of the copy.
	WID_MBP_BOTTOM_UP,        ///< Bottom level of the copy one higher.
	WID_MBP_SURFACE,          ///< Copy the surface too.
	WID_MBP_JOIN,             ///< Join pasted stations to adjacent ones.
	WID_MBP_BEST_EFFORT,      ///< Build what fits.
	WID_MBP_TOOLBAR_RAILTYPE, ///< Build with the toolbar's track type.
	WID_MBP_INFO,             ///< Description of the clipboard.
	WID_MBP_LIST,             ///< Blueprints on disk.
	WID_MBP_SCROLLBAR,        ///< Scrollbar of the list.
	WID_MBP_SAVE,             ///< Save the clipboard to disk.
	WID_MBP_DELETE,           ///< Delete the selected blueprint from disk.
	WID_MBP_RESCAN,           ///< Read the blueprints from disk again.
	WID_MBP_EXPORT,           ///< Put the clipboard as text on the system clipboard.
	WID_MBP_IMPORT,           ///< Take a blueprint from text on the system clipboard.
};

void ShowMetroBlueprintWindow();
std::span<const BlueprintItem> GetBlueprintPastePreview(TileIndex tile);
bool ConBlueprint(std::span<std::string_view> argv);

#endif /* BLUEPRINT_GUI_H */
