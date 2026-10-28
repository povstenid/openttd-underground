"""Metro track (railtype MTRO) sprites for the OpenTTD Underground Metro Set.

Every tile sprite is rendered by casting one ray per screen pixel into a
scene of convex bodies (half-space sets) through the OpenTTD dimetric
projection. The pixel <-> world mapping is the one the underground sprite
generator of this repository uses (media/baseset/openttd), so the ground
pieces line up with the vanilla 64x31 tile diamond.

A ray through screen pixel (sx, sy) of a tile sprite is the world line
    (t, t + A, 2t + A - B),  A = (sx - 0.5) / 2,  B = sy + 1,
and a larger t is nearer to the viewer.

The track is ordinary ballast, sleepers and two rails plus a yellow
conductor rail on one side; the sprite groups follow the NewGRF railtype
spec (underlay 16, track_overlay 10, tunnels 4, bridge_surfaces 6,
level_crossings 10, depots 6, gui 16).
"""

import math

import numpy as np

RAMPS = {
    'grey':     list(range(1, 16)),
    'ballast':  [70, 71, 104, 105, 112, 106, 107],      # brown ballast
    'sleeper':  [1, 1, 2, 3, 4],                        # dark sleepers
    'rail':     [3, 4, 5, 6, 7, 8, 9],
    'yellow':   [66, 67, 67, 68, 68],                   # conductor rail
    'dark':     [1, 1, 2, 2, 3, 4],
    'cc':       list(range(198, 206)),
    'brick':    [32, 33, 34, 35, 36, 37, 38, 39],
    'red':      [180, 181, 182, 183, 184],
    'black':    [1, 1, 1, 2],
}

LIGHT = np.array([0.45, 0.15, 1.0])
LIGHT = LIGHT / np.linalg.norm(LIGHT)

SQ2 = math.sqrt(2.0)


class Body:
    """Convex body: planes (n, d) with n . p + d >= 0 inside."""

    def __init__(self, planes, mat):
        self.planes = [(np.array(n, float), float(d)) for n, d in planes]
        self.mat = mat   # (ramp, level) or callable(x, y, z, normal) -> (ramp, level)


def obox(c, a, hl, hw, z0, z1, mat, slope=None, clip=True, extra=()):
    """Oriented box: centre c (x, y), unit length axis a, half length hl,
    half width hw, height z0..z1 above the slope surface h(x, y) = h0 + hx x + hy y."""
    a = np.array(a, float)
    a /= np.linalg.norm(a)
    b = np.array([-a[1], a[0]])
    cx, cy = c
    h0, hx, hy = slope if slope else (0.0, 0.0, 0.0)
    planes = []
    for ax, half in ((a, hl), (b, hw)):
        d0 = -(ax[0] * cx + ax[1] * cy)
        planes.append(((ax[0], ax[1], 0), d0 + half))     # ax.p - ax.c + half >= 0
        planes.append(((-ax[0], -ax[1], 0), -d0 + half))
    # z - h(x, y) >= z0  and  z - h(x, y) <= z1
    planes.append(((-hx, -hy, 1), -h0 - z0))
    planes.append(((hx, hy, -1), h0 + z1))
    if clip:
        planes += [((1, 0, 0), 0), ((-1, 0, 0), 16), ((0, 1, 0), 0), ((0, -1, 0), 16)]
    planes += list(extra)
    return Body(planes, mat)


def abox(x0, x1, y0, y1, z0, z1, mat):
    return Body([((1, 0, 0), -x0), ((-1, 0, 0), x1), ((0, 1, 0), -y0), ((0, -1, 0), y1),
                 ((0, 0, 1), -z0), ((0, 0, -1), z1)], mat)


def hash01(*v):
    h = 2166136261
    for x in v:
        h = ((h ^ (int(x) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return (h >> 8) / float(1 << 24)


def render(bodies, ref=(0, 0, 0), sx_range=(-40, 40), sy_range=(-50, 40)):
    """Render bodies; returns (img uint8 HxW, xoffs, yoffs) relative to ref."""
    sxs = np.arange(*sx_range)
    sys_ = np.arange(*sy_range)
    SX, SY = np.meshgrid(sxs, sys_)
    A = (SX.ravel() - 0.5) / 2.0
    B = SY.ravel() + 1.0
    # p(t) = O + t D
    O = np.stack([np.zeros_like(A), A, A - B], axis=1)
    D = np.array([1.0, 1.0, 2.0])
    best = np.full(A.shape, -np.inf)
    best_body = np.full(A.shape, -1)
    best_n = np.zeros((A.shape[0], 3))
    for bi, body in enumerate(bodies):
        lo = np.full(A.shape, -np.inf)
        hi = np.full(A.shape, np.inf)
        hi_n = np.zeros((A.shape[0], 3))
        ok = np.ones(A.shape, bool)
        for n, d in body.planes:
            f0 = O @ n + d
            f1 = n @ D
            if abs(f1) < 1e-12:
                ok &= f0 >= 0
                continue
            t = -f0 / f1
            if f1 > 0:      # inside for t >= t
                lo = np.maximum(lo, t)
            else:           # inside for t <= t: the upper bound is the visible face
                upd = t < hi
                hi = np.where(upd, t, hi)
                hi_n[upd] = -n     # outward normal
        hit = ok & (lo <= hi) & (hi > best)
        best = np.where(hit, hi, best)
        best_body = np.where(hit, bi, best_body)
        best_n[hit] = hi_n[hit]
    out = np.zeros(A.shape, np.uint8)
    got = best_body >= 0
    if got.any():
        t = best[got]
        P = O[got] + t[:, None] * D
        nrm = best_n[got]
        nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
        shade = np.round((nrm @ LIGHT - 0.75) * 4.0)
        bidx = best_body[got]
        col = np.zeros(t.shape, np.uint8)
        for bi in np.unique(bidx):
            m = bidx == bi
            mat = bodies[bi].mat
            if callable(mat):
                ramps, lvls = mat(P[m, 0], P[m, 1], P[m, 2], nrm[m])
            else:
                ramps = np.full(m.sum(), mat[0], dtype=object)
                lvls = np.full(m.sum(), float(mat[1]))
            sh = shade[m]
            sub = np.zeros(m.sum(), np.uint8)
            for name in set(ramps):
                mm = ramps == name
                r = RAMPS[name]
                li = np.clip(lvls[mm] + sh[mm], 0, len(r) - 1).astype(int)
                sub[mm] = np.array(r)[li]
            col[m] = sub
        out[got] = col
    img = out.reshape(SY.shape)
    ys, xs = np.nonzero(img)
    if len(xs) == 0:
        return np.zeros((1, 1), np.uint8), 0, 0
    x1, x2, y1, y2 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    rx, ry, rz = ref
    xo = int(sxs[x1]) - 2 * (ry - rx)
    yo = int(sys_[y1]) - (rx + ry - rz)
    return img[y1:y2, x1:x2], xo, yo


# --------------------------------------------------------------------------
# Track geometry
# --------------------------------------------------------------------------
# piece -> (centre, length axis, half length, conductor side (unit, across))
PIECES = {
    'X': ((8, 8), (1, 0), 8.0, (0, 1)),
    'Y': ((8, 8), (0, 1), 8.0, (1, 0)),
    'N': ((4, 4), (1, -1), 4 * SQ2, (-1, -1)),
    'S': ((12, 12), (1, -1), 4 * SQ2, (-1, -1)),
    'E': ((4, 12), (1, 1), 4 * SQ2, (1, -1)),
    'W': ((12, 4), (1, 1), 4 * SQ2, (1, -1)),
}

BALLAST_HW = 3.3
SLEEPER_HW = 2.2
RAIL_V = 1.25
RAIL_HW = 0.25
COND_V = 2.3     # conductor rail offset from the centre line (just outside one running rail)
SLEEPER_STEP = 2.0
SLEEPER_HL = 0.4


def _ballast_mat(x, y, z, n):
    lvl = np.full(x.shape, 3.0)
    for i in range(len(x)):
        r = hash01(int(x[i] * 4), int(y[i] * 4), 7)
        lvl[i] = 2.0 + int(r * 4)
    return np.full(x.shape, 'ballast', dtype=object), lvl


def piece_bodies(piece, slope=None, ballast=True, sleepers=True, rails=True, conductor='full', z_base=0.0):
    c, a, hl, side = PIECES[piece]
    a = np.array(a, float) / np.linalg.norm(a)
    side = np.array(side, float) / np.linalg.norm(side)
    b = np.array([-a[1], a[0]])
    sgn = 1.0 if b @ side > 0 else -1.0
    diag = abs(a[0]) > 0.1 and abs(a[1]) > 0.1
    out = []
    zb = z_base
    if ballast:
        out.append(obox(c, a, hl + 2, BALLAST_HW, zb - 0.01, zb + 0.3, _ballast_mat, slope))
    top = zb + (0.3 if ballast else 0.0)
    if sleepers:
        n = int(hl * 2 / SLEEPER_STEP) + 2
        for i in range(-n, n + 1):
            u = (i + 0.5) * SLEEPER_STEP
            if abs(u) > hl + 1:
                continue
            cc = (c[0] + a[0] * u, c[1] + a[1] * u)
            out.append(obox(cc, a, SLEEPER_HL, SLEEPER_HW, top, top + 0.3, ('sleeper', 2), slope))
    if rails:
        for v in (-RAIL_V, RAIL_V):
            cc = (c[0] + b[0] * v, c[1] + b[1] * v)
            out.append(obox(cc, a, hl + 2, RAIL_HW * (1.4 if diag else 1.0), top + 0.2, top + 0.8, ('rail', 3), slope))
    diag = abs(a[0]) > 0.1 and abs(a[1]) > 0.1
    # pieces that run across the screen (N, S) need a wider box to give a 1 px line
    chw = 0.62 if (diag and a[0] * a[1] < 0) else 0.22
    if conductor:
        v = sgn * COND_V
        cc = (c[0] + b[0] * v, c[1] + b[1] * v)
        if conductor == 'full':
            out.append(obox(cc, a, hl + 2, chw, top + 0.1, top + 0.7, ('yellow', 3), slope))
        else:
            # only near the tile edges, so the rails of other pieces stay clear in junctions
            for s in (-1, 1):
                u0 = s * (hl - 1.6)
                c2 = (cc[0] + a[0] * u0, cc[1] + a[1] * u0)
                out.append(obox(c2, a, 2.2, chw, top + 0.1, top + 0.7, ('yellow', 3), slope))
    return out


SLOPES = {   # h(x, y) = h0 + hx x + hy y for the four inclined track slopes
    'NE': (8.0, -0.5, 0.0),
    'SE': (0.0, 0.0, 0.5),
    'SW': (0.0, 0.5, 0.0),
    'NW': (8.0, 0.0, -0.5),
}

JUNCTIONS = {   # RTO_JUNCTION_*: ballast of all pieces not touching that edge
    'SW': ['Y', 'S', 'W'],
    'NE': ['Y', 'N', 'E'],
    'SE': ['X', 'S', 'E'],
    'NW': ['X', 'N', 'W'],
    'NSEW': ['X', 'Y', 'N', 'S', 'E', 'W'],
}


def underlay():
    """16 sprites in RTO order."""
    sprites = []
    for p in ['X', 'Y', 'N', 'S', 'E', 'W']:
        sprites.append(render(piece_bodies(p)))
    for s, p in (('NE', 'X'), ('SE', 'Y'), ('SW', 'X'), ('NW', 'Y')):
        sprites.append(render(piece_bodies(p, slope=SLOPES[s])))
    # crossing of X and Y with ballast
    b = piece_bodies('X', conductor='ends') + piece_bodies('Y', conductor='ends')
    sprites.append(render(b))
    for j in ['SW', 'NE', 'SE', 'NW', 'NSEW']:
        b = []
        for p in JUNCTIONS[j]:
            b += piece_bodies(p, sleepers=False, rails=False, conductor=None)
        sprites.append(render(b))
    return sprites


def overlay():
    """10 sprites: rails and sleepers without ballast, for junctions (and the
    reservation overlay)."""
    sprites = []
    for p in ['X', 'Y', 'N', 'S', 'E', 'W']:
        sprites.append(render(piece_bodies(p, ballast=False, conductor='ends', z_base=0.3)))
    for s, p in (('NE', 'X'), ('SE', 'Y'), ('SW', 'X'), ('NW', 'Y')):
        sprites.append(render(piece_bodies(p, slope=SLOPES[s], ballast=False, conductor='ends', z_base=0.3)))
    return sprites


def tunnels():
    """Track surface in the tunnel entrance, by DiagDirection NE, SE, SW, NW."""
    return [render(piece_bodies(p)) for p in ['X', 'Y', 'X', 'Y']]


def bridge_surfaces():
    """Flat X, flat Y, then ramps rising towards NE, SE, SW, NW."""
    out = [render(piece_bodies('X', ballast=False)), render(piece_bodies('Y', ballast=False))]
    for s, p in (('NE', 'X'), ('SE', 'Y'), ('SW', 'X'), ('NW', 'Y')):
        out.append(render(piece_bodies(p, slope=SLOPES[s], ballast=False)))
    return out


def crossing_post(lit):
    """A crossing post at (1.5, 1.5) of its 3x3 box: pole, cross sign, two lamps."""
    lamp = ('red', 4) if lit else ('dark', 3)
    b = [abox(1.2, 1.8, 1.2, 1.8, 0, 7.5, ('grey', 9)),
         abox(1.0, 2.0, 1.0, 2.0, 7.5, 8.5, ('grey', 13)),
         abox(1.0, 2.0, 0.6, 1.1, 5.0, 6.0, lamp),
         abox(1.0, 2.0, 1.9, 2.4, 5.0, 6.0, lamp),
         abox(1.0, 2.0, 1.1, 1.9, 5.2, 5.8, ('black', 1))]
    return render(b)


def crossing_rails():
    out = []
    for p in ['X', 'Y']:
        out.append(render(piece_bodies(p, ballast=False, sleepers=False, conductor=None, z_base=-0.2)))
    return out


def level_crossings(lit):
    rails = crossing_rails()
    post = crossing_post(lit)
    return rails + [post] * 8


# --------------------------------------------------------------------------
# Depots
# --------------------------------------------------------------------------
WALL_H = 13.0


def _wall_mat(x, y, z, n):
    ramps = np.full(x.shape, 'grey', dtype=object)
    lvl = np.full(x.shape, 10.0)
    band = (z > WALL_H - 2.6) & (z < WALL_H - 1.0)
    ramps[band] = 'cc'
    lvl[band] = 5
    # small windows
    along = np.where(np.abs(n[:, 0]) > 0.5, y, x)
    win = (z > 5.5) & (z < 8.5) & (((along - 2) % 4.0) > 1.2) & (((along - 2) % 4.0) < 2.8)
    ramps[win] = 'dark'
    lvl[win] = 3
    base = z < 1.2
    lvl[base] = 7
    return ramps, lvl


def _roof_mat(x, y, z, n):
    return np.full(x.shape, 'grey', dtype=object), np.full(x.shape, 5.0)


def depot_parts(entry):
    """Walls and roof of a depot whose door faces `entry` (NE, SE, SW, NW)."""
    lo, hi = 2.0, 14.0
    t = 1.0
    walls = {
        'NE': abox(lo, lo + t, lo, hi, 0, WALL_H, _wall_mat),     # x = lo side
        'SW': abox(hi - t, hi, lo, hi, 0, WALL_H, _wall_mat),     # x = hi side
        'NW': abox(lo, hi, lo, lo + t, 0, WALL_H, _wall_mat),     # y = lo side
        'SE': abox(lo, hi, hi - t, hi, 0, WALL_H, _wall_mat),     # y = hi side
    }
    # roof: an overhanging slab with a ridge along the track axis
    axis_x = entry in ('NE', 'SW')
    mid = (lo + hi) / 2
    roof = [abox(lo - 0.5, hi + 0.5, lo - 0.5, hi + 0.5, WALL_H, WALL_H + 1.0, _roof_mat)]
    if axis_x:
        roof.append(abox(lo, hi, mid - 2.5, mid + 2.5, WALL_H + 1.0, WALL_H + 2.2, ('grey', 7)))
    else:
        roof.append(abox(mid - 2.5, mid + 2.5, lo, hi, WALL_H + 1.0, WALL_H + 2.2, ('grey', 7)))
    # the door wall is open with a lintel
    door = walls.pop(entry)
    lintel = {
        'NE': abox(lo, lo + t, lo, hi, WALL_H - 3, WALL_H, _wall_mat),
        'SW': abox(hi - t, hi, lo, hi, WALL_H - 3, WALL_H, _wall_mat),
        'NW': abox(lo, hi, lo, lo + t, WALL_H - 3, WALL_H, _wall_mat),
        'SE': abox(lo, hi, hi - t, hi, WALL_H - 3, WALL_H, _wall_mat),
    }[entry]
    side_posts = []
    return walls, roof, lintel


def depots():
    """SE_1, SE_2, SW_1, SW_2, NE, NW (the base set order of depot sprites)."""
    out = []
    # SE entry: back (NE wall + NW wall) at (2, 2), front (SW wall, roof, lintel) at (13, 2)
    w, roof, lintel = depot_parts('SE')
    out.append(render([w['NE'], w['NW']], ref=(2, 2, 0)))
    out.append(render([w['SW']] + roof + [lintel], ref=(13, 2, 0)))
    # SW entry: back (NW wall + NE wall) at (2, 2), front (SE wall, roof, lintel) at (2, 13)
    w, roof, lintel = depot_parts('SW')
    out.append(render([w['NW'], w['NE']], ref=(2, 2, 0)))
    out.append(render([w['SE']] + roof + [lintel], ref=(2, 13, 0)))
    # NE and NW entries: the door faces away, one closed building
    w, roof, lintel = depot_parts('NE')
    out.append(render(list(w.values()) + roof + [lintel], ref=(2, 13, 0)))
    w, roof, lintel = depot_parts('NW')
    out.append(render(list(w.values()) + roof + [lintel], ref=(13, 2, 0)))
    return out


# --------------------------------------------------------------------------
# GUI: 8 toolbar buttons then 8 cursors (20x20 pixel art)
# --------------------------------------------------------------------------
GREY_D, GREY_L, BROWN, YELLOW, BLACK = 4, 12, 106, 68, 1


def _canvas(n=20):
    return np.zeros((n, n), np.uint8)


def _line(img, x0, y0, x1, y1, c):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) * 2 + 1
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        y = y0 + (y1 - y0) * i / n
        xi, yi = int(round(x)), int(round(y))
        if 0 <= xi < img.shape[1] and 0 <= yi < img.shape[0]:
            img[yi, xi] = c


def _track_icon(dx, dy):
    """Straight track along screen vector (dx, dy) through the centre."""
    img = _canvas()
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    px, py = -uy, ux
    cx, cy = 9.5, 9.5
    for s in range(-4, 5):
        x, y = cx + ux * s * 2.0, cy + uy * s * 2.0
        _line(img, x - px * 3.5, y - py * 3.5, x + px * 3.5, y + py * 3.5, BROWN)
    for off, c in ((-2.0, GREY_L), (2.0, GREY_L), (5.0, YELLOW)):
        x0, y0 = cx + px * off - ux * 9, cy + py * off - uy * 9
        x1, y1 = cx + px * off + ux * 9, cy + py * off + uy * 9
        _line(img, x0, y0, x1, y1, c)
    return img


def gui_sprites():
    icons = []
    icons.append(_track_icon(0, 1))       # N-S (vertical on screen)
    icons.append(_track_icon(-2, 1))      # X
    icons.append(_track_icon(1, 0))       # E-W
    icons.append(_track_icon(2, 1))       # Y
    auto = np.maximum(_track_icon(-2, 1), _track_icon(2, 1))   # autorail: both diagonals
    icons.append(auto)
    dep = _canvas()                         # depot: a shed with a yellow-railed door
    dep[6:17, 3:17] = 10
    dep[4:6, 4:16] = 6
    dep[3, 6:14] = 6
    dep[8:10, 3:17] = 200
    dep[11:17, 7:13] = BLACK
    _line(dep, 8, 17, 8, 19, GREY_L)
    _line(dep, 11, 17, 11, 19, GREY_L)
    _line(dep, 13, 17, 13, 19, YELLOW)
    icons.append(dep)
    tun = _canvas()                         # tunnel: hill with a portal
    for y in range(20):
        for x in range(20):
            if (x - 10) ** 2 / 100 + (y - 19) ** 2 / 150 < 1:
                tun[y, x] = 84 if y < 12 else 33
    for y in range(10, 20):
        for x in range(5, 15):
            if (x - 9.5) ** 2 / 20 + (y - 19) ** 2 / 81 < 1:
                tun[y, x] = BLACK
    _line(tun, 8, 15, 8, 19, GREY_L)
    _line(tun, 11, 15, 11, 19, GREY_L)
    _line(tun, 13, 15, 13, 19, YELLOW)
    icons.append(tun)
    conv = _track_icon(-2, 1)               # convert: track with a red arrow
    for a in range(0, 300, 6):
        r = math.radians(a)
        x, y = 10 + 7 * math.cos(r), 10 + 7 * math.sin(r)
        conv[int(round(y)), int(round(x))] = 182
    conv[3:6, 15:18] = 182
    icons.append(conv)
    sprites = [(img, 0, 0) for img in icons]
    cursors = [(img, -10, -10) for img in icons]
    return sprites + cursors
