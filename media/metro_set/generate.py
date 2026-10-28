#!/usr/bin/env python3
"""OpenTTD Underground Metro Set: sprite and NML generator.

Writes, from the tables in this file:
  gfx/metro_cars.png   8 bpp sprite sheet in the TTD DOS palette
  metro_set.nml        the NML source (properties, sprite sets, switches)
  docs/preview.png     the sheet enlarged 3x, for the README

Every sprite is rendered by casting one ray per screen pixel through the
OpenTTD dimetric projection (screen x = 2*(y-x), screen y = x+y-z, world
units of 1/16 tile) into a simple car model: a box body with a domed roof,
optional cab windscreen slope, bogies and underframe. Every surface point
picks a palette ramp and a shade by its face and the light direction, so
all eight views share one light. Company colour uses the 1CC remap range
(palette 198..205). No anti-aliasing: every pixel is a palette index.

The art and the code are original work for this set; no sprite or code of
any other set was used. GPL v2, see README.md.

Usage: python generate.py   (needs Pillow and numpy)
"""

import math
import os

import numpy as np
from PIL import Image

import track_art

HERE = os.path.dirname(os.path.abspath(__file__))
PALETTE_SRC = os.path.join(HERE, '..', 'baseset', 'openttd', 'openttdgui.png')

# Vehicle length in OpenTTD units (8 = full length = half a tile).
CAR_LEN = 8

# --------------------------------------------------------------------------
# Palette ramps (dark -> light), indices of the TTD DOS palette
# --------------------------------------------------------------------------
RAMPS = {
    'cc':      list(range(198, 206)),           # company colour, remapped by OpenTTD
    'grey':    list(range(1, 16)),
    'cream':   [33, 34, 35, 36, 37, 38, 39],
    'bluegrey': list(range(16, 24)),
    'brown':   [104, 105, 106, 107, 108, 109, 110, 111],
    'glass':   [16, 17, 18, 19, 20, 21, 22],
    'darkglass': [1, 1, 2, 3, 4, 16],
    'head':    [67, 68, 69, 15],
    'tail':    [180, 181, 182, 183],
    'steel':   [2, 3, 4, 5, 6, 7],
}


def load_palette():
    ref = Image.open(PALETTE_SRC)
    assert ref.mode == 'P'
    pal = ref.getpalette()[:768]
    assert pal[0:3] == [0, 0, 255], 'index 0 must be the transparent blue'
    return pal


# --------------------------------------------------------------------------
# The cars
# --------------------------------------------------------------------------
# Every style describes the look of one prototype family. Heights are in
# world z units (1 screen pixel each), lengths in vehicle steps.
STYLES = {
    # 1935: riveted, small framed windows, domed roof, flat cab front.
    'a': dict(body='cc', body_lvl=4, roof='grey', roof_lvl=4, roof_dome=1.0,
              belt=None, window_band=(4.0, 5.5), window='glass', window_frames=True,
              rivets=True, doors=4, door='cc', door_lvl=3, chamfer=0.25, slope=None,
              front_window=(4.0, 5.5), front_split=True, skirt='steel'),
    # 1959: smooth, two-tone, rounded cab corners.
    'e': dict(body='cc', body_lvl=4, roof='grey', roof_lvl=6, roof_dome=0.8,
              belt=('cream', 4, 3.3, 3.8), window_band=(4.0, 5.5), window='glass', window_frames=False,
              rivets=False, doors=4, door='cc', door_lvl=3, chamfer=0.6, slope=None,
              front_window=(4.0, 5.5), front_split=True, skirt='steel', upper=('cream', 4)),
    # 1976: boxy, a light stripe under the windows, big flat front windows.
    '717': dict(body='cc', body_lvl=4, roof='grey', roof_lvl=7, roof_dome=0.5,
                belt=('grey', 13, 3.2, 3.8), window_band=(4.0, 5.6), window='glass', window_frames=False,
                rivets=False, doors=4, door='cc', door_lvl=3, chamfer=0.15, slope=None,
                front_window=(3.9, 5.7), front_split=True, skirt='steel'),
    # 1991: light body, company colour window band, sloped cab.
    '720': dict(body='grey', body_lvl=12, roof='grey', roof_lvl=9, roof_dome=0.4,
                belt=('cc', 4, 3.0, 3.8), window_band=(4.0, 5.6), window='glass', window_frames=False,
                rivets=False, doors=4, door='grey', door_lvl=10, chamfer=0.3, slope=(0.9, 3.0),
                front_window=(3.8, 5.8), front_split=False, skirt='steel', band_cc=True),
    # 2016: silver body, continuous dark glazing, company colour doors and nose.
    '765': dict(body='grey', body_lvl=11, roof='grey', roof_lvl=8, roof_dome=0.4,
                belt=('cc', 5, 2.4, 3.0), window_band=(3.8, 5.8), window='darkglass', window_lvl=2, window_frames=False,
                rivets=False, doors=4, door='cc', door_lvl=4, chamfer=0.5, slope=(1.4, 2.4),
                front_window=(3.4, 5.9), front_split=False, skirt='steel', continuous_glass=True,
                nose_cc=True),
}

HALF_LEN = 3.7       # body half length (vehicle steps), the rest is the gap
HALF_W = 1.3         # body half width (world units)
Z_BODY = 1.4         # body bottom
Z_WALL = 6.6         # top of the side wall
Z_ROOF = 0.5         # extra height of the roof crown


def car_material(st, face, u, v, w, front_cab, rear_cab):
    """Return (ramp name, level) arrays for surface points; face is an int array:
    0 side, 1 roof, 2 end, 3 windscreen slope, 4 bogie/underframe."""
    n = u.shape[0]
    ramp = np.full(n, 'cc', dtype=object)
    lvl = np.full(n, float(st['body_lvl']))
    ramp[:] = st['body']

    au = np.abs(u)
    side = face == 0
    roof = face == 1
    end = face == 2
    slope = face == 3
    under = face == 4

    # ---- sides ----
    if 'upper' in st:
        m = side & (w > 3.8)
        ramp[m] = st['upper'][0]
        lvl[m] = st['upper'][1]
    if st['belt']:
        r, l, z0, z1 = st['belt']
        m = (side | end) & (w >= z0) & (w < z1)
        if st.get('band_cc'):
            m = (side | end) & (w >= z0) & (w < 5.9)
        ramp[m] = r
        lvl[m] = l
    # doors: evenly spaced along the car
    nd = st['doors']
    door_u = np.array([-HALF_LEN + (i + 0.5) * 2 * HALF_LEN / nd for i in range(nd)])
    door_w = 0.32
    in_door = np.zeros(n, dtype=bool)
    for du in door_u:
        in_door |= np.abs(u - du) < door_w
    m = side & in_door & (w < Z_WALL - 0.4)
    ramp[m] = st['door']
    lvl[m] = st['door_lvl']
    z0, z1 = st['window_band']
    if st.get('continuous_glass'):
        win = side & (w >= z0) & (w < z1) & (au < HALF_LEN - 0.5)
    else:
        # windows between the doors, door windows a bit higher
        seg = (u + HALF_LEN) / (2 * HALF_LEN / nd)
        frac = seg - np.floor(seg)
        between = (np.abs(frac - 0.0) > 0.12) & (np.abs(frac - 1.0) > 0.12)
        win = side & (w >= z0) & (w < z1) & (au < HALF_LEN - 0.45)
        if st['window_frames']:
            # small framed panes: drop a pixel column every 0.5 step
            pane = ((u * 2.0) % 1.0) > 0.25
            win &= pane | in_door
        win &= between | in_door
    ramp[win] = st['window']
    lvl[win] = st.get('window_lvl', 5.0)
    m = win & in_door
    lvl[m] -= 1.0
    if st['rivets']:
        m = side & ~win & ~in_door & (np.abs(w - 2.6) < 0.35) & (((u * 2.0) % 1.0) < 0.5)
        ramp[m] = st['body']
        lvl[m] = st['body_lvl'] + 2
    m = side & (w < Z_BODY + 0.5)
    ramp[m] = st['skirt']
    lvl[m] = 2

    # ---- roof ----
    ramp[roof] = st['roof']
    lvl[roof] = st['roof_lvl']

    # ---- ends ----
    fr = end & (u > 0)
    re = end & (u < 0)
    for mask, cab, is_front in ((fr, front_cab, True), (re, rear_cab, False)):
        if not cab:
            # gangway end: a darker door in the middle
            m = mask & (np.abs(v) < 0.5) & (w < 5.8)
            ramp[m] = 'grey'
            lvl[m] = 4
            continue
        if st.get('nose_cc'):
            m = mask & (w < st['front_window'][0])
            ramp[m] = 'cc'
            lvl[m] = 5
        fz0, fz1 = st['front_window']
        m = mask & (w >= fz0) & (w < fz1) & (np.abs(v) < HALF_W - 0.35)
        if st['front_split']:
            m &= np.abs(v) > 0.18
        ramp[m] = st['window']
        lvl[m] = 3
        lamp = mask & (np.abs(w - 2.6) < 0.45) & (np.abs(np.abs(v) - 0.85) < 0.3)
        ramp[lamp] = 'head' if is_front else 'tail'
        lvl[lamp] = 9
    # windscreen slope (720, 765): glass in the middle, body at the edges
    if st['slope']:
        for sgn, cab, is_front in ((1, front_cab, True), (-1, rear_cab, False)):
            mask = slope & (np.sign(u) == sgn)
            if not cab:
                continue
            m = mask & (np.abs(v) < HALF_W - 0.3) & (w > st['front_window'][0])
            ramp[m] = st['window']
            lvl[m] = 4
            if st.get('nose_cc'):
                m2 = mask & ~m
                ramp[m2] = 'cc'
                lvl[m2] = 5

    # ---- bogies and underframe ----
    ramp[under] = 'steel'
    lvl[under] = 1
    return ramp, lvl


def car_shape(st, u, v, w, front_cab, rear_cab):
    """Inside test of the car in local coordinates. Returns (inside, face)."""
    au = np.abs(u)
    av = np.abs(v)
    # roof profile: wall up to Z_WALL, dome to Z_WALL + Z_ROOF * dome
    dome = st['roof_dome']
    roof_h = Z_WALL + Z_ROOF * dome * (1.0 - (v / HALF_W) ** 2) + Z_ROOF * (1 - dome) * 0.6
    # plan view: chamfered corners at cab ends
    ch = st['chamfer']
    m_side = HALF_W - av
    m_roof = roof_h - w
    m_bottom = w - Z_BODY
    m_end = HALF_LEN - au
    m_corner = (HALF_LEN + HALF_W - ch) - (au + av)
    inside = (m_side > 0) & (m_roof > 0) & (m_bottom > 0) & (m_end > 0) & (m_corner > 0)
    margins = [m_side, m_roof, m_end, m_corner * 0.7]
    faces = [0, 1, 2, 2]
    if st['slope']:
        run, rise = st['slope']   # the slope takes `run` steps of length over `rise` of height
        top = roof_h
        for sgn, cab in ((1, front_cab), (-1, rear_cab)):
            if not cab:
                continue
            # plane through (HALF_LEN, top - rise) and (HALF_LEN - run, top)
            d = sgn * u - (HALF_LEN - run)
            m_slope = (run - d) * rise / run - (w - (top - rise))
            m_slope = np.where(np.sign(u) == sgn, m_slope / math.hypot(1, rise / run), 99.0)
            inside &= m_slope > 0
            margins.append(m_slope)
            faces.append(3)
    stack = np.stack(margins)
    face = np.array(faces)[np.argmin(stack, axis=0)]
    # bogies and underframe below the body
    bog = np.zeros_like(inside)
    for c in (-2.4, 2.4):
        bog |= (np.abs(u - c) < 0.9) & (av < HALF_W - 0.35) & (w > 0.0) & (w < Z_BODY + 0.05)
    frame = (au < 1.3) & (av < HALF_W - 0.25) & (w > 0.5) & (w < Z_BODY + 0.05)
    under = (bog | frame) & ~inside
    face = np.where(under, 4, face)
    return inside | under, face


# directions in OpenTTD order: N NE E SE S SW W NW; forward vector in world x/y
DIRS = [(-1, -1), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1), (0, -1)]
LIGHT = np.array([0.45, 0.15, 1.0])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
CANVAS = (64, 48)
ORIGIN = (32, 32)


def render(st, d, front_cab, rear_cab):
    fx, fy = DIRS[d]
    diag = fx != 0 and fy != 0
    f = np.array([fx, fy], float)       # one vehicle step in world units
    s = np.array([-fy, fx], float)
    s /= np.linalg.norm(s)
    fhat = f / np.linalg.norm(f)
    W, H = CANVAS
    sx, sy = np.meshgrid(np.arange(W) - ORIGIN[0] + 0.5, np.arange(H) - ORIGIN[1] + 0.5)
    sx = sx.ravel()
    sy = sy.ravel()
    x0 = (sy - sx / 2) / 2
    y0 = (sy + sx / 2) / 2
    best_t = np.full(sx.shape, -np.inf)
    hit_face = np.zeros(sx.shape, int)
    hit = np.zeros((sx.shape[0], 3))
    for t in np.arange(4.6, -0.4, -0.025):
        todo = best_t == -np.inf
        if not todo.any():
            break
        x = x0[todo] + t
        y = y0[todo] + t
        z = np.full(x.shape, 2 * t)
        u = (x * f[0] + y * f[1]) / (f @ f)
        v = x * s[0] + y * s[1]
        ins, face = car_shape(st, u, v, z, front_cab, rear_cab)
        idx = np.nonzero(todo)[0][ins]
        best_t[idx] = t
        hit_face[idx] = face[ins]
        hit[idx] = np.stack([u[ins], v[ins], z[ins]], axis=1)
    got = best_t > -np.inf
    out = np.zeros(sx.shape, np.uint8)
    if got.any():
        u, v, w = hit[got].T
        face = hit_face[got]
        ramp, lvl = car_material(st, face, u, v, w, front_cab, rear_cab)
        # face normals in world space
        n = np.zeros((u.shape[0], 3))
        sv = np.sign(v)[:, None]
        su = np.sign(u)[:, None]
        n[face == 0] = np.concatenate([s * 1.0, [0.0]])[None, :] * sv[face == 0]
        n[face == 1] = [0, 0, 1]
        n[face == 2] = np.concatenate([fhat, [0.0]])[None, :] * su[face == 2]
        n[face == 4] = np.concatenate([s, [0.0]])[None, :] * sv[face == 4]
        if st['slope']:
            run, rise = st['slope']
            nn = np.array([rise / run, 1.0])
            nn /= np.linalg.norm(nn)
            m = face == 3
            n[m] = np.concatenate([fhat * nn[0], [0.0]])[None, :] * su[m]
            n[m, 2] = nn[1]
        # domed roof normal leans to the side
        m = face == 1
        n[m, :2] += (s[None, :] * (v[m] / HALF_W * 0.8 * st['roof_dome'])[:, None])
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        b = n @ LIGHT
        shade = np.round((b - 0.75) * 4.0)
        cols = np.zeros(u.shape[0], np.uint8)
        for name, r in RAMPS.items():
            m = ramp == name
            if not m.any():
                continue
            if name in ('head', 'tail'):
                li = np.full(m.sum(), len(r) - 1)
            else:
                li = np.clip(lvl[m] + shade[m], 0, len(r) - 1).astype(int)
            cols[m] = np.array(r)[li]
        out[got] = cols
    img = out.reshape(H, W)
    ys, xs = np.nonzero(img)
    x1, x2, y1, y2 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return img[y1:y2, x1:x2], x1 - ORIGIN[0], y1 - ORIGIN[1]


# --------------------------------------------------------------------------
# The vehicles
# --------------------------------------------------------------------------
# Capacities: nominal load (seats + standing at about 5 persons/m2), taken as
# 56 % of the published full load where only that is given (ratio of the
# published 5/m2 and 10/m2 figures of the 2016 car). Power per car; for the
# 1935 car the motor car power is averaged with its trailer (the set has no
# trailers, every car is a motor car).
VEHICLES = [
    dict(id='metro_type_a', num=0x200, style='a', name='Type A', year=1935, model_life=30,
         life=40, speed=65, power_kw=306, weight=44, capacity=147, loading=20,
         cost=16, running=85, te=0.25, cabs=True),
    dict(id='metro_type_e', num=0x201, style='e', name='Type E', year=1959, model_life=27,
         life=40, speed=90, power_kw=272, weight=32, capacity=148, loading=24,
         cost=18, running=90, te=0.3, cabs=True),
    dict(id='metro_717', num=0x202, style='717', name='717', year=1976, model_life=45,
         life=40, speed=90, power_kw=456, weight=34, capacity=173, loading=26,
         cost=20, running=95, te=0.3, cabs=True),
    dict(id='metro_714', num=0x203, style='717', name='714', year=1976, model_life=45,
         life=40, speed=90, power_kw=456, weight=34, capacity=173, loading=26,
         cost=19, running=95, te=0.3, cabs=False),
    dict(id='metro_720', num=0x204, style='720', name='720', year=1991, model_life=15,
         life=25, speed=100, power_kw=460, weight=35, capacity=185, loading=28,
         cost=22, running=100, te=0.32, cabs=True),
    dict(id='metro_765', num=0x205, style='765', name='765', year=2016, model_life=None,
         life=30, speed=90, power_kw=680, weight=38, capacity=176, loading=32,
         cost=25, running=105, te=0.35, cabs=True),
]

VARIANTS = [('front', True, False), ('rear', False, True), ('mid', False, False), ('both', True, True)]


def build_sheet():
    """Render every sprite and pack the sheet. Returns (image, sprite table)."""
    cell_w, cell_h = 40, 32
    rows = []
    for st_name in STYLES:
        for var, fc, rc in VARIANTS:
            rows.append((st_name, var, fc, rc))
    sheet = np.zeros((cell_h * len(rows) + 1, cell_w * 8 + 1), np.uint8)
    table = {}
    for r, (st_name, var, fc, rc) in enumerate(rows):
        sprites = []
        for d in range(8):
            img, xo, yo = render(STYLES[st_name], d, fc, rc)
            h, w = img.shape
            assert w < cell_w and h < cell_h, (st_name, d, w, h)
            px, py = d * cell_w + 1, r * cell_h + 1
            sheet[py:py + h, px:px + w] = img
            sprites.append((px, py, w, h, xo, yo))
        table[(st_name, var)] = sprites
    return sheet, table


def write_sheet(sheet, pal, path):
    img = Image.frombytes('P', (sheet.shape[1], sheet.shape[0]), sheet.tobytes())
    img.putpalette(pal)
    img.save(path, optimize=True)


def write_preview(sheet, pal, path, k=4):
    img = Image.frombytes('P', (sheet.shape[1], sheet.shape[0]), sheet.tobytes())
    img.putpalette(pal)
    rgb = img.convert('RGB')
    # show company colour as dark blue like the default company
    a = np.array(rgb)
    s = sheet
    a[s == 0] = (255, 255, 255)
    Image.fromarray(a).resize((sheet.shape[1] * k, sheet.shape[0] * k), Image.NEAREST).save(path)


Y_SHIFT = 0   # moves all sprites up/down relative to the vehicle position


def draw_origin(d, length=CAR_LEN):
    """Screen point where OpenTTD puts the sprite origin, relative to the
    vehicle position (Train::UpdateDeltaXY, not flipped)."""
    full = 8
    if d % 2 == 0:   # N, E, S, W
        half = (full - length) // 2
        sign = [(-1, -1), (-1, 1), (1, 1), (1, -1)][d // 2]
        ox, oy = -1 - half * sign[0], -1 - half * sign[1]
    elif d == 1:     # NE
        ox, oy = -((length + 1) // 2) + 1, -1
    elif d == 7:     # NW
        ox, oy = -1, -((length + 1) // 2) + 1
    elif d == 5:     # SW
        ox, oy = -(length // 2) + 1 - (full - length), -1
    else:            # SE
        ox, oy = -1, -(length // 2) + 1 - (full - length)
    return 2 * (oy - ox), ox + oy


def nml_spriteset(name, sprites):
    lines = [f'spriteset({name}, "gfx/metro_cars.png") {{']
    for d, (x, y, w, h, xo, yo) in enumerate(sprites):
        dx, dy = draw_origin(d)
        lines.append(f'    [{x:3d}, {y:3d}, {w:2d}, {h:2d}, {xo - dx:3d}, {yo - dy + Y_SHIFT:3d}]')
    lines.append('}')
    return '\n'.join(lines)


def write_nml(table, path, track_part=''):
    out = []
    out.append('''/*
 * OpenTTD Underground Metro Set
 * GENERATED by generate.py - edit the tables there, not this file.
 * License: GPL v2 (see README.md).
 */

grf {
    grfid: "OUM\\01";
    name: string(STR_GRF_NAME);
    desc: string(STR_GRF_DESC);
    url: string(STR_GRF_URL);
    version: 1;
    min_compatible_version: 1;
}

cargotable { PASS }
railtypetable { MTRO }
''')
    out.append(track_part)
    for (st_name, var), sprites in table.items():
        out.append(nml_spriteset(f'set_{st_name}_{var}', sprites))
        out.append('')
    for st_name in STYLES:
        out.append(f'''// Cab at whichever end of the train the car is, no cab in the middle.
switch (FEAT_TRAINS, SELF, sw_{st_name}_end, position_in_consist_from_end) {{
    0: set_{st_name}_rear;
    set_{st_name}_mid;
}}
switch (FEAT_TRAINS, SELF, sw_{st_name}_single, position_in_consist_from_end) {{
    0: set_{st_name}_both;
    set_{st_name}_front;
}}
switch (FEAT_TRAINS, SELF, sw_{st_name}_gfx, position_in_consist) {{
    0: sw_{st_name}_single;
    sw_{st_name}_end;
}}
''')
    for veh in VEHICLES:
        hp = round(veh['power_kw'] * 1.341)
        ml = 'VEHICLE_NEVER_EXPIRES' if veh['model_life'] is None else veh['model_life']
        gfx = f'sw_{veh["style"]}_gfx' if veh['cabs'] else f'set_{veh["style"]}_mid'
        purchase = f'set_{veh["style"]}_both' if veh['cabs'] else f'set_{veh["style"]}_mid'
        key = veh['id'].upper()
        out.append(f'''item(FEAT_TRAINS, {veh['id']}, {veh['num']}) {{
    property {{
        name:                         string(STR_NAME_{key});
        climates_available:           ALL_CLIMATES;
        introduction_date:            date({veh['year']}, 3, 1);
        model_life:                   {ml};
        vehicle_life:                 {veh['life']};
        reliability_decay:            20;
        refittable_cargo_classes:     bitmask(CC_PASSENGERS);
        non_refittable_cargo_classes: bitmask();
        default_cargo_type:           PASS;
        cargo_capacity:               {veh['capacity']};
        loading_speed:                {veh['loading']};
        cost_factor:                  {veh['cost']};
        running_cost_factor:          {veh['running']};
        running_cost_base:            RUNNING_COST_ELECTRIC;
        sprite_id:                    SPRITE_ID_NEW_TRAIN;
        speed:                        {veh['speed']} km/h;
        power:                        {hp} hp;
        weight:                       {veh['weight']} ton;
        tractive_effort_coefficient:  {veh['te']};
        track_type:                   MTRO;
        engine_class:                 ENGINE_CLASS_ELECTRIC;
        dual_headed:                  0;
        misc_flags:                   bitmask(TRAIN_FLAG_MU);
        visual_effect_and_powered:    visual_effect_and_powered(VISUAL_EFFECT_DISABLE, 0, DISABLE_WAGON_POWER);
        length:                       {CAR_LEN};
        extra_flags:                  bitmask(VEHICLE_FLAG_AUTOREFIT);
    }}
    graphics {{
        additional_text: string(STR_INFO_{key});
        purchase: {purchase};
        {gfx};
    }}
}}
''')
    with open(path, 'w', newline='\n') as fh:
        fh.write('\n'.join(out))


TRACK_GROUPS = [
    ('underlay', track_art.underlay),
    ('overlay', track_art.overlay),
    ('tunnels', track_art.tunnels),
    ('bridges', track_art.bridge_surfaces),
    ('crossing_open', lambda: track_art.level_crossings(False)),
    ('crossing_closed', lambda: track_art.level_crossings(True)),
    ('depots', track_art.depots),
    ('gui', track_art.gui_sprites),
]


def build_track_sheet():
    """Pack the railtype sprites, one row per group. Returns (sheet, table)."""
    groups = [(name, fn()) for name, fn in TRACK_GROUPS]
    rows = []
    for name, sprites in groups:
        x = 1
        cells = []
        for img, xo, yo in sprites:
            h, w = img.shape
            cells.append((x, w, h, xo, yo, img))
            x += w + 1
        rows.append((name, cells, x, max(c[2] for c in cells)))
    width = max(r[2] for r in rows) + 1
    height = sum(r[3] + 1 for r in rows) + 1
    sheet = np.zeros((height, width), np.uint8)
    table = {}
    y = 1
    for name, cells, _, rh in rows:
        out = []
        for x, w, h, xo, yo, img in cells:
            sheet[y:y + h, x:x + w] = img
            out.append((x, y, w, h, xo, yo))
        table[name] = out
        y += rh + 1
    return sheet, table


RAILTYPE_NML = '''
// ---------------------------------------------------------------------------
// Metro track: ordinary track with a yellow conductor rail. Label MTRO, the
// label metro track sets have used for years, so vehicles of other sets made
// for MTRO run on it too. Only metro cars run on it; ordinary trains do not.
// ---------------------------------------------------------------------------
switch (FEAT_RAILTYPES, SELF, sw_mtro_crossing, level_crossing_status) {
    LEVEL_CROSSING_CLOSED: spr_mtro_crossing_closed;
    spr_mtro_crossing_open;
}

item(FEAT_RAILTYPES, metro_track) {
    property {
        label:                    "MTRO";
        name:                     string(STR_MTRO_NAME);
        toolbar_caption:          string(STR_MTRO_TOOLBAR);
        menu_text:                string(STR_MTRO_MENU);
        build_window_caption:     string(STR_MTRO_BUILD_WINDOW);
        autoreplace_text:         string(STR_MTRO_AUTOREPLACE);
        new_engine_text:          string(STR_MTRO_NEW_ENGINE);
        introduction_date:        date(1, 1, 1);
        compatible_railtype_list: ["MTRO"];
        powered_railtype_list:    ["MTRO"];
        railtype_flags:           bitmask();
        construction_cost:        12;
        maintenance_cost:         12;
        speed_limit:              0;
        acceleration_model:       ACC_MODEL_RAIL;
        station_graphics:         RAILTYPE_STATION_NORMAL;
        map_colour:               67;
        sort_order:               19;
    }
    graphics {
        underlay:        spr_mtro_underlay;
        track_overlay:   spr_mtro_overlay;
        tunnels:         spr_mtro_tunnels;
        bridge_surfaces: spr_mtro_bridges;
        level_crossings: sw_mtro_crossing;
        depots:          spr_mtro_depots;
        gui:             spr_mtro_gui;
    }
}
'''


def track_nml(table):
    out = []
    for name, sprites in table.items():
        out.append(f'spriteset(spr_mtro_{name}, "gfx/metro_track.png") {{')
        for (x, y, w, h, xo, yo) in sprites:
            out.append(f'    [{x:4d}, {y:3d}, {w:2d}, {h:2d}, {xo:3d}, {yo:3d}]')
        out.append('}')
        out.append('')
    return '\n'.join(out) + RAILTYPE_NML


def main():
    pal = load_palette()
    os.makedirs(os.path.join(HERE, 'gfx'), exist_ok=True)
    os.makedirs(os.path.join(HERE, 'docs'), exist_ok=True)
    sheet, table = build_sheet()
    write_sheet(sheet, pal, os.path.join(HERE, 'gfx', 'metro_cars.png'))
    write_preview(sheet, pal, os.path.join(HERE, 'docs', 'preview.png'))
    tsheet, ttable = build_track_sheet()
    write_sheet(tsheet, pal, os.path.join(HERE, 'gfx', 'metro_track.png'))
    write_preview(tsheet, pal, os.path.join(HERE, 'docs', 'preview_track.png'), k=3)
    write_nml(table, os.path.join(HERE, 'metro_set.nml'), track_nml(ttable))
    print('sprites:', sum(len(s) for s in table.values()))


if __name__ == '__main__':
    main()
