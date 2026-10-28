/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file blueprint.cpp Blueprints: capturing, transforming, storing and pasting copies of construction. */

#include "../stdafx.h"
#include "blueprint.h"
#include "blueprint_cmd.h"
#include "multilayer_map.h"
#include "station_complex.h"
#include "underground_cmd.h"
#include "underground_signal.h"
#include "../3rdparty/nlohmann/json.hpp"
#include "../command_func.h"
#include "../company_func.h"
#include "../debug.h"
#include "../error.h"
#include "../fileio_func.h"
#include "../gui.h"
#include "../map_func.h"
#include "../rail.h"
#include "../rail_map.h"
#include "../string_func.h"
#include "../strings_func.h"
#include "../tilearea_type.h"
#include "../track_func.h"
#include "../window_func.h"
#include "../core/string_consumer.hpp"

#include "../table/strings.h"

#include <algorithm>
#include <chrono>
#include <map>

#include "../safeguards.h"

/* === Transformations === */

/** Turn and mirror a direction like the footprint. */
static DiagDirection TransformDiagDir(DiagDirection d, BlueprintTransform t)
{
	if (t.mirror && DiagDirToAxis(d) == AXIS_X) d = ReverseDiagDir(d);
	for (uint i = 0; i < t.rotation; i++) d = ChangeDiagDir(d, DIAGDIRDIFF_90LEFT);
	return d;
}

/** Turn and mirror a trackdir: the one leaving through the turned exit edge from the turned entry edge. */
static Trackdir TransformTrackdir(Trackdir td, BlueprintTransform t)
{
	DiagDirection exit = TransformDiagDir(TrackdirToExitdir(td), t);
	DiagDirection back = TransformDiagDir(TrackdirToExitdir(ReverseTrackdir(td)), t);
	for (Trackdir c = TRACKDIR_BEGIN; c < TRACKDIR_END; c++) {
		if (!IsValidTrackdir(c)) continue;
		if (TrackdirToExitdir(c) == exit && TrackdirToExitdir(ReverseTrackdir(c)) == back) return c;
	}
	NOT_REACHED();
}

static Track TransformTrack(Track track, BlueprintTransform t)
{
	return TrackdirToTrack(TransformTrackdir(TrackToTrackdir(track), t));
}

static TrackBits TransformTrackBits(TrackBits bits, BlueprintTransform t)
{
	TrackBits result = TRACK_BIT_NONE;
	for (Track track : SetTrackBitIterator(bits)) result |= TrackToTrackBits(TransformTrack(track, t));
	return result;
}

/**
 * Turn and mirror the signals of an item: the presence bit of each trackdir
 * moves to the turned trackdir, the type and variant of each track to the turned track.
 */
static void TransformSignals(BlueprintItem &item, TrackBits tracks, BlueprintTransform t)
{
	if (item.signal_mask == 0) return;
	BlueprintItem out = item;
	out.signal_mask = 0;
	out.signal_types = 0;
	for (Track track : SetTrackBitIterator(tracks)) {
		if ((item.signal_mask & SignalOnTrack(track)) == 0) continue;
		Trackdir td = TrackToTrackdir(track);
		Trackdir ttd = TransformTrackdir(td, t);
		Track tt = TrackdirToTrack(ttd);
		if (item.signal_mask & SignalAlongTrackdir(td)) out.signal_mask |= SignalAlongTrackdir(ttd);
		if (item.signal_mask & SignalAgainstTrackdir(td)) out.signal_mask |= SignalAgainstTrackdir(ttd);
		SetUndergroundSignalType(out, tt, GetUndergroundSignalType(item, track));
		SetUndergroundSignalVariant(out, tt, GetUndergroundSignalVariant(item, track));
	}
	item.signal_mask = out.signal_mask;
	item.signal_types = out.signal_types;
}

/** The tracks an item carries its signals on. */
static TrackBits GetItemSignalTracks(const BlueprintItem &item)
{
	if (item.kind == BlueprintItemKind::Ramp) return AxisToTrackBits(DiagDirToAxis(static_cast<DiagDirection>(item.dir)));
	return static_cast<TrackBits>(item.tracks);
}

void Blueprint::Transform(BlueprintTransform t)
{
	if (t.IsIdentity()) return;

	for (BlueprintItem &item : this->items) {
		if (t.mirror) item.x = static_cast<uint8_t>(this->w - 1 - item.x);
		uint w = this->w, h = this->h;
		for (uint i = 0; i < t.rotation; i++) {
			uint8_t nx = static_cast<uint8_t>(h - 1 - item.y);
			item.y = item.x;
			item.x = nx;
			std::swap(w, h);
		}

		if (item.HasSignals()) TransformSignals(item, GetItemSignalTracks(item), t);

		switch (item.kind) {
			case BlueprintItemKind::Track:
			case BlueprintItemKind::SurfaceRail:
				item.tracks = static_cast<uint8_t>(TransformTrackBits(static_cast<TrackBits>(item.tracks), t));
				break;
			case BlueprintItemKind::Platform:
				if (t.rotation % 2 == 1) item.dir = static_cast<uint8_t>(OtherAxis(static_cast<Axis>(item.dir)));
				break;
			case BlueprintItemKind::Ramp:
			case BlueprintItemKind::Portal:
			case BlueprintItemKind::Depot:
				item.dir = static_cast<uint8_t>(TransformDiagDir(static_cast<DiagDirection>(item.dir), t));
				break;
			case BlueprintItemKind::Exit:
				break;
		}
	}
	if (t.rotation % 2 == 1) std::swap(this->w, this->h);
}

bool Blueprint::HasSurfaceItems() const
{
	return std::ranges::any_of(this->items, [](const BlueprintItem &i) { return i.IsSurface(); });
}

bool Blueprint::HasUndergroundItems() const
{
	return std::ranges::any_of(this->items, [](const BlueprintItem &i) { return !i.IsSurface(); });
}

/**
 * The levels the underground items lie on, relative to the anchor level.
 * @return The lowest and the highest level; {0, 0} without underground items.
 */
std::pair<int, int> Blueprint::GetLevelRange() const
{
	int lo = INT_MAX, hi = INT_MIN;
	for (const BlueprintItem &i : this->items) {
		if (i.IsSurface()) continue;
		lo = std::min<int>({lo, i.z, i.kind == BlueprintItemKind::Ramp ? i.z2 : i.z});
		hi = std::max<int>({hi, i.z, i.kind == BlueprintItemKind::Ramp ? i.z2 : i.z});
	}
	if (lo == INT_MAX) return {0, 0};
	return {lo, hi};
}

/**
 * Check that a blueprint is consistent, e.g. after reading it from a file.
 * @return Nothing when valid, else a description of the first problem.
 */
std::optional<std::string> Blueprint::Validate() const
{
	if (this->w == 0 || this->h == 0 || this->w > MAX_BLUEPRINT_SIZE || this->h > MAX_BLUEPRINT_SIZE) return "invalid size";
	if (this->items.size() > MAX_BLUEPRINT_ITEMS) return "too many items";
	if (this->railtypes.size() > 255) return "too many rail types";

	for (const BlueprintItem &i : this->items) {
		if (i.x >= this->w || i.y >= this->h) return fmt::format("item at {},{} outside of the footprint", i.x, i.y);
		if (std::abs(i.z) > MAX_BLUEPRINT_LEVEL_OFFSET || std::abs(i.z2) > MAX_BLUEPRINT_LEVEL_OFFSET) return "level out of range";
		if (i.HasRailType() && i.railtype >= this->railtypes.size()) return "unknown rail type index";
		switch (i.kind) {
			case BlueprintItemKind::Track:
			case BlueprintItemKind::SurfaceRail:
				if (i.tracks == 0 || (i.tracks & ~TRACK_BIT_ALL) != 0) return "invalid tracks";
				if (i.signal_mask != 0 && TracksOverlap(static_cast<TrackBits>(i.tracks))) return "signals on crossing tracks";
				break;
			case BlueprintItemKind::Platform:
				if (i.dir > AXIS_Y) return "invalid platform axis";
				if (i.station >= this->num_stations) return "unknown station index";
				break;
			case BlueprintItemKind::Ramp:
				if (i.dir >= DIAGDIR_END) return "invalid direction";
				if (std::abs(i.z - i.z2) != 1) return "ramp must span two adjacent levels";
				break;
			case BlueprintItemKind::Portal:
			case BlueprintItemKind::Depot:
				if (i.dir >= DIAGDIR_END) return "invalid direction";
				break;
			case BlueprintItemKind::Exit:
				if (!IsExplicitStationExit(static_cast<StationExitType>(i.exit_type))) return "invalid exit type";
				if (i.station >= this->num_stations) return "unknown station index";
				break;
			default:
				return "unknown item kind";
		}
		if (i.HasSignals()) {
			if ((i.signal_mask & ~0xF) != 0) return "invalid signals";
			for (Track track : SetTrackBitIterator(GetItemSignalTracks(i))) {
				if ((i.signal_mask & SignalOnTrack(track)) != 0 && GetUndergroundSignalType(i, track) > SIGTYPE_LAST) return "invalid signal type";
			}
		} else if (i.signal_mask != 0) {
			return "signals on an item that cannot carry them";
		}
	}
	return std::nullopt;
}

/* === Capturing === */

/** Index of a rail type in the blueprint's table, added when new. */
static uint8_t GetBlueprintRailTypeIndex(Blueprint &bp, RailType rt)
{
	RailTypeLabel label = GetRailTypeInfo(rt)->label;
	auto it = std::ranges::find(bp.railtypes, label);
	if (it != bp.railtypes.end()) return static_cast<uint8_t>(it - bp.railtypes.begin());
	bp.railtypes.push_back(label);
	return static_cast<uint8_t>(bp.railtypes.size() - 1);
}

/**
 * Find the levels the underground construction in an area lies on.
 * @return The lowest and the highest level, or nothing when there is none.
 */
std::optional<std::pair<int16_t, int16_t>> FindUndergroundLevelRange(TileIndex start, TileIndex end)
{
	if (!_multilayer_map.IsAllocated() || !IsValidTile(start) || !IsValidTile(end)) return std::nullopt;
	int16_t lo = INT16_MAX, hi = INT16_MIN;
	for (TileIndex t : TileArea(start, end)) {
		for (SliceID sid : _multilayer_map.GetColumn(t).slices) {
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			if (s.kind == SliceKind::Surface || s.kind == SliceKind::End) continue;
			lo = std::min(lo, s.span.z_bot);
			hi = std::max<int16_t>(hi, s.span.z_top - 1);
		}
	}
	if (lo == INT16_MAX) return std::nullopt;
	return std::make_pair(lo, hi);
}

/**
 * Copy the construction in an area into a blueprint: the underground levels
 * between z_top and z_bot and, when asked, the surface with every level above
 * z_bot. Reservations, trains, owners and station identities are not copied;
 * platforms only keep which of them belong to the same station.
 * @param spec What to copy.
 * @return The blueprint; empty when there is nothing to copy.
 */
Blueprint CaptureBlueprint(const BlueprintCaptureSpec &spec)
{
	Blueprint bp;
	if (!_multilayer_map.IsAllocated() || !IsValidTile(spec.start) || !IsValidTile(spec.end)) return bp;

	TileArea area(spec.start, spec.end);
	area.w = std::min<uint16_t>(area.w, MAX_BLUEPRINT_SIZE);
	area.h = std::min<uint16_t>(area.h, MAX_BLUEPRINT_SIZE);
	bp.w = static_cast<uint8_t>(area.w);
	bp.h = static_cast<uint8_t>(area.h);

	std::map<uint16_t, uint8_t> stations; ///< Station id -> index in the blueprint.
	auto level_ok = [&](int lo, int hi) {
		if (lo < spec.z_bot) return false;
		if (!spec.surface && hi > spec.z_top) return false;
		return std::abs(lo - spec.anchor) <= MAX_BLUEPRINT_LEVEL_OFFSET && std::abs(hi - spec.anchor) <= MAX_BLUEPRINT_LEVEL_OFFSET;
	};
	auto rel = [&](int z) { return static_cast<int8_t>(z - spec.anchor); };

	for (TileIndex t : area) {
		uint8_t x = static_cast<uint8_t>(TileX(t) - TileX(area.tile));
		uint8_t y = static_cast<uint8_t>(TileY(t) - TileY(area.tile));
		bool portal_here = false;

		for (SliceID sid : _multilayer_map.GetColumn(t).slices) {
			const TileSlice &s = _multilayer_map.GetSlice(sid);
			BlueprintItem item;
			item.x = x;
			item.y = y;
			switch (s.kind) {
				case SliceKind::Track:
					if (!level_ok(s.span.z_bot, s.span.z_bot) || s.track.railtype >= RAILTYPE_END) continue;
					item.kind = BlueprintItemKind::Track;
					item.z = rel(s.span.z_bot);
					item.railtype = GetBlueprintRailTypeIndex(bp, static_cast<RailType>(s.track.railtype));
					item.tracks = s.track.tracks;
					item.signal_mask = s.track.signal_mask;
					item.signal_types = item.signal_mask != 0 ? s.track.signal_types : 0;
					break;

				case SliceKind::StationTile: {
					if (s.station.role != 0 || !level_ok(s.span.z_bot, s.span.z_bot) || s.station.railtype >= RAILTYPE_END) continue;
					auto [it, inserted] = stations.try_emplace(s.station.station_id, static_cast<uint8_t>(stations.size()));
					if (inserted && stations.size() > 255) return Blueprint{};
					item.kind = BlueprintItemKind::Platform;
					item.z = rel(s.span.z_bot);
					item.railtype = GetBlueprintRailTypeIndex(bp, static_cast<RailType>(s.station.railtype));
					item.dir = static_cast<uint8_t>((s.station.tracks & TRACK_BIT_Y) != 0 ? AXIS_Y : AXIS_X);
					item.station = it->second;
					break;
				}

				case SliceKind::Ramp:
					if (!level_ok(s.span.z_bot, s.span.z_top - 1) || s.ramp.railtype >= RAILTYPE_END) continue;
					item.kind = BlueprintItemKind::Ramp;
					item.z = rel(s.ramp.entry_depth);
					item.z2 = rel(s.ramp.exit_depth);
					item.railtype = GetBlueprintRailTypeIndex(bp, static_cast<RailType>(s.ramp.railtype));
					item.dir = s.ramp.direction;
					item.signal_mask = s.ramp.signal_mask;
					item.signal_types = item.signal_mask != 0 ? s.ramp.signal_types : 0;
					break;

				case SliceKind::Portal:
					/* A portal is built with its surface rail; it belongs to both. */
					if (!level_ok(s.span.z_bot, s.span.z_bot) || s.portal.railtype >= RAILTYPE_END) continue;
					item.kind = BlueprintItemKind::Portal;
					item.z = rel(s.span.z_bot);
					item.railtype = GetBlueprintRailTypeIndex(bp, static_cast<RailType>(s.portal.railtype));
					item.dir = s.portal.direction;
					portal_here = true;
					break;

				default:
					continue;
			}
			bp.items.push_back(item);
		}

		if (!spec.surface) continue;

		BlueprintItem item;
		item.x = x;
		item.y = y;
		if (IsPlainRailTile(t)) {
			TrackBits tracks = GetTrackBits(t);
			uint8_t signals = HasSignals(t) ? static_cast<uint8_t>(GetPresentSignals(t)) : 0;
			if (portal_here) {
				/* The portal builds its own track; only signals on it are left to copy. */
				if (signals == 0) continue;
			}
			item.kind = BlueprintItemKind::SurfaceRail;
			item.railtype = GetBlueprintRailTypeIndex(bp, GetRailType(t));
			item.tracks = static_cast<uint8_t>(tracks);
			item.signal_mask = signals;
			for (Track track : SetTrackBitIterator(tracks)) {
				if ((signals & SignalOnTrack(track)) == 0) continue;
				SetUndergroundSignalType(item, track, GetSignalType(t, track));
				SetUndergroundSignalVariant(item, track, GetSignalVariant(t, track));
			}
			bp.items.push_back(item);
		} else if (IsRailDepotTile(t)) {
			item.kind = BlueprintItemKind::Depot;
			item.railtype = GetBlueprintRailTypeIndex(bp, GetRailType(t));
			item.dir = static_cast<uint8_t>(GetRailDepotDirection(t));
			bp.items.push_back(item);
		}
	}

	/* Surface exits of the copied stations. */
	if (spec.surface) {
		for (TileIndex t : area) {
			const StationComplexNode *exit = FindExplicitStationExitAt(t);
			if (exit == nullptr) continue;
			auto it = stations.find(exit->station_id);
			if (it == stations.end()) continue;
			BlueprintItem item;
			item.kind = BlueprintItemKind::Exit;
			item.x = static_cast<uint8_t>(TileX(t) - TileX(area.tile));
			item.y = static_cast<uint8_t>(TileY(t) - TileY(area.tile));
			item.exit_type = static_cast<uint8_t>(exit->exit_type);
			item.station = it->second;
			bp.items.push_back(item);
		}
	}

	bp.num_stations = static_cast<uint8_t>(stations.size());
	if (bp.items.size() > MAX_BLUEPRINT_ITEMS) return Blueprint{};
	return bp;
}

/* === Text form === */

static const std::string_view _bp_track_names[] = {"x", "y", "upper", "lower", "left", "right"};
static const std::string_view _bp_dir_names[] = {"ne", "se", "sw", "nw"};
static const std::string_view _bp_axis_names[] = {"x", "y"};
static const std::string_view _bp_signal_type_names[] = {"block", "entry", "exit", "combo", "pbs", "pbs_oneway"};
static const std::string_view _bp_signal_variant_names[] = {"light", "semaphore"};
static const std::string_view _bp_exit_names[] = {"pavilion", "underpass"};
static const std::string_view _bp_kind_names[] = {"", "track", "platform", "ramp", "portal", "exit", "surface_rail", "depot"};

static constexpr std::string_view BLUEPRINT_FORMAT_KEY = "openttd_metro_blueprint";
static constexpr int BLUEPRINT_FORMAT_VERSION = 1;
static constexpr std::string_view BLUEPRINT_SHARE_PREFIX = "OTTDBP1:";

/** A rail type label as text: its four characters, or hexadecimal when they are not printable. */
static std::string RailTypeLabelToString(RailTypeLabel label)
{
	std::string s;
	for (int shift = 24; shift >= 0; shift -= 8) {
		char c = static_cast<char>(GB(label, shift, 8));
		if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') return fmt::format("0x{:08X}", label);
		s.push_back(c);
	}
	return s;
}

static std::optional<RailTypeLabel> RailTypeLabelFromString(std::string_view s)
{
	if (s.size() == 10 && s.starts_with("0x")) {
		auto v = ParseInteger<uint32_t>(s.substr(2), 16);
		if (v.has_value()) return *v;
		return std::nullopt;
	}
	if (s.size() != 4) return std::nullopt;
	RailTypeLabel label = 0;
	for (char c : s) label = (label << 8) | static_cast<uint8_t>(c);
	return label;
}

/** Find a name in a table of names. */
template <size_t N>
static std::optional<uint8_t> FindName(const std::string_view (&names)[N], std::string_view name)
{
	for (size_t i = 0; i < N; i++) {
		if (!names[i].empty() && names[i] == name) return static_cast<uint8_t>(i);
	}
	return std::nullopt;
}

static nlohmann::json SignalsToJson(const BlueprintItem &item)
{
	nlohmann::json signals = nlohmann::json::array();
	for (Track track : SetTrackBitIterator(GetItemSignalTracks(item))) {
		if ((item.signal_mask & SignalOnTrack(track)) == 0) continue;
		nlohmann::json towards = nlohmann::json::array();
		for (Trackdir td : {TrackToTrackdir(track), ReverseTrackdir(TrackToTrackdir(track))}) {
			if (item.signal_mask & SignalAlongTrackdir(td)) towards.push_back(_bp_dir_names[TrackdirToExitdir(td)]);
		}
		signals.push_back({
			{"track", _bp_track_names[track]},
			{"towards", towards},
			{"type", _bp_signal_type_names[std::min<uint>(GetUndergroundSignalType(item, track), SIGTYPE_LAST)]},
			{"variant", _bp_signal_variant_names[GetUndergroundSignalVariant(item, track)]},
		});
	}
	return signals;
}

static nlohmann::json ItemToJson(const BlueprintItem &item)
{
	nlohmann::json j;
	j["kind"] = _bp_kind_names[to_underlying(item.kind)];
	if (item.IsSurface()) {
		j["pos"] = {item.x, item.y};
	} else {
		j["pos"] = {item.x, item.y, item.z};
	}
	if (item.HasRailType()) j["rail"] = item.railtype;
	switch (item.kind) {
		case BlueprintItemKind::Track:
		case BlueprintItemKind::SurfaceRail: {
			nlohmann::json tracks = nlohmann::json::array();
			for (Track track : SetTrackBitIterator(static_cast<TrackBits>(item.tracks))) tracks.push_back(_bp_track_names[track]);
			j["tracks"] = tracks;
			break;
		}
		case BlueprintItemKind::Platform:
			j["axis"] = _bp_axis_names[item.dir];
			j["station"] = item.station;
			break;
		case BlueprintItemKind::Ramp:
			j["to"] = item.z2;
			j["dir"] = _bp_dir_names[item.dir];
			break;
		case BlueprintItemKind::Portal:
		case BlueprintItemKind::Depot:
			j["dir"] = _bp_dir_names[item.dir];
			break;
		case BlueprintItemKind::Exit:
			j["type"] = _bp_exit_names[item.exit_type];
			j["station"] = item.station;
			break;
	}
	if (item.HasSignals() && item.signal_mask != 0) j["signals"] = SignalsToJson(item);
	return j;
}

/**
 * Write a blueprint as JSON.
 * @param bp The blueprint.
 * @param pretty One item per line, for files people read and edit; else compact.
 */
std::string BlueprintToJson(const Blueprint &bp, bool pretty)
{
	nlohmann::json railtypes = nlohmann::json::array();
	for (RailTypeLabel label : bp.railtypes) railtypes.push_back(RailTypeLabelToString(label));

	nlohmann::json head;
	head[BLUEPRINT_FORMAT_KEY] = BLUEPRINT_FORMAT_VERSION;
	head["name"] = bp.name;
	head["size"] = {bp.w, bp.h};
	head["stations"] = bp.num_stations;
	head["railtypes"] = railtypes;

	if (!pretty) {
		nlohmann::json j = head;
		j["items"] = nlohmann::json::array();
		for (const BlueprintItem &item : bp.items) j["items"].push_back(ItemToJson(item));
		return j.dump();
	}

	/* The head first, one key per line, then the items, one per line. */
	std::string out = "{\n";
	for (auto &[key, value] : head.items()) {
		out += fmt::format("  \"{}\": {},\n", key, value.dump());
	}
	out += "  \"items\": [\n";
	for (size_t i = 0; i < bp.items.size(); i++) {
		out += "    " + ItemToJson(bp.items[i]).dump();
		out += (i + 1 < bp.items.size()) ? ",\n" : "\n";
	}
	out += "  ]\n}\n";
	return out;
}

static const char _base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(std::span<const uint8_t> data)
{
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (size_t i = 0; i < data.size(); i += 3) {
		uint32_t v = data[i] << 16;
		if (i + 1 < data.size()) v |= data[i + 1] << 8;
		if (i + 2 < data.size()) v |= data[i + 2];
		out.push_back(_base64_chars[(v >> 18) & 0x3F]);
		out.push_back(_base64_chars[(v >> 12) & 0x3F]);
		out.push_back(i + 1 < data.size() ? _base64_chars[(v >> 6) & 0x3F] : '=');
		out.push_back(i + 2 < data.size() ? _base64_chars[v & 0x3F] : '=');
	}
	return out;
}

static std::optional<std::vector<uint8_t>> Base64Decode(std::string_view text)
{
	std::vector<uint8_t> out;
	uint32_t v = 0;
	int bits = 0;
	for (char c : text) {
		if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
		const char *p = std::strchr(_base64_chars, c);
		if (p == nullptr || c == '\0') return std::nullopt;
		v = (v << 6) | static_cast<uint32_t>(p - _base64_chars);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((v >> bits) & 0xFF));
		}
	}
	return out;
}

/** A blueprint as one line of text for sharing: a prefix and the compact JSON in base64. */
std::string BlueprintToShareString(const Blueprint &bp)
{
	std::string json = BlueprintToJson(bp, false);
	return std::string(BLUEPRINT_SHARE_PREFIX) + Base64Encode({reinterpret_cast<const uint8_t *>(json.data()), json.size()});
}

/** Read the signals of an item from JSON; throws on malformed data. */
static void SignalsFromJson(BlueprintItem &item, const nlohmann::json &signals)
{
	for (const nlohmann::json &s : signals) {
		auto track = FindName(_bp_track_names, s.at("track").get<std::string>());
		if (!track.has_value()) throw std::runtime_error("unknown signal track");
		Track t = static_cast<Track>(*track);
		if (s.contains("towards")) {
			for (const nlohmann::json &d : s.at("towards")) {
				auto dir = FindName(_bp_dir_names, d.get<std::string>());
				if (!dir.has_value()) throw std::runtime_error("unknown signal direction");
				bool found = false;
				for (Trackdir td : {TrackToTrackdir(t), ReverseTrackdir(TrackToTrackdir(t))}) {
					if (TrackdirToExitdir(td) == static_cast<DiagDirection>(*dir)) {
						item.signal_mask |= SignalAlongTrackdir(td);
						found = true;
					}
				}
				if (!found) throw std::runtime_error("signal direction does not lie along its track");
			}
		} else {
			item.signal_mask |= SignalOnTrack(t);
		}
		auto type = FindName(_bp_signal_type_names, s.value("type", std::string("block")));
		auto variant = FindName(_bp_signal_variant_names, s.value("variant", std::string("light")));
		if (!type.has_value() || !variant.has_value()) throw std::runtime_error("unknown signal type or variant");
		SetUndergroundSignalType(item, t, static_cast<SignalType>(*type));
		SetUndergroundSignalVariant(item, t, static_cast<SignalVariant>(*variant));
	}
}

/** Read an integer in a range; throws when it is no integer or out of the range. */
template <typename T>
static T IntFromJson(const nlohmann::json &j, int64_t lo, int64_t hi)
{
	if (!j.is_number_integer()) throw std::runtime_error("number expected");
	int64_t v = j.get<int64_t>();
	if (v < lo || v > hi) throw std::runtime_error("number out of range");
	return static_cast<T>(v);
}

/** Read an optional integer in a range, with a default; throws when it is no integer or out of the range. */
template <typename T>
static T IntFromJson(const nlohmann::json &j, std::string_view key, T def, int64_t lo, int64_t hi)
{
	std::string k(key);
	if (!j.contains(k)) return def;
	return IntFromJson<T>(j.at(k), lo, hi);
}

/** Read a direction name; throws when unknown. */
static uint8_t DirFromJson(const nlohmann::json &j, std::string_view key)
{
	auto dir = FindName(_bp_dir_names, j.at(key).get<std::string>());
	if (!dir.has_value()) throw std::runtime_error("unknown direction");
	return *dir;
}

static BlueprintItem ItemFromJson(const nlohmann::json &j)
{
	BlueprintItem item;
	auto kind = FindName(_bp_kind_names, j.at("kind").get<std::string>());
	if (!kind.has_value()) throw std::runtime_error("unknown item kind");
	item.kind = static_cast<BlueprintItemKind>(*kind);

	const nlohmann::json &pos = j.at("pos");
	item.x = IntFromJson<uint8_t>(pos.at(0), 0, MAX_BLUEPRINT_SIZE - 1);
	item.y = IntFromJson<uint8_t>(pos.at(1), 0, MAX_BLUEPRINT_SIZE - 1);
	if (!item.IsSurface()) item.z = IntFromJson<int8_t>(pos.at(2), -MAX_BLUEPRINT_LEVEL_OFFSET, MAX_BLUEPRINT_LEVEL_OFFSET);
	if (item.HasRailType()) item.railtype = IntFromJson<uint8_t>(j, "rail", 0, 0, 255);

	switch (item.kind) {
		case BlueprintItemKind::Track:
		case BlueprintItemKind::SurfaceRail:
			for (const nlohmann::json &t : j.at("tracks")) {
				auto track = FindName(_bp_track_names, t.get<std::string>());
				if (!track.has_value()) throw std::runtime_error("unknown track");
				item.tracks |= TrackToTrackBits(static_cast<Track>(*track));
			}
			break;
		case BlueprintItemKind::Platform: {
			auto axis = FindName(_bp_axis_names, j.at("axis").get<std::string>());
			if (!axis.has_value()) throw std::runtime_error("unknown axis");
			item.dir = *axis;
			item.station = IntFromJson<uint8_t>(j, "station", 0, 0, 255);
			break;
		}
		case BlueprintItemKind::Ramp:
			item.z2 = IntFromJson<int8_t>(j.at("to"), -MAX_BLUEPRINT_LEVEL_OFFSET, MAX_BLUEPRINT_LEVEL_OFFSET);
			item.dir = DirFromJson(j, "dir");
			break;
		case BlueprintItemKind::Portal:
		case BlueprintItemKind::Depot:
			item.dir = DirFromJson(j, "dir");
			break;
		case BlueprintItemKind::Exit: {
			auto type = FindName(_bp_exit_names, j.value("type", std::string("pavilion")));
			if (!type.has_value()) throw std::runtime_error("unknown exit type");
			item.exit_type = *type;
			item.station = IntFromJson<uint8_t>(j, "station", 0, 0, 255);
			break;
		}
	}
	if (item.HasSignals() && j.contains("signals")) SignalsFromJson(item, j.at("signals"));
	return item;
}

/**
 * Read a blueprint from text: JSON as written by BlueprintToJson, or a share string.
 * @param text The text.
 * @param[out] error The reason when the text is not a valid blueprint; may be nullptr.
 * @return The blueprint, or nothing on error.
 */
std::optional<Blueprint> ParseBlueprintText(std::string_view text, std::string *error)
{
	auto fail = [error](std::string_view msg) -> std::optional<Blueprint> {
		if (error != nullptr) *error = msg;
		return std::nullopt;
	};

	while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r' || text.front() == '\t')) text.remove_prefix(1);

	std::string json_text;
	if (text.starts_with(BLUEPRINT_SHARE_PREFIX)) {
		auto data = Base64Decode(text.substr(BLUEPRINT_SHARE_PREFIX.size()));
		if (!data.has_value()) return fail("invalid blueprint string");
		json_text.assign(data->begin(), data->end());
	} else {
		json_text = text;
	}

	nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
	if (j.is_discarded() || !j.is_object()) return fail("not a blueprint (invalid JSON)");

	Blueprint bp;
	try {
		if (!j.contains(BLUEPRINT_FORMAT_KEY)) return fail("not a metro blueprint");
		if (IntFromJson<int>(j.at(BLUEPRINT_FORMAT_KEY), 1, INT_MAX) > BLUEPRINT_FORMAT_VERSION) return fail("blueprint of a newer format");
		bp.name = StrMakeValid(j.value("name", std::string()));
		const nlohmann::json &size = j.at("size");
		bp.w = IntFromJson<uint8_t>(size.at(0), 1, MAX_BLUEPRINT_SIZE);
		bp.h = IntFromJson<uint8_t>(size.at(1), 1, MAX_BLUEPRINT_SIZE);
		bp.num_stations = IntFromJson<uint8_t>(j, "stations", 0, 0, 255);
		for (const nlohmann::json &rt : j.at("railtypes")) {
			auto label = RailTypeLabelFromString(rt.get<std::string>());
			if (!label.has_value()) return fail("invalid rail type label");
			bp.railtypes.push_back(*label);
		}
		const nlohmann::json &items = j.at("items");
		if (items.size() > MAX_BLUEPRINT_ITEMS) return fail("too many items");
		for (const nlohmann::json &item : items) bp.items.push_back(ItemFromJson(item));
	} catch (const std::exception &e) {
		return fail(e.what());
	}

	if (auto problem = bp.Validate(); problem.has_value()) return fail(*problem);
	return bp;
}

/* === Command payload === */

/** Kind byte of a station reference record in a payload. */
static constexpr uint8_t PAYLOAD_STATION_REF = 0x80;
/** Flag in the kind byte of a record: the piece has no signals, the record no signal bytes. */
static constexpr uint8_t PAYLOAD_NO_SIGNALS = 0x40;
static constexpr uint8_t PAYLOAD_VERSION = 2;
/**
 * Base64 characters of a payload. A command packet may hold no more than
 * COMPAT_MTU (1460) bytes with all the other parameters, so a blueprint is
 * pasted in parts, which are sent one after the other.
 */
static constexpr size_t MAX_PAYLOAD_CHARS = 1200;
/** Most parts of one paste. */
static constexpr size_t MAX_PASTE_PARTS = 256;
/** Platforms of earlier parts a part names a station by, so it is found when some of them could not be built. */
static constexpr size_t MAX_STATION_REFS = 3;
/** Bytes of a station reference record. */
static constexpr size_t STATION_REF_SIZE = 7;

/** Whether the record of a piece carries signal bytes. */
static bool HasPayloadSignals(const BlueprintItem &i)
{
	return i.HasSignals() && i.signal_mask != 0;
}

/** Bytes of the record of a piece of a kind in a payload. */
static size_t GetPayloadRecordSize(BlueprintItemKind kind, bool signals)
{
	size_t size;
	switch (kind) {
		case BlueprintItemKind::Track: size = 6; break;       // kind x y z rt tracks
		case BlueprintItemKind::Platform: size = 7; break;    // kind x y z rt axis station
		case BlueprintItemKind::Ramp: size = 7; break;        // kind x y z z2 rt dir
		case BlueprintItemKind::Portal: size = 6; break;      // kind x y z rt dir
		case BlueprintItemKind::Exit: size = 5; break;        // kind x y type station
		case BlueprintItemKind::SurfaceRail: size = 5; break; // kind x y rt tracks
		case BlueprintItemKind::Depot: size = 5; break;       // kind x y rt dir
		default: NOT_REACHED();
	}
	return signals ? size + 2 : size;
}

static void WritePayloadRecord(std::vector<uint8_t> &out, const BlueprintItem &i)
{
	bool signals = HasPayloadSignals(i);
	out.push_back(to_underlying(i.kind) | (signals ? 0 : PAYLOAD_NO_SIGNALS));
	out.push_back(i.x);
	out.push_back(i.y);
	if (!i.IsSurface()) out.push_back(static_cast<uint8_t>(i.z));
	switch (i.kind) {
		case BlueprintItemKind::Track:
		case BlueprintItemKind::SurfaceRail:
			out.insert(out.end(), {i.railtype, i.tracks});
			break;
		case BlueprintItemKind::Platform:
			out.insert(out.end(), {i.railtype, i.dir, i.station});
			break;
		case BlueprintItemKind::Ramp:
			out.insert(out.end(), {static_cast<uint8_t>(i.z2), i.railtype, i.dir});
			break;
		case BlueprintItemKind::Portal:
		case BlueprintItemKind::Depot:
			out.insert(out.end(), {i.railtype, i.dir});
			break;
		case BlueprintItemKind::Exit:
			out.insert(out.end(), {i.exit_type, i.station});
			break;
	}
	if (signals) out.insert(out.end(), {i.signal_mask, i.signal_types});
}

/**
 * Read the payload of a paste command. Only the layout is checked here; the
 * command checks every value it uses.
 */
std::optional<BlueprintPayload> DecodeBlueprintPayload(std::string_view data)
{
	auto bytes = Base64Decode(data);
	if (!bytes.has_value() || bytes->empty() || bytes->front() != PAYLOAD_VERSION) return std::nullopt;

	BlueprintPayload payload;
	const std::vector<uint8_t> &b = *bytes;
	size_t pos = 1;
	auto take = [&]() { return b[pos++]; };
	while (pos < b.size()) {
		uint8_t kind = b[pos];
		if (kind == PAYLOAD_STATION_REF) {
			if (pos + STATION_REF_SIZE > b.size()) return std::nullopt;
			pos++;
			BlueprintItem ref;
			ref.kind = BlueprintItemKind::Platform;
			ref.x = take();
			ref.y = take();
			ref.z = static_cast<int8_t>(take());
			ref.station = take();
			ref.dir = take();
			ref.railtype = take();
			payload.station_refs.emplace_back(ref.station, ref);
			continue;
		}
		bool signals = (kind & PAYLOAD_NO_SIGNALS) == 0;
		kind &= ~PAYLOAD_NO_SIGNALS;
		if (kind < to_underlying(BlueprintItemKind::Track) || kind > to_underlying(BlueprintItemKind::Depot)) return std::nullopt;
		BlueprintItem i;
		i.kind = static_cast<BlueprintItemKind>(kind);
		if (!i.HasSignals()) signals = false;
		if (pos + GetPayloadRecordSize(i.kind, signals) > b.size()) return std::nullopt;
		pos++;
		i.x = take();
		i.y = take();
		if (!i.IsSurface()) i.z = static_cast<int8_t>(take());
		switch (i.kind) {
			case BlueprintItemKind::Track:
			case BlueprintItemKind::SurfaceRail:
				i.railtype = take(); i.tracks = take();
				break;
			case BlueprintItemKind::Platform:
				i.railtype = take(); i.dir = take(); i.station = take();
				break;
			case BlueprintItemKind::Ramp:
				i.z2 = static_cast<int8_t>(take()); i.railtype = take(); i.dir = take();
				break;
			case BlueprintItemKind::Portal:
			case BlueprintItemKind::Depot:
				i.railtype = take(); i.dir = take();
				break;
			case BlueprintItemKind::Exit:
				i.exit_type = take(); i.station = take();
				break;
		}
		if (signals) {
			i.signal_mask = take();
			i.signal_types = take();
		}
		payload.items.push_back(i);
	}
	return payload;
}

/** Order in which the kinds are built: stations before their exits, portals before the surface rail on their tiles. */
static int GetBuildOrder(BlueprintItemKind kind)
{
	switch (kind) {
		case BlueprintItemKind::Platform: return 0;
		case BlueprintItemKind::Exit: return 1;
		case BlueprintItemKind::Track: return 2;
		case BlueprintItemKind::Ramp: return 3;
		case BlueprintItemKind::Portal: return 4;
		case BlueprintItemKind::SurfaceRail: return 5;
		case BlueprintItemKind::Depot: return 6;
	}
	NOT_REACHED();
}

/**
 * Split a blueprint into the payloads of paste commands. Each payload is small
 * enough for a network packet. A station the blueprint builds over several
 * payloads is named in the later ones by platforms earlier ones built.
 * @param bp The blueprint.
 * @param spec How to paste it.
 * @param[out] error Why it cannot be pasted.
 * @return The payloads, in the order they must be executed; empty on error.
 */
std::vector<std::string> PlanBlueprintPaste(const Blueprint &bp, const BlueprintPasteSpec &spec, StringID *error)
{
	*error = INVALID_STRING_ID;
	if (bp.IsEmpty()) {
		*error = STR_ERROR_BLUEPRINT_EMPTY;
		return {};
	}

	/* Rail types by label, as this game knows them. */
	std::vector<RailType> railtypes;
	for (RailTypeLabel label : bp.railtypes) {
		RailType rt = spec.railtype != INVALID_RAILTYPE ? spec.railtype : GetRailTypeByLabel(label);
		if (rt == INVALID_RAILTYPE) {
			*error = STR_ERROR_BLUEPRINT_UNKNOWN_RAIL_TYPE;
			return {};
		}
		railtypes.push_back(rt);
	}

	Blueprint t = bp;
	t.Transform(spec.transform);
	for (BlueprintItem &item : t.items) {
		if (item.HasRailType()) item.railtype = static_cast<uint8_t>(railtypes[item.railtype]);
	}
	std::ranges::stable_sort(t.items, [](const BlueprintItem &a, const BlueprintItem &b) {
		if (GetBuildOrder(a.kind) != GetBuildOrder(b.kind)) return GetBuildOrder(a.kind) < GetBuildOrder(b.kind);
		if (a.kind == BlueprintItemKind::Platform || a.kind == BlueprintItemKind::Exit) {
			if (a.station != b.station) return a.station < b.station;
		}
		/* Row by row, so each platform joins a neighbour built before it. */
		if (a.z != b.z) return a.z > b.z;
		if (a.y != b.y) return a.y < b.y;
		return a.x < b.x;
	});

	std::vector<std::string> chunks;
	std::vector<uint8_t> current{PAYLOAD_VERSION};
	std::vector<std::vector<BlueprintItem>> placed(t.num_stations); ///< Platforms of each station in the payloads so far.
	std::vector<bool> known(t.num_stations); ///< The current payload names the station or builds one of its platforms.
	const size_t max_bytes = MAX_PAYLOAD_CHARS / 4 * 3;

	auto flush = [&]() {
		if (current.size() <= 1) return;
		chunks.push_back(Base64Encode(current));
		current.assign(1, PAYLOAD_VERSION);
		std::fill(known.begin(), known.end(), false);
	};
	/* Some platforms an earlier payload built: the first, one in the middle and the last. */
	auto refs_for = [&](uint8_t s) {
		std::vector<const BlueprintItem *> refs;
		const auto &p = placed[s];
		for (size_t idx : {static_cast<size_t>(0), p.size() / 2, p.size() - 1}) {
			if (p.empty() || refs.size() >= MAX_STATION_REFS) break;
			if (std::ranges::find(refs, &p[idx]) == refs.end()) refs.push_back(&p[idx]);
		}
		return refs;
	};

	for (const BlueprintItem &item : t.items) {
		bool station_piece = (item.kind == BlueprintItemKind::Platform || item.kind == BlueprintItemKind::Exit) && item.station < t.num_stations;
		size_t size = GetPayloadRecordSize(item.kind, HasPayloadSignals(item));
		auto ref_bytes = [&]() { return (station_piece && !known[item.station]) ? refs_for(item.station).size() * STATION_REF_SIZE : 0; };
		if (current.size() + size + ref_bytes() > max_bytes) flush();
		if (station_piece && !known[item.station]) {
			for (const BlueprintItem *ref : refs_for(item.station)) {
				current.insert(current.end(), {PAYLOAD_STATION_REF, ref->x, ref->y, static_cast<uint8_t>(ref->z), ref->station, ref->dir, ref->railtype});
			}
		}
		WritePayloadRecord(current, item);
		if (station_piece) {
			known[item.station] = true;
			if (item.kind == BlueprintItemKind::Platform) placed[item.station].push_back(item);
		}
	}
	flush();

	if (chunks.size() > MAX_PASTE_PARTS) {
		*error = STR_ERROR_BLUEPRINT_TOO_BIG;
		return {};
	}
	return chunks;
}

/**
 * Test pasting the payloads of a blueprint, as the local company.
 * @return The total cost, or the first error.
 */
CommandCost TestBlueprintPaste(const std::vector<std::string> &chunks, TileIndex tile, int16_t depth, uint8_t options)
{
	CommandCost total(EXPENSES_CONSTRUCTION);
	DoCommandFlags flags = CommandFlagsToDCFlags(GetCommandFlags<Commands::PasteUndergroundBlueprint>());
	for (const std::string &chunk : chunks) {
		CommandCost ret = std::get<0>(Command<Commands::PasteUndergroundBlueprint>::Do(flags, tile, depth, options, chunk));
		if (ret.Failed()) return ret;
		total.AddCost(ret.GetCost());
	}
	return total;
}

/**
 * A paste being sent part by part: the next part goes when the previous one
 * is done, so a big blueprint does not flood the command queue of a server.
 */
struct PendingBlueprintPaste {
	std::vector<std::string> chunks; ///< The parts.
	size_t next = 0;                 ///< The part to send next.
	TileIndex tile = INVALID_TILE;   ///< North corner of the footprint.
	int16_t depth = 0;               ///< Level of the anchor level.
	uint8_t options = 0;             ///< BlueprintPasteOption bits.
	uint built = 0;                  ///< Pieces built (or already there) so far.
	uint left_out = 0;               ///< Pieces left out so far.
	std::chrono::steady_clock::time_point sent; ///< When the last part was sent.
	bool posting = false;  ///< The last part is being posted right now.
	bool answered = false; ///< The last part was executed.
	bool go_on = false;    ///< Send the next part.
};
static std::optional<PendingBlueprintPaste> _pending_paste;

/** End sending a paste, telling the player when not all of it was built. */
static void FinishBlueprintPaste(bool stopped)
{
	if (!_pending_paste.has_value()) return;
	PendingBlueprintPaste p = std::move(*_pending_paste);
	_pending_paste.reset();
	int x = TileX(p.tile) * TILE_SIZE, y = TileY(p.tile) * TILE_SIZE;
	if (stopped && p.next < p.chunks.size()) {
		ShowErrorMessage(GetEncodedString(STR_BLUEPRINT_PASTE_STOPPED, p.next, p.chunks.size()), {}, WL_WARNING, x, y);
	} else if (p.left_out > 0) {
		ShowErrorMessage(GetEncodedString(STR_BLUEPRINT_PASTE_INCOMPLETE, p.built, p.left_out), {}, WL_WARNING, x, y);
	}
}

/**
 * Send the parts of the paste being sent. In a network game a part is
 * answered later; its callback sends the next one. In a single player game a
 * part is executed while it is posted and its callback asks to go on, which
 * this loop does instead of the callback nesting one paste command in another.
 */
static void SendBlueprintParts()
{
	while (_pending_paste.has_value() && _pending_paste->next < _pending_paste->chunks.size()) {
		PendingBlueprintPaste &p = *_pending_paste;
		std::string chunk = p.chunks[p.next++];
		p.sent = std::chrono::steady_clock::now();
		p.answered = false;
		p.go_on = false;
		p.posting = true;
		bool posted = Command<Commands::PasteUndergroundBlueprint>::Post(STR_ERROR_CAN_T_PASTE_BLUEPRINT, CcPasteUndergroundBlueprint, p.tile, p.depth, p.options, chunk);
		if (!_pending_paste.has_value()) return; // The callback ended the paste.
		PendingBlueprintPaste &q = *_pending_paste;
		q.posting = false;
		/* Refused before it was sent, a part gets no callback. */
		if (!q.answered) {
			if (!posted) FinishBlueprintPaste(true);
			return;
		}
		if (!q.go_on) return;
	}
}

/**
 * A part of a paste of this client was executed: go on with the next one.
 * Without BPO_BEST_EFFORT a part that failed ends the paste.
 */
void CcPasteUndergroundBlueprint(Commands, const CommandCost &result, uint16_t built, uint16_t left_out)
{
	if (!_pending_paste.has_value()) return;
	PendingBlueprintPaste &p = *_pending_paste;
	p.built += built;
	p.left_out += left_out;
	p.answered = true;
	bool more = p.next < p.chunks.size();
	if (more && (result.Succeeded() || (p.options & BPO_BEST_EFFORT) != 0)) {
		if (p.posting) {
			p.go_on = true;
		} else {
			SendBlueprintParts();
		}
		return;
	}
	FinishBlueprintPaste(more);
}

/**
 * Paste a blueprint as the local company: the north corner of its footprint
 * on a tile, its anchor level on a level.
 * Unless pieces that cannot be built may be left out, nothing is built when
 * any piece cannot be.
 * @param bp The blueprint.
 * @param spec How to paste it.
 * @param tile North corner of the footprint.
 * @param depth Level the anchor level of the blueprint goes to.
 * @param estimate_only Only show what it would cost.
 * @return True when the paste was started (or the estimate shown).
 */
bool PostBlueprintPaste(const Blueprint &bp, const BlueprintPasteSpec &spec, TileIndex tile, int16_t depth, bool estimate_only)
{
	/* Where the messages appear, as for any command (world coordinates). */
	int x = TileX(tile) * TILE_SIZE, y = TileY(tile) * TILE_SIZE;

	/* One paste at a time; one whose parts stopped coming back (a new game, a lost connection) is dropped. */
	if (_pending_paste.has_value() && std::chrono::steady_clock::now() - _pending_paste->sent > std::chrono::seconds(30)) _pending_paste.reset();
	if (_pending_paste.has_value() && !estimate_only) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_PASTE_BLUEPRINT), GetEncodedString(STR_ERROR_BLUEPRINT_STILL_PASTING), WL_INFO, x, y);
		return false;
	}

	StringID error;
	std::vector<std::string> chunks = PlanBlueprintPaste(bp, spec, &error);
	if (chunks.empty()) {
		ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_PASTE_BLUEPRINT), GetEncodedString(error), WL_INFO, x, y);
		return false;
	}

	bool best_effort = (spec.options & BPO_BEST_EFFORT) != 0;
	if (!best_effort || estimate_only) {
		CommandCost ret = TestBlueprintPaste(chunks, tile, depth, spec.options);
		if (ret.Failed()) {
			ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_PASTE_BLUEPRINT), x, y, ret);
			return false;
		}
		if (estimate_only) {
			ShowEstimatedCostOrIncome(ret.GetCost(), x, y);
			return true;
		}
		if (ret.GetCost() > GetAvailableMoneyForCommand()) {
			ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_PASTE_BLUEPRINT), GetEncodedString(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY, ret.GetCost()), WL_INFO, x, y);
			return false;
		}
	}

	_pending_paste.emplace();
	_pending_paste->chunks = std::move(chunks);
	_pending_paste->tile = tile;
	_pending_paste->depth = depth;
	_pending_paste->options = spec.options;
	SendBlueprintParts();
	return true;
}

/* === Library === */

static std::optional<Blueprint> _blueprint_clipboard;
static std::vector<BlueprintLibraryEntry> _blueprint_library;
static bool _blueprint_library_scanned = false;

const Blueprint *GetBlueprintClipboard()
{
	return _blueprint_clipboard.has_value() ? &*_blueprint_clipboard : nullptr;
}

void SetBlueprintClipboard(Blueprint bp)
{
	_blueprint_clipboard = std::move(bp);
	InvalidateWindowClassesData(WC_METRO_BLUEPRINTS);
}

/** Read a whole file. */
static std::optional<std::string> ReadWholeFile(const std::string &filename)
{
	auto f = FileHandle::Open(filename, "rb");
	if (!f.has_value()) return std::nullopt;
	std::string data;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), *f)) > 0) {
		data.append(buf, n);
		if (data.size() > 16 * 1024 * 1024) return std::nullopt;
	}
	return data;
}

/** Scanner of the blueprint directories. */
class BlueprintScanner : public FileScanner {
public:
	bool AddFile(const std::string &filename, [[maybe_unused]] size_t basepath_length, [[maybe_unused]] const std::string &tar_filename) override
	{
		auto text = ReadWholeFile(filename);
		if (!text.has_value()) return false;
		std::string error;
		auto bp = ParseBlueprintText(*text, &error);
		if (!bp.has_value()) {
			Debug(misc, 0, "Blueprint {} not loaded: {}", filename, error);
			return false;
		}
		if (bp->name.empty()) {
			std::string_view stem = filename;
			auto sep = stem.find_last_of(PATHSEPCHAR);
			if (sep != std::string_view::npos) stem.remove_prefix(sep + 1);
			if (stem.ends_with(".json")) stem.remove_suffix(5);
			bp->name = stem;
		}
		/* The first directory searched wins, as for other content. */
		for (const BlueprintLibraryEntry &e : _blueprint_library) {
			if (StrEqualsIgnoreCase(e.bp.name, bp->name)) return false;
		}
		_blueprint_library.push_back({filename, std::move(*bp)});
		return true;
	}
};

/** Read the blueprints from the "blueprint" directories again. */
void ScanBlueprintLibrary()
{
	_blueprint_library.clear();
	BlueprintScanner scanner;
	uint n = scanner.Scan(".json", BLUEPRINT_DIR, false, true);
	std::ranges::sort(_blueprint_library, [](const BlueprintLibraryEntry &a, const BlueprintLibraryEntry &b) { return StrNaturalCompare(a.bp.name, b.bp.name) < 0; });
	_blueprint_library_scanned = true;
	Debug(misc, 1, "Blueprints: {} loaded", n);
	InvalidateWindowClassesData(WC_METRO_BLUEPRINTS);
}

const std::vector<BlueprintLibraryEntry> &GetBlueprintLibrary()
{
	if (!_blueprint_library_scanned) ScanBlueprintLibrary();
	return _blueprint_library;
}

/** Find a blueprint of the library by name, ignoring case. */
const Blueprint *FindBlueprint(std::string_view name)
{
	for (const BlueprintLibraryEntry &e : GetBlueprintLibrary()) {
		if (StrEqualsIgnoreCase(e.bp.name, name)) return &e.bp;
	}
	return nullptr;
}

/** The personal "blueprint" directory, where blueprints are saved. */
static std::string GetPersonalBlueprintDir()
{
	return FioGetDirectory(SP_AUTODOWNLOAD_PERSONAL_DIR, BLUEPRINT_DIR);
}

/**
 * A file name for a blueprint name: characters file systems do not take
 * become '_', and the device names of Windows get a '_' added.
 */
static std::string BlueprintFileStem(std::string_view name)
{
	std::string out;
	for (char c : name) {
		out.push_back((static_cast<uint8_t>(c) < 0x20 || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) ? '_' : c);
	}
	while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
	if (out.empty()) out = "blueprint";

	static const std::string_view devices[] = {"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
	std::string_view base = std::string_view(out).substr(0, out.find('.'));
	for (std::string_view d : devices) {
		if (StrEqualsIgnoreCase(base, d)) {
			out += '_';
			break;
		}
	}
	return out;
}

/**
 * Save a blueprint in the personal "blueprint" directory. A blueprint of the
 * same name there is replaced; another one whose file name would be the same
 * is not, the new one gets a file name of its own.
 */
bool SaveBlueprintToDisk(const Blueprint &bp, std::string *error)
{
	std::string dir = GetPersonalBlueprintDir();
	FioCreateDirectory(dir);

	std::string filename;
	for (const BlueprintLibraryEntry &e : GetBlueprintLibrary()) {
		if (StrEqualsIgnoreCase(e.bp.name, bp.name) && IsBlueprintDeletable(e)) filename = e.filename;
	}
	if (filename.empty()) {
		std::string stem = BlueprintFileStem(bp.name);
		filename = dir + stem + ".json";
		for (int n = 2; FileExists(filename); n++) filename = fmt::format("{}{}_{}.json", dir, stem, n);
	}

	auto f = FileHandle::Open(filename, "wb");
	if (!f.has_value()) {
		if (error != nullptr) *error = fmt::format("cannot write {}", filename);
		return false;
	}
	std::string json = BlueprintToJson(bp, true);
	bool ok = fwrite(json.data(), 1, json.size(), *f) == json.size();
	f.reset();
	if (!ok) {
		if (error != nullptr) *error = fmt::format("cannot write {}", filename);
		return false;
	}
	ScanBlueprintLibrary();
	return true;
}

/** Whether a blueprint of the library may be deleted (or replaced): only those saved in the personal directory. */
bool IsBlueprintDeletable(const BlueprintLibraryEntry &entry)
{
	return !entry.filename.empty() && entry.filename.starts_with(GetPersonalBlueprintDir());
}

bool DeleteBlueprintFromDisk(const BlueprintLibraryEntry &entry)
{
	if (!IsBlueprintDeletable(entry)) return false;
	bool ok = FioRemove(entry.filename);
	ScanBlueprintLibrary();
	return ok;
}
