#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Board-aware wrapper around tools/winidea/flash.py.
#
# Usage:
#   tools/winidea/flash.sh tc4d7        [path/to/nuttx]
#   tools/winidea/flash.sh tc397        [path/to/nuttx]
#   tools/winidea/flash.sh tc375        [path/to/nuttx]
#   tools/winidea/flash.sh triboard     [path/to/nuttx]
#   tools/winidea/flash.sh amp tc4d7    [dir-with-nuttx-cpu{0..5}.elf]
#   tools/winidea/flash.sh amp tc397    [dir-with-nuttx-cpu{0..5}.elf]
#
# The first positional selects the lab kit (or 'amp' + kit); the next is
# the ELF (single-CPU) or the directory of 6 per-CPU ELFs (amp).  For
# single-CPU when the path is omitted, ./nuttx in the repo root is used.
#
# Pass-through flags (everything after the elf path) go straight to
# flash.py: e.g. --no-scp, --no-run, --watch-seconds 30, -v.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
flash_py="$here/flash.py"

board="${1:-}"
shift || true

case "$board" in
  tc4d7)
    instance="com.tasking.winIDEA.instance.id-TC4D7"
    remote="nuttx-tc4x.out"
    console_port=9001
    elf="${1:-$here/../../nuttx}"
    ;;
  tc397)
    instance="com.tasking.winIDEA.instance.id-TC397"
    remote="nuttx-tc3x.out"
    console_port=9000
    elf="${1:-$here/../../nuttx}"
    ;;
  tc375)
    instance="${TC375_WINIDEA_INSTANCE:-com.tasking.winIDEA.instance.id-TC375}"
    remote="nuttx-tc375.out"
    console_port="${TC375_CONSOLE_PORT:-9004}"
    elf="${1:-$here/../../nuttx}"
    ;;
  triboard)
    instance="com.tasking.winIDEA.instance.id-TC4D7 - Triboard"
    remote="nuttx-tc4x7-com.out"
    console_port=9002
    elf="${1:-$here/../../nuttx}"
    ;;
  amp)
    # AMP is per-CPU: 6 ELFs flashed sequentially on the kit's instance.
    amp_kit="${1:-tc4d7}"; shift || true
    case "$amp_kit" in
      tc4d7)
        amp_instance="com.tasking.winIDEA.instance.id-TC4D7"
        amp_console_port=9001
        amp_remote_prefix="nuttx-tc4x-cpu"
        ;;
      tc397)
        amp_instance="com.tasking.winIDEA.instance.id-TC397"
        amp_console_port=9000
        amp_remote_prefix="nuttx-tc3x-cpu"
        ;;
      *) echo "amp: unknown kit '$amp_kit' (try tc4d7 | tc397)" >&2; exit 2 ;;
    esac
    dir="${1:-/tmp/amp-$amp_kit}"; shift || true
    for cpu in 0 1 2 3 4 5; do
      "$flash_py" "$dir/nuttx-cpu${cpu}.elf" \
        --instance-id "$amp_instance" \
        --remote-name "${amp_remote_prefix}${cpu}.out" \
        --no-run \
        "$@"
    done
    # After all downloads, kick the run via a final no-scp invocation
    # against CPU 0's image and capture the shared ASCLIN0 console.
    "$flash_py" "$dir/nuttx-cpu0.elf" \
      --instance-id "$amp_instance" \
      --remote-name "${amp_remote_prefix}0.out" \
      --console-port "$amp_console_port" \
      --console-log "$dir/nuttx-amp.console.log" \
      --no-scp \
      "$@"
    exit $?
    ;;
  ""|-h|--help)
    sed -n '3,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
  *)
    echo "unknown board: $board (try tc4d7 | tc397 | tc375 | triboard | amp)" >&2
    exit 2
    ;;
esac

shift || true
exec "$flash_py" "$elf" \
  --instance-id "$instance" \
  --remote-name "$remote" \
  --console-port "$console_port" \
  --console-log "${elf}.console.log" \
  "$@"
