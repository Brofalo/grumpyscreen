# SPDX-License-Identifier: GPL-3.0-only
# Render the spinner across one revolution and assemble an animated GIF plus a
# contact sheet, by calling pono-headless.exe at successive advance_ms offsets.
import subprocess, os
from PIL import Image

SIM = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(SIM, 'pono-headless.exe')
TMP = os.path.join(SIM, '_gif')
os.makedirs(TMP, exist_ok=True)
N = 24
frames = []
for i in range(N):
    ms = round(i * 1000 / N)
    bmp = os.path.join(TMP, 'f%02d.bmp' % i)
    subprocess.run([EXE, bmp, str(ms), 'spinner'], check=True, capture_output=True)
    frames.append(Image.open(bmp).convert('RGB'))

gif = os.path.join(SIM, 'spinner.gif')
frames[0].save(gif, save_all=True, append_images=frames[1:], duration=42, loop=0)
print('wrote', gif, len(frames), 'frames')

# contact sheet: 8 frames, cropped to the spinner, tiled 4x2
cx, cy, h = 240, 118, 88
box = (cx - h, cy - h, cx + h, cy + h)  # 176x176
sample = [frames[i] for i in range(0, N, N // 8)][:8]
cells = [f.crop(box) for f in sample]
cw = ch = 176
sheet = Image.new('RGB', (cw * 4, ch * 2), (10, 14, 23))
for idx, c in enumerate(cells):
    sheet.paste(c, ((idx % 4) * cw, (idx // 4) * ch))
sheet_path = os.path.join(SIM, 'spinner_contact.png')
sheet.save(sheet_path)
print('wrote', sheet_path)
