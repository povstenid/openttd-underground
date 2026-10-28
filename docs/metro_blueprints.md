# Metro blueprints

A blueprint is a copy of construction that can be pasted elsewhere: underground
track, platforms, ramps, portals and signals on a range of levels and, when the
range reaches the surface, surface rail, signals, depots and the exits
(pavilions, underpasses) of the copied stations.

## Using them

- Window: *Metro blueprints* (the *BP* button of the railway construction
  toolbar, Ctrl+B, or the console command `blueprint gui`).
  - **Copy**: drag a rectangle. *Levels: All* copies every underground level with
    construction in the area, otherwise the levels from *top* to *bottom*.
    *Surface* copies up to the surface. The viewed level becomes the anchor
    level of the blueprint: pasting puts it on the viewed level.
  - **Paste**: the north corner of the footprint goes to the tile clicked. A
    ghost shows the pieces on the viewed level, red where something is in the
    way. R / Shift+R turn the blueprint, M mirrors it. Ctrl+Click joins the
    stations to an adjacent own station, Shift+Click shows the cost only.
  - *Build what fits*: leave out pieces that cannot be built. Off (default):
    nothing is built when any piece cannot be built or the money does not suffice.
    The test before building covers the station side too: the station spread,
    joining platforms without distant joining, the distance of the exits to the
    platforms of the blueprint, and signals that could not stand on the track
    the tile would carry. Should a piece still fail while building (another
    player built there meanwhile), it is left out and the paste says how many
    pieces were.
  - *Toolbar track*: build everything with the track type last used in the
    railway construction toolbar.
  - **Save** / **Delete** / **Rescan**: blueprints on disk; only those in the
    personal folder can be deleted or replaced. **Copy as text** / **Paste
    text**: a blueprint as one line of text on the system clipboard (writing
    it there works on Windows; elsewhere the text goes to the console).
- Console: `blueprint copy|paste|list|rescan|load|save|show|export|import|gui`
  (`blueprint` without arguments lists the syntax; `blueprint import` without
  text reads the system clipboard, as a console line is too short for most
  blueprints).
- Game scripts: `GSMetro.CopyBlueprint`, `GSMetro.GetBlueprintPasteChunkCount`,
  `GSMetro.PasteBlueprintChunk` (see `regression/scenarios/metro_blueprint_paste`).

Pasting over an identical copy builds nothing and costs nothing; plain track is
joined by more track, so a blueprint can extend a line. Platforms pasted on
plain track replace it, as building a platform there does.

Not copied: reservations, trains, owners, station names and identities (each
station of a blueprint becomes a new station), vanilla surface stations and
waypoints, bridges, tunnels, roads and level crossings.

## Files

Blueprints are `*.json` files in the `blueprint` folder of the personal
directory (and of the other search paths, see `list_dirs blueprint`). They are
read when the window or the console first needs them and on *Rescan*. The file
is plain JSON, one piece per line:

```json
{
  "name": "Two levels",
  "openttd_metro_blueprint": 1,
  "railtypes": ["RAIL"],
  "size": [17, 4],
  "stations": 2,
  "items": [
    {"kind":"platform","pos":[8,1,0],"rail":0,"axis":"x","station":0},
    {"kind":"track","pos":[10,1,0],"rail":0,"tracks":["x"],"signals":[{"track":"x","towards":["ne","sw"],"type":"block","variant":"light"}]},
    {"kind":"ramp","pos":[12,1,0],"to":-1,"rail":0,"dir":"sw"},
    {"kind":"portal","pos":[5,1,0],"rail":0,"dir":"sw"},
    {"kind":"surface_rail","pos":[2,1],"rail":0,"tracks":["x"]},
    {"kind":"depot","pos":[4,3],"rail":0,"dir":"nw"},
    {"kind":"exit","pos":[8,0],"type":"pavilion","station":0}
  ]
}
```

- `size`: footprint along x and y, at most 64 each. `pos`: offset in the
  footprint; the third value is the level relative to the anchor level (0 is
  the level pasted on, -1 the one below it). Surface pieces have no level.
- `railtypes`: track type labels (4 characters, or `0x` and 8 hex digits);
  `rail` indexes this list. A label the game does not have prevents pasting
  unless *Toolbar track* is on.
- `tracks`: `x`, `y`, `upper`, `lower`, `left`, `right`.
- `dir`: `ne`, `se`, `sw`, `nw`. A portal's direction is the one a train
  diving into it moves in; a ramp's leads from its level `pos[2]` to its level
  `to`; a depot's is the side its entrance faces.
- `station`: which station of the blueprint a platform or exit belongs to,
  0 to `stations` - 1.
- `signals`: per track the directions trains it faces move in (`towards`),
  `type` (`block`, `entry`, `exit`, `combo`, `pbs`, `pbs_oneway`) and
  `variant` (`light`, `semaphore`).
- `exit` `type`: `pavilion` or `underpass`.

A shared line of text is `OTTDBP1:` followed by the compact JSON in base64.

## How pasting works

The client turns and mirrors the blueprint, resolves the track types and
splits it into parts small enough for a network packet: a command packet holds
no more than 1460 bytes (`COMPAT_MTU`), so a part carries about 100 to 150
pieces, and a paste has at most 256 parts
(`CMD_PASTE_UNDERGROUND_BLUEPRINT`, `src/multilayer/blueprint_cmd.cpp`). The
parts are sent one after the other, each when the previous one has been
executed, so a big paste does not fill the command queue of a server. Each
part builds its pieces with the regular construction commands, after testing
all of them, and returns how many pieces it built and left out. A station
built over several parts is named in the later parts by up to three of its
platforms (position, axis and track type), so the command depends only on the
map. Without *Build what fits* the client tests every part before sending any,
and a part that fails ends the paste.
