#!/usr/bin/env bash
# regen_pono_fonts.sh - Regenerate Pono Print font .c files from TTF source.
#
# Night Watch face set (pono-design canon): IBM Plex Mono carries every
# label, readout, and numeral (the instrument lettering); Instrument Serif
# carries the two logbook moments (the pono dictionary entry on the idle
# cockpit, the dedication + motto on the boot screen). The Inter + JetBrains
# Mono set this replaces was the pre-canon palette.
#
# Requires Node.js + lv_font_conv:
#   npm install -g lv_font_conv
#
# Source TTFs at fonts/src/ (OFL, vendored from google/fonts):
#   fonts/src/IBMPlexMono-Regular.ttf
#   fonts/src/IBMPlexMono-Medium.ttf
#   fonts/src/IBMPlexMono-SemiBold.ttf
#   fonts/src/InstrumentSerif-Regular.ttf
#   fonts/src/InstrumentSerif-Italic.ttf
#
# Output lands in assets/pono/fonts/ as compilable .c files. Commit those
# to the fork; they're build inputs, not build artifacts.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${REPO_ROOT}/fonts/src"
OUT_DIR="${REPO_ROOT}/assets/pono/fonts"

mkdir -p "${OUT_DIR}"

if ! command -v lv_font_conv > /dev/null ; then
    echo "regen_pono_fonts: lv_font_conv not found on PATH." >&2
    echo "  install with: npm install -g lv_font_conv" >&2
    exit 1
fi

ASCII="0x20-0x7F"
# Serif faces also carry the Hawaiian orthography for the motto + entry:
# the five macron vowel pairs, plus the curly quotes. Instrument Serif has
# no U+02BB okina; the left single quotation mark (U+2018) is the standard
# typographic stand-in (same turned-comma shape), so motto strings use it.
HAWAIIAN="0x20-0x7F,0x100-0x101,0x12A-0x12B,0x14C-0x14D,0x16A-0x16B,0x2018-0x2019"

gen() {
    local ttf="$1" out="$2" size="$3" range="$4"
    echo "  ${ttf} ${size} -> ${out}"
    lv_font_conv \
        --bpp 4 --size "${size}" \
        --font "${SRC_DIR}/${ttf}" \
        -r "${range}" \
        --format lvgl --no-compress \
        --output "${OUT_DIR}/${out}"
}

echo "Generating Pono Print fonts to ${OUT_DIR}/"

# IBM Plex Mono: the instrument lettering.
gen IBMPlexMono-Regular.ttf  plex_regular_14.c  14 "${ASCII}"   # font_body + font_num_small
gen IBMPlexMono-Medium.ttf   plex_medium_10.c   10 "${ASCII}"   # font_micro (letterspaced caps tags)
gen IBMPlexMono-Medium.ttf   plex_medium_12.c   12 "${ASCII}"   # font_caption
gen IBMPlexMono-Medium.ttf   plex_medium_20.c   20 "${ASCII}"   # font_h2 (screen titles)
gen IBMPlexMono-SemiBold.ttf plex_semibold_20.c 20 "${ASCII}"   # font_num_medium
gen IBMPlexMono-SemiBold.ttf plex_semibold_32.c 32 "${ASCII}"   # font_num_large

# Instrument Serif: the logbook voice, used sparingly.
gen InstrumentSerif-Regular.ttf serif_regular_40.c 40 "${HAWAIIAN}"  # the pono headword
gen InstrumentSerif-Italic.ttf  serif_italic_18.c  18 "${HAWAIIAN}"  # gloss / dedication / motto

echo "Done. Commit the generated .c files to the fork."
