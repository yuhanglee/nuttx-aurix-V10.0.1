# NuttX on QEMU TriCore -- Status

This note tracks the state of running NuttX on the upstream-plus-Infineon
QEMU TriCore target, alongside Zephyr.  It is intended for engineers
working on the TC4Dx port; it is not a user-facing document.

## Repos / branches involved

- NuttX:  `dev/infineon/tricore-qemu-port` (this repo, branched off
  `dev/infineon/tricore-fix-tc4d7`).
- QEMU:   `dev/infineon/nuttx-tricore` in the local QEMU clone at
  `/home/parthiban/work/west/mainline/qemu/qemu`.
- Zephyr: `dev/ifx/tricore-upstream-aggressive` -- used as the
  known-working reference; **not modified**.

## What works

Zephyr `qemu_tc4x` boots to a shell as before:

    qemu-system-tricore -machine KIT_A3G_TC4D7_LITE -nographic \
        -kernel /path/to/zephyr.elf

NuttX TC4D7 (`a3g-tc4d7-lite:nsh`) boots through:

- Reset, `__start_stage2`
- `tricore_endinit_disable` / `tricore_safety_endinit_enable`
- `tricore_wdt_disable` (HOST and CPU0 WDT)
- `up_clockconfig` (OSC, RAMP OSC, SYSPLL, PERPLL, CCU dividers)
- `tricore_start` with the new endinit-protected
  `tricore_enable_sfr_access(0..N)` dance (TC4D7 cleanup-branch fix)
- `nx_start` core init (mm, irq, fs, drivers, clock)
- `up_timer_initialize` (STM CMP0 set, IRQ 10 attached)
- `up_initialize` -> `aurix_serialinit` -> `uart_register`
- First open of `/dev/console` -> `aurix_setup` ->
  `aurix_asclin_init` (CLC, baudrate, frame, flags)

## What does not yet work

After `aurix_setup` returns, an IRQ fires and lands in
`irq_unexpected_isr`.  With `CONFIG_SYSLOG_NONE=y` in the defconfig the
PANIC message is silently dropped, but execution loops in
`dump_assert_info` / `_assert` (visible in `qemu -d in_asm`).

Likely candidates (not yet pinned down):

1. The STM `CMP0` IRQ (SRC index 10 on TC4Dx, see
   `IFX_STM_SRC_INDEX(0, IFX_STM_DEFAULT)`).  `up_timer_initialize`
   does call `irq_attach(10, tricore_timerisr, NULL)` followed by
   `up_enable_irq(10)`, so on paper the handler is in place -- but it is
   suspicious that the panic happens shortly after STM goes live.
2. The 11-bit PIPN extracted from LWSR by `_isr_wrapper`
   (`arch/tricore/src/common/tricore_interrupt.S:28`) versus the 9-bit
   `LWSR.ID` field that QEMU populates (see `FIELD(LWSR, ID, 16, 9)`
   in `qemu/include/hw/intc/tricore_ir.h`).  If bits 25-26 are non-zero
   when QEMU writes LWSR, `irq` will be off by 0x600/0x400.
3. The `aurix_asclin_init` step writes `FLAGSENABLE` (PEE/FEE/RFOE/
   RFUE/TFOE).  Those gate flag-setting, not interrupt generation, but
   it is worth confirming that QEMU's ASCLIN model does not raise an
   interrupt off the back of one of those writes.

## Useful traces

To re-run the boot with execution and unimplemented-MMIO logging:

    timeout 5 qemu-system-tricore \
      -machine KIT_A3G_TC4D7_LITE -nographic \
      -kernel /path/to/nuttx -monitor none -serial null \
      -d in_asm,unimp,guest_errors -D /tmp/qemu.log

then:

    awk '/^IN: /' /tmp/qemu.log | uniq -c | sort -rn | head

The function name visible just before `__start` / `tricore_isr_handler`
is the last user-mode code that ran.  The numbers next to addresses in
`grep tricore_ir /tmp/qemu.log` show which SRC fired and at what
priority.

## Next steps (suggested order)

1. Temporarily flip `CONFIG_SYSLOG_NONE` off and route `_alert` through
   `up_putc` so the actual `"ERROR irq: %d"` line reaches the QEMU
   serial output.  That uniquely identifies the offending IRQ number
   and removes most of the guesswork.
2. Verify in `g_irqvector[10]` (read at runtime via gdb-stub --
   `qemu-system-tricore -s -S -kernel nuttx`) that the STM handler
   actually landed there.
3. If `irq != 10`, fix the wrapper's PIPN width or compare the
   `_isr_wrapper` extraction against the AURIX TC4x IR documentation.
4. If `irq == 10` but the handler is wrong, look at `irq_attach` /
   `g_irqvector` initialisation order vs the STM enable sequence in
   `up_timer_initialize`.

## QEMU-side changes already in place

`hw/tricore/tc4dx_soc.c` now stubs the per-CPU SFR windows for cores
1..5 (`create_unimplemented_device("tc4x-cpu{1..5}-sfr", ...)`); without
these stubs the multi-core PROT/ACCEN writes that NuttX issues during
`tricore_start` take a class-4 trap before the kernel even reaches its
core init.  Zephyr `qemu_tc4x` is unaffected because it only ever
touches CPU0's SFR window.

## NuttX-side changes already in place

`dev/infineon/tricore-fix-tc4d7` carries:

- `tricore_head.S`: invoke `up_clockconfig` before `tricore_start` so
  C-level peripheral access runs at the configured PLL clocks.
- `tricore_initialize.c`: open the endinit window around the
  `tricore_enable_sfr_access()` writes; restore unconditional
  `aurix_(early)serialinit()` calls.
- `tricore_internal.h`: forward-declare the actual endinit / safety
  endinit / clock entry points (the old `endinit_clear/set` declarations
  were stale).

Both `a3g-tc4d7-lite:nsh` and `a2g-tc397xa-3v3-tft:nsh` build cleanly
on top of these.
