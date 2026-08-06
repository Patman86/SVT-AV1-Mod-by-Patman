#!/usr/bin/env bash
# Profile-guided (frontend-PGO) RTC minimal build for SVT-AV1 on AArch64/NEON.
#
# Usage:
#   Build/linux/build_rtc_pgo.sh <yuv_folder>
#
#   <yuv_folder>  Directory of training clips used to profile the encoder.
#                 - *.y4m : used directly (dimensions/fps come from the header).
#                 - *.yuv : dimensions are parsed from the filename, which must contain
#                           <W>x<H> (and optionally <N>fps), e.g. clip_640x360_30fps.yuv.
#                           Files without <W>x<H> in the name are skipped.
#
# Tunable via environment (all optional):
#   FRAMES          frames encoded per training run          (default 300)
#   PRESETS         encoder presets to train                 (default "9 11 13")
#   HLS             hierarchical-levels to train             (default "0 1")
#   TBRS            target bitrates (kbps) to train          (default "400")
#   EXTRA_RELFLAGS  extra compile flags for the final build  (default empty)
#   EXTRA_LDFLAGS   extra link flags for the final build     (default empty)
#
# All build artifacts are written under the normal cmake build tree:
#   Build/linux/Release/          cmake build dir (reused per phase)
#   Build/linux/pgo/rtc.profdata  merged profile
#   Build/linux/devirt_gen/*.h    generated NEON devirtualization headers (opt-in optimization)
#   Bin/Release/SvtAv1EncApp      final PGO-optimized RTC minimal encoder
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"   # <src>/Build/linux
SRC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"                  # <src>
BUILD_SH="$SCRIPT_DIR/build.sh"
GEN_DEVIRT="$SRC_ROOT/Build/gen_devirt.py"

YUV_DIR="${1:-}"
[ -n "$YUV_DIR" ] || { echo "usage: $(basename "$0") <yuv_folder>"; exit 2; }
[ -d "$YUV_DIR" ] || { echo "ERROR: not a directory: $YUV_DIR"; exit 2; }
YUV_DIR="$(cd "$YUV_DIR" && pwd -P)"

FRAMES="${FRAMES:-300}"; PRESETS="${PRESETS:-9 11 13}"; HLS="${HLS:-0 1}"; TBRS="${TBRS:-400}"
EXTRA_RELFLAGS="${EXTRA_RELFLAGS:-}"; EXTRA_LDFLAGS="${EXTRA_LDFLAGS:-}"

LLVM_PROFDATA="$(xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata 2>/dev/null || true)"
[ -n "$LLVM_PROFDATA" ] || { echo "ERROR: llvm-profdata not found (need the LLVM/clang toolchain)"; exit 1; }

cd "$SRC_ROOT"
REL="Build/linux/Release"
PGO_DIR="$SRC_ROOT/Build/linux/pgo"
GENDIR="$SRC_ROOT/Build/linux/devirt_gen"
PROFDATA="$PGO_DIR/rtc.profdata"
DEVIRT_D="-DSVT_AOM_DSP_RTCD_DEVIRT_H=$GENDIR/aom_dsp_rtcd_neon_devirt.h -DSVT_COMMON_DSP_RTCD_DEVIRT_H=$GENDIR/common_dsp_rtcd_neon_devirt.h"

gen_devirt() { python3 "$GEN_DEVIRT" --write --outdir "$GENDIR" >/dev/null; }

echo "== PGO 1/4: instrumented RTC-minimal build =="
gen_devirt
rm -rf "$REL"
CFLAGS="-fprofile-instr-generate $DEVIRT_D" \
CXXFLAGS="-fprofile-instr-generate $DEVIRT_D" \
LDFLAGS="-fprofile-instr-generate" \
    "$BUILD_SH" -x --enable-lto --minimal-build --rtc-build
INSTR="$SRC_ROOT/Build/linux/SvtAv1EncApp-instr"
cp "$SRC_ROOT/Bin/Release/SvtAv1EncApp" "$INSTR"

echo "== PGO 2/4: training on $YUV_DIR =="
rm -rf "$PGO_DIR"; mkdir -p "$PGO_DIR"
export LLVM_PROFILE_FILE="$PGO_DIR/%p.profraw"
COMMON="--rtc 1 --lp 1 --rc 2 --keyint 3000 --pred-struct 1 -b /dev/null"
encodes=0; half=0
shopt -s nullglob
clips=( "$YUV_DIR"/*.y4m "$YUV_DIR"/*.yuv )
[ "${#clips[@]}" -gt 0 ] || { echo "ERROR: no .y4m/.yuv clips in $YUV_DIR"; exit 1; }
for clip in "${clips[@]}"; do
    dim=""
    if [[ "$clip" == *.yuv ]]; then
        bn="$(basename "$clip")"
        if [[ "$bn" =~ ([0-9]+)x([0-9]+) ]]; then
            w="${BASH_REMATCH[1]}"; h="${BASH_REMATCH[2]}"; fps=30
            [[ "$bn" =~ ([0-9]+)fps ]] && fps="${BASH_REMATCH[1]}"
            dim="--width $w --height $h --fps-num $fps --fps-denom 1"
        else
            echo "  skip (no <W>x<H> in filename): $bn"; continue
        fi
    fi
    for p in $PRESETS; do for hl in $HLS; do for tbr in $TBRS; do
        # Alternate the NEON / NEON-dotprod code paths so both are represented in the profile.
        asm=""; [ $((half % 2)) -eq 0 ] && asm="--asm neon"; half=$((half + 1))
        "$INSTR" -i "$clip" $COMMON $dim --hierarchical-levels "$hl" --preset "$p" --tbr "$tbr" --frames "$FRAMES" $asm 2>/dev/null || true
        encodes=$((encodes + 1))
    done; done; done
done
unset LLVM_PROFILE_FILE
raw=$(ls "$PGO_DIR"/*.profraw 2>/dev/null | wc -l | tr -d ' ')
echo "  encodes=$encodes profraw=$raw"
[ "$raw" -gt 0 ] || { echo "ERROR: no profile data produced"; exit 1; }

echo "== PGO 3/4: merge profile -> $PROFDATA =="
"$LLVM_PROFDATA" merge --sparse "$PGO_DIR"/*.profraw -o "$PROFDATA"

echo "== PGO 4/4: optimized RTC-minimal build =="
gen_devirt
rm -rf "$REL"
CFLAGS="-fprofile-instr-use=$PROFDATA -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled $DEVIRT_D $EXTRA_RELFLAGS" \
CXXFLAGS="-fprofile-instr-use=$PROFDATA -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled $DEVIRT_D $EXTRA_RELFLAGS" \
LDFLAGS="-fprofile-instr-use=$PROFDATA $EXTRA_LDFLAGS" \
    "$BUILD_SH" -x --enable-lto --minimal-build --rtc-build

BIN="$SRC_ROOT/Bin/Release/SvtAv1EncApp"
echo "DONE: $BIN"
size -m "$BIN" 2>/dev/null | awk '/Section __text/{print "  __text="$3" B"}' || true
