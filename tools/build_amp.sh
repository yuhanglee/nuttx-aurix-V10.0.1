#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# tools/build_amp.sh -- build the six per-CPU NuttX images for an
# AURIX AMP setup.  Outputs:
#
#   <outdir>/nuttx-cpu0.elf   (full NSH on shared ASCLIN0)
#   <outdir>/nuttx-cpu1.elf   (hello + idle on shared ASCLIN0)
#   ... up to nuttx-cpu5.elf
#
# Usage:
#   tools/build_amp.sh <board> [outdir]
#
#     <board>  = tc4d7  (alias for a3g-tc4d7-lite)
#                tc397  (alias for a2g-tc397xa-3v3-tft)
#     outdir   = output directory (default: ./amp-out-<board>)
#
# Required environment:
#   RLM_LICENSE   for HighTec LLVM
#   PATH          must include the toolchain bin dir, e.g.
#                 /opt/tooling/tricore-llvm/toolchains/tricore/v10.2.0/bin
#                 (and /opt/tooling/tricore-gcc/bin for tricore-elf-gcc deps)

set -euo pipefail

board="${1:-}"
case "$board" in
  tc4d7) board_full="a3g-tc4d7-lite" ;;
  tc397) board_full="a2g-tc397xa-3v3-tft" ;;
  ""|-h|--help)
    sed -n '3,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
  *) echo "unknown board: $board (try tc4d7 | tc397)" >&2; exit 2 ;;
esac

outdir="${2:-amp-out-$board}"
mkdir -p "$outdir"
outdir="$(realpath "$outdir")"

cd "$(dirname "$0")/.."   # repo root

NCPUS="${NCPUS:-6}"
TC_PATH="/opt/tooling/tricore-llvm/toolchains/tricore/v10.2.0"

echo "===== Build CPU 0 ($board_full:amp, NSH) ====="
make distclean -j32 >/dev/null 2>&1
./tools/configure.sh -E "$board_full:amp" >/dev/null
sed -i "s|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH=.*|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH=\"$TC_PATH\"|" .config
EXTRAFLAGS="-O2 -g3 -DCPU=0" make -j32 2>&1 | tail -3
cp nuttx "$outdir/nuttx-cpu0.elf"

for cpu in $(seq 1 $((NCPUS - 1))); do
  echo "===== Build CPU $cpu ($board_full:amp_secondary) ====="
  make distclean -j32 >/dev/null 2>&1
  ./tools/configure.sh -E "$board_full:amp_secondary" >/dev/null
  sed -i "s|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH=.*|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH=\"$TC_PATH\"|" .config
  EXTRAFLAGS="-O2 -g3 -DCPU=$cpu" make -j32 2>&1 | tail -3
  cp nuttx "$outdir/nuttx-cpu$cpu.elf"
done

echo
echo "Done.  Per-CPU images in $outdir:"
ls -la "$outdir"/nuttx-cpu*.elf
