/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file multilayer_map.h Multi-layer map: manages tile columns and slice pool for underground support. */

#ifndef MULTILAYER_MAP_H
#define MULTILAYER_MAP_H

#include "multilayer_types.h"

#include <vector>
#include <span>
#include <unordered_set>

/**
 * The multi-layer map manages the vertical dimension of the game world.
 *
 * It runs in parallel with the flat map (Tile::base_tiles / extended_tiles).
 * Surface tiles are mirrored as Surface slices; underground tiles are new.
 *
 * Ownership: there is exactly one global instance, allocated by Map::Allocate().
 */
class MultiLayerMap {
public:
	MultiLayerMap() = default;
	~MultiLayerMap() = default;

	MultiLayerMap(const MultiLayerMap &) = delete;
	MultiLayerMap &operator=(const MultiLayerMap &) = delete;

	/**
	 * (Re)allocate the multi-layer map to match the flat map dimensions.
	 * Creates one Surface slice per tile column.
	 * @param map_size Total number of tiles (Map::size).
	 */
	void Allocate(uint map_size);

	/** Free all data. */
	void Clear();

	/* === Column access === */

	/**
	 * Get the column at a given tile position.
	 * @param tile The flat map tile index.
	 * @return Reference to the TileColumn.
	 */
	TileColumn &GetColumn(TileIndex tile) { assert(tile.base() < this->columns.size()); return this->columns[tile.base()]; }
	const TileColumn &GetColumn(TileIndex tile) const { assert(tile.base() < this->columns.size()); return this->columns[tile.base()]; }

	/** Check if the multi-layer map is allocated. */
	bool IsAllocated() const { return !this->columns.empty(); }

	/* === Slice pool access === */

	/**
	 * Get a slice by its pool ID.
	 * @param id SliceID.
	 * @return Reference to the TileSlice.
	 */
	TileSlice &GetSlice(SliceID id) { assert(id < this->slice_pool.size()); return this->slice_pool[id]; }
	const TileSlice &GetSlice(SliceID id) const { assert(id < this->slice_pool.size()); return this->slice_pool[id]; }

	/** Check if a SliceID is within pool bounds and refers to a live (non-freed) slice. */
	bool IsValidSlice(SliceID id) const { return id < this->slice_pool.size() && this->slice_pool[id].kind != SliceKind::End; }

	/**
	 * Get the Surface slice for a given tile.
	 * Every column is guaranteed to have exactly one Surface slice (the first one).
	 * @param tile The flat map tile index.
	 * @return Reference to the surface TileSlice.
	 */
	TileSlice &GetSurfaceSlice(TileIndex tile);
	const TileSlice &GetSurfaceSlice(TileIndex tile) const;

	/* === Slice CRUD === */

	/**
	 * Allocate a new slice from the pool and return its ID.
	 * @return The SliceID of the newly allocated slice.
	 */
	SliceID AllocateSlice();

	/**
	 * Free a slice back to the pool.
	 * Does NOT remove it from any column — caller must do that first.
	 * @param id SliceID to free.
	 */
	void FreeSlice(SliceID id);

	/**
	 * Insert a slice into a column at the correct z-order position.
	 * The slice must already be allocated and have its span set.
	 * @param tile  The tile column to insert into.
	 * @param id    SliceID of the slice to insert.
	 * @return True if inserted, false if the ZSpan conflicts with an existing slice.
	 */
	bool InsertSlice(TileIndex tile, SliceID id);

	/**
	 * Remove a slice from a column (does not free from pool).
	 * @param tile  The tile column.
	 * @param id    SliceID to remove.
	 * @return True if found and removed.
	 */
	bool RemoveSliceFromColumn(TileIndex tile, SliceID id);

	/* === Queries === */

	/**
	 * Find the topmost slice at a given (x,y) and z coordinate.
	 * @param tile Tile position.
	 * @param z    Z coordinate to query.
	 * @return SliceID of the topmost slice containing z, or INVALID_SLICE_ID.
	 */
	SliceID FindSliceAt(TileIndex tile, int16_t z) const;

	/**
	 * Find a slice by kind in a column.
	 * @param tile Tile position.
	 * @param kind SliceKind to search for.
	 * @param z    Optional z to narrow search. Use INT16_MIN to match any.
	 * @return SliceID or INVALID_SLICE_ID.
	 */
	SliceID FindSliceByKind(TileIndex tile, SliceKind kind, int16_t z = INT16_MIN) const;

	/**
	 * Create a TileRef for the surface at a given tile.
	 * @param tile Tile position.
	 * @return TileRef pointing to the Surface slice.
	 */
	TileRef MakeSurfaceRef(TileIndex tile) const;

	/**
	 * Get the tile indices of all columns containing a Portal slice.
	 * Maintained incrementally by InsertSlice/RemoveSliceFromColumn so portal
	 * lookups don't need a full map scan.
	 */
	const std::unordered_set<uint32_t> &GetPortalColumns() const { return this->portal_columns; }

	/**
	 * Lowest floor of the underground slices inserted since the map was
	 * allocated (0 when there are none). It does not rise when they are
	 * removed; it keeps levels built deeper than today's limit reachable.
	 */
	int16_t GetDeepestBuiltZ() const { return this->deepest_built_z; }

	/**
	 * Get the total number of allocated slices (including free-list entries).
	 */
	size_t SlicePoolSize() const { return this->slice_pool.size(); }

	/**
	 * Get the number of active (non-free) slices.
	 */
	size_t ActiveSliceCount() const { return this->slice_pool.size() - this->free_list.size(); }

	/* === Save/load support === */

	/**
	 * Replace the entire slice pool with loaded data.
	 * @param slices The loaded slice vector.
	 */
	void RestoreSlicePool(std::vector<TileSlice> &&slices);

	/**
	 * Allocate column vector without creating surface slices.
	 * Used during load when slices come from save data.
	 * @param map_size Number of columns.
	 */
	void AllocateColumns(uint map_size);

	/**
	 * Ensure every column has at least one Surface slice.
	 * Creates missing surface slices for columns not loaded from save data.
	 */
	void EnsureSurfaceSlices();

private:
	std::vector<TileColumn> columns;    ///< One column per (x, y) tile.
	std::vector<TileSlice> slice_pool;  ///< Global pool of all slices.
	std::vector<SliceID> free_list;     ///< Free SliceIDs available for reuse.
	std::unordered_set<uint32_t> portal_columns; ///< Tile indices of columns containing a Portal slice.
	int16_t deepest_built_z = 0;        ///< Lowest z_bot of an underground slice inserted since the map was allocated.
};

/** The global multi-layer map instance. */
extern MultiLayerMap _multilayer_map;

#endif /* MULTILAYER_MAP_H */
