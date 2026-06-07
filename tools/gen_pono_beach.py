# SPDX-License-Identifier: GPL-3.0-only
# gen_pono_beach.py - bake a static detailed beach corner + canned "wash-up"
# foam flipbooks into LVGL canned-animation arrays (lv_animimg). Same pipeline
# as gen_pono_spinner.py: pre-rendered frames -> playback is a per-frame bitmap
# blit (near-zero CPU on the 2-core A7, no GPU), so the boot screen stays smooth.
#
# Output: assets/pono/anim/pono_beach.c
#   const lv_img_dsc_t   pono_beach;                       // static sand wedge
#   const lv_img_dsc_t * pono_wave_v0[F];                  // wash-up variant 0
#   const lv_img_dsc_t * pono_wave_v1[F];                  // wash-up variant 1
#   const lv_img_dsc_t **pono_wave_variants[N];            // table of variants
#   const uint8_t        pono_wave_variant_frames[N];      // frame count per variant
#   const uint8_t        pono_wave_variant_count;          // N
#   const uint16_t       pono_beach_w, pono_beach_h;       // canvas size (placement)
#
# The beach + every wave frame share ONE WxH canvas so the foam overlays the
# beach pixel-for-pixel. Sand is opaque; everything else is transparent (alpha
# 0) so the scrolling ocean shows through. Pixel order B,G,R,A (LV_COLOR_DEPTH=32).
# Regenerate after changing geometry; result is committed.

import os
import numpy as np
from PIL import Image

W = 260            # beach canvas width  (placed at screen x=0)
H = 120            # beach canvas height (placed at screen y=272-H, bottom-left)
SS = 2             # supersample for AA
RNG = np.random.default_rng(7)   # fixed seed -> deterministic, committable output

# Waterline: diagonal from the left edge down to the right; sand fills the
# bottom-left wedge below it, ocean (transparent) above-right.
WL_X0, WL_Y0 = 0.0, H * 0.30
WL_X1, WL_Y1 = W * 0.82, float(H)

# Sand palette (dry high on the wedge -> wet near the waterline).
SAND_DRY = np.array([212, 186, 140], np.float32)   # warm tan
SAND_WET = np.array([150, 122, 86], np.float32)     # darker wet sand
FOAM = np.array([233, 245, 250], np.float32)        # near-white, faintly cyan

Ws, Hs = W * SS, H * SS
ys, xs = np.mgrid[0:Hs, 0:Ws].astype(np.float32)
px, py = xs / SS, ys / SS

# Signed distance into the sand from the waterline (positive = onto the sand).
nx, ny = -(WL_Y1 - WL_Y0), (WL_X1 - WL_X0)          # normal points down-left = into the sand
nlen = float(np.hypot(nx, ny)); nx, ny = nx / nlen, ny / nlen
d_sand = (px - WL_X0) * nx + (py - WL_Y0) * ny      # >0 inside sand

# Stable noise fields (supersampled) for sand grain + foam froth.
def smooth_noise(scale):
    small = RNG.random((int(Hs / scale) + 2, int(Ws / scale) + 2)).astype(np.float32)
    img = Image.fromarray((small * 255).astype(np.uint8)).resize((Ws, Hs), Image.BILINEAR)
    return np.asarray(img, np.float32) / 255.0

grain = smooth_noise(6 * SS) * 0.6 + smooth_noise(2 * SS) * 0.4
froth = smooth_noise(3 * SS) * 0.55 + smooth_noise(1.2 * SS) * 0.45


def downsample(rgba_ss):
    return np.asarray(Image.fromarray(rgba_ss, 'RGBA').resize((W, H), Image.LANCZOS))


def render_beach():
    rgba = np.zeros((Hs, Ws, 4), np.float32)
    edge = np.clip(d_sand / (2.0 * SS), 0.0, 1.0)          # feather the waterline
    # wet->dry gradient over the first ~40px of sand
    wetness = np.clip(d_sand / 42.0, 0.0, 1.0)
    sand = SAND_WET[None, None, :] * (1 - wetness[..., None]) + SAND_DRY[None, None, :] * wetness[..., None]
    # grain shading + a soft dune highlight band
    shade = 0.86 + 0.20 * grain
    dune = 1.0 + 0.10 * np.exp(-((d_sand - 70.0) ** 2) / (2 * 26.0 ** 2))
    sand = np.clip(sand * (shade * dune)[..., None], 0, 255)
    rgba[..., :3] = sand
    rgba[..., 3] = edge * 255.0
    # a couple of rounded rocks for "detail"
    for (rx, ry, rr, tone) in [(46, 96, 11, 96), (88, 110, 7, 120), (150, 116, 9, 108)]:
        dr = np.hypot(px - rx, py - ry)
        m = np.clip((rr - dr) / (1.5 * SS), 0.0, 1.0) * (d_sand > 2)
        rock = np.array([tone, tone - 14, tone - 30], np.float32)
        rlit = rock * (0.8 + 0.5 * np.clip((ry - py) / rr, 0, 1))[..., None]   # top-lit
        rgba[..., :3] = rgba[..., :3] * (1 - m[..., None]) + np.clip(rlit, 0, 255) * m[..., None]
        rgba[..., 3] = np.maximum(rgba[..., 3], m * 255.0)
    return downsample(rgba.astype(np.uint8))


def render_wave(reach, intensity):
    """One foam frame. reach = how far up the sand the foam edge is (px into
    sand); intensity = overall opacity (fades as the wave recedes)."""
    rgba = np.zeros((Hs, Ws, 4), np.float32)
    if intensity <= 0.01:
        return downsample(rgba.astype(np.uint8))
    # Foam occupies the sand band from the waterline (d=0) up to `reach`, with a
    # frothy leading lip near `reach` and thinning toward the trailing edge.
    onsand = d_sand > -1.0
    lead = np.clip(1.0 - np.abs(d_sand - reach) / (10.0 + 5.0 * froth), 0.0, 1.0)  # bright frothy lip
    body = np.clip((reach - d_sand) / max(reach, 1.0), 0.0, 1.0) ** 1.15           # wash sheet behind the lip
    a = np.maximum(lead, body * 0.85)            # opaque enough to read as water sweeping up the sand
    a = a * (0.70 + 0.30 * froth)                # frothy breakup, but stays clearly visible
    a = a * (d_sand > 0.0) * onsand              # only on the sand side
    a = np.clip(a * intensity, 0.0, 1.0)
    rgba[..., 0] = FOAM[0]; rgba[..., 1] = FOAM[1]; rgba[..., 2] = FOAM[2]
    rgba[..., 3] = a * 255.0
    return downsample(rgba.astype(np.uint8))


# Wash-up variants: (max reach px, frame count). A wash advances to max reach
# then recedes; intensity eases in then fades out so it dissolves, not cuts.
# Three reaches (big surge / medium / small lap) give the random sequence variety.
VARIANTS = [(78.0, 11), (52.0, 9), (32.0, 8)]


def build_variants():
    out = []
    for (maxr, nf) in VARIANTS:
        frames = []
        for i in range(nf):
            t = i / (nf - 1)                       # 0..1
            reach = maxr * np.sin(np.pi * t) ** 0.7   # up then down
            inten = np.sin(np.pi * t) ** 0.5          # fade in/out
            frames.append(Image.fromarray(render_wave(reach, inten), 'RGBA'))
        out.append(frames)
    return out


def emit_c(beach_img, variants, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    L = ['// SPDX-License-Identifier: GPL-3.0-only',
         '// GENERATED by tools/gen_pono_beach.py - do not edit by hand.',
         '// Static beach + canned wash-up foam frames (TRUE_COLOR_ALPHA, B,G,R,A).',
         '#include "lvgl.h"', '']

    def emit_img(name, img):
        a = np.asarray(img)
        bgra = a[..., [2, 1, 0, 3]].tobytes()
        L.append('static const uint8_t %s_map[] = {' % name)
        row, out = [], []
        for b in bgra:
            row.append(str(b))
            if len(row) == 64:
                out.append(','.join(row)); row = []
        if row:
            out.append(','.join(row))
        L.append(',\n'.join(out) + ',')
        L.append('};')
        L.append('const lv_img_dsc_t %s = {' % name)
        L.append('  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,')
        L.append('  .header.always_zero = 0, .header.reserved = 0,')
        L.append('  .header.w = %d, .header.h = %d,' % (W, H))
        L.append('  .data_size = %d,' % (W * H * 4))
        L.append('  .data = %s_map,' % name)
        L.append('};')
        L.append('')

    emit_img('pono_beach', beach_img)
    for vi, frames in enumerate(variants):
        for fi, img in enumerate(frames):
            emit_img('pono_wave_v%d_f%d' % (vi, fi), img)
        refs = ', '.join('&pono_wave_v%d_f%d' % (vi, fi) for fi in range(len(frames)))
        L.append('const lv_img_dsc_t * pono_wave_v%d[%d] = { %s };' % (vi, len(frames), refs))
        L.append('')
    vrefs = ', '.join('pono_wave_v%d' % vi for vi in range(len(variants)))
    fcounts = ', '.join(str(len(f)) for f in variants)
    L.append('const lv_img_dsc_t ** pono_wave_variants[%d] = { %s };' % (len(variants), vrefs))
    L.append('const uint8_t pono_wave_variant_frames[%d] = { %s };' % (len(variants), fcounts))
    L.append('const uint8_t pono_wave_variant_count = %d;' % len(variants))
    L.append('const uint16_t pono_beach_w = %d;' % W)
    L.append('const uint16_t pono_beach_h = %d;' % H)
    L.append('')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L))
    total = (1 + sum(len(f) for f in variants)) * W * H * 4
    print('wrote %s (%.1f KB source, %.2f MB rodata, %d wave frames)' %
          (path, os.path.getsize(path) / 1024.0, total / 1048576.0,
           sum(len(f) for f in variants)))


def main():
    beach = Image.fromarray(render_beach(), 'RGBA')
    variants = build_variants()
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, '..', 'assets', 'pono', 'anim', 'pono_beach.c'))
    emit_c(beach, variants, out)
    # previews on a navy ocean-ish bg, for eyeballing the art (not committed)
    if os.environ.get('PONO_BEACH_PREVIEW'):
        bg = Image.new('RGBA', (W, H), (8, 28, 36, 255))
        b = bg.copy(); b.alpha_composite(beach); b.convert('RGB').save(os.path.join(here, 'beach_preview.png'))
        peak = variants[0][len(variants[0]) // 2]   # mid wash-up of variant 0
        c = bg.copy(); c.alpha_composite(beach); c.alpha_composite(peak)
        c.convert('RGB').save(os.path.join(here, 'beach_wave_preview.png'))


if __name__ == '__main__':
    main()
