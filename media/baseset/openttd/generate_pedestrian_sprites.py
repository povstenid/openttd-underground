#!/usr/bin/env python3
"""Generate the pedestrian sprites of the OpenTTD base graphics set.

This script is the single source of truth for the pedestrian art: it writes
  * pedestrians.png  - 8bpp sprite sheet in the OpenTTD DOS palette,
  * pedestrians.nfo  - the sprite list (Action 5 type 0x1D) for grfcodec,
and, with --preview, a 6x magnified preview of the sheet into
docs/pedestrian_preview.png.

Sprite order: first the walkers, variant * 16 + DiagDirection * 4 + animation
frame; then the standing people waiting at stations, 64 + variant * 12 +
DiagDirection faced * 3 + pose (idle, weight shifted with the head turned,
hand at the face). This must match SPR_PEDESTRIAN_BASE,
SPR_PEDESTRIAN_STANDING_BASE and PEDESTRIAN_SPRITE_COUNT in
src/table/sprites.h; the script checks the count there.

The figures are hand drawn 5x9 bitmaps. Shirts and dresses use the company
colour ramp, so the game recolours each pedestrian with a random colour
remap. The draw point (the feet) is the bottom middle pixel.

Usage (from any directory):
    python generate_pedestrian_sprites.py            # sheet + nfo
    python generate_pedestrian_sprites.py --preview  # also the preview
"""

import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))


def load_palette():
    """The canonical DOS palette, taken byte for byte from a baseset sheet
    (grfcodec rejects sheets whose palette differs from it)."""
    ref = Image.open(os.path.join(HERE, 'openttdgui.png'))
    assert ref.mode == 'P'
    pal = ref.getpalette()[:768]
    assert pal[0:3] == [0, 0, 255], 'index 0 must be the transparent blue'
    return pal


# Body parts, independent of the variant.
SKIN = 120
SKIN_SHADE = 117
CC_LIGHT, CC_MID, CC_DARK = 203, 201, 199  # company colour ramp (remapped)

# Variants: (hair, trousers, shoes, dress).
VARIANTS = [
    (106, 130, 1, False),  # man, brown hair, navy trousers
    (65, 4, 2, False),     # man, blond hair, grey trousers
    (63, None, 1, True),   # woman, auburn hair, dress
    (2, None, 34, True),   # woman, black hair, dress
]

# Leg/arm poses per animation frame: stride, passing, other stride, passing.
LEGS_TROUSERS = [
    ['.TTT.', '.T.T.', 'T...T', 'B...B'],
    ['.TTT.', '..T..', '..T..', '..B..'],
    ['.TTT.', '.T.T.', 'T...T', 'B...B'],
    ['.TTT.', '..T..', '..T..', '..B..'],
]
LEGS_DRESS = [
    ['.DDD.', 'DDDDD', '.S.S.', 'B...B'],
    ['.DDD.', '.DDD.', '..S..', '..B..'],
    ['.DDD.', 'DDDDD', '.S.S.', 'B...B'],
    ['.DDD.', '.DDD.', '..S..', '..B..'],
]
# Hands swing out in the stride frames.
ARMS = ['SLCD.', '.LCD.', '.LCDS', '.LCD.']


def figure(variant, direction, frame):
    """5x9 bitmap rows of one pedestrian.

    direction is the DiagDirection of travel: NE (0) and NW (3) walk away from
    the viewer (back view), SE (1) and SW (2) towards it (face visible); the
    head leans towards the side of the screen the figure walks to."""
    hair, trousers, shoes, dress = VARIANTS[variant]
    back = direction in (0, 3)
    right = direction in (0, 1)

    face = 'HH' if back else 'SS'
    head = ('..' + face + '.') if right else ('.' + face + '..')
    top = ('..HH.' if right else '.HH..')
    if dress:
        shoulders = '.HCD.' if back else '.LCD.'  # long hair over the back
    else:
        shoulders = '.LCD.'
    # Walking left mirrors the arm swing, so the figure reads the same way round.
    arms = ARMS[frame if right else (frame + 2) % 4]
    rows = [top, head, shoulders, '.LCD.', arms] + (LEGS_DRESS if dress else LEGS_TROUSERS)[frame]

    cmap = {
        '.': 0, 'H': hair, 'S': SKIN_SHADE if back else SKIN,
        'L': CC_LIGHT, 'C': CC_MID, 'D': CC_DARK,
        'T': trousers, 'B': shoes,
    }
    # Legs under a dress are always shaded skin.
    return [[SKIN_SHADE if ch == 'S' and j >= 6 else cmap[ch] for ch in row] for j, row in enumerate(rows)]


# Standing poses: legs and the arm row for idle, weight shifted, hand at the face.
STAND_LEGS_TROUSERS = [
    ['.TTT.', '.T.T.', '.T.T.', '.B.B.'],
    ['.TTT.', '.T.T.', '.T..T', '.B..B'],
    ['.TTT.', '.T.T.', '.T.T.', '.B.B.'],
]
STAND_LEGS_DRESS = [
    ['.DDD.', '.DDD.', '.S.S.', '.B.B.'],
    ['.DDD.', '.DDD.', '.S..S', '.B..B'],
    ['.DDD.', '.DDD.', '.S.S.', '.B.B.'],
]
STAND_POSES = 3


def standing_figure(variant, direction, pose):
    """5x9 bitmap rows of one standing pedestrian.

    direction is the DiagDirection faced: NE (0) and NW (3) away from the
    viewer (back view), SE (1) and SW (2) towards it. Pose 0 stands still with
    the hands down, pose 1 shifts the weight and turns the head the other way,
    pose 2 holds a hand at the face (a phone, a yawn)."""
    hair, trousers, shoes, dress = VARIANTS[variant]
    back = direction in (0, 3)
    right = direction in (0, 1)
    if pose == 1: right = not right

    face = 'HH' if back else 'SS'
    head = ('..' + face + '.') if right else ('.' + face + '..')
    top = ('..HH.' if right else '.HH..')
    if dress and back:
        shoulders = '.HCD.'  # long hair over the back
    else:
        shoulders = '.LCD.'
    if pose == 2:
        # Forearm raised along the body to the face.
        upper = ('.LCS.' if right else '.SCD.') if not back else '.LCD.'
        arms = ['.LCD.', upper, 'SLCD.' if right else '.LCDS']
    else:
        arms = ['.LCD.', '.LCD.', 'SLCDS']
    rows = [top, head, arms[0], arms[1], arms[2]] + (STAND_LEGS_DRESS if dress else STAND_LEGS_TROUSERS)[pose]
    if shoulders != '.LCD.': rows[2] = shoulders

    cmap = {
        '.': 0, 'H': hair, 'S': SKIN_SHADE if back else SKIN,
        'L': CC_LIGHT, 'C': CC_MID, 'D': CC_DARK,
        'T': trousers, 'B': shoes,
    }
    return [[SKIN_SHADE if ch == 'S' and j >= 6 else cmap[ch] for ch in row] for j, row in enumerate(rows)]


W, H = 5, 9
CELL_W, CELL_H = W + 3, H + 3
OUTLINE = 1  # near black, around the toolbar icon


def transparency_icon():
    """Toolbar icon of the transparency window: two pedestrians at twice the
    size, outlined so they read on the dark green buttons."""
    people = [figure(0, 1, 1), figure(2, 2, 1)]
    gap = 1
    w = 1 + 2 * W + gap + 2 * W + 1
    h = 1 + 2 * H + 1
    bmp = [[0] * w for _ in range(h)]
    for n, fig in enumerate(people):
        ox = 1 + n * (2 * W + gap)
        for j in range(H):
            for i in range(W):
                if fig[j][i]:
                    for dy in range(2):
                        for dx in range(2):
                            bmp[1 + 2 * j + dy][ox + 2 * i + dx] = fig[j][i]
    outlined = [row[:] for row in bmp]
    for j in range(h):
        for i in range(w):
            if bmp[j][i]: continue
            if any(0 <= j + dy < h and 0 <= i + dx < w and bmp[j + dy][i + dx]
                    for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                outlined[j][i] = OUTLINE
    return outlined


def build():
    """All sprites as (name, bitmap, x offset, y offset); figures are drawn at their feet."""
    feet = (-(W // 2), -(H - 1))
    sprites = []
    for v in range(len(VARIANTS)):
        for d in range(4):
            for f in range(4):
                sprites.append((f'variant {v} dir {d} frame {f}', figure(v, d, f)) + feet)
    for v in range(len(VARIANTS)):
        for d in range(4):
            for p in range(STAND_POSES):
                sprites.append((f'standing variant {v} dir {d} pose {p}', standing_figure(v, d, p)) + feet)
    sprites.append(('transparency toolbar icon', transparency_icon(), 0, 0))
    return sprites


def write_sheet(sprites, palette):
    """Figures in a grid of fixed cells, larger sprites in a row below it."""
    cols = 16
    figures = [s for s in sprites if len(s[1]) == H and len(s[1][0]) == W]
    others = sprites[len(figures):]
    assert sprites[:len(figures)] == figures, 'figures must come first'
    rows = (len(figures) + cols - 1) // cols
    extra_w = sum(len(s[1][0]) + 3 for s in others)
    extra_h = max((len(s[1]) + 3 for s in others), default=0)
    img = Image.new('P', (max(cols * CELL_W, extra_w) + 2, rows * CELL_H + extra_h + 2), 0)
    img.putpalette(palette)
    placed = []
    x = 2
    for n, (name, bmp, xoffs, yoffs) in enumerate(sprites):
        if n < len(figures):
            sx = 2 + (n % cols) * CELL_W
            sy = 2 + (n // cols) * CELL_H
        else:
            sx = x
            sy = 2 + rows * CELL_H
            x += len(bmp[0]) + 3
        for j, row in enumerate(bmp):
            for i, c in enumerate(row):
                img.putpixel((sx + i, sy + j), c)
        placed.append((name, sx, sy, len(bmp[0]), len(bmp), xoffs, yoffs))
    img.save(os.path.join(HERE, 'pedestrians.png'), optimize=True)
    return img, placed


def write_nfo(placed):
    lines = [
        '// This file is part of OpenTTD.\n',
        '// OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.\n',
        '// OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n',
        '// See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.\n',
        '//\n',
        '// GENERATED by generate_pedestrian_sprites.py - do not edit by hand.\n',
        '//\n',
        '   -1 * 0\t 0C "Pedestrian sprites"\n',
        f'   -1 * 3\t 05 1D {len(placed):02X}\n',
    ]
    for n, (name, sx, sy, w, h, xoffs, yoffs) in enumerate(placed):
        lines.append(f'// {n:2d} {name}\n')
        lines.append(f'   -1 sprites/pedestrians.png 8bpp {sx:4d} {sy:4d} {w:3d} {h:3d} {xoffs:4d} {yoffs:4d} normal\n')
    with open(os.path.join(HERE, 'pedestrians.nfo'), 'w', newline='\n') as f:
        f.writelines(lines)


def check_count(n):
    path = os.path.join(ROOT, 'src', 'table', 'sprites.h')
    with open(path) as f:
        m = re.search(r'PEDESTRIAN_SPRITE_COUNT\s*=\s*(\d+)', f.read())
    if not m or int(m.group(1)) != n:
        sys.exit(f'PEDESTRIAN_SPRITE_COUNT in src/table/sprites.h must be {n}')


def write_preview(img):
    scale = 6
    big = img.convert('RGB').resize((img.width * scale, img.height * scale), Image.NEAREST)
    # Swap the transparent blue for a pavement grey.
    px = big.load()
    for y in range(big.height):
        for x in range(big.width):
            if px[x, y] == (0, 0, 255):
                px[x, y] = (150, 150, 150)
    out = os.path.join(ROOT, 'docs', 'pedestrian_preview.png')
    big.save(out)
    print('preview:', out)


def main():
    sprites = build()
    check_count(len(sprites))
    img, placed = write_sheet(sprites, load_palette())
    write_nfo(placed)
    print(f'{len(sprites)} sprites written')
    if '--preview' in sys.argv:
        write_preview(img)


if __name__ == '__main__':
    main()
