/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file yapf3d_rail.cpp YAPF3D: A* pathfinder for underground rail tracks. */

#include "../../stdafx.h"
#include "yapf3d_rail.h"
#include "../../multilayer/multilayer_map.h"
#include "../../multilayer/underground_train.h"
#include "../../multilayer/underground_signal.h"
#include "../../pbs.h"
#include "../../debug.h"
#include "../../map_func.h"
#include "../../signal_func.h"
#include "../../track_func.h"
#include "../../direction_func.h"
#include "../../train.h"
#include "../../rail_map.h"
#include "../follow_track.hpp"

#include <queue>

#include "../../safeguards.h"

/** Maximum nodes to explore before giving up. */
static constexpr int YAPF3D_MAX_NODES = 10000;

/** Cost of one underground tile. */
static constexpr int YAPF3D_TILE_COST = 100;

/** Extra cost for curves underground. */
static constexpr int YAPF3D_CURVE_PENALTY = 200;

/** Extra cost for turning round at the buffer stop of a terminus platform. */
static constexpr int YAPF3D_REVERSE_PENALTY = 2000;

/** Extra cost for passing a signal showing red (the block behind it is held by another train): a detour of up to this many tiles is preferred to waiting. */
static constexpr int YAPF3D_RED_SIGNAL_PENALTY = 10 * YAPF3D_TILE_COST;

/** Extra cost for passing a red pre-signal exit (or combo) signal, like rail_firstred_exit_penalty: the way into a free platform of a pre-signal block wins. */
static constexpr int YAPF3D_RED_EXIT_SIGNAL_PENALTY = 100 * YAPF3D_TILE_COST;

/** Extra cost per tile of track held by another train, like rail_pbs_cross_penalty (3 tiles). */
static constexpr int YAPF3D_RESERVED_TRACK_PENALTY = 3 * YAPF3D_TILE_COST;

/** Extra cost per platform tile held by another train, like rail_pbs_station_penalty (8 tiles). */
static constexpr int YAPF3D_RESERVED_PLATFORM_PENALTY = 8 * YAPF3D_TILE_COST;

/** Extra cost for passing a path signal from behind, like rail_pbs_signal_back_penalty. */
static constexpr int YAPF3D_PBS_SIGNAL_BACK_PENALTY = 15 * YAPF3D_TILE_COST;

/**
 * Enumerate underground track neighbors from a given TileRef + Trackdir.
 *
 * For each exit direction of the current trackdir, checks the adjacent tile
 * at the same Z depth for compatible Track slices.
 */
/**
 * Whether a train may run on an underground slice: like on the surface only on
 * rail it has power on (Train::compatible_railtypes), so that a metro train is
 * never routed onto track of another type and stranded there.
 * @param v The train, or nullptr for any train.
 * @param s The slice.
 * @return True if the train may enter it.
 */
static bool CanTrainRunOnSlice(const Train *v, const TileSlice &s)
{
	if (v == nullptr) return true;
	uint8_t rt;
	switch (s.kind) {
		case SliceKind::Track: rt = s.track.railtype; break;
		case SliceKind::StationTile: rt = s.station.railtype; break;
		case SliceKind::Ramp: rt = s.ramp.railtype; break;
		case SliceKind::Portal: rt = s.portal.railtype; break;
		default: return true;
	}
	return rt < RAILTYPE_END && v->compatible_railtypes.Test(static_cast<RailType>(rt));
}

bool CanTrainRunOnUndergroundSlice(const Train *v, const TileSlice &s)
{
	return CanTrainRunOnSlice(v, s);
}

/**
 * Whether another train holds a platform from the given tile on to its end:
 * a train calls at the far end of a platform, so a platform whose first tile
 * is free may still be taken further on.
 * @param ref The platform tile entered.
 * @param td The trackdir it is entered with.
 * @param owner The train asking.
 * @return True if a tile of the platform ahead is held by another train.
 */
static bool IsUndergroundPlatformAheadHeld(const TileRef &ref, Trackdir td, VehicleID owner)
{
	const TileSlice &first = _multilayer_map.GetSlice(ref.slice);
	uint16_t station = first.station.station_id;
	int16_t depth = first.span.z_bot;
	DiagDirection dir = TrackdirToExitdir(td);
	TileRef cur = ref;
	for (uint guard = 0; guard < 64; guard++) {
		if (IsUndergroundTrackReservedByOther(cur, TrackdirToTrack(td), owner)) return true;
		TileIndex next = AddTileIndexDiffCWrap(cur.tile, TileIndexDiffCByDiagDir(dir));
		if (next == INVALID_TILE) return false;
		SliceID sid = _multilayer_map.FindSliceAt(next, depth);
		if (sid == INVALID_SLICE_ID) return false;
		const TileSlice &s = _multilayer_map.GetSlice(sid);
		if (s.kind != SliceKind::StationTile || s.station.station_id != station) return false;
		cur = TileRef{next, sid};
	}
	return false;
}

/**
 * Whether a train coming out of a portal has a way on along the surface. The
 * search ends at an exit portal and leaves the rest to the surface pathfinder,
 * so it must not take a mouth beyond which the line ends or is barred by a
 * one-way signal seen from behind (the vanilla pathfinder's dead end): out of
 * such a mouth, say the wrong way onto the other track of a double line, the
 * train turns round on the surface and is sent the same way out again, for
 * good. The line is followed up to the first signal along the way, a junction,
 * a dive back underground or anything but plain track (a station, a depot).
 * @param mouth The portal tile.
 * @param outward The direction of travel out of the portal.
 * @param v The train.
 * @return True if the train can go on from the mouth.
 */
static bool HasSurfaceWayOnFromMouth(TileIndex mouth, DiagDirection outward, const Train *v)
{
	TileIndex tile = mouth;
	Trackdir td = DiagDirToDiagTrackdir(outward);
	for (int i = 0; i < 16; i++) {
		/* Stations, depots, crossings, tunnels and bridges: left to the surface pathfinder. */
		if (!IsTileType(tile, TileType::Railway) || IsRailDepot(tile)) return true;
		if (HasSignals(tile) && HasSignalOnTrack(tile, TrackdirToTrack(td))) {
			if (HasSignalOnTrackdir(tile, td)) return true;
			if (IsOnewaySignal(tile, TrackdirToTrack(td))) return false;
		}
		CFollowTrackRail ft(v);
		if (!ft.Follow(tile, td)) return false;
		if (ft.is_portal || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) return true;
		tile = ft.new_tile;
		td = FindFirstTrackdir(ft.new_td_bits);
	}
	return true;
}

void Yapf3DEnumerateNeighbors(const TileRef &ref, Trackdir td,
                               std::vector<Yapf3DNeighbor> &out_neighbors, const Train *v)
{
	out_neighbors.clear();
	VehicleID owner = v != nullptr ? v->index : VehicleID::Invalid();

	const TileSlice &cur_slice = _multilayer_map.GetSlice(ref.slice);
	if (cur_slice.kind != SliceKind::Track && cur_slice.kind != SliceKind::Portal &&
			cur_slice.kind != SliceKind::StationTile && cur_slice.kind != SliceKind::Ramp) return;

	/* Get the exit direction from current trackdir. */
	DiagDirection exitdir = TrackdirToExitdir(td);

	/* Leaving a ramp continues at the depth of the exit side. */
	int16_t depth;
	if (cur_slice.kind == SliceKind::Ramp) {
		DiagDirection rd = static_cast<DiagDirection>(cur_slice.ramp.direction);
		if (DiagDirToAxis(exitdir) != DiagDirToAxis(rd)) return;
		depth = (exitdir == rd) ? cur_slice.ramp.exit_depth : cur_slice.ramp.entry_depth;
	} else {
		depth = cur_slice.span.z_bot;
	}

	/* Calculate neighbor tile. */
	TileIndexDiffC diff = TileIndexDiffCByDiagDir(exitdir);
	int nx = TileX(ref.tile) + diff.x;
	int ny = TileY(ref.tile) + diff.y;

	/* Bounds check. */
	if (nx < 0 || ny < 0 || nx >= (int)Map::SizeX() || ny >= (int)Map::SizeY()) return;

	TileIndex neighbor_tile = TileXY(nx, ny);

	/* Find a Track or Portal slice at the same depth in the neighbor column. */
	SliceID neighbor_sid = _multilayer_map.FindSliceAt(neighbor_tile, depth);
	if (neighbor_sid == INVALID_SLICE_ID) return;

	const TileSlice &neighbor_slice = _multilayer_map.GetSlice(neighbor_sid);
	if (neighbor_slice.kind != SliceKind::Track && neighbor_slice.kind != SliceKind::Portal &&
			neighbor_slice.kind != SliceKind::StationTile && neighbor_slice.kind != SliceKind::Ramp) return;
	if (!CanTrainRunOnSlice(v, neighbor_slice)) return;

	TileRef neighbor_ref{neighbor_tile, neighbor_sid};

	/* `TrackEnterdirToTrackdir` expects the movement direction into the tile,
	 * not the side we are coming from. For the neighboring tile that is the
	 * same direction we used to leave the current tile. */
	DiagDirection move_dir = exitdir;

	/* The penalty of entering a track of a track or ramp slice with signals,
	 * or -1 when a one-way signal seen from behind bars it. */
	auto signal_penalty = [&](Track t, Trackdir new_td) -> int {
		if (!HasUndergroundSignalOnTrack(neighbor_slice, t)) return 0;
		/* Like on the surface one-way signals (every type but the path
		 * signal) cannot be passed from behind. */
		if (HasUndergroundOnewaySignalBlockingTrackdir(neighbor_slice, new_td)) return -1;
		if (HasUndergroundSignalOnTrackdir(neighbor_slice, new_td)) {
			/* A signal showing red (its block held by another train) is no
			 * wall, like on the surface: the path may lead up to it and the
			 * train waits there; a free detour is preferred, and much more so
			 * one around a red pre-signal exit. The state of block signals
			 * takes a flood fill of their block: only for real trains, not
			 * for the network survey of the portal registry. */
			SignalType type = GetUndergroundSignalType(neighbor_slice, t);
			if ((IsPbsSignal(type) || v != nullptr) && GetUndergroundSignalState(neighbor_ref, new_td, owner) == SIGNAL_STATE_RED) {
				return (type == SIGTYPE_EXIT || type == SIGTYPE_COMBO) ? YAPF3D_RED_EXIT_SIGNAL_PENALTY : YAPF3D_RED_SIGNAL_PENALTY;
			}
			return 0;
		}
		/* The back of a path signal, like rail_pbs_signal_back_penalty. */
		return YAPF3D_PBS_SIGNAL_BACK_PENALTY;
	};

	if (neighbor_slice.kind == SliceKind::Track || neighbor_slice.kind == SliceKind::StationTile) {
		TrackBits tracks = (neighbor_slice.kind == SliceKind::Track)
			? static_cast<TrackBits>(neighbor_slice.track.tracks)
			: static_cast<TrackBits>(neighbor_slice.station.tracks);
		TrackBits reachable = DiagdirReachesTracks(move_dir) & tracks;

		for (Track t = TRACK_BEGIN; t < TRACK_END; t++) {
			if (!(reachable & TrackToTrackBits(t))) continue;
			Trackdir new_td = TrackEnterdirToTrackdir(t, move_dir);

			int penalty = signal_penalty(t, new_td);
			if (penalty < 0) continue;
			/* Track another train holds or stands on, like rail_pbs_cross_penalty
			 * and rail_pbs_station_penalty: a free way, a free platform above all,
			 * is preferred to waiting for the held one. */
			if (v != nullptr) {
				if (neighbor_slice.kind == SliceKind::StationTile) {
					/* The whole platform ahead counts: the train would stop at its far end. */
					if (IsUndergroundPlatformAheadHeld(neighbor_ref, new_td, owner)) penalty += YAPF3D_RESERVED_PLATFORM_PENALTY;
				} else if (IsUndergroundTrackReservedByOther(neighbor_ref, t, owner)) {
					penalty += YAPF3D_RESERVED_TRACK_PENALTY;
				}
			}

			out_neighbors.push_back({neighbor_ref, new_td, penalty});
		}
	} else if (neighbor_slice.kind == SliceKind::Ramp) {
		/* Ramp — enterable only along its axis at the depth of the entered side. */
		DiagDirection rd = static_cast<DiagDirection>(neighbor_slice.ramp.direction);
		if (DiagDirToAxis(move_dir) != DiagDirToAxis(rd)) return;
		int16_t required_depth = (move_dir == rd) ? neighbor_slice.ramp.entry_depth : neighbor_slice.ramp.exit_depth;
		if (depth != required_depth) return;
		Trackdir new_td = DiagDirToDiagTrackdir(move_dir);
		int penalty = signal_penalty(TrackdirToTrack(new_td), new_td);
		if (penalty < 0) return;
		/* A ramp another train holds or stands on, like rail_pbs_cross_penalty. */
		if (v != nullptr && IsUndergroundTrackReservedByOther(neighbor_ref, DiagDirToDiagTrack(move_dir), owner)) penalty += YAPF3D_RESERVED_TRACK_PENALTY;
		out_neighbors.push_back({neighbor_ref, new_td, penalty});
	} else if (neighbor_slice.kind == SliceKind::Portal) {
		/* Portal exit — return to surface. Only from the tunnel side: a portal
		 * reached from behind (track built on its surface side) is a dead end. */
		if (exitdir != ReverseDiagDir(GetPortalDiveDirection(neighbor_tile, neighbor_slice))) return;
		if (v != nullptr && !HasSurfaceWayOnFromMouth(neighbor_tile, exitdir, v)) return;
		Trackdir new_td = DiagDirToDiagTrackdir(exitdir);
		out_neighbors.push_back({neighbor_ref, new_td, 0});
	}
}

/* Forward declaration — defined after Yapf3DFindPath. */
static void RunYapf3DSearch(
	std::unordered_map<Yapf3DNode::Key, Yapf3DNode, Yapf3DNodeKeyHash> &nodes,
	std::vector<Yapf3DNode *> &open,
	TileIndex target_tile,
	Yapf3DResult &result,
	StationID target_station = StationID::Invalid(),
	const Train *v = nullptr);

/**
 * YAPF3D A* search implementation.
 */
Yapf3DResult Yapf3DFindPath(const Train *v, TileIndex entry_tile, Trackdir entry_td, TileIndex target_tile, StationID target_station)
{
	Yapf3DResult result;

	/* Find the portal slice at the entry tile. */
	SliceID portal_sid = _multilayer_map.FindSliceByKind(entry_tile, SliceKind::Portal);
	if (portal_sid == INVALID_SLICE_ID) {
		/* No portal — can't enter underground. */
		return result;
	}

	const TileSlice &portal_slice = _multilayer_map.GetSlice(portal_sid);
	int16_t depth = portal_slice.span.z_bot;

	/* Find the underground Track slice connected to this portal. */
	DiagDirection exitdir = TrackdirToExitdir(entry_td);
	TileIndexDiffC diff = TileIndexDiffCByDiagDir(exitdir);
	int nx = TileX(entry_tile) + diff.x;
	int ny = TileY(entry_tile) + diff.y;

	if (nx < 0 || ny < 0 || nx >= (int)Map::SizeX() || ny >= (int)Map::SizeY()) return result;

	TileIndex first_ug_tile = TileXY(nx, ny);
	SliceID first_sid = _multilayer_map.FindSliceAt(first_ug_tile, depth);
	if (first_sid == INVALID_SLICE_ID) return result;

	/* Initialize the open set with the first underground tile. */
	std::unordered_map<Yapf3DNode::Key, Yapf3DNode, Yapf3DNodeKeyHash> nodes;
	std::vector<Yapf3DNode *> open; /* Simple open list (not a real priority queue for MVP). */

	TileRef start_ref{first_ug_tile, first_sid};
	DiagDirection move_dir = exitdir;

	/* Check which tracks are available on the first underground tile. */
	const TileSlice &first_slice = _multilayer_map.GetSlice(first_sid);
	if (!CanTrainRunOnSlice(v, portal_slice) || !CanTrainRunOnSlice(v, first_slice)) return result;
	TrackBits first_tracks;
	switch (first_slice.kind) {
		case SliceKind::Track: first_tracks = static_cast<TrackBits>(first_slice.track.tracks); break;
		case SliceKind::StationTile: first_tracks = static_cast<TrackBits>(first_slice.station.tracks); break;
		case SliceKind::Ramp: {
			DiagDirection rd = static_cast<DiagDirection>(first_slice.ramp.direction);
			if (DiagDirToAxis(move_dir) != DiagDirToAxis(rd)) return result;
			if (depth != ((move_dir == rd) ? first_slice.ramp.entry_depth : first_slice.ramp.exit_depth)) return result;
			first_tracks = TrackToTrackBits(AxisToTrack(DiagDirToAxis(rd)));
			break;
		}
		default: return result;
	}
	TrackBits reachable = DiagdirReachesTracks(move_dir) & first_tracks;
	if (reachable == TRACK_BIT_NONE) return result;

	/* Create start nodes for each reachable trackdir. */
	for (Track t = TRACK_BEGIN; t < TRACK_END; t++) {
		if (!(reachable & TrackToTrackBits(t))) continue;
		Trackdir start_td = TrackEnterdirToTrackdir(t, move_dir);
		/* A one-way signal seen from behind bars the first tile like any other. */
		if (HasUndergroundOnewaySignalBlockingTrackdir(first_slice, start_td)) continue;

		Yapf3DNode::Key key{start_ref, start_td};
		auto &node = nodes[key];
		node.ref = start_ref;
		node.td = start_td;
		node.cost = YAPF3D_TILE_COST;
		node.estimate = target_tile != INVALID_TILE ? DistanceManhattan(first_ug_tile, target_tile) * YAPF3D_TILE_COST : 0;
		node.parent = nullptr;
		open.push_back(&node);
	}

	RunYapf3DSearch(nodes, open, target_tile, result, target_station, v);
	return result;
}

/**
 * Shared A* loop used by both Yapf3DFindPath and Yapf3DFindPathFromRef.
 */
static void RunYapf3DSearch(
	std::unordered_map<Yapf3DNode::Key, Yapf3DNode, Yapf3DNodeKeyHash> &nodes,
	std::vector<Yapf3DNode *> &open,
	TileIndex target_tile,
	Yapf3DResult &result,
	StationID target_station,
	const Train *v)
{
	/* Min-heap over total cost with lazy deletion: when a node's cost improves it
	 * is pushed again and the stale entry is skipped on pop. The comparator only
	 * looks at the cost key (never at pointers), so heap order stays deterministic
	 * across clients. */
	using OpenEntry = std::pair<int, Yapf3DNode *>;
	auto cost_greater = [](const OpenEntry &a, const OpenEntry &b) { return a.first > b.first; };
	std::priority_queue<OpenEntry, std::vector<OpenEntry>, decltype(cost_greater)> queue(cost_greater);
	for (Yapf3DNode *n : open) queue.emplace(n->total(), n);

	int iterations = 0;
	Yapf3DNode *best_portal_node = nullptr;
	int best_portal_score = 0;
	Yapf3DNode *best_target_node = nullptr;

	while (!queue.empty() && iterations < YAPF3D_MAX_NODES) {
		auto [key, current] = queue.top();
		queue.pop();
		if (current->closed || key != current->total()) continue; /* Stale heap entry. */

		iterations++;
		current->closed = true;

		/* With a target station any of its platforms will do: the nearest one may
		 * lie on a track that cannot be reached in the right direction. */
		bool at_target = target_tile != INVALID_TILE && current->ref.tile == target_tile;
		if (!at_target && target_station != StationID::Invalid()) {
			const TileSlice &ts = _multilayer_map.GetSlice(current->ref.slice);
			at_target = ts.kind == SliceKind::StationTile && ts.station.station_id == target_station.base();
		}
		if (at_target) {
			if (best_target_node == nullptr || current->cost < best_target_node->cost) {
				best_target_node = current;
			}
			continue;
		}

		const TileSlice &cur_slice = _multilayer_map.GetSlice(current->ref.slice);
		/* An exit to the surface. Without a target it is the goal; with one it is
		 * the fallback when the target lies beyond a surface section of the line
		 * (the surface pathfinder takes over from there): the exit nearest to the
		 * target, counting the way to it. */
		if (cur_slice.kind == SliceKind::Portal && (target_tile == INVALID_TILE || current->parent != nullptr)) {
			int score = current->cost + (target_tile != INVALID_TILE ? DistanceManhattan(current->ref.tile, target_tile) * YAPF3D_TILE_COST : 0);
			if (best_portal_node == nullptr || score < best_portal_score) {
				best_portal_node = current;
				best_portal_score = score;
			}
			continue;
		}

		std::vector<Yapf3DNeighbor> neighbors;
		Yapf3DEnumerateNeighbors(current->ref, current->td, neighbors, v);
		/* A terminus: at the buffer stop of a platform a train turns round, so
		 * the search may too. Destinations behind such a terminus are then found
		 * (the reservation stops at the buffer stop, see Yapf3DReservePath). */
		if (neighbors.empty() && cur_slice.kind == SliceKind::StationTile) {
			neighbors.push_back({current->ref, ReverseTrackdir(current->td), 0});
		}

		for (const Yapf3DNeighbor &nb : neighbors) {
			const TileRef &nref = nb.ref;
			Trackdir ntd = nb.td;
			Yapf3DNode::Key nkey{nref, ntd};

			auto it = nodes.find(nkey);
			if (it != nodes.end() && it->second.closed) continue;

			int new_cost = current->cost + YAPF3D_TILE_COST + nb.penalty;
			if (nref.tile == current->ref.tile) {
				new_cost += YAPF3D_REVERSE_PENALTY;
			} else if (TrackdirToTrack(current->td) != TrackdirToTrack(ntd)) {
				new_cost += YAPF3D_CURVE_PENALTY;
			}

			int estimate = (target_tile != INVALID_TILE)
				? DistanceManhattan(nref.tile, target_tile) * YAPF3D_TILE_COST
				: 0;

			if (it == nodes.end()) {
				auto &node = nodes[nkey];
				node.ref = nref;
				node.td = ntd;
				node.cost = new_cost;
				node.estimate = estimate;
				node.parent = current;
				queue.emplace(node.total(), &node);
			} else if (new_cost < it->second.cost) {
				it->second.cost = new_cost;
				it->second.parent = current;
				queue.emplace(it->second.total(), &it->second);
			}
		}
	}

	if (best_target_node == nullptr && best_portal_node == nullptr) {
		Debug(misc, 2, "yapf3d: no path for train {} (compatible 0x{:X}) target ({},{}) station {}: {} iterations{}, {} nodes, start ({},{}) td {}",
			v != nullptr ? (int)v->index.base() : -1, v != nullptr ? v->compatible_railtypes.base() : 0,
			target_tile == INVALID_TILE ? -1 : (int)TileX(target_tile), target_tile == INVALID_TILE ? -1 : (int)TileY(target_tile),
			target_station == StationID::Invalid() ? -1 : (int)target_station.base(), iterations, iterations >= YAPF3D_MAX_NODES ? " (node limit)" : "",
			nodes.size(), open.empty() ? -1 : (int)TileX(open.front()->ref.tile), open.empty() ? -1 : (int)TileY(open.front()->ref.tile), open.empty() ? -1 : (int)open.front()->td);
	}

	Yapf3DNode *best_node = best_target_node != nullptr ? best_target_node : best_portal_node;
	if (best_node != nullptr) {
		result.path_found = true;
		result.exit_portal = best_node->ref;
		result.total_cost = best_node->cost;

		Yapf3DNode *n = best_node;
		while (n->parent != nullptr) {
			result.path.emplace_back(n->ref, n->td);
			if (n->parent->parent == nullptr) result.first_trackdir = n->parent->td;
			n = n->parent;
		}
		result.path.emplace_back(n->ref, n->td);
		if (n->parent == nullptr) result.first_trackdir = n->td;
	}
}

/**
 * Find a path from an already-underground position (e.g. stopped at a signal).
 */
Yapf3DResult Yapf3DFindPathFromRef(TileRef start_ref, Trackdir start_td, TileIndex target_tile, StationID target_station, const Train *v)
{
	Yapf3DResult result;

	if (!start_ref.IsValid() || !IsValidTrackdir(start_td)) return result;
	if (!_multilayer_map.IsValidSlice(start_ref.slice)) return result;

	const TileSlice &start_slice = _multilayer_map.GetSlice(start_ref.slice);
	/* Portal is a valid start: a train standing on the portal slice (e.g. just
	 * after entering, or reversed onto it) must still be able to find a path,
	 * otherwise it stays marked Stuck forever. Ramps carry trains too. */
	if (start_slice.kind != SliceKind::Track && start_slice.kind != SliceKind::StationTile &&
			start_slice.kind != SliceKind::Portal && start_slice.kind != SliceKind::Ramp) return result;

	std::unordered_map<Yapf3DNode::Key, Yapf3DNode, Yapf3DNodeKeyHash> nodes;
	std::vector<Yapf3DNode *> open;

	Yapf3DNode::Key key{start_ref, start_td};
	auto &node = nodes[key];
	node.ref = start_ref;
	node.td = start_td;
	node.cost = 0;
	node.estimate = (target_tile != INVALID_TILE)
		? DistanceManhattan(start_ref.tile, target_tile) * YAPF3D_TILE_COST
		: 0;
	node.parent = nullptr;
	open.push_back(&node);

	RunYapf3DSearch(nodes, open, target_tile, result, target_station, v);
	return result;
}

/**
 * Reserve underground tracks along a found path.
 * Path is stored in reverse order (exit→start), so we iterate backwards.
 * Reserves all Track/StationTile slices, stops at first PBS signal after start.
 */
bool Yapf3DReservePath(const Yapf3DResult &result, bool skip_first_tile, VehicleID owner, TileIndex *exit_reached,
		const std::function<bool(TileIndex)> &may_exit)
{
	if (exit_reached != nullptr) *exit_reached = INVALID_TILE;
	if (!result.path_found || result.path.empty()) return false;

	/* Path is in reverse order: [exit_portal, ..., start]. Walk from start to exit.
	 * Track what was actually reserved in THIS call so a conflict only rolls back
	 * our own reservations — never the train's own tile skipped via skip_first_tile
	 * or tiles held by an earlier reservation. */
	std::vector<std::pair<TileRef, Track>> reserved_here;
	auto roll_back = [&reserved_here]() {
		for (const auto &[undo_ref, undo_track] : reserved_here) UnreserveUndergroundTrack(undo_ref, undo_track);
		return false;
	};
	bool first = true;
	bool stopped_at_signal = false;
	TileRef last_ref{};
	Trackdir last_td = INVALID_TRACKDIR;
	for (int i = static_cast<int>(result.path.size()) - 1; i >= 0; i--) {
		const auto &[ref, td] = result.path[i];
		if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice)) continue;

		/* The path turns round at a buffer stop here: like on the surface the
		 * reservation ends at the end of the line; the train reserves anew
		 * after it has reversed. */
		if (last_ref.IsValid() && ref.tile == last_ref.tile && td == ReverseTrackdir(last_td)) {
			stopped_at_signal = true;
			break;
		}

		const TileSlice &slice = _multilayer_map.GetSlice(ref.slice);
		Track reserved_track = TrackdirToTrack(td);

		if (skip_first_tile && first) {
			first = false;
			last_ref = ref;
			last_td = td;
			continue;
		}

		if (slice.kind == SliceKind::Track || slice.kind == SliceKind::StationTile || slice.kind == SliceKind::Ramp) {
			/* Conflict — roll back exactly what we reserved so far (never what the
			 * train held before this call, e.g. the tiles it stands on). */
			bool held_before = (GetUndergroundReservedTrackbits(ref, owner) & TrackToTrackBits(reserved_track)) != TRACK_BIT_NONE;
			if (!TryReserveUndergroundTrack(ref, reserved_track, owner)) return roll_back();
			if (!held_before) reserved_here.emplace_back(ref, reserved_track);
			last_ref = ref;
			last_td = td;

			/* A PBS signal ahead of the train ends the reservation: the safe
			 * waiting position. Also on the first tile, which the train has yet
			 * to enter when it comes through a portal (the tile it stands on is
			 * skipped above): reserving past that signal made the train see its
			 * own reservation behind it as a blocked signal and stop for good. */
			if (HasUndergroundSignalOnTrackdir(slice, td)) {
				stopped_at_signal = true;
				break; /* Safe waiting position found. */
			}
		}
		/* Skip Portal slices — they don't have tracks to reserve. */

		first = false;
	}

	/* The reservation runs all the way out of an exit portal. */
	if ((exit_reached != nullptr || may_exit) && !stopped_at_signal && result.path.size() > 1) {
		const TileRef &end = result.path[0].first;
		if (end.IsValid() && _multilayer_map.IsValidSlice(end.slice) && _multilayer_map.GetSlice(end.slice).kind == SliceKind::Portal) {
			/* Coming out is like passing a signal into the surface block in
			 * front of the mouth: refused, the train must not enter the last
			 * underground block before it. */
			if (may_exit && !may_exit(end.tile)) return roll_back();
			if (exit_reached != nullptr) *exit_reached = end.tile;
		}
	}

	/* A path that ends on a platform (the order's target is its nearest tile)
	 * reserves on to the end of the platform, like surface PBS: otherwise a
	 * train standing further along it is not seen and a follower runs into it. */
	if (!stopped_at_signal && last_ref.IsValid() && IsValidTrackdir(last_td) &&
			_multilayer_map.GetSlice(last_ref.slice).kind == SliceKind::StationTile) {
		const TileSlice &start = _multilayer_map.GetSlice(last_ref.slice);
		int16_t depth = start.span.z_bot;
		uint16_t station = start.station.station_id;
		TileIndex tile = last_ref.tile;
		Trackdir td = last_td;
		for (uint guard = 0; guard < 64; guard++) {
			DiagDirection exitdir = TrackdirToExitdir(td);
			TileIndexDiffC diff = TileIndexDiffCByDiagDir(exitdir);
			int nx = TileX(tile) + diff.x;
			int ny = TileY(tile) + diff.y;
			if (nx < 0 || ny < 0 || nx >= (int)Map::SizeX() || ny >= (int)Map::SizeY()) break;
			TileIndex next = TileXY(nx, ny);
			SliceID sid = _multilayer_map.FindSliceAt(next, depth);
			if (sid == INVALID_SLICE_ID) break;
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind != SliceKind::StationTile || s.station.station_id != station) break;
			TrackBits bits = static_cast<TrackBits>(s.station.tracks) & DiagdirReachesTracks(exitdir);
			if (bits == TRACK_BIT_NONE) break;
			Track t = FindFirstTrack(bits);
			TileRef ref{next, sid};
			bool held_before = (GetUndergroundReservedTrackbits(ref, owner) & TrackToTrackBits(t)) != TRACK_BIT_NONE;
			if (!TryReserveUndergroundTrack(ref, t, owner)) return roll_back();
			if (!held_before) reserved_here.emplace_back(ref, t);
			td = TrackEnterdirToTrackdir(t, exitdir);
			tile = next;
		}
	}

	return true;
}

/**
 * Reserve the track ahead of a position up to and including the next signal
 * tile, or up to a dead end, platform end or junction, when no path to a
 * destination exists: the underground counterpart of the surface
 * TryReserveSafeTrack. A train that is lost still moves block by block, but
 * never on track it has not reserved.
 * @param ref The tile the train front is on.
 * @param td The trackdir it faces.
 * @param owner The train.
 * @return True if the stretch ahead is now reserved (possibly empty when the
 *         line ends right ahead), false when another train holds it.
 */
/** Tracks reserved by one Yapf3DReserveToNextSignal call, to undo on failure. */
using Yapf3DReservedTracks = std::vector<std::pair<TileRef, Track>>;

/**
 * One step of Yapf3DReserveToNextSignal: reserve onwards from (cur, cur_td).
 * At a junction every branch is tried in turn, a branch the train holds
 * first, and what a failed branch reserved is undone before the next.
 * @param cur The tile reached.
 * @param cur_td The trackdir on it.
 * @param v The train.
 * @param reserved Tracks newly reserved so far.
 * @param length Tiles reserved along this branch so far.
 * @param budget Tiles the whole search may still look at.
 * @return True when the stretch up to the next signal (or the end of the line) is held.
 */
static bool ReserveLostTrainStretch(const TileRef &cur, Trackdir cur_td, const Train *v, Yapf3DReservedTracks &reserved, uint length, uint &budget)
{
	/* Far enough: a lost train holds a stretch, not the whole network. */
	if (length >= 64) return true;
	if (budget == 0) return false;
	budget--;

	VehicleID owner = v->index;
	std::vector<Yapf3DNeighbor> neighbors;
	Yapf3DEnumerateNeighbors(cur, cur_td, neighbors, nullptr);
	/* Rail the train has no power on ends the line for it, as for the path
	 * search: a lost train driving on there was stranded for good. */
	std::erase_if(neighbors, [v](const Yapf3DNeighbor &nb) { return !CanTrainRunOnSlice(v, _multilayer_map.GetSlice(nb.ref.slice)); });
	if (neighbors.empty()) {
		/* Dead end or buffer stop: the line ends here. Right behind a portal
		 * it means there is no way in at all (say, a one-way signal against
		 * the train): a dive would hold nothing. */
		if (length == 0) {
			const TileSlice &start = _multilayer_map.GetSlice(cur.slice);
			if (start.kind == SliceKind::Portal && TrackdirToExitdir(cur_td) == GetPortalDiveDirection(cur.tile, start)) return false;
		}
		return true;
	}

	/* A branch the train already holds goes first. */
	std::stable_partition(neighbors.begin(), neighbors.end(), [owner](const Yapf3DNeighbor &n) {
		return (GetUndergroundReservedTrackbits(n.ref, owner) & TrackToTrackBits(TrackdirToTrack(n.td))) != TRACK_BIT_NONE;
	});

	for (const Yapf3DNeighbor &nb : neighbors) {
		const TileSlice &slice = _multilayer_map.GetSlice(nb.ref.slice);
		/* A portal carries no reservation: the way out is free. */
		if (slice.kind != SliceKind::Track && slice.kind != SliceKind::StationTile && slice.kind != SliceKind::Ramp) return true;

		size_t mark = reserved.size();
		Track t = TrackdirToTrack(nb.td);
		bool held_before = (GetUndergroundReservedTrackbits(nb.ref, owner) & TrackToTrackBits(t)) != TRACK_BIT_NONE;
		if (!TryReserveUndergroundTrack(nb.ref, t, owner)) continue;
		if (!held_before) reserved.emplace_back(nb.ref, t);

		if (HasUndergroundSignalOnTrackdir(slice, nb.td)) return true;
		if (ReserveLostTrainStretch(nb.ref, nb.td, v, reserved, length + 1, budget)) return true;

		/* Held further on: undo this branch and try the next one. */
		while (reserved.size() > mark) {
			UnreserveUndergroundTrack(reserved.back().first, reserved.back().second);
			reserved.pop_back();
		}
	}
	return false;
}

bool Yapf3DReserveToNextSignal(const TileRef &ref, Trackdir td, const Train *v)
{
	if (!ref.IsValid() || !_multilayer_map.IsValidSlice(ref.slice) || !IsValidTrackdir(td)) return false;

	/* Like a lost surface train, take a branch that is free at a junction.
	 * Reporting success without holding anything let the train pass a signal
	 * or dive into a portal onto track another train was about to use. */
	Yapf3DReservedTracks reserved;
	uint budget = 512;
	if (ReserveLostTrainStretch(ref, td, v, reserved, 0, budget)) return true;
	for (const auto &[undo_ref, undo_track] : reserved) UnreserveUndergroundTrack(undo_ref, undo_track);
	return false;
}
