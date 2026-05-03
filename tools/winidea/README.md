# winIDEA hardware-flash automation for NuttX on Infineon AURIX

This directory mirrors `tools/winidea/flash.py` from the Zephyr tree and
adds a small NuttX-side wrapper.  It drives a remote `winIDEA` instance
on the Windows lab host over `isystem.connect` (TCP), copies the local
ELF up via `sshpass+scp`, swaps the active session's symbol/program
file paths, and `download` + `resetAndRun`s the CPU.  The same flow is
already verified on TC4D7 hardware from the Zephyr session.

Use it for TC4D7 or TC397 builds — including the per-CPU AMP ELFs
produced by `tools/build_amp.sh`.

## Files

| File         | Purpose                                                           |
|--------------|-------------------------------------------------------------------|
| `flash.py`   | Standalone CLI flasher.  Takes any ELF path; no NuttX assumptions. |
| `flash.sh`   | Board-aware wrapper that fills in `--instance-id`/`--remote-name`. |
| `README.md`  | This file.                                                         |

`flash.py` is intentionally identical to the Zephyr-side
`tools/winidea/flash.py` so a fix in one tree can be diffed against
the other.

## One-time setup on the calling host

```bash
pip install --user --break-system-packages isystem.connect
sudo apt install sshpass openssh-client

export SSHPASS='August13@Architecture'        # per shell
```

## Lab topology (192.168.1.0/24)

| What                     | IP              | User           |
|--------------------------|-----------------|----------------|
| `venba` (this dev host)  | `192.168.1.4`   | `parthiban`    |
| Windows host (winIDEA)   | `192.168.1.2`   | `bharathi`     |
| `kural` lab host         | `192.168.1.3`   | `parthiban`    |
|   (USB-serial consoles + TASKING/HighTec licence: `5053@192.168.1.3`) |

Windows blocks ICMP by default; verify the winIDEA host with
`nc -zw3 192.168.1.2 22`, not `ping`.

Pass `--host` or set `WINIDEA_HOST=...` to override the default
(`192.168.1.2`).

Two `winIDEA` instances are running on the Windows host:

* `com.tasking.winIDEA.instance.id-TC4D7` (DAP `MW8MIRZ2`, TC4D7XP, port 5316)
* `com.tasking.winIDEA.instance.id-TC397` (DAP `MW8MIRYH`, TC390 BD, port 5315)

Both bind `0.0.0.0` so no firewall changes are needed.

## Filename convention on `D:/Parthiban/`

```
nuttx-tc4x.out         NuttX TC4D7
nuttx-tc3x.out         NuttX TC397
nuttx-tc3x-can.out     NuttX TC397, CAN variant
nuttx-tc4x-cpuN.out    NuttX TC4D7 AMP per-CPU image (N=0..3)
zephyr-tc4d7-cpu0.out  Zephyr TC4D7 (Zephyr session)
zephyr-tc397-cpu0.out  Zephyr TC397
```

Stick to these names; the Zephyr runner uses the same convention.

## Quick start

```bash
# After a normal NuttX build (./nuttx in the repo root):
tools/winidea/flash.sh tc4d7
tools/winidea/flash.sh tc397

# Flash a specific ELF instead of ./nuttx:
tools/winidea/flash.sh tc4d7 build/tc4d7/nuttx

# Flash the four-CPU AMP build (defaults to /tmp/amp/nuttx-cpu{0..3}.elf):
tools/winidea/flash.sh amp
tools/winidea/flash.sh amp /path/to/elf-dir

# Direct CLI for unusual cases:
tools/winidea/flash.py path/to/nuttx \
    --instance-id com.tasking.winIDEA.instance.id-TC4D7 \
    --remote-name nuttx-tc4x.out
```

Anything after the first one or two positional arguments to `flash.sh`
is forwarded verbatim to `flash.py`, so flags work normally:

```bash
tools/winidea/flash.sh tc4d7 ./nuttx --no-scp --watch-seconds 30 -v
```

### Useful flags (passed through to `flash.py`)

| Flag                 | Effect                                                        |
|----------------------|---------------------------------------------------------------|
| `--no-scp`           | ELF already at `D:/Parthiban/<remote-name>`; skip the copy.    |
| `--no-run`           | Download but leave CPU stopped (attach with the winIDEA UI).   |
| `--watch-seconds N`  | After `resetAndRun`, poll for unexpected halts for `N` s.      |
| `-v`                 | Verbose: log scp command line and other diagnostics.           |

On an unexpected halt within the watch window, `flash.py` dumps
`PC/PSW/PCXI/A10/A11` and the call stack — same data the winIDEA UI
shows on a trap.

## Watching the serial console

In a separate terminal:

```bash
nc 192.168.1.3 9001       # TC4D7 console
nc 192.168.1.3 9000       # TC397 console
```

The `socat` listeners on the lab host bridge those ports to
`/dev/ttyUSB1` (TC4D7) and `/dev/ttyUSB0` (TC397).

## Why no `make flash` target?

NuttX's `Makefile.unix` doesn't have a clean board-extensible flash
hook (Zephyr has `west` runners; we don't).  Adding one for a single
SoC family would reach into the top-level build, and that's more
surface area than the wrapper saves.  The shell wrapper is small,
matches `tools/build_amp.sh`, and stays out of the build graph.

## Licence-server caveat

`RLM_LICENSE=5053@192.168.1.3` is the HighTec/TASKING TriCore clang
floating licence.  It's only reachable from the normal `192.168.1.x`
subnet, so on the temporary `10.x` subnet **fresh TriCore code can't
be compiled**.  Already-built ELFs flash and run fine — the runner
itself doesn't need clang.
