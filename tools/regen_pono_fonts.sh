#!/usr/bin/env bash
# regen_pono_fonts.sh — Regenerate Pono Print font .c files from TTF source.
#
# Phase A.2 per docs/design/pono-print-phase-a-technical-brief.md.
#
# Requires Node.js + lv_font_conv:
#   npm install -g lv_font_conv
#
# Source TTFs should be at fonts/src/ at the fork repo root:
#   fonts/src/Inter-Regular.ttf
#   fonts/src/Inter-Medium.ttf
#   fonts/src/Inter-Bold.ttf
#   fonts/src/JetBrainsMono-Regular.ttf
#   fonts/src/JetBrainsMono-Bold.ttf
#
# Output lands in assets/pono/fonts/ as compilable .c files. Commit those
# to the fork; they're build inputs, not build artifacts.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${REPO_ROOT}/fonts/src"
OUT_DIR="${REPO_ROOT}/assets/pono/fonts"

mkdir -p "${OUT_DIR}"

# Verify lv_font_conv on PATH.
if ! command -v lv_font_conv > /dev/null ; then
    echo "regen_pono_fonts: lv_font_conv not found on PATH." >&2
    echo "  install with: npm install -g lv_font_conv" >&2
    exit 1
fi

# ---- Inter (sans-serif UI text per spec §1.2) ----
# Sizes 10/12/14 use the lighter weights; 20/28 use Bold; 48 is display Bold.
gen_inter() {
    local weight="$1"     # Regular | Medium | Bold
    local size="$2"
    local out="${OUT_DIR}/inter_${weight,,}_${size}.c"
    echo "  inter ${weight} ${size} -> ${out}"
    lv_font_conv \
        --bpp 4 --size "${size}" \
        --font "${SRC_DIR}/Inter-${weight}.ttf" \
        -r 0x20-0x7F \
        --format lvgl --no-compress \
        --output "${out}"
}

# ---- JetBrains Mono (mono for numbers per spec §1.2) ----
gen_jbm() {
    local weight="$1"     # Regular | Bold
    local size="$2"
    local out="${OUT_DIR}/jbm_${weight,,}_${size}.c"
    echo "  jbm ${weight} ${size} -> ${out}"
    lv_font_conv \
        --bpp 4 --size "${size}" \
        --font "${SRC_DIR}/JetBrainsMono-${weight}.ttf" \
        -r 0x20-0x7F \
        --format lvgl --no-compress \
        --output "${out}"
}

echo "Generating Pono Print fonts to ${OUT_DIR}/"

# Inter
gen_inter Regular 14   # font_body
gen_inter Medium  10   # font_micro
gen_inter Medium  12   # font_caption
gen_inter Bold    20   # font_h2
gen_inter Bold    28   # font_h1
gen_inter Bold    48   # font_display

# JBM
gen_jbm   Regular 14   # font_num_small
gen_jbm   Bold    20   # font_num_medium
gen_jbm   Bold    32   # font_num_large

echo "Done. Commit the generated .c files to the fork."
echo
echo "i18n note: range is ASCII only (0x20-0x7F)."
echo "  For Japanese (Phase G8), add a second pass at bpp 2 with kanji range,"
echo "  e.g. -r 0x4E00-0x9FFF for CJK Unified Ideographs."
