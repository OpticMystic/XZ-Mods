"""Build ui/cable_anim_data.h, the loopback-cable explainer's asset, from the Blender bake.

  python build_cable_anim.py BAKE.json [--out cable_anim_data.h]

The bake (cable-anim/blender/bake.py, run in Blender 5.2) holds the unit's feature lines as 3D
polylines, the filled faces, and for every 60 fps frame the camera, both plug poses, the layer gains and
which parts of every line are visible (hidden lines removed by ray casting against the scene). This
script only quantises and packs it; ui/cable_anim.c projects and draws it at any size and frame rate.

Packing (all little tables of integers, read by cable_anim.c):
  vertices      int16 x, y, z in 1/32 mm (world lines and fills) or plug-local 1/32 mm (plug lines/fills)
  polylines     first vertex, count, layer, space (world / plug A / plug B), closed
  visibility    one byte stream per polyline (and the cable's two silhouettes), run after run over the
                loop: varint frames, then a byte kind<<6 | k; kind 0 hidden, 1 whole line, 2 the same k
                intervals for every frame of the run, 3 k intervals per frame. An interval is two 12-bit
                fractions of the line's length packed in 3 bytes.
  frames        camera position and target (1/8 mm), plug A x y z tilt and plug B x y z yaw roll
                (1/32 mm, 1/100 degree), five layer gains and two label gains (0..255), the signal's
                head along the cable (0..65534, 65535 none)
"""
import argparse, json, math, os, sys

LAYERS = ['device', 'port_a', 'port_b', 'selector', 'cable', 'usb2']
# lines that belong to a layer by their object: USB 2's slot ring is the softer "also works" accent
LAYER_OF_OBJECT = {'slot_bezel2': 'usb2'}
SPACES = {'world': 0, 'plug_a': 1, 'plug_b': 2}
POS = 32.0      # units per mm for model coordinates
CAM = 8.0       # units per mm for the camera

def q16(v, scale):
    n = int(round(v * scale))
    if not -32768 <= n <= 32767:
        raise ValueError(f'{v} does not fit int16 at scale {scale}')
    return n

def u12(x):
    return max(0, min(4095, int(round(x * 4095))))

def varint(n):
    out = bytearray()
    while True:
        b = n & 127; n >>= 7
        out.append(b | (128 if n else 0))
        if not n:
            return out

def merge_to(ivs, most=63):
    """Close the smallest gaps until at most `most` intervals remain."""
    ivs = [list(v) for v in ivs]
    while len(ivs) > most:
        k = min(range(len(ivs) - 1), key=lambda i: ivs[i + 1][0] - ivs[i][1])
        ivs[k][1] = ivs[k + 1][1]; del ivs[k + 1]
    return ivs

def pack_vis(frames):
    """frames: per frame, a list of (s0, s1). Returns the byte stream and the number of runs."""
    q = []
    for ivs in frames:
        ivs = merge_to(ivs)
        iv = tuple((u12(a), u12(b)) for a, b in ivs if u12(b) > u12(a))
        if len(iv) == 1 and iv[0] == (0, 4095):
            q.append(('full', ()))
        elif not iv:
            q.append(('hidden', ()))
        else:
            q.append(('part', iv))
    out = bytearray(); runs = 0; i = 0; n = len(q)
    while i < n:
        kind, iv = q[i]
        j = i + 1
        if kind in ('full', 'hidden') or True:
            while j < n and q[j] == q[i]:
                j += 1
        if kind == 'hidden' or kind == 'full':
            out += varint(j - i); out.append((0 if kind == 'hidden' else 1) << 6)
        elif j - i > 1:
            out += varint(j - i); out.append(2 << 6 | len(iv))
            for a, b in iv: out += bytes([a >> 4, ((a & 15) << 4) | (b >> 8), b & 255])
        else:
            # a run of partial frames with the same interval count
            k = len(iv)
            j = i + 1
            while j < n and q[j][0] == 'part' and len(q[j][1]) == k and (j + 1 >= n or q[j + 1] != q[j]):
                j += 1
            out += varint(j - i); out.append(3 << 6 | k)
            for f in range(i, j):
                for a, b in q[f][1]: out += bytes([a >> 4, ((a & 15) << 4) | (b >> 8), b & 255])
        if max(len(x[1]) for x in q[i:j]) > 63:
            raise ValueError('more than 63 intervals on one line')
        runs += 1; i = j
    return bytes(out), runs

def c_bytes(name, data):
    lines = [f'static const uint8_t {name}[{max(1, len(data))}]={{']
    for k in range(0, len(data), 32):
        lines.append(','.join(str(b) for b in data[k:k + 32]) + ',')
    if not data: lines.append('0')
    lines.append('};')
    return '\n'.join(lines)

def build(bake, out_path):
    frames = bake['frames']
    N = len(frames)
    assert bake['f0'] == 0 and N == round(bake['loop_s'] * bake['fps']), 'the bake must cover the whole loop'
    verts, pls, along = [], [], []
    for pl in bake['polylines']:
        first = len(verts)
        if len(pl['pts']) > 64: raise ValueError('a line has more than 64 corners')
        for p in pl['pts']:
            verts.append(tuple(q16(c, POS) for c in p))
        # each corner's place along the line, a fraction of its length (the visible intervals are in these)
        pts = pl['pts'] + ([pl['pts'][0]] if pl['closed'] else [])
        lens = [math.dist(a, b) for a, b in zip(pts, pts[1:])]; total = sum(lens) or 1.0
        acc = 0.0
        for k in range(len(pl['pts'])):
            along.append(min(65535, int(round(acc / total * 65535)))); acc += lens[k] if k < len(lens) else 0.0
        layer = LAYER_OF_OBJECT.get(pl.get('obj'), pl['layer'])
        pls.append((first, len(pl['pts']), LAYERS.index(layer), SPACES[pl['space']], int(pl['closed'])))
    vis_stream = bytearray(); vis_off = []; runs = 0
    for v in bake['vis']:
        data, r = pack_vis(v); vis_off.append(len(vis_stream)); vis_stream += data; runs += r
    cable_off = []
    for side in bake['cable_vis']:
        data, r = pack_vis(side); cable_off.append(len(vis_stream)); vis_stream += data; runs += r
    # fills: the solids (any order: one colour), each recess's faces (drawn dark, seen only through its
    # opening), the slot covers, the plugs
    m = bake['model']
    pockets = m['pockets']
    def inside(box, pts):
        c = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
        return all(box[i] <= c[i] <= box[i + 3] for i in range(3))
    groups = {'solid': [], 'cover': [], 'plug': []}
    by_pocket = [[] for _ in pockets]
    for fl in bake['fills']:
        if fl['space'] != 'world': groups['plug'].append(fl)
        elif fl['obj'] in ('cover1', 'cover2'): groups['cover'].append(fl)
        elif fl['mat'] == 'hole':
            hits = [i for i, pk in enumerate(pockets) if inside(pk['box'], fl['pts'])]
            if not hits: raise ValueError(f'a dark face of {fl["obj"]} is in no recess')
            by_pocket[hits[0]].append(fl)
        else: groups['solid'].append(fl)
    fill_verts, fills = [], []
    def emit(fl):
        first = len(fill_verts)
        for p in fl['pts']:
            fill_verts.append(tuple(q16(c, POS) for c in p))
        if len(fl['pts']) > 24: raise ValueError('fill polygon has more than 24 corners')
        fills.append((first, len(fl['pts']), SPACES[fl['space']] | (4 if fl['mat'] == 'hole' else 0)))
    for fl in groups['solid']: emit(fl)
    solids = len(fills)
    pocket_rows = []
    for i, pk in enumerate(pockets):
        first = len(fills)
        for fl in by_pocket[i]: emit(fl)
        rim = pk['rim']
        # the opening's normal must point out of the recess
        cx = [sum(p[k] for p in rim) / len(rim) for k in range(3)]
        bc = [(pk['box'][k] + pk['box'][k + 3]) / 2 for k in range(3)]
        nx = ny = nz = 0.0
        for a, b in zip(rim, rim[1:] + rim[:1]):
            nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1])
        if nx * (cx[0] - bc[0]) + ny * (cx[1] - bc[1]) + nz * (cx[2] - bc[2]) < 0: rim = rim[::-1]
        rf = len(fill_verts)
        for p in rim: fill_verts.append(tuple(q16(c, POS) for c in p))
        parent = [q['name'] for q in pockets].index(pk['parent']) if pk['parent'] else 255
        pocket_rows.append((rf, len(rim), parent, first, len(fills) - first))
    covers = len(fills)
    for fl in groups['cover']: emit(fl)
    plugs = len(fills)
    for fl in groups['plug']: emit(fl)
    fr_rows = []
    for fr in frames:
        cam = [q16(c, CAM) for c in fr['cam']]
        a = [q16(c, POS) for c in fr['a'][:3]] + [q16(fr['a'][3], 100.0)]
        b = [q16(c, POS) for c in fr['b'][:3]] + [q16(fr['b'][3], 100.0), q16(fr['b'][4], 100.0)]
        g = [max(0, min(255, int(round(fr['g'][k] * 255)))) for k in LAYERS[:5]]
        lab = [max(0, min(255, int(round(x * 255)))) for x in fr['labels']]
        side = sum(1 << k for k, v in enumerate(fr.get('label_side', [1, 1])) if v < 0)
        pulse = 65535 if fr['pulse'] is None else max(0, min(65534, int(round(fr['pulse'] * 65534))))
        fr_rows.append((cam, a, b, g, lab, side, pulse))
    h = []
    h.append('/* SPDX-License-Identifier: MIT */')
    h.append('/* Generated by ui/build_cable_anim.py from the cable-anim Blender bake. Do not edit. */')
    h.append('#ifndef XZ_CABLE_ANIM_DATA_H\n#define XZ_CABLE_ANIM_DATA_H\n#include <stdint.h>')
    h.append(f'enum {{ XZ_CA_FRAMES={N}, XZ_CA_FPS={bake["fps"]}, XZ_CA_LOOP_MS={int(round(bake["loop_s"] * 1000))}, '
             f'XZ_CA_VERTS={len(verts)}, XZ_CA_POLYLINES={len(pls)}, XZ_CA_FILLS={len(fills)}, XZ_CA_FILL_VERTS={len(fill_verts)}, '
             f'XZ_CA_VIS_BYTES={len(vis_stream)} }};')
    h.append('#define XZ_CA_POS (1.0f/32.0f)  /* mm per vertex unit */')
    h.append('#define XZ_CA_CAM (1.0f/8.0f)   /* mm per camera unit */')
    for k in ('A_TAIL', 'B_TAIL', 'REAR_Y', 'TOP', 'CABLE_R', 'HFOV_DEG', 'SAFE_ASPECT'):
        h.append(f'#define XZ_CA_{k} {float(m[k]):.6f}f')
    k = m['cable_shape']
    consts = dict(FREE_A0=k['free_a'][0], FREE_A1=k['free_a'][1], FREE_B0=k['free_b'][0], FREE_B1=k['free_b'][1],
                  DRAPE_FROM=k['drape_from'], DRAPE_OVER=k['drape_over'], EDGE_X=k['edge'][0], EDGE_DY=k['edge'][1],
                  EDGE_DZ=k['edge'][2], EDGE_DIR_X=k['edge_dir'][0], EDGE_DIR_Y=k['edge_dir'][1], EDGE_DIR_Z=k['edge_dir'][2],
                  SEG1_0=k['seg1'][0], SEG1_1=k['seg1'][1], SEG2_0=k['seg2'][0], SEG2_1=k['seg2'][1])
    h.append('/* the cable shape (cable-anim/model CABLE_SHAPE) */')
    for name, v in consts.items():
        h.append(f'#define XZ_CA_{name} {float(v):.6f}f')
    h.append(f'#define XZ_CA_REST_SELECTOR_MS {int(round(m["rest_mark_selector"] * 1000))}u')
    for name, p in m['anchors'].items():
        h.append(f'static const float xz_ca_anchor_{name}[3]={{{p[0]:.3f}f,{p[1]:.3f}f,{p[2]:.3f}f}};')
    h.append('static const int16_t xz_ca_vert[XZ_CA_VERTS][3]={')
    for k in range(0, len(verts), 8):
        h.append(''.join('{%d,%d,%d},' % v for v in verts[k:k + 8]))
    h.append('};')
    h.append('/* where each vertex lies along its line: 0..65535 of the line length (a closed line ends at 65535, back at its first) */')
    h.append('static const uint16_t xz_ca_along[XZ_CA_VERTS]={')
    for k in range(0, len(along), 24):
        h.append(','.join(map(str, along[k:k + 24])) + ',')
    h.append('};')
    h.append('/* first vertex, vertex count, layer, space (0 world, 1 plug A, 2 plug B), closed, visibility offset */')
    h.append('static const struct { uint16_t first, count; uint8_t layer, space, closed; uint32_t vis; } xz_ca_polyline[XZ_CA_POLYLINES]={')
    for k in range(0, len(pls), 6):
        h.append(''.join('{%d,%d,%d,%d,%d,%d},' % (*p, vis_off[k + i]) for i, p in enumerate(pls[k:k + 6])))
    h.append('};')
    h.append(f'static const uint32_t xz_ca_cable_vis[2]={{{cable_off[0]},{cable_off[1]}}};')
    h.append(c_bytes('xz_ca_vis', vis_stream))
    h.append('static const int16_t xz_ca_fill_vert[XZ_CA_FILL_VERTS][3]={')
    for k in range(0, len(fill_verts), 8):
        h.append(''.join('{%d,%d,%d},' % v for v in fill_verts[k:k + 8]))
    h.append('};')
    h.append(f'enum {{ XZ_CA_SOLIDS={solids}, XZ_CA_COVERS={covers}, XZ_CA_PLUGS={plugs}, XZ_CA_POCKETS={len(pocket_rows)} }};')
    h.append('/* recesses: opening (first fill vertex, count; facing out), the recess it is seen through (255 none), its faces */')
    h.append('static const struct { uint16_t rim, rim_count; uint8_t parent; uint16_t first, count; } xz_ca_pocket[XZ_CA_POCKETS]={')
    h.append(''.join('{%d,%d,%d,%d,%d},' % r for r in pocket_rows))
    h.append('};')
    h.append('/* first vertex, count, flags: space (bits 0-1), hole (bit 2) */')
    h.append('static const struct { uint16_t first; uint8_t count, flags; } xz_ca_fill[XZ_CA_FILLS]={')
    for k in range(0, len(fills), 10):
        h.append(''.join('{%d,%d,%d},' % f for f in fills[k:k + 10]))
    h.append('};')
    h.append('/* camera position and target, plug A x y z tilt, plug B x y z yaw roll, gains (device, USB 1,')
    h.append('   rear socket, selectors, cable), label gains (REAR USB, USB 1), labels set left of their anchor (bit')
    h.append('   per label), signal head */')
    h.append('struct xz_ca_frame_row { int16_t cam[6], a[4], b[5]; uint8_t gain[5], label[2], label_left; uint16_t pulse; };')
    h.append('static const struct xz_ca_frame_row xz_ca_frame[XZ_CA_FRAMES]={')
    for cam, a, b, g, lab, side, pulse in fr_rows:
        h.append('{{%s},{%s},{%s},{%s},{%s},%d,%d},' % (','.join(map(str, cam)), ','.join(map(str, a)), ','.join(map(str, b)),
                                                       ','.join(map(str, g)), ','.join(map(str, lab)), side, pulse))
    h.append('};\n#endif')
    text = '\n'.join(h) + '\n'
    with open(out_path, 'w', newline='\r\n') as fp:   # source/** files are CRLF
        fp.write(text)
    size = len(verts) * 8 + len(pls) * 12 + len(vis_stream) + len(fill_verts) * 6 + len(fills) * 4 + N * 40
    return dict(frames=N, verts=len(verts), polylines=len(pls), vis_bytes=len(vis_stream), runs=runs,
                fills=len(fills), fill_verts=len(fill_verts), approx_bytes=size)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('bake')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cable_anim_data.h'))
    a = ap.parse_args()
    print(json.dumps(build(json.load(open(a.bake)), a.out)))
