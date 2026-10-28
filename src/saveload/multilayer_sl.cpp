/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file multilayer_sl.cpp Save/load for underground slices (portals, tracks, etc). */

#include "../stdafx.h"
#include "saveload.h"
#include "../multilayer/multilayer_map.h"
#include "../multilayer/underground_signal.h"
#include "../multilayer/portal_registry.h"
#include "../multilayer/station_complex.h"
#include "../multilayer/underground_train.h"
#include "../map_func.h"
#include "../debug.h"

#include <set>

#include "../safeguards.h"

/*
 * Strategy: save only non-Surface slices (Track, Portal, Ramp, StationTile).
 * Each saved record = one slice with its tile index and all fields.
 * On load, rebuild columns from saved slices. Surface slices are auto-created
 * by Map::Allocate() and don't need saving.
 */

/** Flat struct for saving a slice + its tile location. */
struct SliceSaveData {
	uint32_t tile_index = 0;
	uint8_t kind = 0;
	int16_t z_top = 0;
	int16_t z_bot = 0;
	uint8_t owner = 0;
	uint8_t payload0 = 0; /* track.railtype OR portal.direction OR ramp.direction */
	uint8_t payload1 = 0; /* track.tracks */
	uint8_t payload2 = 0; /* track.signal_mask OR ramp.signal_mask */
	uint8_t payload3 = 0; /* track.reserved OR ramp.reserved (since SLV_METRO_RUNTIME_STATE) */
	uint32_t payload4 = 0; /* portal.target_slice OR ramp depths packed OR track/station reserved_by */
	uint16_t payload5 = 0; /* station.station_id OR track.signal_types | TRACK_SIGNAL_TYPES_SAVED OR ramp.signal_types */
	uint16_t paid_soil = 0; /* track/station/portal/ramp.paid_soil */
	uint32_t payload6 = 0; /* track.reserved_by2 OR ramp.reserved_by (since SLV_METRO_RUNTIME_STATE) */
};

/**
 * Soil price multiplier (tenths) assumed paid for slices of saves from before
 * SLV_METRO_PAID_SOIL: metro v1 charged x4 the rail price where v2 charges x10
 * the surface price times the soil, so x0.4 keeps refunds below what was paid.
 */
static constexpr uint16_t LEGACY_PAID_SOIL = 4;

/** Flag in payload5 of a Track slice: its low byte holds track.signal_types. Older saves left payload5 at 0 and had only one-way path signals. */
static constexpr uint16_t TRACK_SIGNAL_TYPES_SAVED = 0x8000;

static const SaveLoad _slice_save_desc[] = {
	SLE_VAR(SliceSaveData, tile_index, SLE_UINT32),
	SLE_VAR(SliceSaveData, kind,       SLE_UINT8),
	SLE_VAR(SliceSaveData, z_top,      SLE_INT16),
	SLE_VAR(SliceSaveData, z_bot,      SLE_INT16),
	SLE_VAR(SliceSaveData, owner,      SLE_UINT8),
	SLE_VAR(SliceSaveData, payload0,   SLE_UINT8),
	SLE_VAR(SliceSaveData, payload1,   SLE_UINT8),
	SLE_VAR(SliceSaveData, payload2,   SLE_UINT8),
	SLE_VAR(SliceSaveData, payload3,   SLE_UINT8),
	SLE_VAR(SliceSaveData, payload4,   SLE_UINT32),
	SLE_VAR(SliceSaveData, payload5,   SLE_UINT16),
	SLE_CONDVAR(SliceSaveData, paid_soil, SLE_UINT16, SLV_METRO_PAID_SOIL, SL_MAX_VERSION),
	SLE_CONDVAR(SliceSaveData, payload6, SLE_UINT32, SLV_METRO_RUNTIME_STATE, SL_MAX_VERSION),
};

/** Pack a TileSlice into SliceSaveData. */
static SliceSaveData PackSlice(TileIndex tile, const TileSlice &s)
{
	SliceSaveData d;
	d.tile_index = tile.base();
	d.kind = static_cast<uint8_t>(s.kind);
	d.z_top = s.span.z_top;
	d.z_bot = s.span.z_bot;
	d.owner = s.owner;

	switch (s.kind) {
		case SliceKind::Track:
			d.payload0 = s.track.railtype;
			d.payload1 = s.track.tracks;
			d.payload2 = s.track.signal_mask;
			d.payload3 = s.track.reserved;
			d.payload4 = s.track.reserved_by;
			d.payload5 = TRACK_SIGNAL_TYPES_SAVED | s.track.signal_types;
			d.paid_soil = s.track.paid_soil;
			d.payload6 = s.track.reserved_by2;
			break;
		case SliceKind::Portal:
			d.payload0 = s.portal.direction;
			d.payload1 = s.portal.railtype;
			d.payload4 = s.portal.target_slice;
			d.paid_soil = s.portal.paid_soil;
			break;
		case SliceKind::StationTile:
			d.payload5 = s.station.station_id;
			d.payload0 = s.station.role;
			d.payload1 = s.station.tracks;
			d.payload2 = s.station.railtype;
			d.payload3 = s.station.reserved;
			d.payload4 = s.station.reserved_by;
			d.paid_soil = s.station.paid_soil;
			break;
		case SliceKind::Ramp:
			d.payload0 = s.ramp.direction;
			d.payload1 = s.ramp.railtype;
			d.payload2 = s.ramp.signal_mask;
			d.payload5 = s.ramp.signal_types;
			d.payload4 = (static_cast<uint32_t>(static_cast<uint16_t>(s.ramp.entry_depth)) << 16) |
			             static_cast<uint16_t>(s.ramp.exit_depth);
			d.paid_soil = s.ramp.paid_soil;
			d.payload3 = s.ramp.reserved;
			d.payload6 = s.ramp.reserved_by;
			break;
		default:
			break;
	}
	return d;
}

/** Unpack SliceSaveData into a TileSlice. */
static TileSlice UnpackSlice(const SliceSaveData &d)
{
	TileSlice s;
	uint16_t paid_soil = IsSavegameVersionBefore(SLV_METRO_PAID_SOIL) ? LEGACY_PAID_SOIL : d.paid_soil;
	s.kind = static_cast<SliceKind>(d.kind);
	s.span = {d.z_top, d.z_bot};
	s.owner = d.owner;

	switch (s.kind) {
		case SliceKind::Track:
			s.track.railtype = d.payload0;
			s.track.tracks = d.payload1;
			s.track.signal_mask = d.payload2;
			s.track.reserved = d.payload3;
			s.track.reserved_by = d.payload4;
			s.track.reserved_by2 = d.payload6;
			s.track.signal_types = (d.payload5 & TRACK_SIGNAL_TYPES_SAVED) != 0 ? GB(d.payload5, 0, 8) : UNDERGROUND_LEGACY_SIGNAL_TYPES;
			s.track.paid_soil = paid_soil;
			break;
		case SliceKind::Portal:
			s.portal.direction = d.payload0;
			/* Older saves did not store the portal rail type; AfterLoadGame derives it from the surface tile. */
			s.portal.railtype = IsSavegameVersionBefore(SLV_METRO_PORTAL_RAILTYPE) ? 0xFF : d.payload1;
			s.portal.target_slice = d.payload4;
			s.portal.paid_soil = paid_soil;
			break;
		case SliceKind::StationTile:
			s.station.station_id = d.payload5;
			s.station.role = d.payload0;
			s.station.tracks = d.payload1;
			s.station.railtype = d.payload2;
			s.station.reserved = d.payload3;
			s.station.reserved_by = d.payload4;
			s.station.paid_soil = paid_soil;
			break;
		case SliceKind::Ramp:
			s.ramp.direction = d.payload0;
			s.ramp.railtype = d.payload1;
			s.ramp.entry_depth = static_cast<int16_t>(d.payload4 >> 16);
			s.ramp.exit_depth = static_cast<int16_t>(d.payload4 & 0xFFFF);
			/* Older saves left payload2 and payload5 of ramps at 0: no signals. */
			s.ramp.signal_mask = d.payload2;
			s.ramp.signal_types = GB(d.payload5, 0, 8);
			s.ramp.paid_soil = paid_soil;
			/* Older saves did not store ramp reservations; AfterLoadGame rebuilds them. */
			if (!IsSavegameVersionBefore(SLV_METRO_RUNTIME_STATE)) {
				s.ramp.reserved = d.payload3;
				s.ramp.reserved_by = d.payload6;
			}
			break;
		default:
			break;
	}
	return s;
}

/**
 * MLSL chunk: All non-surface slices.
 * Saves Track, Portal, Ramp, StationTile slices with tile location.
 */
struct MLSLChunkHandler : ChunkHandler {
	MLSLChunkHandler() : ChunkHandler('MLSL', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_slice_save_desc);

		uint idx = 0;
		for (uint i = 0; i < Map::Size(); i++) {
			TileIndex tile(i);
			const TileColumn &col = _multilayer_map.GetColumn(tile);
			for (SliceID sid : col.slices) {
				const TileSlice &s = _multilayer_map.GetSlice(sid);
				if (s.kind == SliceKind::Surface || s.kind == SliceKind::End) continue;

				SlSetArrayIndex(idx++);
				SliceSaveData d = PackSlice(tile, s);
				SlObject(&d, _slice_save_desc);
			}
		}
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_MULTILAYER_MAP)) return;

		const std::vector<SaveLoad> slt = SlTableHeader(_slice_save_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			SliceSaveData d;
			SlObject(&d, slt);

			TileIndex tile(d.tile_index);
			if (tile.base() >= Map::Size()) continue;

			TileSlice s = UnpackSlice(d);

			/* Allocate and insert into multilayer map. */
			SliceID new_id = _multilayer_map.AllocateSlice();
			_multilayer_map.GetSlice(new_id) = s;
			_multilayer_map.InsertSlice(tile, new_id);
		}

		/* Rebuild portal connections after loading. */
		RecomputePortalConnections();
	}
};

/* MLCL: empty stub for format compatibility. */
struct DummySL { uint8_t dummy = 0; };
static const SaveLoad _dummy_desc[] = { SLE_VAR(DummySL, dummy, SLE_UINT8) };

struct MLCLChunkHandler : ChunkHandler {
	MLCLChunkHandler() : ChunkHandler('MLCL', CH_TABLE) {}
	void Save() const override { SlTableHeader(_dummy_desc); }
	void Load() const override {
		if (IsSavegameVersionBefore(SLV_MULTILAYER_MAP)) return;
		SlTableHeader(_dummy_desc);
		while (SlIterateArray() != -1) { DummySL d; SlObject(&d, _dummy_desc); }
	}
};

/** Flat struct for saving the underground state of a train. */
struct UndergroundTrainSaveData {
	uint32_t vehicle = 0;
	uint32_t entry_reserved_portal = 0;
	uint64_t retry_after_tick = 0;
};

static const SaveLoad _underground_train_desc[] = {
	SLE_VAR(UndergroundTrainSaveData, vehicle,               SLE_UINT32),
	SLE_VAR(UndergroundTrainSaveData, entry_reserved_portal, SLE_UINT32),
	SLE_VAR(UndergroundTrainSaveData, retry_after_tick,      SLE_UINT64),
};

/**
 * MLUT chunk: per-train underground state (the path a train holds for diving
 * into a portal, when it may try to reserve again). It was an empty stub
 * before SLV_METRO_RUNTIME_STATE.
 */
struct MLUTChunkHandler : ChunkHandler {
	MLUTChunkHandler() : ChunkHandler('MLUT', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_underground_train_desc);

		uint idx = 0;
		for (const auto &[train, state] : GetUndergroundTrainStates()) {
			SlSetArrayIndex(idx++);
			UndergroundTrainSaveData d;
			d.vehicle = train.base();
			d.entry_reserved_portal = state.entry_reserved_portal.base();
			d.retry_after_tick = state.retry_after_tick;
			SlObject(&d, _underground_train_desc);
		}
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_MULTILAYER_MAP)) return;
		if (IsSavegameVersionBefore(SLV_METRO_RUNTIME_STATE)) {
			SlTableHeader(_dummy_desc);
			while (SlIterateArray() != -1) { DummySL d; SlObject(&d, _dummy_desc); }
			return;
		}

		const std::vector<SaveLoad> slt = SlTableHeader(_underground_train_desc);
		while (SlIterateArray() != -1) {
			UndergroundTrainSaveData d;
			SlObject(&d, slt);
			UndergroundTrainState state;
			state.entry_reserved_portal = TileIndex(d.entry_reserved_portal);
			state.retry_after_tick = d.retry_after_tick;
			SetUndergroundTrainState(VehicleID(d.vehicle), state);
		}
	}
};

/** Flat struct for saving a portal exit claim. */
struct PortalClaimSaveData {
	uint32_t tile = 0;
	uint32_t train = 0;
};

static const SaveLoad _portal_claim_desc[] = {
	SLE_VAR(PortalClaimSaveData, tile,  SLE_UINT32),
	SLE_VAR(PortalClaimSaveData, train, SLE_UINT32),
};

/** MLPC chunk: the exit portals whose mouth a train below ground has claimed. */
struct MLPCChunkHandler : ChunkHandler {
	MLPCChunkHandler() : ChunkHandler('MLPC', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_portal_claim_desc);

		uint idx = 0;
		for (const auto &[tile, train] : GetPortalExitClaims()) {
			SlSetArrayIndex(idx++);
			PortalClaimSaveData d;
			d.tile = tile.base();
			d.train = train.base();
			SlObject(&d, _portal_claim_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_portal_claim_desc);
		while (SlIterateArray() != -1) {
			PortalClaimSaveData d;
			SlObject(&d, slt);
			ClaimPortalExit(TileIndex(d.tile), VehicleID(d.train));
		}
	}
};

/** Flat struct for saving a StationComplexNode. */
struct SCNodeSaveData {
	uint32_t tile_index = 0;
	uint32_t slice_id = 0;
	uint16_t station_id = 0;
	uint8_t role = 0;
	int16_t depth = 0;
	uint8_t exit_type = 0xFF; ///< StationExitType; 0xFF when the savegame predates exit types.
};

/** Stored exit type of the former automatic exit above each platform; such nodes are dropped on load. */
static constexpr uint8_t SC_LEGACY_IMPLICIT_EXIT = 2;

static const SaveLoad _scnode_save_desc[] = {
	SLE_VAR(SCNodeSaveData, tile_index,  SLE_UINT32),
	SLE_VAR(SCNodeSaveData, slice_id,    SLE_UINT32),
	SLE_VAR(SCNodeSaveData, station_id,  SLE_UINT16),
	SLE_VAR(SCNodeSaveData, role,        SLE_UINT8),
	SLE_VAR(SCNodeSaveData, depth,       SLE_INT16),
	SLE_CONDVAR(SCNodeSaveData, exit_type, SLE_UINT8, SLV_METRO_STATION_EXITS, SL_MAX_VERSION),
};

struct STCXChunkHandler : ChunkHandler {
	STCXChunkHandler() : ChunkHandler('STCX', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_scnode_save_desc);

		/* In station order, so the same game always saves the same way. */
		std::vector<uint16_t> station_ids;
		station_ids.reserve(_station_complexes.size());
		for (const auto &item : _station_complexes) station_ids.push_back(item.first);
		std::ranges::sort(station_ids);

		uint idx = 0;
		for (uint16_t station_id : station_ids) {
			for (const auto &node : _station_complexes.at(station_id).nodes) {
				SlSetArrayIndex(idx++);
				SCNodeSaveData d;
				d.tile_index = node.ref.tile.base();
				d.slice_id = node.ref.slice;
				d.station_id = node.station_id;
				d.role = static_cast<uint8_t>(node.role);
				d.depth = node.depth;
				d.exit_type = static_cast<uint8_t>(node.exit_type);
				SlObject(&d, _scnode_save_desc);
			}
		}
	}

	void Load() const override
	{
		if (IsSavegameVersionBefore(SLV_MULTILAYER_MAP)) return;

		const std::vector<SaveLoad> slt = SlTableHeader(_scnode_save_desc);

		_station_complexes.clear();

		uint dropped_exits = 0;

		int index;
		while ((index = SlIterateArray()) != -1) {
			SCNodeSaveData d;
			SlObject(&d, slt);

			/* The automatic exit above every platform no longer exists: passenger
			 * catchment only comes from exits the player built. */
			if (static_cast<StationRole>(d.role) == StationRole::ExitSurface && d.exit_type == SC_LEGACY_IMPLICIT_EXIT) {
				dropped_exits++;
				continue;
			}

			StationComplex &sc = GetOrCreateStationComplex(d.station_id);
			sc.station_id = d.station_id;

			StationComplexNode node;
			node.ref = TileRef{TileIndex(d.tile_index), d.slice_id};
			node.role = static_cast<StationRole>(d.role);
			node.station_id = d.station_id;
			node.depth = d.depth;
			node.exit_type = d.exit_type == static_cast<uint8_t>(StationExitType::Underpass) ? StationExitType::Underpass : StationExitType::Pavilion;
			sc.nodes.push_back(node);
		}

		if (IsSavegameVersionBefore(SLV_METRO_STATION_EXITS)) {
			/* Exits had no type: the exit a platform created on its own tile was the
			 * automatic one and is dropped; every other exit was built explicitly and
			 * becomes a pavilion (without a surface building, as it was). */
			for (auto &[station_id, sc] : _station_complexes) {
				std::set<uint32_t> platform_tiles;
				for (const auto &node : sc.nodes) {
					if (node.role == StationRole::Platform) platform_tiles.insert(node.ref.tile.base());
				}
				dropped_exits += static_cast<uint>(std::erase_if(sc.nodes, [&](const StationComplexNode &node) {
					return node.role == StationRole::ExitSurface && platform_tiles.contains(node.ref.tile.base());
				}));
			}
			std::erase_if(_station_complexes, [](const auto &item) { return item.second.nodes.empty(); });
		}
		if (dropped_exits != 0) {
			Debug(sl, 1, "Dropped {} automatic underground station exit(s) above platforms; catchment now only comes from built exits", dropped_exits);
		}
		InvalidateStationExitCache();
	}
};

static const MLSLChunkHandler MLSL;
static const MLCLChunkHandler MLCL;
static const MLUTChunkHandler MLUT;
static const STCXChunkHandler STCX;
static const MLPCChunkHandler MLPC;
static const ChunkHandlerRef multilayer_chunk_handlers[] = { MLSL, MLCL, MLUT, STCX, MLPC };
extern const ChunkHandlerTable _multilayer_chunk_handlers(multilayer_chunk_handlers);
