# AMP on TC4D7 Lite Kit

Asymmetric Multi-Processing setup for the Infineon AURIX TC4D7 Lite
Kit.  Each of the four application CPUs runs its own independent
NuttX kernel with no shared scheduler or IPC.  The build produces
four per-CPU ELF images that get loaded into the four PFLASH banks
the AURIX TC4Dx exposes:

  | CPU | PFLASH bank addr | DSPR        | DLMU partition | LMU partition | Image                |
  |-----|------------------|-------------|----------------|---------------|----------------------|
  | 0   | 0x80000000 (4MB) | 0x70000000  | 0x90000000     | 0x90400000    | NSH on ASCLIN0       |
  | 1   | 0x80400000 (4MB) | 0x60000000  | 0x90080000     | 0x90480000    | hello+idle ASCLIN1   |
  | 2   | 0x80800000 (2MB) | 0x50000000  | 0x90100000     | 0x90500000    | hello+idle ASCLIN2   |
  | 3   | 0x80A00000 (4MB) | 0x40000000  | 0x90180000     | 0x90580000    | hello+idle ASCLIN3   |

CPU 0 is the boot CPU.  It runs NSH on its ASCLIN0 console and, in
its `tricore_start()`, releases CPUs 1..3 by setting their `BOOTCON.PC`
to the per-CPU image entry-point and clearing `BOOTCON.HALT`.  Each
secondary CPU then boots from its own image, sets up its own ASCLIN
console, prints `Hello from CPU N`, and parks the idle thread on the
TriCore `wait` instruction.

## Build

```
export RLM_LICENSE="5053@192.168.1.3"
export PATH="/opt/tooling/tricore-llvm/toolchains/tricore/v10.2.0/bin:/opt/tooling/tricore-gcc/bin:$PATH"
cd /home/parthiban/work/west/mainline/nuttx
git checkout dev/infineon/tricore-amp
tools/build_amp.sh ./amp-out
```

That writes `amp-out/nuttx-cpu{0,1,2,3}.elf`.

If you prefer the manual loop the user originally asked for:

```
for cpu in 0 1 2 3; do
  make distclean -j32
  ./tools/configure.sh -E a3g-tc4d7-lite:$([ $cpu -eq 0 ] && echo amp_cpu0 || echo amp_secondary)
  sed -i 's|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH=.*|CONFIG_TRICORE_LLVM_TOOLCHAIN_PATH="/opt/tooling/tricore-llvm/toolchains/tricore/v10.2.0"|' .config
  EXTRAFLAGS="-O2 -g3 -DCPU=$cpu" make -j32
  cp nuttx amp-out/nuttx-cpu$cpu.elf
done
```

## Run on QEMU

```
QEMU=/home/parthiban/work/west/mainline/qemu/qemu-nuttx/build/qemu-system-tricore
$QEMU -machine KIT_A3G_TC4D7_LITE -nographic -monitor none \
    -kernel       amp-out/nuttx-cpu0.elf \
    -device loader,file=amp-out/nuttx-cpu1.elf \
    -device loader,file=amp-out/nuttx-cpu2.elf \
    -device loader,file=amp-out/nuttx-cpu3.elf \
    -serial mon:stdio \
    -serial unix:/tmp/cpu1.sock,server,nowait \
    -serial unix:/tmp/cpu2.sock,server,nowait \
    -serial unix:/tmp/cpu3.sock,server,nowait
```

In other terminals:
```
socat - UNIX-CONNECT:/tmp/cpu1.sock     # CPU 1 console
socat - UNIX-CONNECT:/tmp/cpu2.sock     # CPU 2 console
socat - UNIX-CONNECT:/tmp/cpu3.sock     # CPU 3 console
```

CPU 0's NSH appears on stdio; each secondary prints `Hello from CPU N`
on its socket as soon as CPU 0 releases it.

## Run on TC4D7 Lite Kit hardware

In winIDEA, load the four per-CPU ELFs into the same project so they
land at their respective PFLASH banks (the ELF LMAs already point at
0x80000000, 0x80400000, 0x80800000, 0x80A00000).  Reset and run.

### ASCLIN pin assignments

The four serial consoles use the iLLD-default ASCLIN pins.  **Verify
these against the TC4D7 Lite Kit schematic** before connecting your
USB-TTL adapter -- they are best-effort defaults and may need to be
changed if a particular ASCLIN pin isn't broken out on this kit.

  | CPU | ASCLIN  | TX pin | RX pin | Verified? |
  |-----|---------|--------|--------|-----------|
  | 0   | ASCLIN0 | P14.0  | P14.1  | OK (existing single-CPU console)  |
  | 1   | ASCLIN1 | P15.0  | P15.1  | TODO: confirm with schematic     |
  | 2   | ASCLIN2 | P15.4  | P15.5  | TODO: confirm with schematic     |
  | 3   | ASCLIN3 | P32.0  | P32.1  | TODO: confirm with schematic     |

If a pin needs changing, edit the per-CPU branches in
`boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_appinit.c` ->
`board_aurix_setup_serial_pin()`.  All four are wired at 115200-8N1
with `ALT2` for TX and pull-up input for RX.

### Expected output

CPU 0 (ASCLIN0):
```
NuttShell (NSH) NuttX-10.4.0
nsh>
```

Each secondary (ASCLIN1/2/3):
```
NuttX AMP secondary: hello from CPU 1
```

(... or 2, or 3.)

## What changed for AMP

NuttX (`dev/infineon/tricore-amp` on top of `dev/infineon/tricore-qemu-port`):

  - `arch/tricore/src/aurix/tricore_serial.c` -- `-DCPU=N` selects
    ASCLIN N base + IRQs (N maps to MMIO 0xF46C0000+0x200N and IRQ
    triple `172/173/174 + N3`).
  - `arch/tricore/src/common/tricore_irq.h` -- `TRICORE_DEFAULT_IR_TOS`
    becomes `(CPU)` so each CPU's `up_enable_irq()` writes the
    matching TOS field, routing the IRQ to that CPU's ICU.
  - `boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_appinit.c` --
    per-CPU pinmux in `board_aurix_setup_serial_pin()`.
  - `boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_amp_secondary.c`
    -- `amp_secondary_main()` init thread for CPUs 1..3.
  - `boards/tricore/tc4xx/a3g-tc4d7-lite/configs/amp_cpu0/defconfig`
  - `boards/tricore/tc4xx/a3g-tc4d7-lite/configs/amp_secondary/defconfig`
  - `tools/build_amp.sh` -- build helper.

QEMU (`dev/ifx/tricore-amp` on top of `dev/ifx/tricore-nuttx-qemu`):

  - `hw/tricore/tc4dx_soc.c` -- realize 4 CPUs (CPU 0 boots, 1..3
    start-powered-off), 4 ASCLIN instances, IR with 4 ISPs.
  - `hw/tricore/tc4x_cpu.c` -- per-CPU memory regions placed directly
    in system memory at the global per-CPU addresses; PFLASH bank N
    at 0x80000000+Npflash_size with a non-cached alias at
    0xA0000000+Npflash_size; per-CPU SFR stub now models BOOTCON
    and PC so that CPU 0's `tricore_start()` BOOTCON.HALT clear
    actually resumes the secondary CPU at its new PC.
