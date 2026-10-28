#!/usr/bin/env python3
"""Generate the metro sprites of the OpenTTD base graphics set.

This script is the single source of truth for the metro art: it writes
  * metro_station.png  - 8bpp sprite sheet in the OpenTTD DOS palette,
  * metro_station.nfo  - the sprite list (Action 5 type 0x1C) for grfcodec,
and, with --preview, PNG previews of every asset composited onto mock
tiles (4x magnified) into docs/metro_art_preview/.

The order of SPRITES below is the order in the GRF and must match the
SPR_METRO_* constants in src/table/sprites.h; the script checks that
METRO_STATION_SPRITE_COUNT there equals len(SPRITES).

Usage (from any directory):
    python generate_metro_sprites.py            # sheet + nfo
    python generate_metro_sprites.py --preview  # also the previews

How the art is made
-------------------
Most sprites are rendered from a tiny scene of axis-aligned boxes and
planes by casting one ray per screen pixel through the exact OpenTTD
dimetric projection (screen x = 2*(y-x), screen y = x+y-z, world units of
1/16 tile). The pixel <-> world mapping reproduces the vanilla 64x31 ground
diamond pixel for pixel, so ground sprites line up with the vanilla track
and slope sprites. Every material picks a palette ramp and a level; the
renderer darkens it per face (top / south-west side / south-east side), so
the light direction is the same on all assets. Thin details (rails, posts)
are drawn as depth-tested 3D lines and small icons are hand-drawn bitmaps.
No anti-aliasing: every pixel is a palette index.
"""

import math
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))

# --------------------------------------------------------------------------
# Palette
# --------------------------------------------------------------------------

def load_palette():
    """The canonical DOS palette, taken byte for byte from a baseset sheet
    (grfcodec rejects sheets whose palette differs from it)."""
    ref = Image.open(os.path.join(HERE, 'openttdgui.png'))
    assert ref.mode == 'P'
    pal = ref.getpalette()[:768]
    assert pal[0:3] == [0, 0, 255], 'index 0 must be the transparent blue'
    return pal

# Palette ramps used by the art, dark -> light. Only indices 1..214 are used:
# 0 is transparent, 215..226 are remap/unused and 227..254 are animated.
RAMPS = {
    'grey':   list(range(1, 16)),            # neutral greys (concrete, steel)
    'rock':   [16, 17, 18, 19, 20, 21, 22, 23],  # blue-grey cut rock
    'cc':     list(range(198, 206)),         # company colour (remapped)
    'glass':  list(range(154, 162)),         # teal glass
    'yellow': [65, 66, 67, 51],              # safety line
    'lamp':   [67, 68, 69],                  # lamp glow
    'green':  [206, 207, 208, 209],
    'red':    [162, 163, 164, 165],
    'steel':  [70, 71, 122, 123, 124, 125],  # dark red-brown paint of the tunnel ribs
}

# Light: the top is lit, the south-west faces (+x) a bit darker, the
# south-east faces (+y) darker still; inner faces of pits/tunnels darker yet.
FACE_SHADE = {'top': 0, 'px': -1, 'py': -2, 'nx': -3, 'ny': -4, 'bot': -3, 'slope': 0}


def col(ramp, level):
    r = RAMPS[ramp]
    return r[max(0, min(len(r) - 1, level))]


def hash01(*v):
    """Deterministic pseudo random number in [0, 1) from integers."""
    h = 2166136261
    for x in v:
        h ^= (int(x) + 0x9E3779B9) & 0xFFFFFFFF
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= h >> 13
        h = (h * 0x5bd1e995) & 0xFFFFFFFF
        h ^= h >> 15
    return (h & 0xFFFFFF) / float(0x1000000)

# --------------------------------------------------------------------------
# Projection and ray casting
# --------------------------------------------------------------------------
# Pixel (sx, sy) relative to RemapCoords(0, 0, 0) of the tile is the world
# line  y - x = A = (sx - 0.5) / 2,  x + y - z = B = sy + 1,  i.e. the points
# (t, t + A, 2t + A - B); a larger t is nearer to the viewer.

SX_RANGE = range(-48, 49)
SY_RANGE = range(-56, 64)


def ray(sx, sy):
    return (sx - 0.5) / 2.0, sy + 1.0


def project(x, y, z):
    """Screen pixel of a world point (inverse of ray())."""
    return int(math.floor(2 * (y - x) + 1.0)), int(math.floor(x + y - z - 0.5))


class Hit:
    __slots__ = ('t', 'face', 'x', 'y', 'z')

    def __init__(self, t, face, A, B):
        self.t = t
        self.face = face
        self.x = t
        self.y = t + A
        self.z = 2 * t + A - B


class Box:
    """Axis-aligned box. mat(face, x, y, z, sx, sy) -> (ramp, level) | int | None.
    inside=True renders the inner far faces (a pit / tunnel void seen through
    an opening); clip_top additionally requires the ray to enter through the top."""

    def __init__(self, x0, x1, y0, y1, z0, z1, mat, tag='main', inside=False, clip_top=False, swap=False, through=None):
        if swap:
            x0, x1, y0, y1 = y0, y1, x0, x1
        self.b = (x0, x1, y0, y1, z0, z1)
        self.mat = mat
        self.tag = tag
        self.inside = inside
        self.clip_top = clip_top
        self.swap = swap
        self.through = through

    def hit(self, A, B):
        x0, x1, y0, y1, z0, z1 = self.b
        hi = [(x1, 'px'), (y1 - A, 'py'), ((z1 - A + B) / 2.0, 'top')]
        lo = [(x0, 'nx'), (y0 - A, 'ny'), ((z0 - A + B) / 2.0, 'bot')]
        t_out, f_out = min(hi)
        t_in, f_in = max(lo)
        if t_in > t_out:
            return None
        if self.clip_top:
            tt = (z1 - A + B) / 2.0
            if not (x0 <= tt <= x1 and y0 <= tt + A <= y1):
                return None
        if self.inside:
            if self.through is not None and not self.through(A, B):
                return None
            return Hit(t_in, f_in, A, B)
        return Hit(t_out, f_out, A, B)


class Plane:
    """Ground plane z = a*x + b*y + c over the rectangle [x0,x1] x [y0,y1]."""

    def __init__(self, a, b, c, mat, tag='main', x0=0, x1=16, y0=0, y1=16):
        self.p = (a, b, c)
        self.r = (x0, x1, y0, y1)
        self.mat = mat
        self.tag = tag
        self.swap = False

    def hit(self, A, B):
        a, b, c = self.p
        d = 2.0 - a - b
        t = (c + b * A - A + B) / d
        x0, x1, y0, y1 = self.r
        if not (x0 <= t <= x1 and y0 <= t + A <= y1):
            return None
        return Hit(t, 'slope' if (a or b) else 'top', A, B)


class Prism:
    """Convex solid: the points p with n . p <= d for every (n, d, face) in
    planes (n a 3-vector). The face label of the plane the ray enters through
    is passed to mat."""

    def __init__(self, planes, mat, tag='main'):
        self.planes = planes
        self.mat = mat
        self.tag = tag
        self.swap = False

    def hit(self, A, B):
        # p(t) = (t, t + A, 2t + A - B); a larger t is nearer to the viewer
        t_hi, f_hi, t_lo = 1e9, None, -1e9
        for (nx, ny, nz), d, face in self.planes:
            k = nx + ny + 2 * nz
            c = d - ny * A - nz * (A - B)
            if abs(k) < 1e-12:
                if c < 0:
                    return None
            elif k > 0:
                if c / k < t_hi:
                    t_hi, f_hi = c / k, face
            else:
                t_lo = max(t_lo, c / k)
        if f_hi is None or t_lo > t_hi:
            return None
        return Hit(t_hi, f_hi, A, B)


def local(mat):
    """Wrap a material written for the X axis so it works on a swapped (Y axis) box."""
    swapf = {'px': 'py', 'py': 'px', 'nx': 'ny', 'ny': 'nx'}

    def m(face, x, y, z, sx, sy):
        return mat(swapf.get(face, face), y, x, z, sx, sy)
    return m


class Scene:
    def __init__(self):
        self.prims = []
        self.lines = []    # (p0, p1, colour, tag)
        self.stamps = []   # (point, bitmap rows, colour map, tag)

    def add(self, p):
        if getattr(p, 'swap', False):
            p.mat = local(p.mat)
        self.prims.append(p)
        return p

    def line(self, p0, p1, colour, tag='main', swap=False):
        if swap:
            p0 = (p0[1], p0[0], p0[2])
            p1 = (p1[1], p1[0], p1[2])
        self.lines.append((p0, p1, colour, tag))

    def stamp(self, p, bitmap, cmap, tag='main', swap=False):
        if swap:
            p = (p[1], p[0], p[2])
        self.stamps.append((p, bitmap, cmap, tag))

    def render(self):
        """-> dict (sx, sy) -> (colour, tag)."""
        out = {}
        depth = {}
        for sy in SY_RANGE:
            for sx in SX_RANGE:
                A, B = ray(sx, sy)
                best = None
                for p in self.prims:
                    h = p.hit(A, B)
                    if h is None or (best is not None and h.t <= best[0].t):
                        continue
                    c = p.mat(h.face, h.x, h.y, h.z, sx, sy)
                    if c is None:
                        continue
                    if isinstance(c, tuple):
                        c = col(c[0], c[1] + FACE_SHADE[h.face])
                    best = (h, c, p.tag)
                if best is not None:
                    out[(sx, sy)] = (best[1], best[2])
                    depth[(sx, sy)] = best[0].t
        for p0, p1, colour, tag in self.lines:
            n = int(max(abs(p1[i] - p0[i]) for i in range(3)) * 8) + 2
            for i in range(n + 1):
                f = i / float(n)
                p = [p0[k] + (p1[k] - p0[k]) * f for k in range(3)]
                px = project(*p)
                if depth.get(px, -1e9) <= p[0] + 0.3:
                    out[px] = (colour, tag)
                    depth[px] = p[0]
        for p, bitmap, cmap, tag in self.stamps:
            cx, cy = project(*p)
            h = len(bitmap)
            w = len(bitmap[0])
            for j, row in enumerate(bitmap):
                for i, ch in enumerate(row):
                    if ch in cmap:
                        out[(cx - w // 2 + i, cy - h + 1 + j)] = (cmap[ch], tag)
        return out

# --------------------------------------------------------------------------
# Sprites
# --------------------------------------------------------------------------

class Sprite:
    def __init__(self, name, pixels, ref=(0, 0, 0), comment='', extra_yoffs=0):
        """pixels: dict (sx, sy) -> colour in tile screen coordinates.
        ref: the world point the engine draws the sprite at (bounding box
        origin + offset for sortable sprites, (0,0,0) for ground sprites)."""
        self.name = name
        self.comment = comment
        pts = [k for k, v in pixels.items() if v]
        if not pts:
            pixels = {(0, 0): 0}
            pts = [(0, 0)]
        x0 = min(p[0] for p in pts)
        x1 = max(p[0] for p in pts)
        y0 = min(p[1] for p in pts)
        y1 = max(p[1] for p in pts)
        self.w = x1 - x0 + 1
        self.h = y1 - y0 + 1
        rx, ry, rz = ref
        self.xoffs = x0 - 2 * (ry - rx)
        self.yoffs = y0 - (rx + ry - rz) + extra_yoffs
        self.data = [[pixels.get((x0 + i, y0 + j), 0) for i in range(self.w)] for j in range(self.h)]
        self.ref = ref


def from_scene(name, scene, ref=(0, 0, 0), tags=None, comment=''):
    px = scene.render()
    return Sprite(name, {k: c for k, (c, t) in px.items() if tags is None or t in tags}, ref, comment)


def from_bitmap(name, bitmap, cmap, comment='', anchor=None):
    """Screen aligned icon; anchor = pixel of the bitmap at the draw point (default: centre)."""
    h = len(bitmap)
    w = len(bitmap[0])
    ax, ay = anchor if anchor else (w // 2, h // 2)
    px = {}
    for j, row in enumerate(bitmap):
        for i, ch in enumerate(row):
            if ch in cmap and cmap[ch]:
                px[(i - ax, j - ay)] = cmap[ch]
    return Sprite(name, px, (0, 0, 0), comment)


def diamond_mask(sx, sy):
    """True for the pixels of the vanilla flat 64x31 ground sprite."""
    A, B = ray(sx, sy)
    return abs(A) <= min(B, 32 - B)

# ----- materials ------------------------------------------------------------

def speckle(sx, sy, seed, p):
    return hash01(sx, sy, seed) < p


def rock_mat(base, seed):
    """Cut rock: blue-grey with sparse light/dark grains and a few seams."""
    def m(face, x, y, z, sx, sy):
        r = hash01(sx, sy, seed)
        lvl = base
        if r < 0.05:
            lvl -= 1
        elif r > 0.95:
            lvl += 1
        # two faint strata seams crossing the tile
        if face == 'top' and abs((x * 0.6 + y) - 12.3) < 0.28 and 2 < x < 13:
            lvl -= 1
        return ('rock', lvl)
    return m


def concrete_tiles(base, grid=4.0, seed=1):
    def m(face, x, y, z, sx, sy):
        lvl = base
        if face == 'top':
            fx, fy = x % grid, y % grid
            if fx < 0.35 or fy < 0.35:
                lvl -= 1
            elif speckle(sx, sy, seed, 0.05):
                lvl += 1
        return ('grey', lvl)
    return m


def solid(ramp, level):
    return lambda face, x, y, z, sx, sy: (ramp, level)

# ----- assets ---------------------------------------------------------------

def slab(name, base, seed, comment):
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, rock_mat(base, seed)))
    return from_scene(name, s, comment=comment)


def ceiling():
    """Checkerboard used to dim the surface in the X-ray view (kept from the
    old sheet; the engine draws it with z=7, the y offset compensates)."""
    px = {}
    for sy in range(0, 31):
        for sx in range(-31, 33):
            if diamond_mask(sx, sy) and (sx + sy) % 2 == 0:
                A, B = ray(sx, sy)
                px[(sx, sy)] = 2 if abs(A) < 0.3 * min(B, 32 - B) else 3
    return Sprite('SPR_METRO_CEILING', px, comment='X-ray view ceiling dim (drawn at z=7)', extra_yoffs=7)


def ramp_tile(name, descent):
    """Concrete incline in the slab style. descent: 'NE','SE','SW','NW'."""
    a, b, c = {'NE': (0.5, 0, 0), 'SE': (0, -0.5, 8), 'SW': (-0.5, 0, 8), 'NW': (0, 0.5, 0)}[descent]
    shade = {'NE': -1, 'SE': 0, 'SW': 1, 'NW': -2}[descent]
    along_x = descent in ('NE', 'SW')

    def m(face, x, y, z, sx, sy):
        u, w = (x, y) if along_x else (y, x)  # u along the slope, w across
        lvl = 10 + shade
        if w < 1.5 or w > 14.5:
            lvl += 2  # kerbs
            if w < 0.5 or w > 15.5:
                lvl -= 3
        elif (u % 2.0) < 0.5:
            lvl -= 1  # anti-slip grooves
        elif speckle(sx, sy, 7, 0.04):
            lvl -= 1
        return ('grey', lvl)
    s = Scene()
    s.add(Plane(a, b, c, m))
    return from_scene(name, s, comment=f'ramp descending towards {descent}')


def platform_floor(name, axis_y):
    def m(face, x, y, z, sx, sy):
        if 5 <= y <= 11:  # track bed between the platforms
            r = hash01(sx, sy, 11)
            return ('grey', 5 + (1 if r > 0.8 else (-1 if r < 0.15 else 0)))
        return concrete_tiles(10)(face, x, y, z, sx, sy)
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m, swap=axis_y))
    return from_scene(name, s, comment='station floor ' + ('Y' if axis_y else 'X'))


PLATFORM_H = 3


def platform_mat(edge_lo, edge_hi, line_lo, line_hi):
    def m(face, x, y, z, sx, sy):
        if face == 'top':
            if edge_lo <= y <= edge_hi:
                return ('grey', 13)
            if line_lo <= y <= line_hi:
                return ('yellow', 2)
            return concrete_tiles(11, seed=3)(face, x, y, z, sx, sy)
        if z < 0.6:
            return ('grey', 7)
        return ('grey', 10)
    return m


def platform_rear(name, axis_y):
    s = Scene()
    sw = axis_y
    s.add(Box(0, 16, 0, 5, 0, PLATFORM_H, platform_mat(4.5, 5, 3.6, 4.3), swap=sw))
    # bench with backrest (company colour)
    s.add(Box(1, 4, 1, 2.2, PLATFORM_H, PLATFORM_H + 1.5, solid('cc', 6), swap=sw))
    s.add(Box(1, 4, 0.5, 1, PLATFORM_H, PLATFORM_H + 3.5, solid('cc', 5), swap=sw))
    # lamp post
    s.line((14.5, 1.0, PLATFORM_H), (14.5, 1.0, PLATFORM_H + 11), 4, swap=sw)
    s.add(Box(13.8, 15.2, 0.4, 1.6, PLATFORM_H + 11, PLATFORM_H + 12.5, solid('lamp', 2), swap=sw))
    s.add(Box(13.6, 15.4, 0.2, 1.8, PLATFORM_H + 12.5, PLATFORM_H + 13, solid('grey', 5), swap=sw))
    return from_scene(name, s, comment='rear platform ' + ('Y' if axis_y else 'X'))


def platform_front(name, axis_y):
    s = Scene()
    s.add(Box(0, 16, 11, 16, 0, PLATFORM_H, platform_mat(11, 11.5, 11.7, 12.4), swap=axis_y))
    ref = (11, 0, 0) if axis_y else (0, 11, 0)
    return from_scene(name, s, ref=ref, comment='front platform ' + ('Y' if axis_y else 'X'))


def pillar(name, axis_y):
    s = Scene()
    sw = axis_y

    def m(face, x, y, z, sx, sy):
        lvl = 11
        if z > PLATFORM_H + 20:
            lvl = 12
        elif PLATFORM_H + 9 < z < PLATFORM_H + 10.5:
            return ('cc', 5)  # company colour band
        if face != 'top' and speckle(sx, sy, 5, 0.05):
            lvl -= 1
        return ('grey', lvl)
    s.add(Box(7, 9, 1, 3, PLATFORM_H, PLATFORM_H + 20, m, swap=sw))
    s.add(Box(6.5, 9.5, 0.5, 3.5, PLATFORM_H + 20, PLATFORM_H + 22, m, swap=sw))
    ref = (1, 7, PLATFORM_H) if axis_y else (7, 1, PLATFORM_H)
    return from_scene(name, s, ref=ref, comment='pillar ' + ('Y' if axis_y else 'X'))


def board(name, axis_y):
    s = Scene()
    sw = axis_y
    z0, z1 = PLATFORM_H + 6, PLATFORM_H + 10

    def m(face, x, y, z, sx, sy):
        if face == 'py':  # towards the track
            c = int((12 - x) * 2)
            r = int(z1 - z)
            if r <= 0 or r >= 3 or c <= 0 or c >= 13:
                return ('cc', 4)
            if 2 <= c <= 11 and hash01(c, r, 21) < 0.55:
                return 3
            return 14
        return ('cc', 6)
    s.line((5.6, 2.0, PLATFORM_H), (5.6, 2.0, z0), 4, swap=sw)
    s.line((11.4, 2.0, PLATFORM_H), (11.4, 2.0, z0), 4, swap=sw)
    s.add(Box(5, 12, 1.8, 2.4, z0, z1, m, swap=sw))
    ref = (1, 5, PLATFORM_H) if axis_y else (5, 1, PLATFORM_H)
    return from_scene(name, s, ref=ref, comment='station name board ' + ('Y' if axis_y else 'X'))


# Profile of the tunnel ribs: straight legs up to TUNNEL_SPRING, then an
# elliptical vault. The outer face reaches the edges of the tile.
TUNNEL_SPRING = 11.0
TUNNEL_OUTER = (8.0, 6.0)   # half width, rise of the vault
TUNNEL_INNER = (6.5, 4.8)
TUNNEL_TOP = TUNNEL_SPRING + TUNNEL_OUTER[1]

# Evacuation walkway along the front side of a tunnel (in X-axis terms the
# +y side), inside the opening of the ribs; its top is about a sixth of the
# tunnel height above the floor.
WALKWAY_Y = (12.0, TUNNEL_INNER[0] + 8.0)
WALKWAY_H = 3.0

# Cable tray along the back side of a tunnel (the -y side): black cables in
# a row of stripes at about half of the tunnel height, sagging between the
# ribs (the rib of a plain tile stands at x = 2..3).
CABLE_Y = (0.6, 0.9)
CABLE_Z = (5.5, 8.0, 10.5)
CABLE_SUPPORT = 2.5
CABLE_SAG = 0.8

# A diagonal piece runs through a corridor 16 / sqrt(2) wide; its rib fills it.
DIAG_TUNNEL_OUTER = (5.9, TUNNEL_OUTER[1])
DIAG_TUNNEL_INNER = (4.6, TUNNEL_INNER[1])


def in_tunnel_arch(w, z, half, rise):
    """w across the tunnel, 8 = its axis."""
    if abs(w - 8) > half or z < 0:
        return False
    return z <= TUNNEL_SPRING or ((w - 8) / half) ** 2 + ((z - TUNNEL_SPRING) / rise) ** 2 <= 1


FLANGE = 0.5


def rib_part(w, z, outer, inner):
    """Part of the I-beam tunnel ring at (w, z), w across the tunnel with 8 on
    its axis: 'flange' along the outer and the inner edge, 'web' between them,
    None outside the ring."""
    if not in_tunnel_arch(w, z, *outer) or in_tunnel_arch(w, z, *inner):
        return None
    if not in_tunnel_arch(w, z, outer[0] - FLANGE, outer[1] - FLANGE):
        return 'flange'
    if in_tunnel_arch(w, z, inner[0] + FLANGE, inner[1] + FLANGE):
        return 'flange'
    return 'web'


def tunnel_rib_diag(name, across, centre, ref):
    """Tunnel rib over a diagonal (half-tile) track piece, same ring as the
    straight rib. across: unit direction of the ring's plane in the xy plane,
    (1, 1) for the UPPER/LOWER pieces and (1, -1) for LEFT/RIGHT; the track runs
    perpendicular to it. centre: middle of the track piece the ring stands on.
    The ring is not axis-aligned, so it is built from small cubes."""
    s = Scene()
    spring = TUNNEL_SPRING
    outer, inner = DIAG_TUNNEL_OUTER, DIAG_TUNNEL_INNER
    ux, uy = across[0] / math.sqrt(2), across[1] / math.sqrt(2)
    vx, vy = -uy, ux   # along the track

    def ring(part, sx, sy):
        # no per-face shade: the cubes' faces do not follow the ring
        if part == 'web':
            return col('steel', 2)
        return col('steel', 3 if speckle(sx, sy, 72, 0.3) else 4)

    c = 0.25
    t = -outer[0]
    while t <= outer[0] + 1e-6:
        z = 0.0
        while z < spring + outer[1]:
            zc = z + c
            part = rib_part(t + 8, zc, outer, inner)
            if part is not None:
                cubes = ((-0.25, c), (0.25, c)) if part == 'flange' else ((0.0, 0.15),)
                for sv, half in cubes:
                    px = centre[0] + t * ux + sv * vx
                    py = centre[1] + t * uy + sv * vy
                    s.add(Box(px - half, px + half, py - half, py + half, z, z + 2 * c,
                              lambda face, x, y, zz, sx, sy, part=part: ring(part, sx, sy)))
            z += 2 * c
        t += 0.25
    return from_scene(name, s, ref=ref, comment='tunnel rib over diagonal track ' + name[-1])


def board_short(name, axis_y):
    """Metro-style station: a narrower name board that fits between two pillars."""
    s = Scene()
    sw = axis_y
    z0, z1 = PLATFORM_H + 6, PLATFORM_H + 10

    def m(face, x, y, z, sx, sy):
        if face == 'py':  # towards the track
            c = int((10 - x) * 2)
            r = int(z1 - z)
            if r <= 0 or r >= 3 or c <= 0 or c >= 9:
                return ('cc', 4)
            if 2 <= c <= 7 and hash01(c, r, 22) < 0.55:
                return 3
            return 14
        return ('cc', 6)
    s.line((5.6, 2.0, PLATFORM_H), (5.6, 2.0, z0), 4, swap=sw)
    s.line((9.4, 2.0, PLATFORM_H), (9.4, 2.0, z0), 4, swap=sw)
    s.add(Box(5, 10, 1.8, 2.4, z0, z1, m, swap=sw))
    ref = (1, 5, PLATFORM_H) if axis_y else (5, 1, PLATFORM_H)
    return from_scene(name, s, ref=ref, comment='short station name board ' + ('Y' if axis_y else 'X'))


def station_portal(name, axis_y):
    """Metro-style station: the end wall across both platforms with an arched
    opening over the track, in the style of the tunnel ribs (vault springing at
    z = 12). In X-axis terms the wall is the box x in [0, 1], full width; the
    engine draws it at either end of the station."""
    s = Scene()
    spring, half, height = 12.0, 4.5, 20.0

    def opening(w, z):
        if abs(w - 8) > half:
            return False
        return z <= spring or (w - 8) ** 2 + (z - spring) ** 2 <= half * half

    def ring(w, z):
        if abs(w - 8) > half + 1.2:
            return False
        return z <= spring or (w - 8) ** 2 + (z - spring) ** 2 <= (half + 1.2) ** 2

    def wall(face, w, z, sx, sy):
        if face == 'top':
            return ('grey', 12)
        if ring(w, z):
            return ('grey', 13)           # concrete ring round the opening
        if height - 3 < z < height - 2:
            return ('cc', 5)              # company colour band
        lvl = 10 if int(z) % 4 == 0 else 11
        if speckle(sx, sy, 81, 0.04):
            lvl -= 1
        return ('grey', lvl)
    # Cells, like the tunnel rib: the ray caster sees only the nearest box face.
    step = 0.5
    for i in range(int(16 / step)):
        w0 = i * step
        for j in range(int(height / step)):
            z0 = j * step
            wc, zc = w0 + step / 2, z0 + step / 2
            if not opening(wc, zc):
                s.add(Box(0, 1, w0, w0 + step, z0, z0 + step,
                          lambda face, x, y, z, sx, sy, wc=wc, zc=zc: wall(face, wc, zc, sx, sy), swap=axis_y))
    return from_scene(name, s, ref=(0, 0, 0), comment='station end portal ' + ('Y' if axis_y else 'X'))


def tunnel(d):
    """Tunnel mouth on the slab for DiagDirection d (direction into the tunnel,
    like the vanilla tunnel sprites): a rock block with a concrete headwall."""
    s = Scene()
    axis_y = d in ('SE', 'NW')
    visible_mouth = d in ('NE', 'NW')   # the mouth faces the viewer
    # In X-axis terms (swap for Y): the body spans u in [0,8] or [8,16].
    if visible_mouth:
        bx0, bx1, face_x = 0, 8, 8
    else:
        bx0, bx1, face_x = 8, 16, 16    # far end of the block shows the section

    def in_arch(w, z, grow=0.0):
        r = 4.5 + grow
        if abs(w - 8) > r:
            return False
        return z <= 7 or (w - 8) ** 2 + (z - 7) ** 2 <= r * r

    def block(face, x, y, z, sx, sy):
        if face == 'px' and abs(x - face_x) < 1e-6:
            if in_arch(y, z):
                return None if visible_mouth else 2
            if in_arch(y, z, 1.3):
                return ('grey', 13)      # concrete ring
            lvl = 11 if int(z) % 4 else 10  # headwall courses
            return ('grey', lvl)
        if face == 'top':
            if y < 1.6 or y > 14.4 or x < bx0 + 0.6 or x > bx1 - 0.6:
                return ('grey', 12)      # coping
        return rock_mat(5, 30 + len(d))(face, x, y, z, sx, sy)
    s.add(Box(bx0, bx1, 1, 15, 0, 15, block, tag='front', swap=axis_y))

    if visible_mouth:
        def void(face, x, y, z, sx, sy):
            depth = face_x - x
            if face == 'bot':
                if abs(y - 6.5) < 0.4 or abs(y - 9.5) < 0.4:
                    return 5 if depth < 3 else 3
                return 2
            return 3 if depth < 2.5 else (2 if depth < 5 else 1)
        s.add(Box(0, 8, 3.5, 12.5, 0, 11.5, void, tag='rear', inside=True, swap=axis_y))
    else:
        def shade(face, x, y, z, sx, sy):
            return 2 if (sx + sy) % 2 == 0 else None
        s.add(Box(6.5, 8, 3.5, 12.5, 0, 0, shade, tag='rear', swap=axis_y))

    base = {'NE': 0, 'SE': 1, 'SW': 2, 'NW': 3}[d]
    rear = from_scene(f'SPR_METRO_TUNNEL_BASE + {base * 2}', s, tags={'rear'},
                      comment=f'tunnel mouth {d}: rear (ground)')
    front = from_scene(f'SPR_METRO_TUNNEL_BASE + {base * 2 + 1}', s, ref=(15, 15, 0), tags={'front'},
                       comment=f'tunnel mouth {d}: front (roof bounds)')
    return [rear, front]


def tunnel_rib(name, axis_y):
    """Steel tunnel rib over a straight underground track, an I-beam bent into
    a ring and painted dark red-brown: one ring per tile, so a row of tiles
    reads as a tunnel while trains stay visible between the rings. The ring is
    as wide as the tile; the vault springs above the top of a signal. In
    X-axis terms the ring stands at x in [2, 3], behind the signal positions of
    its own tile (x = 4 and x = 11); the signals of the tile behind it project
    into the opening."""
    s = Scene()

    def ring(face, part, sx, sy):
        if part == 'web':
            return ('steel', 3)
        if face == 'top':
            return ('steel', 5)
        return ('steel', 4 if speckle(sx, sy, 71, 0.7) else 5)
    # The ray caster sees only the nearest face of a box, so one box with the
    # opening cut out of its material would lose every pixel whose nearest face
    # lies in the opening. Build the ring from small cells instead: the flanges
    # as wide as the rib, the web thin between them.
    step = 0.5
    for i in range(int(16 / step)):
        w0 = i * step
        for j in range(int((TUNNEL_TOP + 1) / step)):
            z0 = j * step
            part = rib_part(w0 + step / 2, z0 + step / 2, TUNNEL_OUTER, TUNNEL_INNER)
            if part is None:
                continue
            x0, x1 = (2, 3) if part == 'flange' else (2.35, 2.65)
            s.add(Box(x0, x1, w0, w0 + step, z0, z0 + step,
                      lambda face, x, y, z, sx, sy, part=part: ring(face, part, sx, sy), swap=axis_y))
    ref = (0, 2, 0) if axis_y else (2, 0, 0)
    return from_scene(name, s, ref=ref, comment='tunnel rib over straight track ' + ('Y' if axis_y else 'X'))


def tunnel_walkway(name, axis_y):
    """Evacuation walkway along the front side of a straight tunnel tile: a
    narrow concrete ledge with a yellow edge towards the track and a handrail
    on the wall side. Drawn as a ground sprite: nothing on the tile stands in
    front of it."""
    s = Scene()
    y0, y1 = WALKWAY_Y

    def m(face, x, y, z, sx, sy):
        if face == 'top':
            if y < y0 + 0.5:
                return ('yellow', 2)
            lvl = 11 if (x % 2.0) > 0.3 else 9   # grating
            return ('grey', lvl)
        if face == 'ny':
            return ('grey', 11 if z > WALKWAY_H - 0.6 else 9)
        return ('grey', 10)
    s.add(Box(0, 16, y0, y1, 0, WALKWAY_H, m, swap=axis_y))
    rail_y, rail_z = y1 - 0.4, WALKWAY_H + 2.5
    s.line((0, rail_y, rail_z), (16, rail_y, rail_z), col('yellow', 1), swap=axis_y)
    for px in (1.0, 5.0, 9.0, 13.0):
        s.line((px, rail_y, WALKWAY_H), (px, rail_y, rail_z), col('grey', 5), swap=axis_y)
    return from_scene(name, s, comment='tunnel evacuation walkway ' + ('Y' if axis_y else 'X'))


def tunnel_cables(name, axis_y, support=CABLE_SUPPORT):
    """Cable tray along the back side of a straight tunnel tile: black cables
    in stripes, each hanging in a shallow curve from rib to rib. support: x of
    the rib that holds them."""
    s = Scene()
    step = 0.5

    def cable(face, x, y, z, sx, sy):
        return 1 if face == 'top' else 2
    for i in range(int(16 / step)):
        x0 = i * step
        u = ((x0 + step / 2 - support) % 16) / 16
        sag = CABLE_SAG * math.sin(math.pi * u)
        for zc in CABLE_Z:
            s.add(Box(x0, x0 + step, CABLE_Y[0], CABLE_Y[1], zc - sag, zc - sag + 0.5, cable, swap=axis_y))
    # brackets on the rib holding the tray
    for zc in CABLE_Z:
        s.add(Box(support - 0.4, support + 0.4, 0, CABLE_Y[1] + 0.4, zc - 0.5, zc, solid('grey', 6), swap=axis_y))
    return from_scene(name, s, comment='tunnel cable tray ' + ('Y' if axis_y else 'X'))


# Diagonal track pieces: the corridor of a piece is the strip between two
# lines q = const, q a linear form of (x, y). The walkway and the cables lie on
# the same lines as on the straight tiles they connect to, so they continue
# across the tile edges. For the pieces that run up and down the screen
# (LEFT/RIGHT) the walkway is on the right side of the screen, matching the
# X-axis tiles (a Y-axis tile meets it on the other side).
#   piece: (q coefficients, walkway q range with the track side first, cables q range)
DIAG_PIECES = {
    'UPPER': ((1, 1), (12.0, 14.5), (0.6, 0.9)),
    'LOWER': ((1, 1), (28.0, 30.5), (16.6, 16.9)),
    'RIGHT': ((-1, 1), (12.0, 14.5), (0.6, 0.9)),
    'LEFT':  ((1, -1), (4.0, 1.5), (15.1, 15.4)),
}

TILE_PLANES = [((1, 0, 0), 16, 'end'), ((-1, 0, 0), 0, 'end'), ((0, 1, 0), 16, 'end'), ((0, -1, 0), 0, 'end')]


def strip_planes(ab, lo, hi, face_lo, face_hi):
    a, b = ab
    return [((a, b, 0), hi, face_hi), ((-a, -b, 0), -lo, face_lo)]


def line_in_tile(ab, q):
    """End points of the line a*x + b*y = q inside the tile."""
    a, b = ab
    pts = []
    for x in (0.0, 16.0):
        y = (q - a * x) / b
        if 0 <= y <= 16:
            pts.append((x, y))
    for y in (0.0, 16.0):
        x = (q - b * y) / a
        if 0 <= x <= 16:
            pts.append((x, y))
    pts = sorted(set(pts))
    return (pts[0], pts[-1]) if len(pts) >= 2 else None


def tunnel_walkway_diag(name, piece):
    ab, (q_track, q_wall), _ = DIAG_PIECES[piece]
    lo, hi = min(q_track, q_wall), max(q_track, q_wall)
    s = Scene()
    shade = {'top': 0, 'end': -1, 'track': -2, 'wall': -1}

    def m(face, x, y, z, sx, sy):
        if face == 'top':
            q = ab[0] * x + ab[1] * y
            if abs(q - q_track) < 0.7:
                return col('yellow', 2)
            along = ab[1] * x - ab[0] * y
            return col('grey', 11 if (along % 2.8) > 0.4 else 9)
        if face == 'track':
            return col('grey', 11 + shade[face] if z > WALKWAY_H - 0.6 else 9 + shade[face])
        return col('grey', 10 + shade.get(face, -1))
    faces = ('track', 'wall') if q_track < q_wall else ('wall', 'track')
    s.add(Prism(strip_planes(ab, lo, hi, *faces) + TILE_PLANES +
                [((0, 0, 1), WALKWAY_H, 'top'), ((0, 0, -1), 0, 'bot')], m))
    q_rail = q_wall + (0.6 if q_wall < q_track else -0.6)
    seg = line_in_tile(ab, q_rail)
    if seg:
        rail_z = WALKWAY_H + 2.5
        (x0, y0), (x1, y1) = seg
        s.line((x0, y0, rail_z), (x1, y1, rail_z), col('yellow', 1))
        n = int(math.hypot(x1 - x0, y1 - y0) / 4.5)
        for i in range(n + 1):
            f = (i + 0.5) / (n + 1)
            px, py = x0 + (x1 - x0) * f, y0 + (y1 - y0) * f
            s.line((px, py, WALKWAY_H), (px, py, rail_z), col('grey', 5))
    return from_scene(name, s, comment='tunnel evacuation walkway, diagonal ' + piece)


def tunnel_cables_diag(name, piece):
    ab, _, (lo, hi) = DIAG_PIECES[piece]
    s = Scene()

    def cable(face, x, y, z, sx, sy):
        return 1 if face == 'top' else 2
    for zc in CABLE_Z:
        s.add(Prism(strip_planes(ab, lo, hi, 'side', 'side') + TILE_PLANES +
                    [((0, 0, 1), zc + 0.5, 'top'), ((0, 0, -1), -zc, 'bot')], cable))
    return from_scene(name, s, comment='tunnel cable tray, diagonal ' + piece)


def paved_ground(name):
    def m(face, x, y, z, sx, sy):
        if x < 0.75 or y < 0.75 or x > 15.25 or y > 15.25:
            return ('grey', 9)
        lvl = 12
        if (x - 0.75) % 2.5 < 0.3 or (y - 0.75) % 2.5 < 0.3:
            lvl = 10
        elif speckle(sx, sy, 41, 0.06):
            lvl = 11
        return ('grey', lvl)
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene(name, s, comment='pavilion paved ground')


def pavilion(name, axis_y):
    """Entrance pavilion: glass box on a plinth, company colour fascia, a sign
    with an 'M' and the stairs down behind the door (door on the +x end)."""
    s = Scene()
    sw = axis_y
    s.add(Box(3, 13, 3, 13, 0, 1, solid('grey', 9), swap=sw))

    def glass(face, x, y, z, sx, sy):
        if face == 'px':
            if abs(y - 8) <= 2.2 and z <= 7.5:  # doorway with stairs down
                if abs(y - 8) > 1.7 or z > 7:
                    return ('grey', 6)
                return 4 if int(z * 1.0) % 2 else 2
            mull = (y - 3.5) % 3.0 < 0.5
        elif face == 'py':
            mull = (x - 3.5) % 3.0 < 0.5
        else:
            return ('grey', 9)
        if mull or abs(z - 7.25) < 0.5:
            return ('grey', 7)
        lvl = 6 if (int(z) + int((x if face == 'py' else y) * 2)) % 7 == 0 else 5
        return ('glass', lvl)
    s.add(Box(3.5, 12.5, 3.5, 12.5, 1, 10, glass, swap=sw))

    def roof(face, x, y, z, sx, sy):
        if face == 'top':
            return ('grey', 10 if not speckle(sx, sy, 51, 0.1) else 9)
        return ('cc', 6)
    s.add(Box(2.5, 13.5, 2.5, 13.5, 10, 12, roof, swap=sw))

    # 'M' totem at the front corner of the forecourt
    s.line((14.6, 14.0, 0), (14.6, 14.0, 15), 4, swap=sw)
    s.line((14.6, 13.8, 0), (14.6, 13.8, 15), 5, swap=sw)
    s.stamp((14.6, 13.9, 15), SIGN_ICON, SIGN_CMAP, swap=sw)
    return from_scene(name, s, comment='pavilion, entrance towards ' + ('Y' if axis_y else 'X') + ' road')


SIGN_ICON = [
    'kkkkkkk',
    'kWcccWk',
    'kWWcWWk',
    'kWcWcWk',
    'kWcccWk',
    'kWcccWk',
    'kkkkkkk',
]
SIGN_CMAP = {'k': 199, 'c': 203, 'W': 15}


def underpass(name, axis_y):
    """Stairwell into the pavement on the road's rear side, with railings and an M post."""
    s = Scene()
    sw = axis_y

    def pit(face, x, y, z, sx, sy):
        if face == 'bot':   # treads, descending towards +x
            return ('grey', 7 if int((x - 3.5) * 1.25) % 2 == 0 else 5)
        if face == 'nx':
            return ('grey', 6)
        return ('grey', 4)
    s.add(Box(3.5, 11.5, 0.5, 3.0, -6, 0, pit, inside=True, clip_top=True, swap=sw))
    # flat coping around the opening
    s.add(Box(3.2, 11.8, 3.0, 3.3, 0, 0.3, solid('grey', 13), swap=sw))
    s.add(Box(11.5, 11.8, 0.2, 3.0, 0, 0.3, solid('grey', 13), swap=sw))
    # railings: top rail on both long sides and the far end, a few posts
    rh = 3.0
    for yy in (0.4, 3.1):
        for xx in (3.6, 7.5, 11.6):
            s.line((xx, yy, 0.3), (xx, yy, rh), 3, swap=sw)
        s.line((3.6, yy, rh), (11.6, yy, rh), 4, swap=sw)
    s.line((11.6, 0.4, rh), (11.6, 3.1, rh), 4, swap=sw)
    # M post
    s.line((13.6, 1.6, 0), (13.6, 1.6, 9), 4, swap=sw)
    s.line((13.6, 1.4, 0), (13.6, 1.4, 9), 5, swap=sw)
    s.stamp((13.6, 1.5, 9), SIGN_ICON, SIGN_CMAP, swap=sw)
    return from_scene(name, s, comment='underpass stairwell, road along ' + ('Y' if axis_y else 'X'))


EXIT_ICON = [
    '..kkkkkkkkkkkk..',
    '.kggggggggggggk.',
    'kggggggggggWWWgk',
    'kgggWgggggWWWWgk',
    'kggWWWggggWggggk',
    'kgWWWWWgggWggggk',
    'kgggWgggWWWggggk',
    'kgggWgggWggggggk',
    'kgggWgWWWggggggk',
    'kgggggWggggggggk',
    'kgggWWWggggggggk',
    'kgggWggggggggggk',
    'kgWWWggggggggggk',
    'kgggggggggggggGk',
    '.kGGGGGGGGGGGGk.',
    '..kkkkkkkkkkkk..',
]

ARROW_DOWN = [
    '...kkk...',
    '...kok...',
    '...kok...',
    '...kok...',
    'kkkkokkkk',
    'kooooooOk',
    '.kooooOk.',
    '..kooOk..',
    '...kOk...',
    '....k....',
]


def markers():
    exit_m = from_bitmap('SPR_METRO_EXIT_MARKER', EXIT_ICON,
                         {'k': 1, 'g': 207, 'G': 206, 'W': 15, '.': None},
                         comment='slice view: station exit marker')
    down = from_bitmap('SPR_METRO_RAMP_ARROW_DOWN', ARROW_DOWN,
                       {'k': 1, 'o': 164, 'O': 163, '.': None},
                       comment='slice view: ramp leads down')
    up = from_bitmap('SPR_METRO_RAMP_ARROW_UP', ARROW_DOWN[::-1],
                     {'k': 1, 'o': 208, 'O': 207, '.': None},
                     comment='slice view: ramp leads up')
    return [exit_m, down, up]


# ----- metro portals (surface + slice view) ------------------------------------
#
# All portal geometry is written in a per-direction frame: u is the distance
# from the tile edge on the tunnel side (the side the DiagDirection points to,
# into the hill / underground) towards the open side, w runs across.

DIRS = ('NE', 'SE', 'SW', 'NW')


class DirFrame:
    def __init__(self, d):
        self.d = d
        # (axis of u, u grows with the world axis?)
        self.ax, self.pos = {'NE': ('x', True), 'SW': ('x', False), 'NW': ('y', True), 'SE': ('y', False)}[d]

    def uw(self, x, y):
        a, b = (x, y) if self.ax == 'x' else (y, x)
        return (a if self.pos else 16 - a), b

    def xy_range(self, u0, u1, w0, w1):
        a0, a1 = (u0, u1) if self.pos else (16 - u1, 16 - u0)
        return (a0, a1, w0, w1) if self.ax == 'x' else (w0, w1, a0, a1)

    def rel_face(self, face):
        if face in ('top', 'bot', 'slope'):
            return face
        if face in ('nx', 'ny'):
            return 'inner'
        along = (face == 'px') == (self.ax == 'x')
        if not along:
            return 'side'
        return 'front' if self.pos else 'back'

    def box(self, u0, u1, w0, w1, z0, z1, mat, **kw):
        x0, x1, y0, y1 = self.xy_range(u0, u1, w0, w1)

        def m(face, x, y, z, sx, sy):
            u, w = self.uw(x, y)
            return mat(self.rel_face(face), u, w, z, sx, sy)
        return Box(x0, x1, y0, y1, z0, z1, m, **kw)

    def crossing(self, A, B, u):
        """(w, z) where the pixel ray (A, B) crosses the plane at distance u."""
        a = u if self.pos else 16 - u
        if self.ax == 'x':
            x = a
            y = x + A
            w = y
        else:
            y = a
            x = y - A
            w = x
        return w, x + y - B

    def point(self, u, w, z):
        x0, x1, y0, y1 = self.xy_range(u, u, w, w)
        return (x0, y0, z)


def arch(w, z, z0, half=4.0, spring=6.0):
    """Arched opening centred on w=8, floor z0, straight sides up to z0+spring."""
    if abs(w - 8) > half or z < z0:
        return False
    return z <= z0 + spring or (w - 8) ** 2 + (z - z0 - spring) ** 2 <= half * half


def concrete(face, u, w, z, sx, sy, base=11):
    lvl = base
    if face != 'top' and int(z) % 4 == 0:
        lvl -= 1          # formwork courses
    elif speckle(sx, sy, 61, 0.04):
        lvl -= 1
    return ('grey', lvl)


def void_mat(face_u):
    """Inside of a tunnel seen through a mouth at u=face_u; darker deeper in."""
    return lambda face, u, w, z, sx, sy: _void(face, face_u - u, w)


def _void(face, depth, w):
    if face == 'bot':
        if abs(w - 6.5) < 0.4 or abs(w - 9.5) < 0.4:
            return 5 if depth < 3 else 3
        return 2
    return 3 if depth < 2 else (2 if depth < 5 else 1)


def surface_portal(d, hill):
    """Surface entrance of the metro for DiagDirection d. hill: a concrete
    headwall set into the incline at its top edge (the tile rises by one level
    towards d); otherwise a cut-and-cover entrance block on flat ground."""
    f = DirFrame(d)
    s = Scene()
    visible = d in ('NE', 'NW')       # the mouth faces the viewer
    if hill:
        ground = lambda u: 8 - u / 2.0
        floor = 8.0                    # the rail reaches the mouth at the top of the incline
        wall_u, wall_top = 1.5, 8 + 12
    else:
        ground = lambda u: 0.0
        floor = 0.0
        wall_u, wall_top = 7.0, 12

    def wall(face, u, w, z, sx, sy):
        if z < ground(u) - 0.01:
            return None                # below the terrain
        if face == 'front' and abs(u - wall_u) < 1e-6:
            if arch(w, z, floor - 1):
                return None            # the opening
            if arch(w, z, floor - 1, 5.2, 6.0):
                return ('grey', 13)    # concrete ring
        if face == 'top':
            return ('grey', 12 if (w < 1.2 or w > 14.8 or u > wall_u - 0.6) else 10)
        return concrete(face, u, w, z, sx, sy)
    s.add(f.box(0, wall_u, 0.5, 15.5, 0, wall_top, wall, tag='front'))

    if hill:
        # stepped wing walls flanking the incline
        for i in range(3):
            u0, u1 = wall_u + i * 1.5, wall_u + (i + 1) * 1.5
            top = ground(u0) + 3 - i
            for w0, w1 in ((0.5, 1.5), (14.5, 15.5)):
                s.add(f.box(u0, u1, w0, w1, 0, top, wall, tag='front'))
    # the void behind the opening (ground sprite)
    if visible:
        # only seen through the opening
        through = lambda A, B: arch(*f.crossing(A, B, wall_u), floor - 1)
        s.add(f.box(-8, wall_u, 4, 12, floor - 1, floor + 10, void_mat(wall_u), tag='rear', inside=True, through=through))
    # 'M' plaque in company colour on top of the headwall
    s.stamp(f.point(wall_u / 2, 8, wall_top), SIGN_ICON, SIGN_CMAP, tag='front')

    kind = 'HILL' if hill else 'DIVE'
    i = DIRS.index(d)
    rear = from_scene(f'SPR_METRO_PORTAL_{kind}_BASE + {i * 2}', s, tags={'rear'},
                      comment=f'surface portal {kind.lower()} {d}: rear (ground)')
    front = from_scene(f'SPR_METRO_PORTAL_{kind}_BASE + {i * 2 + 1}', s, ref=(15, 15, 0), tags={'front'},
                       comment=f'surface portal {kind.lower()} {d}: front (roof bounds)')
    return [rear, front]


# Bounding boxes of the slice-view portal gates: a thin box on the open edge.
GATE_ORIGIN = {'NE': (15, 0, 0), 'SE': (0, 0, 0), 'SW': (0, 0, 0), 'NW': (0, 15, 0)}


def portal_gate(d):
    """Slice view, dive portal: the concrete frame at the top of the exit
    ramp (open edge, one level above the plane) through which the rail
    leaves for the surface."""
    f = DirFrame(d)
    s = Scene()

    def gate(face, u, w, z, sx, sy):
        if face in ('front', 'back', 'side') and arch(w, z, 8):
            return None
        if face in ('front', 'back') and arch(w, z, 8, 5.2, 6.0):
            return ('grey', 13)
        if face == 'top':
            return ('grey', 12)
        return concrete(face, u, w, z, sx, sy)
    s.add(f.box(15, 16, 0.5, 15.5, 0, 8 + 12, gate))
    s.stamp(f.point(15.5, 8, 8 + 12), SIGN_ICON, SIGN_CMAP)
    i = DIRS.index(d)
    return from_scene(f'SPR_METRO_PORTAL_GATE_BASE + {i}', s, ref=GATE_ORIGIN[d],
                      comment=f'slice view dive portal {d}: gate on the open edge')


def portal_ramp(d):
    """Slice view, dive portal: concrete ramp rising from the plane (tunnel
    side) one level up to the open edge, with its retaining side walls."""
    f = DirFrame(d)
    s = Scene()

    def top(face, x, y, z, sx, sy):
        u, w = f.uw(x, y)
        lvl = 10
        if w < 1.5 or w > 14.5:
            lvl = 12 if 0.5 < w < 15.5 else 9
        elif (u % 2.0) < 0.5:
            lvl = 9
        return ('grey', lvl)
    a, b, c = {'NE': (0.5, 0, 0), 'SW': (-0.5, 0, 8), 'NW': (0, 0.5, 0), 'SE': (0, -0.5, 8)}[d]
    s.add(Plane(a, b, c, top))

    def side(face, u, w, z, sx, sy):
        if z > u / 2.0 + 1e-6:
            return None
        return ('grey', 9 if int(z) % 3 else 8)
    s.add(f.box(0, 16, 0, 16, 0, 8, side))
    i = DIRS.index(d)
    return from_scene(f'SPR_METRO_PORTAL_RAMP_BASE + {i}', s,
                      comment=f'slice view dive portal {d}: ramp up to the surface')


# Open cut of a portal diving into flat ground: the floor descends two levels
# from the open edge (z = 0) to the tunnel edge (z = -16), so the tunnel mouth
# has the full height of the tunnel ribs.
CUT_DROP = 16.0


def open_cut(d, drop=CUT_DROP, base='SPR_METRO_PORTAL_DIVE_BASE'):
    """Surface portal on flat ground: an open cut. A concrete floor slopes down
    `drop` (two levels, or one for a shallow portal) from the open edge to a
    headwall with the arched tunnel mouth,
    shaped like the tunnel ribs (vault springing 12 above the floor);
    retaining walls with a low parapet line both sides. Rear (ground sprite):
    floor, far wall and, when it faces the viewer, the headwall with the dark
    tunnel behind it. Front (sortable, vanilla tunnel roof bounds): parapets
    of the near wall and, when the mouth faces away, of the headwall; their
    faces below the ground are earth, hidden by the tiles in front."""
    f = DirFrame(d)
    s = Scene()
    visible = d in ('NE', 'NW')       # the mouth faces the viewer
    floor = lambda u: -drop + u * drop / 16.0
    spring, half = 12.0, 4.5
    # the headwall stands above the ground as far as the arch needs (a shallow
    # cut has its mouth sticking out, like the tunnel ribs)
    head_u, head_top = 1.5, max(3.0, -drop + spring + half + 2.0)

    def opening(w, z):
        return arch(w, z, -drop, half, spring)

    # floor (ground sprite)
    def floor_mat(face, x, y, z, sx, sy):
        u, w = f.uw(x, y)
        lvl = 5 + int(u * 6 / 16)          # darker deeper down, towards the mouth
        if (u % 2.0) < 0.35:
            lvl -= 2                        # transverse joints
        if speckle(sx, sy, 91, 0.05):
            lvl -= 1
        return ('grey', lvl)
    k = drop / 16.0
    a, b, c = {'NE': (k, 0, -drop), 'SW': (-k, 0, 0), 'NW': (0, k, -drop), 'SE': (0, -k, 0)}[d]
    x0, x1, y0, y1 = f.xy_range(0, 16, 2, 14)
    s.add(Plane(a, b, c, floor_mat, tag='rear', x0=x0, x1=x1, y0=y0, y1=y1))
    # the track on the floor: sleepers and two rails (the flat surface rail of the
    # tile is hidden under the cut)
    for i in range(8):
        u0 = 0.3 + i * 2.0
        zs = floor(u0 + 0.4)
        s.add(f.box(u0, u0 + 0.8, 5.6, 10.4, zs, zs + 0.5, solid('grey', 4), tag='rear'))
    for ww in (6.8, 9.2):
        s.line(f.point(0, ww, floor(0) + 0.9), f.point(16, ww, floor(16) + 0.9), col('grey', 12), tag='rear')

    def wall(face, u, w, z, sx, sy):
        if face == 'top':
            return ('grey', 12)
        if z > -0.01:
            return ('grey', 12 if z > 0.6 else 11)   # parapet above the ground
        return concrete(face, u, w, z, sx, sy, base=8 + int((z + drop) / 5))   # darker lower down
    s.add(f.box(head_u, 16, 0.5, 2, -drop, 1.5, wall, tag='rear'))     # far side wall
    # The near side wall only shows its parapet: below the ground its outer face
    # is earth, hidden by the tile in front.
    s.add(f.box(head_u, 16, 14, 15.5, 0, 1.5, wall, tag='front'))         # near side parapet

    def headwall(face, u, w, z, sx, sy):
        if face in ('front', 'back', 'side') and opening(w, z):
            return None
        if face in ('front', 'back') and arch(w, z, -drop, half + 1.2, spring):
            return ('grey', 13)            # concrete ring
        if face == 'top':
            return ('grey', 12)
        return concrete(face, u, w, z, sx, sy)
    if visible:
        s.add(f.box(0, head_u, 0.5, 15.5, -drop, head_top, headwall, tag='rear'))
        through = lambda A, B: opening(*f.crossing(A, B, head_u))
        s.add(f.box(-8, head_u, 3.5, 12.5, -drop, 1, void_mat(head_u), tag='rear', inside=True, through=through))
        s.stamp(f.point(head_u / 2, 8, head_top), SIGN_ICON, SIGN_CMAP, tag='rear')
    else:
        # The mouth faces away: the track just runs under the ground, only the
        # parapet over the mouth shows.
        s.add(f.box(0, head_u, 0.5, 15.5, 0, 1.5, wall, tag='front'))
        s.stamp(f.point(head_u / 2, 8, 1.5), SIGN_ICON, SIGN_CMAP, tag='front')

    i = DIRS.index(d)
    rear = from_scene(f'{base} + {i * 2}', s, tags={'rear'},
                      comment=f'surface portal open cut {d}, drop {int(drop)}: rear (ground)')
    front = from_scene(f'{base} + {i * 2 + 1}', s, ref=(15, 15, 0), tags={'front'},
                       comment=f'surface portal open cut {d}, drop {int(drop)}: front (roof bounds)')
    return [rear, front]


def portal_sprites():
    sp = []
    for d in DIRS:
        sp.extend(surface_portal(d, True))
    for d in DIRS:
        sp.extend(open_cut(d))
    for d in DIRS:
        sp.append(portal_gate(d))
    for d in DIRS:
        sp.append(portal_ramp(d))
    return sp



# ----- toolbar icons -----------------------------------------------------------
#
# The underground toolbar buttons show the metro art itself: the in-game
# sprites are composited in tile screen coordinates and shrunk 3x by a
# majority vote per 3x3 block, then small markers are drawn at icon scale.

def screen_pixels(sp):
    """Pixels of a generated sprite in tile screen coordinates."""
    rx, ry, rz = sp.ref
    x0 = sp.xoffs + 2 * (ry - rx)
    y0 = sp.yoffs + (rx + ry - rz)
    out = {}
    for j, row in enumerate(sp.data):
        for i, c in enumerate(row):
            if c:
                out[(x0 + i, y0 + j)] = c
    return out


def shrink(px, k=2):
    blocks = {}
    for (x, y), c in px.items():
        blocks.setdefault((x // k, y // k), []).append(c)
    out = {}
    for key, cs in blocks.items():
        if len(cs) * 2 < k * k:
            continue   # mostly transparent
        best = max(set(cs), key=lambda c: (cs.count(c), -c))
        out[key] = best
    return out


ICON_SIGNAL = [
    '.kk.',
    'kRRk',
    'kRRk',
    'kGGk',
    '.kk.',
    '.k..',
    '.k..',
    '.k..',
    'kkk.',
]


def icon(name, layers, stamps=(), comment=''):
    """Toolbar icon from generated sprites (drawn in order) plus bitmap stamps
    [(bitmap, cmap, (x, y) at icon scale relative to the tile centre)]."""
    px = {}
    for sp in layers:
        px.update(screen_pixels(sp))
    small = shrink(px)
    for bitmap, cmap, (ox, oy) in stamps:
        for j, row in enumerate(bitmap):
            for i, ch in enumerate(row):
                c = cmap.get(ch)
                if c:
                    small[(ox + i, oy + j)] = c
    # centre on the middle of the drawing
    xs = [p[0] for p in small]
    ys = [p[1] for p in small]
    cx, cy = (min(xs) + max(xs) + 1) // 2, (min(ys) + max(ys) + 1) // 2
    return Sprite(name, {(x - cx, y - cy): c for (x, y), c in small.items()}, comment=comment)


def toolbar_icons():
    rail_x = mock_ground('rail_x')
    slab_s = slab('icon', 4, 1, '')
    rib = tunnel_rib('icon', False)
    grass = mock_ground('grass')
    road_x = mock_ground('road_x')
    arrow_dn = (ARROW_DOWN, {'k': 1, 'o': 164, 'O': 163}, (9, -16))
    arrow_up = (ARROW_DOWN[::-1], {'k': 1, 'o': 208, 'O': 207}, (9, -16))
    signal = (ICON_SIGNAL, {'k': 1, 'R': 164, 'G': 208}, (-2, -6))

    # X-ray: a checkered, see-through surface lifted over the tunnel.
    lifted = {}
    for (x, y), c in shrink(screen_pixels(grass)).items():
        if (x + y) % 2 == 0:
            lifted[(x, y - 6)] = c
    xs = [p[0] for p in shrink(screen_pixels(slab_s))]
    ys = [p[1] for p in shrink(screen_pixels(slab_s))]
    xpx = {}
    base = shrink({**screen_pixels(slab_s), **screen_pixels(rail_x), **screen_pixels(rib)})
    xpx.update(base)
    xpx.update(lifted)
    cx, cy = (min(xs) + max(xs) + 1) // 2, (min(ys) + max(ys) + 1) // 2 - 3
    xray = Sprite('SPR_METRO_ICON_XRAY', {(x - cx, y - cy): c for (x, y), c in xpx.items()}, comment='toolbar icon: X-ray view')

    rail = icon('SPR_METRO_ICON_RAIL', [slab_s, rail_x, rib], comment='toolbar icon: underground track')
    rear, front = surface_portal('NW', False)
    portal = icon('SPR_METRO_ICON_PORTAL', [grass, rear, front], comment='toolbar icon: portal')
    ramp = ramp_tile('icon', 'NE')
    ramp_rails = mock_incline('NE', 'ramp')
    ramp_dn = icon('SPR_METRO_ICON_RAMP_DOWN', [ramp, ramp_rails], [arrow_dn], comment='toolbar icon: ramp down')
    ramp_up = icon('SPR_METRO_ICON_RAMP_UP', [ramp, ramp_rails], [arrow_up], comment='toolbar icon: ramp up')
    sig = icon('SPR_METRO_ICON_SIGNAL', [slab_s, rail_x], [signal], comment='toolbar icon: signal')
    station = icon('SPR_METRO_ICON_STATION', [platform_floor('icon', False), rail_x, platform_rear('icon', False),
                                              platform_front('icon', False), pillar('icon', False)], comment='toolbar icon: station')
    pav = icon('SPR_METRO_ICON_PAVILION', [paved_ground('icon'), pavilion('icon', False)], comment='toolbar icon: pavilion exit')
    under = icon('SPR_METRO_ICON_UNDERPASS', [road_x, underpass('icon', False)], comment='toolbar icon: underpass exit')
    return [xray, rail, portal, ramp_dn, ramp_up, sig, station, pav, under]


# --------------------------------------------------------------------------
# Soil (metro v2 spec 2.1, 2.4, 2.5)
# --------------------------------------------------------------------------
# The slab of the slice view is drawn in the 'rock' ramp at level 4 (grains
# at 3 and 5, veins darker, sheen lighter). A recolour sprite per soil layer
# maps that ramp onto the layer's own ramp; the texture (roots, veins, grain,
# sparkles, glassy sheen) comes from a few slab variants. Details that must
# keep their colour (green roots, white sparkles) use indices outside the
# rock ramp, so the recolour leaves them alone.

# Soil layers in SoilLayer order: (name, 8-step ramp dark -> light (index 4
# is the main colour), texture).
SOIL_LAYERS = [
    ('earth',         [1, 1, 1, 2, 104, 105, 106, 107],          'roots'),
    ('clay light',    [60, 61, 62, 63, 64, 65, 66, 192],         'plain'),
    ('clay',          [70, 71, 72, 73, 75, 76, 77, 78],          'plain'),
    ('clay dark',     [104, 105, 106, 107, 108, 109, 110, 111],  'plain'),
    ('marble light',  [8, 9, 10, 11, 14, 14, 15, 15],            'veins'),
    ('marble grey',   [4, 5, 6, 7, 9, 10, 11, 12],               'veins'),
    ('granite pink',  [123, 124, 125, 126, 127, 48, 13, 14],     'grain'),
    ('granite grey',  [16, 17, 18, 19, 20, 21, 22, 13],          'grain'),
    ('granite dark',  [1, 2, 16, 16, 17, 18, 18, 19],            'grain'),
    ('granite black', [1, 1, 2, 2, 3, 4, 5, 6],                  'sparkle'),
    ('obsidian',      [1, 1, 1, 1, 1, 130, 133, 135],            'glass'),
]
assert len(SOIL_LAYERS) == 11, 'one entry per SoilLayer (src/multilayer/underground_soil.h)'
SOIL_TEXTURES = ('roots', 'veins', 'grain', 'sparkle', 'glass')  # 'plain' is SPR_METRO_SLAB

# Sand below rivers, canals and the sea (SoilLayer SandLight, Sand, SandDark,
# appended after Obsidian): pale beige, darker with depth, so that it hands
# over to the dark clay below. Its own grainy slab with faint ripples, and its
# own run of recolours after the metro block (SPR_METRO_SAND_*).
SAND_LAYERS = [
    ('sand light',    [35, 36, 37, 37, 38, 39, 39, 39],          'sand'),
    ('sand',          [33, 34, 35, 36, 37, 38, 38, 39],          'sand'),
    ('sand dark',     [32, 32, 33, 34, 35, 36, 36, 37],          'sand'),
]
ROOT_GREEN = [82, 83, 84, 85]
SPARKLE = [14, 15]


def random_walks(seed, count, steps, step_len, turn, start=None):
    """Meandering polylines across the tile (world units); -> list of point lists."""
    walks = []
    for k in range(count):
        if start is None:
            x = 1 + 14 * hash01(seed, k, 1)
            y = 1 + 14 * hash01(seed, k, 2)
        else:
            x, y = start(k)
        ang = 2 * math.pi * hash01(seed, k, 3)
        pts = [(x, y)]
        for i in range(steps):
            ang += (hash01(seed, k, i, 4) - 0.5) * 2 * turn
            x += math.cos(ang) * step_len
            y += math.sin(ang) * step_len
            pts.append((x, y))
        walks.append(pts)
    return walks


def near_walk(walks, x, y, width):
    """Distance test of a world point against the polylines."""
    for pts in walks:
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            dx, dy = bx - ax, by - ay
            ll = dx * dx + dy * dy
            f = 0.0 if ll == 0 else max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / ll))
            px, py = ax + dx * f - x, ay + dy * f - y
            if px * px + py * py < width * width:
                return True
    return False


def soil_slab(texture, variant):
    """A slab with the texture of a group of soil layers, in the rock ramp at level 4."""
    seed = 100 + 10 * (SOIL_TEXTURES + ('sand',)).index(texture) + variant
    if texture == 'sand':
        # Fine dense grain and a few faint wavy ripples, as on a river bed.
        phase = 16 * hash01(seed, 9)

        def m(face, x, y, z, sx, sy):
            w = (x + y + phase + 1.2 * math.sin((x - y) * 0.7 + phase)) % 5.0
            if w < 0.55:
                return ('rock', 5)
            if w < 1.0:
                return ('rock', 3)
            r = hash01(sx, sy, seed)
            if r < 0.14:
                return ('rock', 3)
            if r > 0.86:
                return ('rock', 6 if r > 0.97 else 5)
            return ('rock', 4)
        s = Scene()
        s.add(Box(0, 16, 0, 16, 0, 0, m))
        return from_scene(f'SPR_METRO_SAND_SLAB_BASE + {variant}', s, comment=f'soil slab: sand below water, variant {variant}')
    if texture == 'roots':
        # Thin roots growing down from the edges of the tile, a few crumbs.
        roots = random_walks(seed, 3, 9, 1.3, 0.6,
                             start=lambda k: (hash01(seed, k, 7) * 16, 0.5) if k % 2 else (0.5, hash01(seed, k, 8) * 16))
        twigs = random_walks(seed + 5, 3, 3, 1.0, 1.0)

        def m(face, x, y, z, sx, sy):
            if near_walk(roots, x, y, 0.32):
                return ROOT_GREEN[int(hash01(sx, sy, seed) * 3)]
            if near_walk(twigs, x, y, 0.25):
                return ROOT_GREEN[0]
            r = hash01(sx, sy, seed)
            return ('rock', 5 if r > 0.9 else 3 if r < 0.06 else 4)
    elif texture == 'veins':
        veins = random_walks(seed, 2, 14, 1.4, 0.35)
        faint = random_walks(seed + 5, 3, 6, 1.2, 0.6)

        def m(face, x, y, z, sx, sy):
            if near_walk(veins, x, y, 0.3):
                return ('rock', 1)
            if near_walk(faint, x, y, 0.22):
                return ('rock', 3)
            return ('rock', 5 if hash01(sx, sy, seed) > 0.96 else 4)
    elif texture in ('grain', 'sparkle'):
        def m(face, x, y, z, sx, sy):
            r = hash01(sx, sy, seed)
            if texture == 'sparkle' and r > 0.975:
                return SPARKLE[int(hash01(sx, sy, seed, 1) * 2)]
            if r < 0.05:
                return ('rock', 2)
            if r < 0.22:
                return ('rock', 3)
            if r > 0.9:
                return ('rock', 6 if r > 0.97 else 5)
            return ('rock', 4)
    else:  # glass: smooth black with a few glassy highlights
        c = 4 + 8 * hash01(seed, 1)

        def m(face, x, y, z, sx, sy):
            for off, lo, hi in ((0.0, 3, 8), (6.0, 8, 12)):
                d = abs(x - y * 0.5 - c - off)
                if lo < y < hi and d < 0.3:
                    return ('rock', 6)
                if lo < y < hi and d < 0.8:
                    return ('rock', 5)
            return ('rock', 4)
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene(f'SPR_METRO_SOIL_SLAB_BASE + {2 * SOIL_TEXTURES.index(texture) + variant}', s,
                      comment=f'soil slab: {texture}, variant {variant}')


def concrete_floor():
    """Light concrete floor under the objects of the viewed level: square plates."""
    def m(face, x, y, z, sx, sy):
        if (x % 8.0) < 0.45 or (y % 8.0) < 0.45:
            return ('grey', 9)
        return ('grey', 12 if speckle(sx, sy, 11, 0.06) else 11)
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene('SPR_METRO_CONCRETE_FLOOR', s, comment='slice view floor under built tiles')


def basement_floor():
    """Floor of the slice view where a house foundation reaches down: someone
    else's concrete, dark grey with diagonal stripes - unlike the light metro
    concrete with its square plates."""
    def m(face, x, y, z, sx, sy):
        if ((x + y) % 4.0) < 1.3:
            return ('grey', 6)
        return ('grey', 5 if speckle(sx, sy, 31, 0.08) else 4)
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene('SPR_METRO_BASEMENT_FLOOR', s, comment='slice view floor of a house foundation')


# Water column under rivers, canals and the sea, by the level in it: the
# first level below the bed is light, the third (deep sea) dark navy.
WATER_RAMPS = [
    [148, 149, 150, 151],
    [146, 147, 148, 149],
    [144, 145, 146, 147],
]


def water_floor(level):
    ramp = WATER_RAMPS[level]

    def m(face, x, y, z, sx, sy):
        # Plain water with a few faint ripples.
        r = hash01(sx, sy, 40 + level)
        return ramp[2] if r > 0.96 else ramp[0] if r < 0.04 else ramp[1]
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene(f'SPR_METRO_WATER_FLOOR_BASE + {level}', s, comment=f'slice view water column, {level + 1} below the bed')


# Underground parking where a house foundation reaches down (spec 3.4). The
# garage of a house has its walls on the two back edges of the house (x = 0:
# north-east, y = 0: north-west); the cars stand along them, the rest is the
# driving aisle. The back corner tile of the house (its north tile) holds the
# ramp: narrow, steep and straight along the north-east wall, from the street
# at the top corner down to two thirds of the tile. Car bodies are in the
# company colour ramp; the engine recolours them per tile.
PARKING_ROLES = ('corner', 'nw_wall', 'ne_wall', 'open')


def parking_tile(role):
    s = Scene()
    role_name = PARKING_ROLES[role]
    wall_nw = role_name in ('corner', 'nw_wall')
    wall_ne = role_name in ('corner', 'ne_wall')
    ramp_x, ramp_end = 2.4, 16.0 * 2 / 3
    # parking bays: (axis of the row, positions of the cars across it)
    if role_name == 'corner':
        nw_cars, ne_cars = (5.2, 10.8), ()
    elif role_name == 'nw_wall':
        nw_cars, ne_cars = (1.2, 6.1, 11.0), ()
    elif role_name == 'ne_wall':
        nw_cars, ne_cars = (), (1.2, 6.1, 11.0)
    else:
        nw_cars, ne_cars = (), ()
    car_w, car_l = 3.8, 6.2

    def floor(face, x, y, z, sx, sy):
        # white bay lines between the cars, out to the front of the row
        if nw_cars and y < 1 + car_l + 0.4 and x > (ramp_x + 2.2 if role_name == 'corner' else 0):
            lines = [c - 0.8 for c in nw_cars] + [nw_cars[-1] + car_w + 0.8]
            if any(abs(x - b) < 0.3 for b in lines) or abs(y - (1 + car_l + 0.4)) < 0.25:
                return ('grey', 13)
        if ne_cars and x < 1 + car_l + 0.4:
            lines = [c - 0.8 for c in ne_cars] + [ne_cars[-1] + car_w + 0.8]
            if any(abs(y - b) < 0.3 for b in lines) or abs(x - (1 + car_l + 0.4)) < 0.25:
                return ('grey', 13)
        return ('grey', 7 if speckle(sx, sy, 60, 0.06) else 6)
    s.add(Box(0, 16, 0, 16, 0, 0, floor))

    def wall(face, x, y, z, sx, sy):
        if face == 'top':
            return ('grey', 10)
        return ('grey', 9 + (1 if int(z) % 4 == 0 else 0))
    if wall_ne:
        s.add(Box(0, 0.8, 0, 16, 0, 10, wall))
    if wall_nw:
        s.add(Box(0, 16, 0, 0.8, 0, 10, wall))

    def body(face, x, y, z, sx, sy):
        return ('cc', 5 if face == 'top' else 4)

    def cabin(face, x, y, z, sx, sy):
        return ('cc', 6) if face == 'top' else 1  # dark windows all round

    for c in nw_cars:  # long along y, nose to the north-west wall
        s.add(Box(c, c + car_w, 1.0, 1.0 + car_l, 0, 2.2, body))
        s.add(Box(c + 0.5, c + car_w - 0.5, 2.6, 5.6, 2.2, 3.8, cabin))
    for c in ne_cars:  # long along x, nose to the north-east wall
        s.add(Box(1.0, 1.0 + car_l, c, c + car_w, 0, 2.2, body))
        s.add(Box(2.6, 5.6, c + 0.5, c + car_w - 0.5, 2.2, 3.8, cabin))

    if role_name == 'corner':
        def ramp_mat(face, x, y, z, sx, sy):
            return col('grey', 10)  # one fixed shade: the cells of the slope must not show as steps

        def curb_mat(face, x, y, z, sx, sy):
            return ('yellow', 2) if face == 'top' else ('grey', 8)
        step = 0.5
        for i in range(int(16 / step)):
            for j in range(int(16 / step)):
                x0, y0 = i * step, j * step
                d, a = abs(x0 + step / 2 - ramp_x), y0 + step / 2
                if d > 1.9 or a > ramp_end:
                    continue
                h = max(0.3, 8.0 * (1.0 - a / ramp_end))
                curb = d > 1.4
                s.add(Box(x0, x0 + step, y0, y0 + step, 0, h + (0.5 if curb else 0), curb_mat if curb else ramp_mat))
    return from_scene(f'SPR_METRO_PARKING_BASE + {role}', s, comment=f'underground parking: {role_name}')


def sea_forbidden():
    """Ground below the sea where nothing may be built: dark rock hatched in red."""
    def m(face, x, y, z, sx, sy):
        if ((x - y) % 4.0) < 0.9:
            return 180
        return 2 if hash01(sx, sy, 50) < 0.15 else 3
    s = Scene()
    s.add(Box(0, 16, 0, 16, 0, 0, m))
    return from_scene('SPR_METRO_SEA_FORBIDDEN', s, comment='slice view: no construction below the sea')


class Recolour:
    """A recolour sprite (a 256 byte palette remap) in the sprite list."""

    def __init__(self, name, table, comment=''):
        assert len(table) == 256
        self.name = name
        self.table = table
        self.comment = comment


def luminance(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def nearest_index(palette, rgb, candidates=range(1, 215)):
    best, best_d = 1, None
    for i in candidates:
        p = palette[3 * i:3 * i + 3]
        d = 2 * (p[0] - rgb[0]) ** 2 + 4 * (p[1] - rgb[1]) ** 2 + 3 * (p[2] - rgb[2]) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def soil_recolours(palette, layers=SOIL_LAYERS, slab_base='SPR_METRO_SOIL_PAL_BASE', xray_base='SPR_METRO_SOIL_XRAY_PAL_BASE'):
    """Per layer: the slab recolour (rock ramp -> soil ramp) and the muted X-ray
    recolour of the bare land backdrop (every colour by its brightness)."""
    slab_pals, xray_pals = [], []
    rock = RAMPS['rock']
    for idx, (name, ramp, texture) in enumerate(layers):
        table = list(range(256))
        for k, c in enumerate(rock):
            table[c] = ramp[k]
        slab_pals.append(Recolour(f'{slab_base} + {idx}', table, f'soil recolour: {name}'))

        # The hue of the layer at the brightness of each colour, half way to grey,
        # so the backdrop keeps the contrast of the newspaper grey it replaces.
        # Black layers darken it a little. No green or blue may creep in.
        main = palette[3 * ramp[4]:3 * ramp[4] + 3]
        lm = luminance(main)
        hue = [ch / lm for ch in main] if lm > 60 else [1.0, 1.0, 1.0]
        if name == 'earth':
            hue = [1.25, 0.95, 0.55]  # dark brown, the earth ramp is too dark to give a hue
        dark = 1.0 if lm > 60 else 0.85 if lm > 20 else 0.7
        candidates = [i for i in range(1, 215) if not (80 <= i <= 103 or 128 <= i <= 161 or 198 <= i <= 214)]
        table = list(range(256))
        for i in range(1, 227):
            lum = luminance(palette[3 * i:3 * i + 3]) * dark
            rgb = [lum * (0.5 + 0.5 * h) for h in hue]
            table[i] = nearest_index(palette, rgb, candidates)
        xray_pals.append(Recolour(f'{xray_base} + {idx}', table, f'X-ray soil recolour: {name}'))
    return slab_pals + xray_pals


def build_sprites():
    sp = []
    # 1. station platforms
    sp.append(platform_floor('SPR_METRO_PLATFORM_FLOOR_X', False))
    sp.append(platform_floor('SPR_METRO_PLATFORM_FLOOR_Y', True))
    sp.append(platform_rear('SPR_METRO_PLATFORM_X_REAR', False))
    sp.append(platform_front('SPR_METRO_PLATFORM_X_FRONT', False))
    sp.append(platform_rear('SPR_METRO_PLATFORM_Y_REAR', True))
    sp.append(platform_front('SPR_METRO_PLATFORM_Y_FRONT', True))
    sp.append(board('SPR_METRO_BOARD_X', False))
    sp.append(board('SPR_METRO_BOARD_Y', True))
    sp.append(pillar('SPR_METRO_PILLAR_X', False))
    sp.append(pillar('SPR_METRO_PILLAR_Y', True))
    # 2. tunnel mouths, DiagDirection order, (rear, front) pairs
    for d in ('NE', 'SE', 'SW', 'NW'):
        sp.extend(tunnel(d))
    # 3. pavilion
    sp.append(paved_ground('SPR_METRO_PAVILION_GROUND'))
    sp.append(pavilion('SPR_METRO_PAVILION_X', False))
    sp.append(pavilion('SPR_METRO_PAVILION_Y', True))
    # 4. underpass
    sp.append(underpass('SPR_METRO_UNDERPASS_X', False))
    sp.append(underpass('SPR_METRO_UNDERPASS_Y', True))
    # 5. markers
    sp.extend(markers())
    # 6. slab
    sp.append(slab('SPR_METRO_SLAB', 4, 1, 'slice view slab, even levels'))
    sp.append(slab('SPR_METRO_SLAB_DARK', 3, 2, 'slice view slab, odd levels'))
    # 7. ramps, by descent DiagDirection
    for i, d in enumerate(('NE', 'SE', 'SW', 'NW')):
        sp.append(ramp_tile(f'SPR_METRO_RAMP_BASE + {i}', d))
    # X-ray ceiling dim
    sp.append(ceiling())
    # 8. metro portals: surface (hillside, dive) and slice view (gate, ramp)
    sp.extend(portal_sprites())
    # 9. tunnel ribs over straight underground track
    sp.append(tunnel_rib('SPR_METRO_TUNNEL_RIB_X', False))
    sp.append(tunnel_rib('SPR_METRO_TUNNEL_RIB_Y', True))
    # 10. metro-style stations: short board, end portals
    sp.append(board_short('SPR_METRO_BOARD_SHORT_X', False))
    sp.append(board_short('SPR_METRO_BOARD_SHORT_Y', True))
    sp.append(station_portal('SPR_METRO_STATION_PORTAL_X', False))
    sp.append(station_portal('SPR_METRO_STATION_PORTAL_Y', True))
    # 11. tunnel ribs over diagonal track: A for UPPER/LOWER, B for LEFT/RIGHT
    sp.append(tunnel_rib_diag('SPR_METRO_TUNNEL_RIB_DIAG_A', (1, 1), (4, 4), (0, 0, 0)))
    sp.append(tunnel_rib_diag('SPR_METRO_TUNNEL_RIB_DIAG_B', (1, -1), (4, 12), (0, 8, 0)))
    # 12. underground toolbar icons
    sp.extend(toolbar_icons())
    # 13. open cut of a portal one level below flat ground
    for d in DIRS:
        sp.extend(open_cut(d, 8.0, 'SPR_METRO_PORTAL_CUT_SHALLOW_BASE'))
    # 14. soil: slab textures (two variants each), concrete floor, recolours per layer
    for t in SOIL_TEXTURES:
        for v in range(2):
            sp.append(soil_slab(t, v))
    sp.append(concrete_floor())
    sp.extend(soil_recolours(load_palette()))
    # 15. what the surface takes of the ground: house foundations, water, the forbidden sea bed
    sp.append(basement_floor())
    for level in range(3):
        sp.append(water_floor(level))
    sp.append(sea_forbidden())
    # 16. sand below water: slab (two variants), slab and X-ray recolours per sand layer
    for v in range(2):
        sp.append(soil_slab('sand', v))
    sp.extend(soil_recolours(load_palette(), SAND_LAYERS, 'SPR_METRO_SAND_PAL_BASE', 'SPR_METRO_SAND_XRAY_PAL_BASE'))
    # 17. straight tunnels: evacuation walkway and cable tray
    sp.append(tunnel_walkway('SPR_METRO_TUNNEL_ART_BASE + 0', False))
    sp.append(tunnel_walkway('SPR_METRO_TUNNEL_ART_BASE + 1', True))
    sp.append(tunnel_cables('SPR_METRO_TUNNEL_ART_BASE + 2', False))
    sp.append(tunnel_cables('SPR_METRO_TUNNEL_ART_BASE + 3', True))
    # 18. the same on a tile with signals (the rib stands in the middle) and on diagonal pieces
    sp.append(tunnel_cables('SPR_METRO_TUNNEL_ART_BASE + 4', False, 7.5))
    sp.append(tunnel_cables('SPR_METRO_TUNNEL_ART_BASE + 5', True, 7.5))
    for i, piece in enumerate(('UPPER', 'LOWER', 'LEFT', 'RIGHT')):
        sp.append(tunnel_walkway_diag(f'SPR_METRO_TUNNEL_ART_BASE + {6 + i}', piece))
    # only these two pieces have their back line inside the tile; on UPPER and
    # LEFT it is the corner of the tile
    sp.append(tunnel_cables_diag('SPR_METRO_TUNNEL_ART_BASE + 10', 'LOWER'))
    sp.append(tunnel_cables_diag('SPR_METRO_TUNNEL_ART_BASE + 11', 'RIGHT'))
    # 19. underground parking under houses, by the place of the tile in the house
    for role in range(4):
        sp.append(parking_tile(role))
    return sp

# --------------------------------------------------------------------------
# Sheet + NFO
# --------------------------------------------------------------------------

SHEET_W = 512
PAD = 4

NFO_HEADER = """// This file is part of OpenTTD.
// OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
// OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
//
// GENERATED by generate_metro_sprites.py - do not edit by hand.
//
   -1 * 0	 0C "Metro sprites"
   -1 * 3	 05 1C {count:02X}
"""


def pack(sprites):
    x = y = PAD
    row_h = 0
    for s in sprites:
        if isinstance(s, Recolour):
            continue
        if x + s.w + PAD > SHEET_W:
            x = PAD
            y += row_h + PAD
            row_h = 0
        s.sx, s.sy = x, y
        x += s.w + PAD
        row_h = max(row_h, s.h)
    return y + row_h + PAD


def write_sheet(sprites, palette):
    h = pack(sprites)
    img = Image.new('P', (SHEET_W, h), 0)
    img.putpalette(palette)
    for s in sprites:
        if isinstance(s, Recolour):
            continue
        for j, row in enumerate(s.data):
            for i, c in enumerate(row):
                if c:
                    img.putpixel((s.sx + i, s.sy + j), c)
    img.save(os.path.join(HERE, 'metro_station.png'))


def write_nfo(sprites):
    lines = [NFO_HEADER.format(count=len(sprites))]
    for idx, s in enumerate(sprites):
        lines.append(f'// {idx:2d} {s.name}: {s.comment}\n')
        if isinstance(s, Recolour):
            body = ' '.join(f'{c:02X}' for c in s.table)
            lines.append(f'   -1 * 257\t 00 {body}\n')
            continue
        lines.append(f'   -1 sprites/metro_station.png 8bpp {s.sx:4d} {s.sy:4d} {s.w:3d} {s.h:3d} {s.xoffs:4d} {s.yoffs:4d} normal\n')
    with open(os.path.join(HERE, 'metro_station.nfo'), 'w', newline='\n') as f:
        f.write(''.join(lines))


def check_header(sprites):
    path = os.path.join(ROOT, 'src', 'table', 'sprites.h')
    try:
        text = open(path).read()
    except OSError:
        print('note: src/table/sprites.h not found, constant check skipped')
        return
    m = re.search(r'METRO_STATION_SPRITE_COUNT\s*=\s*(\d+)', text)
    assert m and int(m.group(1)) == len(sprites), \
        f'METRO_STATION_SPRITE_COUNT in sprites.h must be {len(sprites)}'
    for idx, s in enumerate(sprites):
        base, _, off = s.name.partition(' + ')
        off = int(off) if off else 0
        m = re.search(base + r'\s*=\s*SPR_METRO_STATION_BASE\s*\+\s*(\d+);', text)
        assert m and int(m.group(1)) + off == idx, f'{base} must be SPR_METRO_STATION_BASE + {idx - off}'

# --------------------------------------------------------------------------
# Previews
# --------------------------------------------------------------------------

def mock_ground(kind):
    """Stand-ins for vanilla sprites in the previews only (not in the GRF)."""
    s = Scene()
    if kind == 'grass':
        s.add(Box(0, 16, 0, 16, 0, 0, lambda f, x, y, z, sx, sy: 85 + (1 if hash01(sx, sy, 3) > 0.7 else 0) - (1 if hash01(sx, sy, 4) > 0.85 else 0)))
    elif kind in ('road_x', 'road_y'):
        def m(f, x, y, z, sx, sy):
            w = y if kind == 'road_x' else x
            if w < 3 or w > 13:
                return ('grey', 11)
            if abs(w - 8) < 0.3 and int(((x if kind == 'road_x' else y)) / 2) % 2 == 0:
                return 14
            return ('grey', 6)
        s.add(Box(0, 16, 0, 16, 0, 0, m))
    elif kind in ('rail_x', 'rail_y'):
        sw = kind == 'rail_y'
        for i in range(8):
            s.add(Box(i * 2 + 0.4, i * 2 + 1.2, 5.5, 10.5, 0, 0, solid('grey', 1) if False else (lambda *a: 34), swap=sw))
        for yy in (6.6, 9.4):
            s.line((0, yy, 0.6), (16, yy, 0.6), 10, swap=sw)
    px = s.render()
    return Sprite('mock', {k: c for k, (c, t) in px.items()})


def mock_train(axis_y, x0, x1):
    s = Scene()
    s.add(Box(x0, x1, 5.5, 10.5, 1, 9, lambda f, x, y, z, sx, sy: ('red', 2 if 3 < z < 6 and f != 'top' else 3) if f != 'top' else ('grey', 11), swap=axis_y))
    px = s.render()
    return Sprite('mock', {k: c for k, (c, t) in px.items()})



def mock_incline(d, kind, rails=True):
    """Preview stand-in: a tile inclined by one level. kind 'hill' rises towards d
    (grass), 'ramp' rises away from d (no ground, only the rails)."""
    f = DirFrame(d)
    s = Scene()
    zf = (lambda u: 8 - u / 2.0) if kind == 'hill' else (lambda u: u / 2.0)
    if kind == 'hill':
        a, b, c = {'NE': (-0.5, 0, 8), 'SW': (0.5, 0, 0), 'NW': (0, -0.5, 8), 'SE': (0, 0.5, 0)}[d]
        s.add(Plane(a, b, c, lambda fc, x, y, z, sx, sy: 85 + (1 if hash01(sx, sy, 3) > 0.7 else 0) - (1 if hash01(sx, sy, 4) > 0.85 else 0)))
    if rails:
        for ww in (6.6, 9.4):
            s.line(f.point(0, ww, zf(0) + 0.6), f.point(16, ww, zf(16) + 0.6), 10)
    px = s.render()
    return Sprite('mock', {k: c for k, (c, t) in px.items()})


def mock_train_on(d, u0, u1, zf):
    """Preview stand-in: a short wagon between u0 and u1 following z = zf(u)."""
    f = DirFrame(d)
    s = Scene()
    um = (u0 + u1) / 2.0
    s.add(f.box(u0, u1, 5.5, 10.5, zf(um) + 1, zf(um) + 9,
                lambda fc, u, w, z, sx, sy: ('grey', 11) if fc == 'top' else ('red', 2 if (z - zf(um)) < 6 else 3)))
    px = s.render()
    return Sprite('mock', {k: c for k, (c, t) in px.items()})

class Canvas:
    def __init__(self, w, h, ox, oy, palette):
        self.img = Image.new('RGB', (w, h), (40, 40, 48))
        self.ox, self.oy = ox, oy
        self.pal = palette

    def draw(self, spr, tx, ty, tz=0, ref=None, dx=0, dy=0):
        rx, ry, rz = ref if ref is not None else spr.ref
        bx = self.ox + 32 * (ty - tx) + 2 * (ry - rx) + spr.xoffs + dx
        by = self.oy + 16 * (tx + ty) - tz + (rx + ry - rz) + spr.yoffs + dy
        for j, row in enumerate(spr.data):
            for i, c in enumerate(row):
                if c:
                    if 198 <= c <= 205:   # preview company colour: red
                        c = [178, 179, 180, 181, 182, 183, 184, 165][c - 198]
                    x, y = bx + i, by + j
                    if 0 <= x < self.img.width and 0 <= y < self.img.height:
                        self.img.putpixel((x, y), tuple(self.pal[3 * c:3 * c + 3]))

    def save(self, path):
        self.img.resize((self.img.width * 4, self.img.height * 4), Image.NEAREST).save(path)


def previews(sprites, palette):
    out = os.path.join(ROOT, 'docs', 'metro_art_preview')
    os.makedirs(out, exist_ok=True)
    S = {s.name: s for s in sprites}
    slab_a, slab_b = S['SPR_METRO_SLAB'], S['SPR_METRO_SLAB_DARK']

    def slab_grid(c, n, m, dark=False):
        for tx in range(n):
            for ty in range(m):
                c.draw(slab_b if dark else slab_a, tx, ty)

    # Station platforms, both axes, with a mock train on the middle tile.
    for axis_y in (False, True):
        a = 'Y' if axis_y else 'X'
        c = Canvas(240, 170, 120, 50, palette)
        slab_grid(c, 4, 4)
        tiles = [(0, 1), (1, 1), (2, 1)] if not axis_y else [(1, 0), (1, 1), (1, 2)]
        rail = mock_ground('rail_y' if axis_y else 'rail_x')
        for tx, ty in tiles:
            c.draw(S[f'SPR_METRO_PLATFORM_FLOOR_{a}'], tx, ty)
            c.draw(rail, tx, ty)
        for i, (tx, ty) in enumerate(tiles):
            c.draw(S[f'SPR_METRO_PLATFORM_{a}_REAR'], tx, ty)
            c.draw(S[f'SPR_METRO_PILLAR_{a}' if i % 2 == 0 else f'SPR_METRO_BOARD_{a}'], tx, ty)
            if i == 1:
                c.draw(mock_train(axis_y, 1, 15), tx, ty)
            c.draw(S[f'SPR_METRO_PLATFORM_{a}_FRONT'], tx, ty)
        c.save(os.path.join(out, f'platform_{a.lower()}.png'))

    # Tunnel mouths, four directions.
    c = Canvas(500, 150, 250, 20, palette)
    for i, d in enumerate(('NE', 'SE', 'SW', 'NW')):
        tx, ty = 4 - 2 * i, 2 * i - 2
        for ax in (-1, 0, 1):
            for ay in (-1, 0, 1):
                c.draw(slab_a, tx + ax, ty + ay)
        axis_y = d in ('SE', 'NW')
        c.draw(mock_ground('rail_y' if axis_y else 'rail_x'), tx, ty)
        c.draw(S[f'SPR_METRO_TUNNEL_BASE + {i * 2}'], tx, ty)
        c.draw(S[f'SPR_METRO_TUNNEL_BASE + {i * 2 + 1}'], tx, ty)
    c.save(os.path.join(out, 'tunnel_mouths.png'))

    # Pavilions on grass next to roads.
    grass = mock_ground('grass')
    c = Canvas(260, 150, 130, 30, palette)
    for tx in range(4):
        for ty in range(4):
            c.draw(grass, tx, ty)
    for ty in range(4):
        c.draw(mock_ground('road_y'), 2, ty)
    for tx in range(2):
        c.draw(mock_ground('road_x'), tx, 3)
    c.draw(S['SPR_METRO_PAVILION_GROUND'], 1, 1)
    c.draw(S['SPR_METRO_PAVILION_X'], 1, 1)
    c.draw(S['SPR_METRO_PAVILION_GROUND'], 1, 2)
    c.draw(S['SPR_METRO_PAVILION_Y'], 1, 2)
    c.save(os.path.join(out, 'pavilion.png'))

    # Underpass stairwells on both road axes.
    c = Canvas(260, 140, 130, 30, palette)
    for tx in range(4):
        for ty in range(4):
            c.draw(grass, tx, ty)
    for tx in range(4):
        c.draw(mock_ground('road_x'), tx, 0)
    for ty in range(1, 4):
        c.draw(mock_ground('road_y'), 3, ty)
    c.draw(S['SPR_METRO_UNDERPASS_X'], 1, 0)
    c.draw(S['SPR_METRO_UNDERPASS_Y'], 3, 2)
    c.save(os.path.join(out, 'underpass.png'))

    # Slab depth banding, ramps and markers.
    c = Canvas(300, 170, 150, 30, palette)
    for tx in range(5):
        for ty in range(5):
            c.draw(slab_a if (tx // 2) % 2 == 0 else slab_b, tx, ty)
    c.draw(S['SPR_METRO_RAMP_BASE + 0'], 1, 1)
    c.draw(S['SPR_METRO_RAMP_BASE + 1'], 1, 3)
    c.draw(S['SPR_METRO_RAMP_BASE + 2'], 3, 1)
    c.draw(S['SPR_METRO_RAMP_BASE + 3'], 3, 3)
    c.draw(S['SPR_METRO_RAMP_ARROW_DOWN'], 1, 1, ref=(8, 4, 7))
    c.draw(S['SPR_METRO_RAMP_ARROW_UP'], 3, 3, ref=(8, 4, 7))
    c.draw(S['SPR_METRO_EXIT_MARKER'], 2, 2, ref=(8, 8, 0))
    c.save(os.path.join(out, 'slab_ramps_markers.png'))

    # Metro portals: surface (hillside, dive) and the slice view of both kinds.
    grass = mock_ground('grass')
    c = Canvas(600, 330, 120, 40, palette)
    step = {'NE': (-1, 0), 'SW': (1, 0), 'NW': (0, -1), 'SE': (0, 1)}
    for row, hill in enumerate((True, False)):
        for k, d in enumerate(DIRS):
            px, py = -2 * k + 3 * row, 2 * k + 3 * row - 1
            dx, dy = step[d]
            axis_y = d in ('SE', 'NW')
            tiles = sorted([(px - dx, py - dy, 'open'), (px, py, 'portal'), (px + dx, py + dy, 'hill')], key=lambda q: q[0] + q[1])
            for tx, ty, what in tiles:
                if what == 'portal':
                    c.draw(mock_incline(d, 'hill') if hill else grass, tx, ty)
                    if not hill:
                        c.draw(mock_ground('rail_y' if axis_y else 'rail_x'), tx, ty)
                elif what == 'hill' and hill:
                    c.draw(grass, tx, ty, tz=8)
                else:
                    c.draw(grass, tx, ty)
                    if what == 'open':
                        c.draw(mock_ground('rail_y' if axis_y else 'rail_x'), tx, ty)
            i = DIRS.index(d)
            base = 'HILL' if hill else 'DIVE'
            c.draw(S[f'SPR_METRO_PORTAL_{base}_BASE + {i * 2}'], px, py)
            c.draw(S[f'SPR_METRO_PORTAL_{base}_BASE + {i * 2 + 1}'], px, py)
    # Slice view: a dive portal (ramp + gate) and a hillside mouth on the slab.
    for k, d in enumerate(DIRS):
        px, py = -2 * k + 6, 2 * k + 5
        dx, dy = step[d]
        for ax in (-1, 0, 1):
            for ay in (-1, 0, 1):
                c.draw(slab_a, px + ax, py + ay)
        axis_y = d in ('SE', 'NW')
        i = DIRS.index(d)
        c.draw(mock_ground('rail_y' if axis_y else 'rail_x'), px + dx, py + dy)
        c.draw(S[f'SPR_METRO_PORTAL_RAMP_BASE + {i}'], px, py)
        c.draw(mock_incline(d, 'ramp'), px, py)
        if d in ('SW', 'SE'):
            c.draw(S[f'SPR_METRO_PORTAL_GATE_BASE + {i}'], px, py)
            c.draw(mock_train_on(d, 4, 12, lambda u: u / 2.0), px, py)
        else:
            c.draw(mock_train_on(d, 4, 12, lambda u: u / 2.0), px, py)
            c.draw(S[f'SPR_METRO_PORTAL_GATE_BASE + {i}'], px, py)
    c.save(os.path.join(out, 'portals.png'))

    # The raw sheet, magnified.
    sheet = Image.open(os.path.join(HERE, 'metro_station.png')).convert('RGB')
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(os.path.join(out, 'sheet_2x.png'))
    print('previews written to', out)


def main():
    palette = load_palette()
    sprites = build_sprites()
    write_sheet(sprites, palette)
    write_nfo(sprites)
    for idx, s in enumerate(sprites):
        if isinstance(s, Recolour):
            print(f'{idx:2d} {s.name:34s} recolour  {s.comment}')
            continue
        print(f'{idx:2d} {s.name:34s} {s.w:3d}x{s.h:<3d} offs {s.xoffs:4d},{s.yoffs:4d}  {s.comment}')
    check_header(sprites)
    if '--preview' in sys.argv:
        previews(sprites, palette)


if __name__ == '__main__':
    main()
