/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_complex.cpp Implementation of StationComplex management. */

#include "../stdafx.h"
#include "station_complex.h"
#include "multilayer_map.h"
#include "../station_base.h"
#include "../station_map.h"
#include "../settings_type.h"
#include "../tile_map.h"

#include "../safeguards.h"

/** Global registry of station complexes. */
std::unordered_map<uint16_t, StationComplex> _station_complexes;

/**
 * Find or create a StationComplex for a given station ID.
 * @param station_id The station pool index.
 * @return Reference to the StationComplex.
 */
StationComplex &GetOrCreateStationComplex(uint16_t station_id)
{
	auto it = _station_complexes.find(station_id);
	if (it != _station_complexes.end()) return it->second;

	auto &sc = _station_complexes[station_id];
	sc.station_id = station_id;
	return sc;
}

/**
 * Remove a StationComplex when the station is deleted.
 * @param station_id The station pool index.
 */
void RemoveStationComplex(uint16_t station_id)
{
	_station_complexes.erase(station_id);
	InvalidateStationExitCache();
}

/** Per-tile lookup of explicit exits: tile index -> (station id, node index). Rebuilt lazily. */
static std::unordered_map<uint32_t, std::pair<uint16_t, size_t>> _station_exit_cache;
/** Whether #_station_exit_cache must be rebuilt before use. */
static bool _station_exit_cache_dirty = true;

void InvalidateStationExitCache()
{
	_station_exit_cache_dirty = true;
}

/** Rebuild the per-tile lookup of explicit exits when it is stale. */
static void UpdateStationExitCache()
{
	if (!_station_exit_cache_dirty) return;
	_station_exit_cache.clear();
	for (const auto &[station_id, sc] : _station_complexes) {
		for (size_t i = 0; i < sc.nodes.size(); i++) {
			const StationComplexNode &n = sc.nodes[i];
			if (!n.IsExplicitExit()) continue;
			_station_exit_cache.emplace(n.ref.tile.base(), std::make_pair(station_id, i));
		}
	}
	_station_exit_cache_dirty = false;
}

const StationComplexNode *FindExplicitStationExitAt(TileIndex tile)
{
	if (_station_complexes.empty()) return nullptr;
	UpdateStationExitCache();
	auto it = _station_exit_cache.find(tile.base());
	if (it == _station_exit_cache.end()) return nullptr;
	auto sc_it = _station_complexes.find(it->second.first);
	if (sc_it == _station_complexes.end() || it->second.second >= sc_it->second.nodes.size()) {
		/* Someone changed the nodes without invalidating; recover. */
		InvalidateStationExitCache();
		UpdateStationExitCache();
		it = _station_exit_cache.find(tile.base());
		if (it == _station_exit_cache.end()) return nullptr;
		sc_it = _station_complexes.find(it->second.first);
	}
	const StationComplexNode &n = sc_it->second.nodes[it->second.second];
	if (!n.IsExplicitExit() || n.ref.tile != tile) return nullptr;
	return &n;
}

bool IsStationExitPavilionTile(TileIndex tile)
{
	const StationComplexNode *n = FindExplicitStationExitAt(tile);
	if (n == nullptr || n->exit_type != StationExitType::Pavilion) return false;
	return IsTileType(tile, TileType::Station) && GetStationIndex(tile) == StationID(n->station_id);
}

uint16_t ForgetExplicitStationExitAt(TileIndex tile)
{
	const StationComplexNode *n = FindExplicitStationExitAt(tile);
	if (n == nullptr) return UINT16_MAX;
	uint16_t station_id = n->station_id;
	auto sc_it = _station_complexes.find(station_id);
	if (sc_it != _station_complexes.end()) {
		std::erase_if(sc_it->second.nodes, [tile](const StationComplexNode &node) {
			return node.ref.tile == tile && node.IsExplicitExit();
		});
		if (sc_it->second.nodes.empty()) _station_complexes.erase(sc_it);
	}
	InvalidateStationExitCache();
	return station_id;
}

uint GetStationExitCatchmentRadius(StationExitType type)
{
	if (!_settings_game.station.modified_catchment) return CA_UNMODIFIED;
	switch (type) {
		case StationExitType::Pavilion:  return CA_BUS;
		case StationExitType::Underpass: return CA_BUS - 1;
		default: NOT_REACHED();
	}
}

/**
 * Check whether routing a train to this station must target a metro portal:
 * the station is an underground complex without any real surface rail
 * station tiles, so the surface pathfinder has nothing to aim at.
 */
bool StationUsesPortalRouting(uint16_t station_id)
{
	auto it = _station_complexes.find(station_id);
	if (it == _station_complexes.end()) return false;

	const Station *st = Station::GetIfValid(StationID(station_id));
	if (st == nullptr) return false;

	return st->train_station.tile == INVALID_TILE || !IsRailStationTile(st->train_station.tile);
}

void ResolveStationComplexesAfterLoad()
{
	InvalidateStationExitCache();
	for (auto it = _station_complexes.begin(); it != _station_complexes.end(); ) {
		Station *st = Station::GetIfValid(StationID(it->first));
		if (st == nullptr) {
			it = _station_complexes.erase(it);
			continue;
		}

		/* Note: do NOT clear st->rect / st->train_station here. AfterLoadGame has
		 * already rebuilt the rect from surface station tiles; for stations that
		 * mix surface parts with underground platforms wiping it would lose the
		 * surface tiles. Resolved nodes only EXTEND the rect below. */
		std::vector<StationComplexNode> resolved;
		resolved.reserve(it->second.nodes.size());
		TileIndex first_platform_tile = INVALID_TILE;

		for (StationComplexNode node : it->second.nodes) {
			node.station_id = it->first;
			if (!IsValidTile(node.ref.tile)) continue;

			switch (node.role) {
				case StationRole::Platform: {
					SliceID sid = _multilayer_map.FindSliceAt(node.ref.tile, node.depth);
					if (sid == INVALID_SLICE_ID) continue;

					const TileSlice &slice = _multilayer_map.GetSlice(sid);
					if (slice.kind != SliceKind::StationTile || slice.station.station_id != it->first) continue;

					node.ref.slice = sid;
					st->rect.BeforeAddTile(node.ref.tile, StationRect::ADD_FORCE);
					if (first_platform_tile == INVALID_TILE) first_platform_tile = node.ref.tile;
					break;
				}

				case StationRole::ExitSurface:
					node.ref.slice = _multilayer_map.FindSliceByKind(node.ref.tile, SliceKind::Surface);
					if (node.ref.slice == INVALID_SLICE_ID) continue;
					st->rect.BeforeAddTile(node.ref.tile, StationRect::ADD_FORCE);
					break;

				case StationRole::PortalTrack: {
					SliceID sid = _multilayer_map.FindSliceAt(node.ref.tile, node.depth);
					if (sid == INVALID_SLICE_ID) continue;
					node.ref.slice = sid;
					break;
				}
			}

			resolved.push_back(node);
		}

		if (resolved.empty()) {
			it = _station_complexes.erase(it);
			continue;
		}

		it->second.station_id = it->first;
		it->second.nodes = std::move(resolved);

		if (first_platform_tile != INVALID_TILE) {
			if (!st->facilities.Test(StationFacility::Train)) st->AddFacility(StationFacility::Train, first_platform_tile);
			if (st->train_station.tile == INVALID_TILE) st->train_station = TileArea(first_platform_tile, 1, 1);
		}

		++it;
	}
}
