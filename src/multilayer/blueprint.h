/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint.h Blueprints: copies of underground (and surface rail) construction that can be pasted elsewhere. */

#ifndef METRO_BLUEPRINT_H
#define METRO_BLUEPRINT_H

#include "../tile_type.h"
#include "../rail_type.h"
#include "../strings_type.h"
#include "../command_type.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** Largest footprint side of a blueprint, in tiles. */
static constexpr uint MAX_BLUEPRINT_SIZE = 64;
/** Largest number of items in a blueprint. */
static constexpr uint MAX_BLUEPRINT_ITEMS = 8192;
/** Largest distance of a blueprint level from its anchor level. */
static constexpr int MAX_BLUEPRINT_LEVEL_OFFSET = 16;

/** What a blueprint item builds. The values are part of the command payload and must stay stable. */
enum class BlueprintItemKind : uint8_t {
	Track = 1,       ///< Underground track, with its signals.
	Platform = 2,    ///< Underground station platform tile.
	Ramp = 3,        ///< Ramp between two underground levels, with its signals.
	Portal = 4,      ///< Portal between the surface and an underground level.
	Exit = 5,        ///< Surface exit (pavilion or underpass) of an underground station.
	SurfaceRail = 6, ///< Plain rail on the surface, with its signals.
	Depot = 7,       ///< Rail depot on the surface.
};

/**
 * One piece of a blueprint, at a position of its footprint.
 * Underground items lie on a level relative to the anchor level of the blueprint
 * (0 is the anchor level, -1 the level below it); surface items lie on the
 * terrain, whatever its height.
 */
struct BlueprintItem {
	BlueprintItemKind kind = BlueprintItemKind::Track; ///< What the item builds.
	uint8_t x = 0;            ///< X offset in the footprint.
	uint8_t y = 0;            ///< Y offset in the footprint.
	int8_t z = 0;             ///< Level relative to the anchor; a ramp's entry level. Unused for surface items.
	int8_t z2 = 0;            ///< A ramp's exit level, relative to the anchor.
	uint8_t railtype = 0;     ///< Index into Blueprint::railtypes (in a command payload: the RailType itself).
	uint8_t tracks = 0;       ///< Track, SurfaceRail: the TrackBits.
	uint8_t dir = 0;          ///< Ramp, Portal, Depot: DiagDirection (as in the slice/tile); Platform: Axis.
	uint8_t signal_mask = 0;  ///< Track, Ramp, SurfaceRail: vanilla signal presence bits (see underground_signal.h).
	uint8_t signal_types = 0; ///< Track, Ramp, SurfaceRail: signal types and variants, laid out like m2 of a signal tile.
	uint8_t station = 0;      ///< Platform, Exit: index of the station within the blueprint.
	uint8_t exit_type = 0;    ///< Exit: StationExitType.

	/** Whether the item lies on the surface rather than on an underground level. */
	bool IsSurface() const
	{
		return this->kind == BlueprintItemKind::Exit || this->kind == BlueprintItemKind::SurfaceRail || this->kind == BlueprintItemKind::Depot;
	}

	/** Whether the item uses a rail type. */
	bool HasRailType() const { return this->kind != BlueprintItemKind::Exit; }

	/** Whether the item carries signals. */
	bool HasSignals() const
	{
		return this->kind == BlueprintItemKind::Track || this->kind == BlueprintItemKind::Ramp || this->kind == BlueprintItemKind::SurfaceRail;
	}

	bool operator==(const BlueprintItem &other) const = default;
};

/** A rotation and mirroring of a blueprint. The mirror is applied before the rotation. */
struct BlueprintTransform {
	uint8_t rotation = 0; ///< Number of quarter turns (0..3), each turning north-east into north-west.
	bool mirror = false;  ///< Mirror along the x axis (north-east <-> south-west) first.

	bool IsIdentity() const { return this->rotation == 0 && !this->mirror; }
	bool operator==(const BlueprintTransform &other) const = default;
};

/** A copy of construction that can be pasted elsewhere. */
struct Blueprint {
	std::string name;                    ///< Name given by the player, may be empty.
	uint8_t w = 0;                       ///< Footprint size along x.
	uint8_t h = 0;                       ///< Footprint size along y.
	uint8_t num_stations = 0;            ///< Number of distinct stations the platforms belong to.
	std::vector<RailTypeLabel> railtypes; ///< Rail types used, by label.
	std::vector<BlueprintItem> items;    ///< The pieces.

	bool IsEmpty() const { return this->items.empty(); }
	bool HasSurfaceItems() const;
	bool HasUndergroundItems() const;
	std::pair<int, int> GetLevelRange() const;
	void Transform(BlueprintTransform transform);
	std::optional<std::string> Validate() const;
};

/** What to copy into a blueprint. */
struct BlueprintCaptureSpec {
	TileIndex start = INVALID_TILE; ///< One corner of the area.
	TileIndex end = INVALID_TILE;   ///< The other corner of the area.
	int16_t z_top = -1;             ///< Highest underground level copied.
	int16_t z_bot = -1;             ///< Lowest underground level copied.
	bool surface = false;           ///< Copy the surface too: rail, depots, station exits, and every level above z_bot.
	int16_t anchor = -1;            ///< Level that becomes level 0 of the blueprint.
};

Blueprint CaptureBlueprint(const BlueprintCaptureSpec &spec);
std::optional<std::pair<int16_t, int16_t>> FindUndergroundLevelRange(TileIndex start, TileIndex end);

std::string BlueprintToJson(const Blueprint &bp, bool pretty);
std::string BlueprintToShareString(const Blueprint &bp);
std::optional<Blueprint> ParseBlueprintText(std::string_view text, std::string *error);

/** Options of pasting a blueprint, as passed to the paste command. */
enum BlueprintPasteOption : uint8_t {
	BPO_JOIN_ADJACENT = 1 << 0, ///< Join the stations of the blueprint to an own station next to them.
	BPO_BEST_EFFORT   = 1 << 1, ///< Build what can be built and leave out what cannot; otherwise all or nothing.
};

/** Everything that decides how a blueprint is pasted. */
struct BlueprintPasteSpec {
	BlueprintTransform transform{};   ///< Rotation and mirroring.
	RailType railtype = INVALID_RAILTYPE; ///< Build every piece with this rail type; INVALID_RAILTYPE keeps the blueprint's.
	uint8_t options = 0;              ///< BlueprintPasteOption bits.
};

std::vector<std::string> PlanBlueprintPaste(const Blueprint &bp, const BlueprintPasteSpec &spec, StringID *error);
CommandCost TestBlueprintPaste(const std::vector<std::string> &chunks, TileIndex tile, int16_t depth, uint8_t options);
bool PostBlueprintPaste(const Blueprint &bp, const BlueprintPasteSpec &spec, TileIndex tile, int16_t depth, bool estimate_only);

/* Payload of the paste command. */
struct BlueprintPayload {
	std::vector<BlueprintItem> items; ///< The pieces, rail types resolved, footprint transformed.
	std::vector<std::pair<uint8_t, BlueprintItem>> station_refs; ///< Platforms of earlier chunks naming the station a blueprint station became.
};
std::optional<BlueprintPayload> DecodeBlueprintPayload(std::string_view data);

/* The library: the clipboard and the blueprints on disk. */

/** A blueprint in the library. */
struct BlueprintLibraryEntry {
	std::string filename; ///< Full path of the file; empty for the clipboard.
	Blueprint bp;         ///< The blueprint.
};

const std::vector<BlueprintLibraryEntry> &GetBlueprintLibrary();
void ScanBlueprintLibrary();
const Blueprint *GetBlueprintClipboard();
void SetBlueprintClipboard(Blueprint bp);
const Blueprint *FindBlueprint(std::string_view name);
bool SaveBlueprintToDisk(const Blueprint &bp, std::string *error);
bool IsBlueprintDeletable(const BlueprintLibraryEntry &entry);
bool DeleteBlueprintFromDisk(const BlueprintLibraryEntry &entry);

#endif /* METRO_BLUEPRINT_H */
