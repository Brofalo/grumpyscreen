# SPDX-License-Identifier: GPL-3.0-only
# gen_pono_flag.py - bake the Hawaii state flag into LVGL images.
#
# Two outputs, same render pipeline (one supersampled flat flag, downscaled):
#   assets/pono/anim/pono_flag.c       192x96  STATIC  (home top bar via zoom)
#   assets/pono/anim/pono_flag_boot.c  320x160 FRAMES  (the boot screen, flying)
#
# The boot flag flies (Jack 2026-06-13): a canned flipbook, kept slight on
# purpose so you half-doubt it moves at all. Each frame is a straight bitmap
# blit, no per-frame vector math, so a 2-core A7 with no GPU stays smooth. The
# motion is a traveling vertical-shear wave - each column shifts up/down by a
# few px following a sine that grows from zero at the hoist (left, where the
# cloth is pinned) to the fly (right). The phase sweeps a whole 2*pi over the
# frame set, so the loop closes on itself with no jump.
#   const lv_img_dsc_t * pono_flag_boot_frames[N];   // TRUE_COLOR_ALPHA, B,G,R,A
#   const uint8_t        pono_flag_boot_frame_count;
#   const uint16_t       pono_flag_boot_w / _h;
#
# The Hawaii flag: eight horizontal stripes (top->bottom white, red, blue,
# white, red, blue, white, red - one per island) with the British Union Jack in
# the upper-hoist quadrant. Supersampled then LANCZOS-downscaled so the saltire
# diagonals and cross fimbriation stay clean.
#
# Eyeball before wiring:  PONO_FLAG_PREVIEW=1 python tools/gen_pono_flag.py
# Regenerate after any geometry/wave change; result is committed.

import os
import numpy as np
from PIL import Image

W = 192            # home-bar flag width  (2:1 = correct Hawaii proportion)
H = 96             # home-bar flag height
BOOT_W = 320       # boot-screen flag (the hero that flies); 2:1, dominant at 480 wide
BOOT_H = 160
SS = 4             # supersample for clean diagonals

# --- wave (the boot flag only) ----------------------------------------------
WAVE_FRAMES = 36   # one full loop; at 60 fps that is a 600 ms cycle
WAVE_AMP_PX = 2.0  # max vertical shift at the fly, in on-screen px. Tiny on purpose.
WAVE_COUNT  = 2.0  # crests across the width (a flag, not a ripple tank)
WAVE_RAMP   = 1.35 # amplitude envelope hoist->fly (pinned at the hoist, free at the fly)
WAVE_SHADE  = 0.10 # fold lighting: brighten the up-slopes, shade the down-slopes

# Flag palette, nudged a touch brighter than spec so it reads on the dark UI.
WHITE = np.array([245, 245, 248], np.float32)
RED   = np.array([206,  26,  54], np.float32)
BLUE  = np.array([ 12,  40, 122], np.float32)

# Eight stripes, top -> bottom (one per main island).
STRIPES = [WHITE, RED, BLUE, WHITE, RED, BLUE, WHITE, RED]


def render_union_jack(cw, ch):
    """Union Jack into a cw x ch (2:1) field. Blue base, white St Andrew
    saltire, counterchanged red St Patrick saltire, white-fimbriated red
    St George cross on top."""
    yy, xx = np.mgrid[0:ch, 0:cw].astype(np.float32)
    field = np.empty((ch, cw, 3), np.float32)
    field[:] = BLUE

    def perp_dist(p0, p1):
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        L = float(np.hypot(dx, dy))
        nx, ny = -dy / L, dx / L
        return (xx - p0[0]) * nx + (yy - p0[1]) * ny   # signed perpendicular

    d1 = perp_dist((0, 0), (cw, ch))        # main diagonal  TL -> BR
    d2 = perp_dist((0, ch), (cw, 0))        # anti diagonal  BL -> TR

    white_hw = ch * (1.0 / 6.0)             # half-width of the white saltire arm
    red_hw = ch * (1.0 / 15.0)              # half-width of the red saltire arm
    off = white_hw - red_hw                 # push red to one edge of the white

    sal_white = (np.abs(d1) < white_hw) | (np.abs(d2) < white_hw)
    field[sal_white] = WHITE

    # Counterchange: the red hugs opposite edges either side of centre so the
    # saltire pinwheels (the canonical Union Jack look).
    left = xx < cw / 2.0
    red1 = ((np.abs(d1 - off) < red_hw) & ~left) | ((np.abs(d1 + off) < red_hw) & left)
    red2 = ((np.abs(d2 - off) < red_hw) & left) | ((np.abs(d2 + off) < red_hw) & ~left)
    field[(red1 | red2) & sal_white] = RED

    gx_half = ch * 0.10                      # St George arm half-width (1/5 total)
    fimb = ch * (1.0 / 15.0)                 # white fimbriation each side
    vert = np.abs(xx - cw / 2.0)
    horz = np.abs(yy - ch / 2.0)
    field[(vert < gx_half + fimb) | (horz < gx_half + fimb)] = WHITE
    field[(vert < gx_half) | (horz < gx_half)] = RED
    return field


def render_flag_ss(w, h, ss=SS):
    """The flat flag at supersample resolution (hs x ws x 3 float)."""
    ws, hs = w * ss, h * ss
    img = np.zeros((hs, ws, 3), np.float32)
    sh = hs / 8.0
    for i, col in enumerate(STRIPES):
        y0 = int(round(i * sh))
        y1 = int(round((i + 1) * sh))
        img[y0:y1, :, :] = col
    cw, ch = ws // 2, hs // 2               # canton = upper-hoist quadrant
    img[0:ch, 0:cw, :] = render_union_jack(cw, ch)
    return img


def downscale(ss_img, w, h):
    return np.asarray(Image.fromarray(np.clip(ss_img, 0, 255).astype(np.uint8),
                                      'RGB').resize((w, h), Image.LANCZOS))


def wave_frame(base_ss, w, h, ss, phase):
    """Displace the flat flag by the traveling vertical-shear wave at `phase`,
    shade the folds, and downscale to (w, h). Returns (h, w, 3) uint8."""
    hs, ws, _ = base_ss.shape
    u = (np.arange(ws, dtype=np.float32) + 0.5) / ws            # 0..1 hoist->fly
    env = (WAVE_AMP_PX * ss) * np.power(u, WAVE_RAMP)           # SS-px amplitude
    sy = env * np.sin(WAVE_COUNT * 2.0 * np.pi * u - phase)     # per-column shift
    yy = np.arange(hs, dtype=np.float32)[:, None]
    srcy = np.clip(yy - sy[None, :], 0.0, hs - 1.001)           # sample source row
    y0 = np.floor(srcy).astype(np.int32)
    fr = (srcy - y0)[..., None]
    xidx = np.arange(ws)[None, :]
    out = base_ss[y0, xidx] * (1.0 - fr) + base_ss[np.minimum(y0 + 1, hs - 1), xidx] * fr
    if WAVE_SHADE > 0.0:
        slope = np.gradient(sy)                                 # local cloth tilt
        sh = 1.0 + WAVE_SHADE * np.clip(slope / (WAVE_AMP_PX * ss + 1e-3), -1.0, 1.0)
        out = out * sh[None, :, None]
    return downscale(out, w, h)


def _byte_rows(buf):
    row, out = [], []
    for b in buf:
        row.append(str(b))
        if len(row) == 64:
            out.append(','.join(row)); row = []
    if row:
        out.append(','.join(row))
    return ',\n'.join(out) + ','


def _dsc(sym, w, h):
    return ['const lv_img_dsc_t %s = {' % sym,
            '  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,',
            '  .header.always_zero = 0, .header.reserved = 0,',
            '  .header.w = %d, .header.h = %d,' % (w, h),
            '  .data_size = %d,' % (w * h * 4),
            '  .data = %s_map,' % sym,
            '};']


def emit_static(rgb, path, sym, w, h):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rgba = np.dstack([rgb, np.full((h, w), 255, np.uint8)])     # opaque
    bgra = rgba[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()
    L = ['// SPDX-License-Identifier: GPL-3.0-only',
         '// GENERATED by tools/gen_pono_flag.py - do not edit by hand.',
         '// Hawaii state flag, static (TRUE_COLOR_ALPHA, B,G,R,A, fully opaque).',
         '#include "lvgl.h"', '',
         'static const uint8_t %s_map[] = {' % sym, _byte_rows(bgra), '};']
    L += _dsc(sym, w, h)
    L += ['const uint16_t %s_w = %d;' % (sym, w),
          'const uint16_t %s_h = %d;' % (sym, h), '']
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L))
    print('wrote %s (%.1f KB source, %d B rodata)' %
          (path, os.path.getsize(path) / 1024.0, w * h * 4))


def emit_frames(frames, path, sym, w, h):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    n = len(frames)
    L = ['// SPDX-License-Identifier: GPL-3.0-only',
         '// GENERATED by tools/gen_pono_flag.py - do not edit by hand.',
         '// Hawaii state flag flying: a looping traveling-wave flipbook for',
         '// lv_animimg (TRUE_COLOR_ALPHA, B,G,R,A, fully opaque). Subtle on',
         '// purpose; one loop = %d frames (600 ms at 60 fps).' % n,
         '#include "lvgl.h"', '']
    for i, rgb in enumerate(frames):
        rgba = np.dstack([rgb, np.full((h, w), 255, np.uint8)])
        bgra = rgba[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()
        L.append('static const uint8_t %s_f%02d_map[] = {' % (sym, i))
        L.append(_byte_rows(bgra))
        L.append('};')
        L += _dsc('%s_f%02d' % (sym, i), w, h)
        L.append('')
    refs = ', '.join('&%s_f%02d' % (sym, i) for i in range(n))
    L.append('const lv_img_dsc_t * %s_frames[%d] = { %s };' % (sym, n, refs))
    L.append('const uint8_t %s_frame_count = %d;' % (sym, n))
    L.append('const uint16_t %s_w = %d;' % (sym, w))
    L.append('const uint16_t %s_h = %d;' % (sym, h))
    L.append('')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L))
    print('wrote %s (%d frames, %.1f KB source, %.2f MB rodata)' %
          (path, n, os.path.getsize(path) / 1024.0, n * w * h * 4 / 1048576.0))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    anim = os.path.normpath(os.path.join(here, '..', 'assets', 'pono', 'anim'))

    # Home-bar flag: static.
    emit_static(downscale(render_flag_ss(W, H), W, H),
                os.path.join(anim, 'pono_flag.c'), 'pono_flag', W, H)

    # Boot flag: flying. One supersampled flat flag, displaced per frame.
    base = render_flag_ss(BOOT_W, BOOT_H)
    frames = [wave_frame(base, BOOT_W, BOOT_H, SS, 2.0 * np.pi * i / WAVE_FRAMES)
              for i in range(WAVE_FRAMES)]
    emit_frames(frames, os.path.join(anim, 'pono_flag_boot.c'),
                'pono_flag_boot', BOOT_W, BOOT_H)

    if os.environ.get('PONO_FLAG_PREVIEW'):
        bg = Image.new('RGB', (W + 24, H + 24), (16, 20, 28))
        bg.paste(Image.fromarray(downscale(render_flag_ss(W, H), W, H), 'RGB'), (12, 12))
        bg.resize(((W + 24) * 4, (H + 24) * 4), Image.NEAREST)\
          .save(os.path.join(here, 'flag_preview.png'))
        # A contact sheet of the wave loop, to eyeball the flutter.
        cols = 6
        rows = (WAVE_FRAMES + cols - 1) // cols
        sheet = Image.new('RGB', (cols * (BOOT_W + 8) + 8, rows * (BOOT_H + 8) + 8), (13, 11, 9))
        for i, fr in enumerate(frames):
            r, c = divmod(i, cols)
            sheet.paste(Image.fromarray(fr, 'RGB'), (8 + c * (BOOT_W + 8), 8 + r * (BOOT_H + 8)))
        sheet.save(os.path.join(here, 'flag_boot_preview.png'))
        # An animated GIF of the loop (the real proof of the subtle flutter).
        Image.fromarray(frames[0]).save(
            os.path.join(here, 'flag_boot_loop.gif'), save_all=True,
            append_images=[Image.fromarray(f) for f in frames[1:]],
            duration=int(1000 * WAVE_FRAMES / 60 / WAVE_FRAMES), loop=0, optimize=False)
        print('wrote flag_preview.png + flag_boot_preview.png + flag_boot_loop.gif')


if __name__ == '__main__':
    main()
