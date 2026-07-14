#!/bin/bash
#
# Build EB Garamond + Source Han Serif .cpfont files for SD card use, one
# locale-specific family at a time:
#
#   EBGaramondSHS-TC  — Source Han Serif TW regional SubsetOTF
#   EBGaramondSHS-SC  — Source Han Serif CN regional SubsetOTF
#   EBGaramondSHS-JA  — Source Han Serif JP regional SubsetOTF
#   EBGaramondSHS-KO  — Source Han Serif KR regional SubsetOTF (+ Hangul)
#
# CJK coverage comes from Adobe's regional SubsetOTF as-is (no second-pass
# charset trim). fontconvert keeps every glyph present in that face that falls
# inside the locale intervals. Firmware SC↔TC remaps still apply at runtime
# for cross-orthography EPUB text.
#
# Layout (all locales):
#   Latin  — EB Garamond Regular / Bold / Italic / BoldItalic
#   CJK    — weight-matched Source Han Serif Regular / Bold
#            (italic Han falls back to regular at runtime)
#
# Usage:
#   bash build-ebgaramond-cjk-sd.sh              # all locales
#   bash build-ebgaramond-cjk-sd.sh tc sc        # selected locales
#   LOCALES=ja,ko bash build-ebgaramond-cjk-sd.sh
#
# Prerequisites:
#   pip install freetype-py fonttools
#
# Source fonts default to lib/EpdFont/scripts/source_fonts/ (vendored).
# Override with env vars if needed:
#   SOURCE_FONTS_DIR=/path/to/source_fonts
#   EB_GARAMOND_DIR=/path/to/EBGaramond
#   SHS_TC_DIR=... SHS_SC_DIR=... SHS_JA_DIR=... SHS_KO_DIR=...
#   PYTHON=/path/to/venv/bin/python bash build-ebgaramond-cjk-sd.sh

set -euo pipefail

cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
FONTCONVERT="$PWD/fontconvert_sdcard.py"

SOURCE_FONTS_DIR="${SOURCE_FONTS_DIR:-$PWD/source_fonts}"
EB_GARAMOND_DIR="${EB_GARAMOND_DIR:-$SOURCE_FONTS_DIR/EBGaramond}"
SHS_TC_DIR="${SHS_TC_DIR:-$SOURCE_FONTS_DIR/SourceHanSerifTW}"
SHS_SC_DIR="${SHS_SC_DIR:-$SOURCE_FONTS_DIR/SourceHanSerifCN}"
SHS_JA_DIR="${SHS_JA_DIR:-$SOURCE_FONTS_DIR/SourceHanSerifJP}"
SHS_KO_DIR="${SHS_KO_DIR:-$SOURCE_FONTS_DIR/SourceHanSerifKR}"

EB_REGULAR="$EB_GARAMOND_DIR/EBGaramond-Regular.ttf"
EB_BOLD="$EB_GARAMOND_DIR/EBGaramond-Bold.ttf"
EB_ITALIC="$EB_GARAMOND_DIR/EBGaramond-Italic.ttf"
EB_BOLDITALIC="$EB_GARAMOND_DIR/EBGaramond-BoldItalic.ttf"

# Regional SubsetOTF already carries the language-appropriate glyph set.
# Intervals below only select which of those glyphs get rasterized into .cpfont.
LATIN_INTERVALS="latin-ext,greek,symbols,(0x0413-0x0413),(0x2030-0x205F),(0x2122-0x2122),(0x2460-0x24FF),(0x2580-0x259F)"
CJK_INTERVALS_BASE="latin-ext,greek,cjk,symbols,(0x0413-0x0413),(0x2030-0x205F),(0x2122-0x2122),(0x2460-0x24FF),(0x2580-0x259F),(0xFE10-0xFE19),(0xFE30-0xFE48),(0xFE50-0xFE6F)"
CJK_INTERVALS_TC="${CJK_INTERVALS_BASE},(0x3100-0x312F)"
CJK_INTERVALS_SC="$CJK_INTERVALS_BASE"
CJK_INTERVALS_JA="$CJK_INTERVALS_BASE"
CJK_INTERVALS_KO="${CJK_INTERVALS_BASE},hangul"

usage() {
  cat <<'EOF' >&2
Usage: build-ebgaramond-cjk-sd.sh [locale ...]

Locales: tc sc ja ko  (default: all)

Examples:
  bash build-ebgaramond-cjk-sd.sh
  bash build-ebgaramond-cjk-sd.sh tc
  LOCALES=sc,ja bash build-ebgaramond-cjk-sd.sh
EOF
}

normalize_locales() {
  local raw=("$@")
  local out=()
  local token
  if [ "${#raw[@]}" -eq 0 ]; then
    if [ -n "${LOCALES:-}" ]; then
      IFS=',' read -r -a raw <<< "$LOCALES"
    else
      raw=(tc sc ja ko)
    fi
  fi
  for token in "${raw[@]}"; do
    token="$(printf '%s' "$token" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
    case "$token" in
      "" ) continue ;;
      -h|--help|help)
        # Must not exit from a process-substitution subshell.
        echo "__HELP__"
        return 0
        ;;
      tc|sc|ja|ko) out+=("$token") ;;
      *)
        echo "Error: unknown locale '$token' (expected: tc sc ja ko)" >&2
        usage
        exit 1
        ;;
    esac
  done
  if [ "${#out[@]}" -eq 0 ]; then
    echo "Error: no locales selected" >&2
    usage
    exit 1
  fi
  printf '%s\n' "${out[@]}"
}

build_locale() {
  local locale="$1"
  local font_name shs_regular shs_bold cjk_intervals output_dir

  case "$locale" in
    tc)
      font_name="EBGaramondSHS-TC"
      shs_regular="$SHS_TC_DIR/SourceHanSerifTW-Regular.otf"
      shs_bold="$SHS_TC_DIR/SourceHanSerifTW-Bold.otf"
      cjk_intervals="$CJK_INTERVALS_TC"
      ;;
    sc)
      font_name="EBGaramondSHS-SC"
      shs_regular="$SHS_SC_DIR/SourceHanSerifCN-Regular.otf"
      shs_bold="$SHS_SC_DIR/SourceHanSerifCN-Bold.otf"
      cjk_intervals="$CJK_INTERVALS_SC"
      ;;
    ja)
      font_name="EBGaramondSHS-JA"
      shs_regular="$SHS_JA_DIR/SourceHanSerifJP-Regular.otf"
      shs_bold="$SHS_JA_DIR/SourceHanSerifJP-Bold.otf"
      cjk_intervals="$CJK_INTERVALS_JA"
      ;;
    ko)
      font_name="EBGaramondSHS-KO"
      shs_regular="$SHS_KO_DIR/SourceHanSerifKR-Regular.otf"
      shs_bold="$SHS_KO_DIR/SourceHanSerifKR-Bold.otf"
      cjk_intervals="$CJK_INTERVALS_KO"
      ;;
    *)
      echo "Error: internal unknown locale '$locale'" >&2
      exit 1
      ;;
  esac

  output_dir="output/$font_name"

  for f in "$EB_REGULAR" "$EB_BOLD" "$EB_ITALIC" "$EB_BOLDITALIC" \
    "$shs_regular" "$shs_bold"; do
    if [ ! -f "$f" ]; then
      echo "Error: required file not found: $f" >&2
      exit 1
    fi
  done

  mkdir -p "$output_dir"

  echo ""
  echo "=== Building $font_name ($locale) ==="
  echo "  SHS Regular: $shs_regular"
  echo "  SHS Bold:    $shs_bold"
  echo "Generating .cpfont files into $output_dir ..."
  # Pass regional SubsetOTF faces through unchanged; fontconvert drops
  # codepoints absent from the face when validating intervals.
  "$PYTHON" "$FONTCONVERT" \
    --regular "$EB_REGULAR" \
    --bold "$EB_BOLD" \
    --italic "$EB_ITALIC" \
    --bolditalic "$EB_BOLDITALIC" \
    --fallback-regular "$shs_regular" \
    --fallback-bold "$shs_bold" \
    --fallback-bolditalic "$shs_bold" \
    --intervals "$LATIN_INTERVALS" \
    --regular-intervals "$cjk_intervals" \
    --bold-intervals "$cjk_intervals" \
    --bolditalic-intervals "$cjk_intervals" \
    --sizes 12,14,16,18 \
    --name "$font_name" \
    --output-dir "$output_dir/"

  echo "Done: $font_name"
  echo "  mkdir -p <sd>/.fonts/$font_name"
  echo "  cp $output_dir/${font_name}_*.cpfont <sd>/.fonts/$font_name/"
  ls -lh "$output_dir"/*.cpfont
}

SELECTED_LOCALES=()
while IFS= read -r locale; do
  if [ "$locale" = "__HELP__" ]; then
    usage
    exit 0
  fi
  SELECTED_LOCALES+=("$locale")
done < <(normalize_locales "$@")

if [ "${#SELECTED_LOCALES[@]}" -eq 0 ]; then
  echo "Error: no locales selected" >&2
  usage
  exit 1
fi

echo "Locales: ${SELECTED_LOCALES[*]}"
echo "Source fonts: $SOURCE_FONTS_DIR"
echo "EB Garamond: $EB_GARAMOND_DIR"
echo "SHS TW/CN/JP/KO: $SHS_TC_DIR | $SHS_SC_DIR | $SHS_JA_DIR | $SHS_KO_DIR"

for locale in "${SELECTED_LOCALES[@]}"; do
  build_locale "$locale"
done

echo ""
echo "All requested locales finished."
