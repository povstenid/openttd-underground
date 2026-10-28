/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file multilayer_types.h Types for the multi-layer map system (underground/metro support). */

#ifndef MULTILAYER_TYPES_H
#define MULTILAYER_TYPES_H

#include "../tile_type.h"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <functional>

/** Unique identifier for a slice in the global slice pool. */
using SliceID = uint32_t;

static constexpr SliceID INVALID_SLICE_ID = std::numeric_limits<SliceID>::max();

/**
 * Vertical span of a slice within a tile column.
 * z_top is the ceiling, z_bot is the floor. z_top > z_bot.
 * Surface is at z=0, underground is negative.
 * One unit of z equals TILE_HEIGHT (8 world coords).
 */
struct ZSpan {
	int16_t z_top; ///< Top of the span (ceiling).
	int16_t z_bot; ///< Bottom of the span (floor).

	/** Check if this span overlaps with another. */
	bool Overlaps(const ZSpan &other) const
	{
		return this->z_bot < other.z_top && other.z_bot < this->z_top;
	}

	/** Check if a z coordinate is within this span. */
	bool Contains(int16_t z) const
	{
		return z >= this->z_bot && z < this->z_top;
	}

	bool operator==(const ZSpan &other) const = default;
};

/** The kind of content a slice represents. */
enum class SliceKind : uint8_t {
	Surface,     ///< The surface tile (mirrors flat map data). Exactly one per column.
	Track,       ///< Underground rail track.
	StationTile, ///< Underground station platform tile.
	Portal,      ///< Entry/exit between surface and underground.
	Ramp,        ///< Depth transition (connects two z-levels at one tile).
	End,         ///< End marker.
};

/**
 * Payload for Track slices — stores rail data.
 * Uses raw integer types to avoid heavy includes; interpret via rail_type.h / track_type.h at use sites.
 */
struct TrackPayload {
	uint8_t railtype = 0xFF; ///< RailType value (0xFF = INVALID_RAILTYPE).
	uint8_t tracks = 0;     ///< TrackBits bitmask.
	uint8_t signal_mask = 0; ///< Surface-style directional signal bits present on this track slice.
	uint8_t reserved = 0;   ///< PBS reservation state.
	uint8_t signal_types = 0; ///< SignalType and SignalVariant of the signals, laid out like m2 of a vanilla signal tile (see underground_signal.h).
	uint32_t reserved_by = 0; ///< VehicleID of the train holding the reserved tracks other than the lower and the right one; meaningful only while one of them is reserved.
	uint16_t paid_soil = 0; ///< Soil price multiplier (tenths) paid for its cheapest track piece; refunds never exceed it (0: not known).
	uint32_t reserved_by2 = 0; ///< VehicleID of the train holding the lower or the right track, which can be reserved beside the upper or the left one by another train (rebuilt from the trains when loading a game saved before SLV_METRO_RUNTIME_STATE).
};

/**
 * Payload for StationTile slices.
 */
struct StationPayload {
	uint16_t station_id = 0; ///< Index into Station pool.
	uint8_t role = 0;        ///< 0 = Platform, 1 = ExitSurface.
	uint8_t tracks = 0;      ///< TrackBits for platform direction (X or Y).
	uint8_t railtype = 0xFF; ///< RailType value (0xFF = INVALID_RAILTYPE).
	uint8_t reserved = 0;    ///< PBS reservation state.
	uint32_t reserved_by = 0; ///< VehicleID of the train holding the reservation; meaningful only while reserved != 0.
	uint16_t paid_soil = 0;  ///< Soil price multiplier (tenths) paid for it; refunds never exceed it (0: not known).
};

/**
 * Payload for Portal slices — connects surface to underground.
 */
struct PortalPayload {
	SliceID target_slice = INVALID_SLICE_ID; ///< The slice on the other side of the portal.
	uint8_t direction = 0;                   ///< DiagDirection value.
	uint8_t railtype = 0xFF;                 ///< RailType of the portal (0xFF = INVALID_RAILTYPE), used for infrastructure accounting.
	uint16_t paid_soil = 0;                  ///< Soil price multiplier (tenths) paid for it; refunds never exceed it (0: not known).
};

/**
 * Payload for Ramp slices — connects two depth levels.
 */
struct RampPayload {
	int16_t entry_depth = 0;  ///< Level the ramp starts on (e.g. -1).
	int16_t exit_depth = 0;   ///< Level the ramp leads to, one below or above the entry (e.g. -2).
	uint8_t direction = 0;    ///< DiagDirection of travel from the entry level to the exit level.
	uint8_t railtype = 0xFF;  ///< RailType value.
	uint8_t reserved = 0;     ///< PBS reservation state of its one track (rebuilt from the trains when loading a game saved before SLV_METRO_RUNTIME_STATE).
	uint8_t signal_mask = 0;  ///< Surface-style directional signal bits on its one track, like TrackPayload::signal_mask.
	uint8_t signal_types = 0; ///< SignalType and SignalVariant of its signals, like TrackPayload::signal_types.
	uint32_t reserved_by = 0; ///< VehicleID of the train holding the reservation; meaningful only while reserved != 0.
	uint16_t paid_soil = 0;   ///< Soil price multiplier (tenths) paid for it; refunds never exceed it (0: not known).
};

/**
 * A single horizontal slab within a tile column.
 * Each slice has a kind, a vertical span, and kind-specific payload.
 */
struct TileSlice {
	SliceKind kind; ///< What this slice represents.
	ZSpan span;     ///< Vertical extent.
	uint8_t owner;  ///< Owner value (0x10 = OWNER_NONE).

	union {
		TrackPayload track;
		StationPayload station;
		PortalPayload portal;
		RampPayload ramp;
	};

	TileSlice()
	{
		/* Zero all bytes including the full union without triggering -Wclass-memaccess. */
		std::fill_n(reinterpret_cast<unsigned char *>(this), sizeof(*this), static_cast<unsigned char>(0));
		this->kind = SliceKind::End;
		this->owner = 0x10; /* OWNER_NONE */
	}
};

/**
 * A vertical column of slices at a single (x, y) map position.
 * Slices are ordered by z_bot ascending. The Surface slice is always present.
 */
struct TileColumn {
	std::vector<SliceID> slices; ///< Ordered list of SliceIDs (by z_bot ascending).

	/** Check if this column has any underground slices (non-Surface). */
	bool HasUnderground() const { return this->slices.size() > 1; }

	/** Get the number of slices in this column. */
	size_t Count() const { return this->slices.size(); }
};

/**
 * A reference to a specific slice at a specific (x, y) position.
 * Used by underground-aware code instead of plain TileIndex.
 */
struct TileRef {
	TileIndex tile = INVALID_TILE;        ///< The (x, y) position.
	SliceID slice = INVALID_SLICE_ID;     ///< Which slice in the column.

	bool IsValid() const { return this->tile != INVALID_TILE && this->slice != INVALID_SLICE_ID; }

	bool operator==(const TileRef &other) const = default;
};

/** Hash for TileRef so it can be used in unordered containers. */
struct TileRefHash {
	size_t operator()(const TileRef &ref) const
	{
		size_t h = std::hash<uint32_t>{}(ref.tile.base());
		h ^= std::hash<uint32_t>{}(ref.slice) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

#endif /* MULTILAYER_TYPES_H */
