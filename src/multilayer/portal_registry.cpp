/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file portal_registry.cpp BFS-based portal connection precomputation. */

#include "../stdafx.h"
#include "portal_registry.h"
#include "multilayer_map.h"
#include "underground_train.h"
#include "../pathfinder/yapf/yapf3d_rail.h"
#include "../map_func.h"
#include "../track_func.h"
#include "../direction_func.h"
#include "../debug.h"

#include "../safeguards.h"

#include <unordered_map>
#include <unordered_set>
#include <queue>

/** Global registry: entry_tile → list of connections. */
static std::unordered_map<uint32_t, std::vector<PortalConnection>> _portal_connections;

/** Empty vector for tiles with no connections. */
static const std::vector<PortalConnection> _empty_connections;

const std::vector<PortalConnection> &GetPortalConnections(TileIndex entry_tile)
{
	auto it = _portal_connections.find(entry_tile.base());
	if (it != _portal_connections.end()) return it->second;
	return _empty_connections;
}

void ClearPortalConnections()
{
	_portal_connections.clear();
}

/**
 * BFS from a portal tile through the underground network, the way a train
 * entering it would run: along trackdirs, round curves, over ramps to other
 * levels and never through a one-way signal from behind. Records every exit
 * portal reached (the first time, with the shortest way).
 */
static void BFSFromPortal(TileIndex entry_tile, SliceID portal_sid)
{
	const TileSlice &entry_ps = _multilayer_map.GetSlice(portal_sid);
	DiagDirection entry_dir = GetPortalDiveDirection(entry_tile, entry_ps);

	struct Node {
		TileRef ref;
		Trackdir td;
		int distance;
	};

	std::queue<Node> queue;
	std::unordered_set<uint64_t> visited;
	auto key = [](const TileRef &ref, Trackdir td) {
		return (static_cast<uint64_t>(ref.tile.base()) << 32) | (static_cast<uint64_t>(ref.slice) << 4) | static_cast<uint64_t>(td);
	};

	/* The train stands on the portal slice facing into the tunnel. */
	TileRef start{entry_tile, portal_sid};
	Trackdir start_td = DiagDirToDiagTrackdir(entry_dir);
	queue.push({start, start_td, 0});
	visited.insert(key(start, start_td));

	std::vector<Yapf3DNeighbor> neighbors;
	while (!queue.empty()) {
		Node current = queue.front();
		queue.pop();

		Yapf3DEnumerateNeighbors(current.ref, current.td, neighbors, nullptr);
		for (const Yapf3DNeighbor &nb : neighbors) {
			if (!visited.insert(key(nb.ref, nb.td)).second) continue;

			const TileSlice &slice = _multilayer_map.GetSlice(nb.ref.slice);
			if (slice.kind == SliceKind::Portal) {
				if (nb.ref.tile == entry_tile) continue;
				/* Only a portal facing along the track that leads into it is a
				 * way out; one across the line (or facing the other way) is a
				 * dead end, a train would dive in and never come out there. */
				DiagDirection out_dir = ReverseDiagDir(GetPortalDiveDirection(nb.ref.tile, slice));
				if (TrackdirToExitdir(current.td) != out_dir || TrackdirToExitdir(nb.td) != out_dir) continue;
				/* An exit portal; the way out continues on the surface in the
				 * direction the train moves when it leaves the portal. Keep the
				 * first (shortest) way to each exit. */
				auto &conns = _portal_connections[entry_tile.base()];
				bool known = false;
				for (const PortalConnection &c : conns) {
					if (c.exit_tile == nb.ref.tile) {
						known = true;
						break;
					}
				}
				if (known) continue;

				PortalConnection conn;
				conn.entry_tile = entry_tile;
				conn.exit_tile = nb.ref.tile;
				conn.path_length = current.distance + 1;
				conn.entry_dir = entry_dir;
				conn.exit_dir = ReverseDiagDir(GetPortalDiveDirection(nb.ref.tile, slice));
				conn.depth = slice.span.z_bot;
				conns.push_back(conn);
				/* Don't continue BFS through a portal: it's an endpoint. */
				continue;
			}

			queue.push({nb.ref, nb.td, current.distance + 1});
		}
	}
}

/** Number of live PortalConnectionsBatch objects. */
static int _portal_batch_depth = 0;
/** A recompute was asked for while a batch was live. */
static bool _portal_batch_dirty = false;

PortalConnectionsBatch::PortalConnectionsBatch()
{
	_portal_batch_depth++;
}

PortalConnectionsBatch::~PortalConnectionsBatch()
{
	if (--_portal_batch_depth > 0 || !_portal_batch_dirty) return;
	_portal_batch_dirty = false;
	RecomputePortalConnections();
}

/**
 * Recompute all portal connections by scanning the map for Portal slices
 * and running BFS from each one.
 */
void RecomputePortalConnections()
{
	if (_portal_batch_depth > 0) {
		_portal_batch_dirty = true;
		return;
	}

	_portal_connections.clear();

	if (!_multilayer_map.IsAllocated()) return;

	int portal_count = 0;

	/* Only columns known to contain a Portal slice need visiting — the index is
	 * maintained by InsertSlice/RemoveSliceFromColumn, so no full map scan. */
	for (uint32_t tile_idx : _multilayer_map.GetPortalColumns()) {
		TileIndex tile(tile_idx);
		const TileColumn &col = _multilayer_map.GetColumn(tile);

		for (SliceID sid : col.slices) {
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind == SliceKind::Portal) {
				BFSFromPortal(tile, sid);
				portal_count++;
			}
		}
	}

	int connection_count = 0;
	for (const auto &[key, conns] : _portal_connections) {
		connection_count += static_cast<int>(conns.size());
	}

	Debug(misc, 1, "Portal registry: {} portals, {} connections", portal_count, connection_count);
}
