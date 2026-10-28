/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file multilayer_map.cpp Implementation of the multi-layer map (slice pool, column management). */

#include "../stdafx.h"
#include "multilayer_map.h"
#include "../debug.h"

#include "../safeguards.h"

/** The global multi-layer map instance. */
MultiLayerMap _multilayer_map;

/** Surface slices are logical sentinels, not physical terrain spans. */
static constexpr ZSpan SURFACE_SLICE_SENTINEL_SPAN = {INT16_MAX, static_cast<int16_t>(INT16_MAX - 1)};

/**
 * (Re)allocate the multi-layer map.
 * Creates one Surface slice per tile column, mirroring the flat map.
 */
void MultiLayerMap::Allocate(uint map_size)
{
	this->Clear();

	Debug(map, 1, "Allocating multi-layer map for {} tiles", map_size);

	this->columns.resize(map_size);

	/* Pre-allocate surface slices: one per tile. */
	this->slice_pool.resize(map_size);
	for (uint i = 0; i < map_size; i++) {
		TileSlice &s = this->slice_pool[i];
		s.kind = SliceKind::Surface;
		s.span = SURFACE_SLICE_SENTINEL_SPAN;
		s.owner = 0x10;  /* OWNER_NONE — will be synced from flat map later. */
		s.track = {};

		this->columns[i].slices.push_back(static_cast<SliceID>(i));
	}
}

void MultiLayerMap::Clear()
{
	this->columns.clear();
	this->slice_pool.clear();
	this->free_list.clear();
	this->portal_columns.clear();
	this->deepest_built_z = 0;
}

/* === Surface slice access === */

TileSlice &MultiLayerMap::GetSurfaceSlice(TileIndex tile)
{
	SliceID sid = this->FindSliceByKind(tile, SliceKind::Surface);
	assert(sid != INVALID_SLICE_ID);
	return this->slice_pool[sid];
}

const TileSlice &MultiLayerMap::GetSurfaceSlice(TileIndex tile) const
{
	SliceID sid = this->FindSliceByKind(tile, SliceKind::Surface);
	assert(sid != INVALID_SLICE_ID);
	return this->slice_pool[sid];
}

/* === Slice pool CRUD === */

SliceID MultiLayerMap::AllocateSlice()
{
	if (!this->free_list.empty()) {
		SliceID id = this->free_list.back();
		this->free_list.pop_back();
		this->slice_pool[id] = TileSlice{}; /* Reset to defaults. */
		return id;
	}

	SliceID id = static_cast<SliceID>(this->slice_pool.size());
	this->slice_pool.emplace_back();
	return id;
}

void MultiLayerMap::FreeSlice(SliceID id)
{
	assert(id < this->slice_pool.size());
	this->slice_pool[id].kind = SliceKind::End; /* Mark as unused. */
	this->free_list.push_back(id);
}

/**
 * Insert a slice into a column, maintaining z_bot ascending order.
 * Rejects insertion if the ZSpan overlaps with any existing slice.
 */
bool MultiLayerMap::InsertSlice(TileIndex tile, SliceID id)
{
	if (tile.base() >= this->columns.size()) return false;
	TileColumn &col = this->columns[tile.base()];
	const TileSlice &new_slice = this->slice_pool[id];

	/* Find insertion point and check for overlaps. */
	auto it = col.slices.begin();
	for (; it != col.slices.end(); ++it) {
		const TileSlice &existing = this->slice_pool[*it];
		if (existing.span.Overlaps(new_slice.span)) {
			return false; /* ZSpan conflict. */
		}
		if (existing.span.z_bot >= new_slice.span.z_bot) {
			break; /* Insert before this one. */
		}
	}

	col.slices.insert(it, id);
	if (new_slice.kind == SliceKind::Portal) this->portal_columns.insert(tile.base());
	if (new_slice.kind != SliceKind::Surface) this->deepest_built_z = std::min(this->deepest_built_z, new_slice.span.z_bot);
	return true;
}

bool MultiLayerMap::RemoveSliceFromColumn(TileIndex tile, SliceID id)
{
	if (tile.base() >= this->columns.size()) return false;
	TileColumn &col = this->columns[tile.base()];
	for (auto it = col.slices.begin(); it != col.slices.end(); ++it) {
		if (*it == id) {
			col.slices.erase(it);
			/* Keep the portal column index in sync; a column may hold portals at several depths. */
			if (this->slice_pool[id].kind == SliceKind::Portal &&
					this->FindSliceByKind(tile, SliceKind::Portal) == INVALID_SLICE_ID) {
				this->portal_columns.erase(tile.base());
			}
			return true;
		}
	}
	return false;
}

/* === Queries === */

SliceID MultiLayerMap::FindSliceAt(TileIndex tile, int16_t z) const
{
	if (tile.base() >= this->columns.size()) return INVALID_SLICE_ID;
	const TileColumn &col = this->columns[tile.base()];
	for (auto it = col.slices.rbegin(); it != col.slices.rend(); ++it) {
		const TileSlice &s = this->slice_pool[*it];
		if (s.span.Contains(z)) return *it;
	}
	return INVALID_SLICE_ID;
}

SliceID MultiLayerMap::FindSliceByKind(TileIndex tile, SliceKind kind, int16_t z) const
{
	if (tile.base() >= this->columns.size()) return INVALID_SLICE_ID;
	const TileColumn &col = this->columns[tile.base()];
	for (SliceID id : col.slices) {
		const TileSlice &s = this->slice_pool[id];
		if (s.kind != kind) continue;
		if (z != INT16_MIN && !s.span.Contains(z)) continue;
		return id;
	}
	return INVALID_SLICE_ID;
}

TileRef MultiLayerMap::MakeSurfaceRef(TileIndex tile) const
{
	SliceID id = this->FindSliceByKind(tile, SliceKind::Surface);
	return TileRef{tile, id};
}

/* === Save/load support === */

void MultiLayerMap::RestoreSlicePool(std::vector<TileSlice> &&slices)
{
	this->slice_pool = std::move(slices);
	this->free_list.clear();

	/* Rebuild free list from End-marked entries. */
	for (size_t i = 0; i < this->slice_pool.size(); i++) {
		if (this->slice_pool[i].kind == SliceKind::End) {
			this->free_list.push_back(static_cast<SliceID>(i));
		}
	}
}

void MultiLayerMap::AllocateColumns(uint map_size)
{
	this->columns.clear();
	this->columns.resize(map_size);
}

void MultiLayerMap::EnsureSurfaceSlices()
{
	for (size_t i = 0; i < this->columns.size(); i++) {
		TileColumn &col = this->columns[i];
		if (!col.slices.empty()) continue;

		/* Column has no slices — create a default Surface slice. */
		SliceID id = this->AllocateSlice();
		TileSlice &s = this->slice_pool[id];
		s.kind = SliceKind::Surface;
		s.span = SURFACE_SLICE_SENTINEL_SPAN;
		s.owner = 0x10;
		col.slices.push_back(id);
	}
}
