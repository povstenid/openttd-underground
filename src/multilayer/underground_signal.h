/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file underground_signal.h Signal type, variant and direction helpers for underground track slices. */

#ifndef UNDERGROUND_SIGNAL_H
#define UNDERGROUND_SIGNAL_H

#include "multilayer_types.h"
#include "../core/bitmath_func.hpp"
#include "../signal_func.h"
#include "../signal_type.h"
#include "../track_func.h"

/*
 * TrackPayload::signal_mask (and RampPayload::signal_mask for the one axis track of a ramp) holds the vanilla per-trackdir presence bits
 * (SignalAlongTrackdir / SignalAgainstTrackdir), TrackPayload::signal_types the
 * type and variant per signal track, exactly like m2 of a vanilla signal tile:
 * bits 0..2 type and bit 3 variant of the track X, Y, upper or left, bits 4..6
 * and bit 7 of the track lower or right.
 */

/** signal_types of track slices from savegames that knew only one-way path signals (light). */
static constexpr uint8_t UNDERGROUND_LEGACY_SIGNAL_TYPES = SIGTYPE_PBS_ONEWAY | (SIGTYPE_PBS_ONEWAY << 4);

/** A slice payload carrying underground signals: TrackPayload or RampPayload. */
template <typename P>
concept UndergroundSignalPayload = requires(P p) { p.signal_mask; p.signal_types; };

/** Bit position of the signal type of a track in TrackPayload::signal_types. */
inline uint8_t UndergroundSignalTypePos(Track track)
{
	return (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 0;
}

template <UndergroundSignalPayload P>
inline SignalType GetUndergroundSignalType(const P &tp, Track track)
{
	return static_cast<SignalType>(GB(tp.signal_types, UndergroundSignalTypePos(track), 3));
}

template <UndergroundSignalPayload P>
inline void SetUndergroundSignalType(P &tp, Track track, SignalType type)
{
	SB(tp.signal_types, UndergroundSignalTypePos(track), 3, type);
}

template <UndergroundSignalPayload P>
inline SignalVariant GetUndergroundSignalVariant(const P &tp, Track track)
{
	return static_cast<SignalVariant>(GB(tp.signal_types, UndergroundSignalTypePos(track) + 3, 1));
}

template <UndergroundSignalPayload P>
inline void SetUndergroundSignalVariant(P &tp, Track track, SignalVariant variant)
{
	SB(tp.signal_types, UndergroundSignalTypePos(track) + 3, 1, variant);
}

/** Whether the track carries a signal in either direction. */
template <UndergroundSignalPayload P>
inline bool HasUndergroundSignalOnTrack(const P &tp, Track track)
{
	return (tp.signal_mask & SignalOnTrack(track)) != 0;
}

/** Whether a signal faces a train running along the trackdir. */
template <UndergroundSignalPayload P>
inline bool HasUndergroundSignalOnTrackdir(const P &tp, Trackdir td)
{
	return (tp.signal_mask & SignalAlongTrackdir(td)) != 0;
}

/**
 * Whether a train running along the trackdir meets the back of a one-way
 * signal it may not pass, like HasOnewaySignalBlockingTrackdir on the surface:
 * every signal type but the (two-way passable) path signal is one-way.
 */
template <UndergroundSignalPayload P>
inline bool HasUndergroundOnewaySignalBlockingTrackdir(const P &tp, Trackdir td)
{
	return (tp.signal_mask & SignalAgainstTrackdir(td)) != 0 && (tp.signal_mask & SignalAlongTrackdir(td)) == 0 &&
			GetUndergroundSignalType(tp, TrackdirToTrack(td)) != SIGTYPE_PBS;
}

/** The one track of a ramp slice: the axis it climbs along. */
inline Track GetRampTrack(const RampPayload &rp)
{
	return AxisToTrack(DiagDirToAxis(static_cast<DiagDirection>(rp.direction)));
}

/**
 * The level a train leaving a slice towards a direction continues at: the
 * level of that side for a ramp, the slice's own level for anything else.
 */
inline int16_t GetUndergroundLeaveDepth(const TileSlice &s, DiagDirection exitdir)
{
	if (s.kind != SliceKind::Ramp) return s.span.z_bot;
	return exitdir == static_cast<DiagDirection>(s.ramp.direction) ? s.ramp.exit_depth : s.ramp.entry_depth;
}

/** Whether a slice kind carries signals: plain track and ramps. */
inline bool CanSliceCarrySignals(const TileSlice &s)
{
	return s.kind == SliceKind::Track || s.kind == SliceKind::Ramp;
}

/** The signal presence bits of a track or ramp slice, 0 for any other slice. */
inline uint8_t GetUndergroundSignalMask(const TileSlice &s)
{
	switch (s.kind) {
		case SliceKind::Track: return s.track.signal_mask;
		case SliceKind::Ramp: return s.ramp.signal_mask;
		default: return 0;
	}
}

/** Signal type of the track of a track or ramp slice; only meaningful when it has a signal. */
inline SignalType GetUndergroundSignalType(const TileSlice &s, Track track)
{
	return s.kind == SliceKind::Ramp ? GetUndergroundSignalType(s.ramp, track) : GetUndergroundSignalType(s.track, track);
}

/** Signal variant of the track of a track or ramp slice; only meaningful when it has a signal. */
inline SignalVariant GetUndergroundSignalVariant(const TileSlice &s, Track track)
{
	return s.kind == SliceKind::Ramp ? GetUndergroundSignalVariant(s.ramp, track) : GetUndergroundSignalVariant(s.track, track);
}

/** Whether a track or ramp slice carries a signal on the track in either direction. */
inline bool HasUndergroundSignalOnTrack(const TileSlice &s, Track track)
{
	return (GetUndergroundSignalMask(s) & SignalOnTrack(track)) != 0;
}

/** Whether a signal of a track or ramp slice faces a train running along the trackdir. */
inline bool HasUndergroundSignalOnTrackdir(const TileSlice &s, Trackdir td)
{
	return (GetUndergroundSignalMask(s) & SignalAlongTrackdir(td)) != 0;
}

/** Whether a train running along the trackdir of a track or ramp slice meets the back of a one-way signal. */
inline bool HasUndergroundOnewaySignalBlockingTrackdir(const TileSlice &s, Trackdir td)
{
	switch (s.kind) {
		case SliceKind::Track: return HasUndergroundOnewaySignalBlockingTrackdir(s.track, td);
		case SliceKind::Ramp: return HasUndergroundOnewaySignalBlockingTrackdir(s.ramp, td);
		default: return false;
	}
}

#endif /* UNDERGROUND_SIGNAL_H */
