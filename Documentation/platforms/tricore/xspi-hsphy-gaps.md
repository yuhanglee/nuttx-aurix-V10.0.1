# TC4xx xSPI / HSPHY bring-up audit -- DLL lock timeout

One-time engineering audit of the TC4xx xSPI + HSPHY init paths in the
NuttX TriCore tree.  Snapshot taken on branch
`dev/infineon/tricore-amp` after the 2026-04-29 Triboard sanity run
failed with the symptom recorded below.

This document enumerates code-evidence gaps; it does not change code.

## 1. Symptom recap

Triboard-tc4x7-com console excerpt from `board_app_initialize()`:

```
tc4x_hsphy_xspi_init: hsphy: DLL lock timeout
xspi: tc4x_xspi_initialize failed
board_app_initialize: ERROR: tc4x7_xspi_initialize failed: -19
```

- The first line is emitted from
  `arch/tricore/src/tc4xx/tc4x_hsphy.c:148` after the polling loop on
  `HSPHY_DLL_CFG.LOCKSTAT` (bit 30) exhausts its 1 ms budget.
- The second line is emitted from
  `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_xspi.c:262`
  because `tc4x_xspi_initialize(0)` returned `NULL` after
  `tc4x_hsphy_xspi_init()` propagated `-ETIMEDOUT`
  (see `arch/tricore/src/tc4xx/tc4x_xspi.c:169`).
- The third line is emitted from
  `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_appinit.c:102`.
  The shown numeric value `-19` is `-ENODEV`, which
  `tc4x7_xspi_initialize()` returns at line 263 after the lower layer
  fail; the underlying first failure is `-ETIMEDOUT` (`-116`) from
  the HSPHY layer.

Net: the upper layer error is `-ENODEV` (no device), but the actual
root cause is "DLL never asserted LOCKSTAT inside `timeout_us`"
(`tc4x_hsphy.c:144`).

## 2. HSPHY init sequence as currently coded

Order of operations on the `tc4x7_xspi_initialize()` path on Triboard.
References are file:line in this tree.

1. `boards/.../triboard-tc4x7-com/src/tc4x7_xspi.c:65 tc4x7_xspi_pinmux()`
   - Configures five P16 pads (CK_N, CK_P, DQ0, DQ1, CS_N) by calling
     `aurix_config_gpio()` with `GPIO_PERIPH_OWN_PAD` set
     (`tc4x7_xspi.c:54-56`).
   - The `GPIO_PERIPH_OWN_PAD` flag triggers a write to
     `PORT.PCSRSEL` (`arch/tricore/src/tc4xx/tcxx_gpio.c:131-134`,
     `hardware/tc4xx_port.h:42`) handing pad ownership to the
     peripheral mux.  This is the only place `PCSRSEL.PCSR=1` is
     programmed; it is on the right path.

2. `arch/tricore/src/tc4xx/tc4x_xspi.c:88 tc4x_xspi_ccu_enable()`
   - Sets `PERCCUCON1.XSPIPERON` (BIT(25)) and `UP` (BIT(30))
     (`tc4x_xspi.c:95`) so the CCU routes the kernel clock to the
     xSPI module domain.
   - Brackets the write with `CCUSTAT.LCK` polling
     (`tc4x_xspi.c:74-80`).
   - Note: this does **not** touch `PERCCUCON1.XSPISLDIV`
     (`hardware/tc4x_clock.h:227-228`), so the xSPI peripheral
     clock divider is left at reset.  The reset value is 0, which
     on AURIX CCU dividers normally encodes "clock disabled" -- see
     4 below.

3. `arch/tricore/src/tc4xx/tc4x_hsphy.c:83 tc4x_hsphy_xspi_init()`
   3a. `tc4x_hsphy_clock_enable()` clears `HSPHY_CLC.DISR` (BIT(0))
       and polls `DISS` (BIT(1)) until it clears
       (`tc4x_hsphy.c:60-77`).  This is the kernel clock ungate;
       reset value of `CLC` is 0x3 (DISR=DISS=1) as documented at
       `hardware/tc4x_hsphy.h:54`.

   3b. CMNCFG is **not** written.  The comment at
       `tc4x_hsphy.c:101` says "left at reset default for xSPI".
       See 3 root cause B for why this is risky in the multi-init
       case.

   3c. Build `DLL_CFG` with `REFCLKSE` (`tc4x_hsphy.c:107`,
       BIT(21) per `hardware/tc4x_hsphy.h:72`), conditionally
       `PEN3VX` / `PMODE` / `FMODE` / `RXSAMPLESEL`
       (`tc4x_hsphy.c:109-127`), and pack `TXCFG` / `RXCFG`
       (`tc4x_hsphy.c:129-132`).  TXEN, RXEN, POWER are deliberately
       cleared at this step (comment at `tc4x_hsphy.c:103-105`).

   3d. Single 32-bit write of the assembled `DLL_CFG`
       (`tc4x_hsphy.c:134`).

   3e. Or-in `POWER | TXEN | RXEN`
       (`tc4x_hsphy.c:138-139`) and write again.

   3f. Poll `LOCKSTAT` (BIT(30) of `DLL_CFG`,
       `hardware/tc4x_hsphy.h:79`) with the `tc4x_hsphy_busy_us(10)`
       NOP loop, up to `timeout_us` accumulated waited time
       (`tc4x_hsphy.c:144-154`).

4. `arch/tricore/src/tc4xx/tc4x_xspi.c:105 tc4x_xspi_module_clc_enable()`
   - Writes `0` to `XSPI0_CLC` (`tc4x_xspi.c:109`) and polls
     `DISS` (`tc4x_xspi.c:111`).
   - Reset value of `CLC` is `0x3` per the comment at
     `tc4x_xspi.c:101-103`.
   - **This step is gated behind the HSPHY init succeeding**
     (`tc4x_xspi.c:169-178`); since HSPHY currently fails, the xSPI
     IP block's own clock is never turned on in the failure path.

5. `xspi_dw_ssi_initialize()` (`drivers/spi/xspi_dw_ssi.c:617`) takes
   over with the IP-level reset
   (`xspi_dw_ssi.c:587-611 dw_ssi_hw_reset()`):
   - SSIENR=0
   - mask all interrupts
   - drain interrupt clear-on-read regs
   - SER=CTRLR0=CTRLR1=SPI_CTRLR0=BAUDR=TXFTLR=RXFTLR=0
   - CTRLR2.PORTSEL = `XSPI_DW_SSI_CTRLR2_PORTSEL_P16` (HSFAST) or
     `_GPIO` based on `cfg->use_gpio_pads`
     (`xspi_dw_ssi.c:607-610`,
      bit defined in `xspi_dw_ssi_regs.h:88-90`).

What is **not** done anywhere in the xSPI path:

- VMONP supplies (`VDDHSIF`, `VDDPHY0..2`, `VDDPHPHY0..2`).
- HSPHY kernel reset via `RST.CTRLA` / `RST.CTRLB` and poll on
  `RST.STAT.KRST`.
- Any per-port `PHYCTRL0` / `PHYCTRL1` programming (the xSPI uses
  PHY[0] on TC4D7).
- `CMNCFG.FSR` explicit write (assumed reset-default already 0).
- `XSPISLDIV` programming.

Both Zephyr's `phy_hsphy.c` and NuttX's own `tc4x_geth.c` do all
five of those steps for the Ethernet path; the xSPI path skips them.

## 3. Probable root causes, ranked

### A. PMS / VMONP supplies for HSPHY pad domain are not enabled (most likely)

The HSPHY pad domains and DLL on TC4xx require the PMS-side
voltage monitors / regulator gates `VDDHSIF`, `VDDPHY0..2`, and
`VDDPHPHY0..2` to be turned on before the HSPHY block produces a
stable reference clock and before the DLL can lock.  The Zephyr
ethernet PHY driver
(`/home/parthiban/work/west/mainline/zephyr-tc4xx/drivers/ethernet/phy/phy_hsphy.c:18-87`)
performs this exact sequence -- `OVENABLE`, poll `RESULT > RESETVAL`,
deassert `RESETOFF` -- for **seven** rails before touching the HSPHY
CLC.  NuttX's own ethernet stack mirrors the pattern at
`arch/tricore/src/tc4xx/tc4x_geth.c:64-98`
(`pms_enable_supply()` / `init_pms_supplies()`).

The xSPI driver path has **no** equivalent.  `tc4x_hsphy_xspi_init()`
goes straight from `clock_enable()` to programming `DLL_CFG`
(`tc4x_hsphy.c:95-139`).  If the rails are not held on by
something else earlier in the boot -- which is not the case in the
current build, `tc4x_geth.c::init_pms_supplies()` is only called
when `tc4x7_ethernet_initialize()` runs -- then the HSPHY DLL is
being asked to lock against an unpowered analog domain.  That
matches the symptom: DLL never asserts `LOCKSTAT`.

What to verify:
- Read `PMS_VMONP_VDDHSIFSTAT` (`tc4x_geth.h:85`) before calling
  `tc4x_hsphy_xspi_init()` and confirm `RESULT > RESETVAL`.
- Read `VDDPHY0STAT` / `VDDPHY1STAT` / `VDDPHY2STAT` likewise.
- If any read returns `RESULT == 0`, this rail is gated off and
  the DLL physically cannot lock.

Fix shape:
- Promote `pms_enable_supply()` and the VMONP register addresses
  in `tc4x_geth.h` to a shared header (e.g. `hardware/tc4x_pms.h`)
  and call the supply-enable sequence from `tc4x_hsphy_xspi_init()`
  before the `DLL_CFG` write.

### B. HSPHY kernel reset (`RST.CTRLA` / `CTRLB`) is never issued

Both Zephyr (`phy_hsphy.c:94-100`) and NuttX-Ethernet
(`tc4x_geth.c:120-135`) issue:

```
RST.CTRLB.STATCLR = 1           (bit 31)
RST.CTRLA.KRST    = 1           (bit 0)
RST.CTRLB.KRST    = 1           (bit 0)
poll RST.STAT.KRST == 1
```

immediately after ungating `CLC` and before doing any per-instance
PHY power-up.  This is the documented two-stage kernel-reset
handshake on AURIX modules.

`tc4x_hsphy.c` does not perform this.  `hardware/tc4x_hsphy.h`
does not even define the `RST_CTRLA` / `RST_CTRLB` / `RST_STAT`
register offsets (only `CLC=0x000`, `CMNCFG=0x200`, `DLL_CFG=0x628`
at `tc4x_hsphy.h:46-48`); the matching addresses
`0xF200_000C`, `0xF200_0010`, `0xF200_0014` are present in
`tc4x_geth.h:61-63` though.

Net effect: the HSPHY block emerges from RAMP-state reset and
clock-ungate without an explicit kernel reset.  If a previous
configuration (e.g. an Ethernet bring-up earlier in the same boot)
left state in HSPHY registers, this state is carried into the xSPI
config.  More importantly, the analog blocks inside the PHY may
need the kernel-reset deassertion edge to seed their state machines.

What to verify:
- Boot with `CONFIG_TC4XX_GETH=n` so `tc4x_geth.c::init_pms_supplies()`
  and `init_hsphy_for_rmii()` never run, and re-run the xSPI bring-up.
  If the failure persists unchanged, kernel-reset is not the trigger.
  If the failure mode changes (e.g. now only fails on warm boot),
  the kernel reset is on the critical path.
- Add a `RST.CTRLA/CTRLB/STAT` register declaration to
  `hardware/tc4x_hsphy.h` and emulate the Ethernet sequence.

### C. CMNCFG.FSR may be left in the Ethernet selection from a previous init

The TC4xx HSPHY PHY-side mux is `CMNCFG.FSR` at bit 4
(`hardware/tc4x_hsphy.h:62`,
 `modules/hal/infineon/tc4x/Sfr/TC4Dx/IfxHsphy_regdef.h:233`):
- `FSR=0`: xSPI mode
- `FSR=1`: RGMII / RMII Ethernet on port 16

`tc4x_geth.c:147` does
`xgmac_modreg(BIT(4), BIT(4), TC4XX_HSPHY_CMNCFG)` -- sets
FSR=1 for Ethernet.  `tc4x_hsphy.c:101` says "left at reset default
for xSPI" and writes nothing to CMNCFG.

In `tc4x7_appinit.c:90-104` Ethernet runs (line 91) **before** xSPI
(line 99), so on Triboard the xSPI init starts with `FSR=1` already
written by `init_hsphy_for_rmii()`.  In that state the HSPHY internal
muxing is routing the PHY pads / DLL clock distribution to the
Ethernet side, so the xSPI clock loop the DLL is locking against is
not even being routed.

What to verify:
- Disable Ethernet (`CONFIG_TC4XX_GETH=n`) and rerun.  If DLL locks,
  CMNCFG mux is the proximate cause, regardless of (A) and (B).
- Read back `CMNCFG` immediately before the failing
  `LOCKSTAT` poll and confirm bit 4.

Fix: the xSPI HSPHY init must explicitly write `CMNCFG.FSR=0`
before powering up the DLL, exactly as the Ethernet path writes
`FSR=1`.  The `tc4x_hsphy.c:101` comment is wrong-by-omission for any
boot where Ethernet has already run.

### D. DLL reference clock divider XSPISLDIV is not programmed

`PERCCUCON1.XSPISLDIV` (`hardware/tc4x_clock.h:227-228`,
bits [7:4]) is the divider feeding `f_xspi_sl` (the slow / serial
clock used by the xSPI module **and** by the HSPHY DLL reference
input).  `tc4x_clockconfig.c::tc4x_ccu_set_dividers()` programs
`MCANDIV`, `QSPIDIV`, `MSCDIV`, `I2CDIV`, `PPUDIV`, `ASCLINFDIV`,
`ASCLINSDIV`, `CANXLDIV`
(`tc4x_clockconfig.c:359-401`) -- but **not** `XSPISLDIV`.  The
reset value of CCU dividers on AURIX is "0 = clock off"; the field
is left at 0 throughout the boot.

If the DLL reference clock is ungated only via `XSPIPERON`
(`tc4x_xspi.c:95`) but the divider-fed clock itself is 0 Hz, the
DLL has no edge to lock to.  This is the single most plausible
"clock not present" explanation that matches the symptom and the
visible code.

What to verify:
- Use the `EXTCON` clock-out (`hardware/tc4x_clock.h:301-338`,
  the `TC4X_CLOCK_EXTCON` register currently flagged as
  "FIXME test fragments only") to route `f_xspi_sl` to a pin and
  confirm a non-zero frequency.  If 0 Hz, divider is the culprit.
- Inspect `PERCCUCON1` after `tc4x_clockconfig` runs and after
  `tc4x_xspi_ccu_enable()`; field [7:4] should be non-zero.

Fix: extend `tc4x_clockconfig.c::tc4x_ccu_set_dividers()` to
program `XSPISLDIV` to a sane value (typically `fsource2 /
target_xspi_sl`), and also pick `CLKSELXSPI` if the SoC has one
(not present in `hardware/tc4x_clock.h` today, double-check the
TRM).  Then update `tc4x_xspi_ccu_enable()` to `OR` the new field
in instead of touching only `XSPIPERON | UP`.

### E. Software polling time-base is wrong, masking real lock time

`tc4x_hsphy_busy_us()` at `tc4x_hsphy.c:47-54` is a fixed
`for(i=0; i<us*10; i++) nop;` loop.  At a 500 MHz core
(`hardware/tc4x_clock.h:252-258` `SYSPLL` config gives 500 MHz),
each NOP is roughly 2 ns; 10 NOPs is ~20 ns, so a nominal
"10 us" wait is closer to 20 ns, and a 1000 us timeout completes
in roughly 2 us of real time.

The DLL lock spec in similar Synopsys / Infineon HSPHY blocks is
in the tens of microseconds at minimum, and can be hundreds of
microseconds during a cold start.  Even if every other root cause
in this list is invalid, the loop almost certainly times out before
the DLL has had a chance to lock.

What to verify:
- Replace `tc4x_hsphy_busy_us(10)` with `up_udelay(10)`
  (`up_udelay` is available -- it is used in the same tree at
  `tc4x_geth.c:138` and `tc4xx_i2c.c:115`) and re-run.  If the
  failure resolves without any other change, the timeout was
  the only blocker.
- If the timeout fires earlier than expected, instrument the loop
  to count iterations and compare against the requested `timeout_us`.

This bug exists in `tc4x_xspi.c:65-72` (`tc4x_xspi_busy_us()`) too,
on the same NOP-counts-as-microseconds basis.  It is identical
boilerplate in two files.

### F. Triboard pin-mux drift vs Lite Kit

`tc4d7_xspi.c:71-73` in the Lite Kit board explicitly delegates
pin-mux to "elsewhere" and mentions port 20 GPIO pads (25 MHz max,
3.3 V).  Triboard `tc4x7_xspi.c:65-90` does its own
`tc4x7_xspi_pinmux()` for HSFAST port 16.

Differences worth checking:
- Triboard maps DQ0=P16.7, DQ1=P16.3 (`tc4x7_xspi.c:50-51`).  The
  P16 alternate function table in the TC4xx user manual must be
  consulted to confirm `GPIO_ALT0` is the HSFAST mode for those
  pins; `tc4x7_xspi.c:55` hard-codes `GPIO_ALT0`.  If the correct
  alt for HSFAST is `ALT1` or higher on these specific pins, the
  pads end up in some other mode (often GPIO input or a different
  module).  The DLL reference clock would then not be looped through
  the actual pad, and `LOCKSTAT` could fail.
- `GPIO_PERIPH_OWN_PAD` triggers `PCSRSEL=1` for the pin, but does
  **not** touch `HWSEL` / `PROT` settings beyond the protection
  group write at `tcxx_gpio.c:136-140`.  Confirm with the TRM
  that a HSFAST pad on TC4D7 needs only `PCSRSEL=1` and the IOCR
  alt-func, not also `HWSELSTAT` or `PDISC`.

What to verify:
- Build for `a3g-tc4d7-lite` (which goes through port-20 GPIO pads
  and skips HSPHY entirely via `CONFIG_TC4XX_XSPI_PORTSEL_GPIO=y`)
  and confirm the JEDEC RDID succeeds.  If yes, the IP and the
  generic xSPI plumbing is fine and the issue is contained to the
  HSFAST/HSPHY side on Triboard.

### G. RXSAMPLESEL mismatch with actual RWDS wiring

`tc4x7_xspi.c:54-57 XSPI_PAD()` enumerates only CK_N, CK_P, DQ0,
DQ1, CS_N -- five pads.  `g_tc4x_hsphy_cfg.rxsamplesel = true`
(`tc4x_xspi.c:149`) and the comment says "no RWDS strobe".  This
matches: RWDS is not muxed, so RXSAMPLESEL=1 is correct for an
S26HS512 in 1S/4S mode.

This is **not** a likely root cause of "DLL won't lock" because
RXSAMPLESEL gates only the RX-data sample clock; the DLL itself
locks against the reference clock inside the HSPHY independently.
Listed here for completeness only.

## 4. xSPI driver gaps (post-HSPHY)

These would only surface once HSPHY is locking and `xspi_nor_probe()`
actually runs.  They are not the cause of the current `-19` failure
but should be noted now.

- **xSPI module clock divider not programmed.**
  `PERCCUCON1.XSPISLDIV` and any `CLKSELXSPI` mux are missing from
  `tc4x_clockconfig.c::tc4x_ccu_set_dividers()`
  (lines 359-401).  Even after HSPHY locks, the IP's `ssi_clk` may
  be derived from a 0 Hz source and `BAUDR` math
  (`xspi_dw_ssi.c:132-154 dw_ssi_calc_sckdv`) will compute against
  the static `CONFIG_TC4XX_XSPI_SSI_CLK_HZ=100000000`
  (`defconfig:71`) but the actual SCLK on the wire will be 0 or
  unrelated.  The Lite Kit defconfig says the same value;
  no SoC code verifies it.

- **No RX_SAMPLE_DLY / DDR_DRIVE_EDGE programming.**
  `xspi_dw_ssi_regs.h:69,71` define
  `RX_SAMPLE_DLY_OFFSET` (0x2f0) and `DDR_DRIVE_EDGE_OFFSET` (0x2f8)
  but `xspi_dw_ssi.c::dw_ssi_hw_reset()` does not write them.  At
  100 MHz / SDR these are typically 0 and acceptable, but at any
  HSPHY-mediated > 50 MHz frequency the RX sample delay must be
  trimmed against silicon.  Treat as a known v2 todo, not a blocker.

- **CTRLR0.SSI_IS_MST is never set.**
  `dw_ssi_transfer()` assembles CTRLR0 at `xspi_dw_ssi.c:462-475`
  with `FRF_SPI | tmod | spi_frf | DFS | SCPH | SCPOL`, but never
  ORs `XSPI_DW_SSI_CTRLR0_SSI_IS_MST` (BIT(31), defined at
  `xspi_dw_ssi_regs.h:121`).  On DWC_SSI the IP defaults to master
  on most configs, but on parts where this bit is documented as
  needing explicit set-to-1 the controller will sit idle.  Verify
  against the TC4xx-specific DWC_SSI configuration ID.

- **No mode-register / continuous-read logic for S26HS512.**
  The Cypress / Infineon S26HS series uses a mode-register byte
  after the address for continuous-read mode.  `xspi_op_s` has no
  `mode_byte` field (`include/nuttx/spi/xspi.h:240-263`).  Fine for
  RDID smoke, but not for octal-DDR HyperBus operation.

- **`dummy` is hard-clipped at 31** (`xspi_dw_ssi.c:289-292`).
  S26HS in 8D-8D-8D wants ~16 dummy cycles at 200 MHz, so 31 is
  enough today.  In 8S-8S-8S at 166 MHz with variable latency it
  is also enough.  Fine for v1.

- **CS de-assert tail not configurable.**
  `xspi_dw_ssi.c:489-565` programs `SER` once, then writes
  `SSIENR=1` and disables it again at the end.  No
  `XSPI_DW_SSI_CTRLR0_SSTE` (`xspi_dw_ssi_regs.h:112`,
  Slave Select Toggle Enable) handling.  Acceptable for v1,
  document for v2.

- **HSPHY xSPI per-port PHYCTRL never touched.**
  The Ethernet path writes `PHYCTRL1[i]` to clear PWRDWN and RST
  (`tc4x_geth.c:137-139`).  TC4xx HSPHY almost certainly has the
  same per-port wrappers around the xSPI side.  Check whether the
  TC4D7 TRM defines `PHYCTRL1[2]` (or similar) for the xSPI
  partition; if so, add the equivalent wake-up writes.

## 5. Recommended fix order

Strictly ordered.  Do (1) first; if it does not fix the symptom by
itself, layer (2) on top, then (3), and so on.  Each step is a
minimal, code-pointable change.

1. **Fix the timing primitive.**  In
   `arch/tricore/src/tc4xx/tc4x_hsphy.c:47-54`, replace
   `tc4x_hsphy_busy_us()` with a thin wrapper around `up_udelay()`.
   Same in `arch/tricore/src/tc4xx/tc4x_xspi.c:65-72`.  This single
   change may move the failure to a later step or resolve it
   outright if the DLL was actually locking but we were never
   waiting long enough.

2. **Power up the HSPHY-relevant PMS rails.**  Lift
   `pms_enable_supply()` / `init_pms_supplies()` from
   `arch/tricore/src/tc4xx/tc4x_geth.c:64-98` into a shared
   `tc4x_pms.c` (or call the existing function before
   `tc4x_hsphy_xspi_init`).  Required rails per the Zephyr driver
   (`zephyr-tc4xx/drivers/ethernet/phy/phy_hsphy.c:18-87`):
   `VDDHSIF`, `VDDPHY0..2`, `VDDPHPHY0..2`.  Register addresses
   already exist in `arch/tricore/src/tc4xx/tc4x_geth.h:74-92`.

3. **Issue the HSPHY kernel reset before configuring the DLL.**
   Add `RST_CTRLA` / `RST_CTRLB` / `RST_STAT` definitions to
   `arch/tricore/src/tc4xx/hardware/tc4x_hsphy.h` (offsets 0x00C,
   0x010, 0x014; addresses confirmed in
   `arch/tricore/src/tc4xx/tc4x_geth.h:61-63`).  In
   `tc4x_hsphy_xspi_init()`, between `clock_enable()` and
   `DLL_CFG` programming, replicate the
   `tc4x_geth.c:120-135` sequence (`STATCLR=1`, `KRST` on A and B,
   poll `STAT.KRST`).

4. **Explicitly select xSPI mux in CMNCFG.**  In
   `tc4x_hsphy_xspi_init()`, write `0` to `CMNCFG.FSR` (BIT(4),
   `hardware/tc4x_hsphy.h:62`) before the first `DLL_CFG`
   write.  Replace the `tc4x_hsphy.c:101` comment with the actual
   write.  This is mandatory for the
   "Ethernet-then-xSPI" boot flow used on Triboard.

5. **Program the xSPI slow-clock divider.**  Add an
   `XSPISLDIV` (and `CLKSELXSPI` if present) write to
   `tc4x_ccu_set_dividers()` in
   `arch/tricore/src/tc4xx/tc4x_clockconfig.c:359-401`, sourcing
   from the same fsource1/2 used for the QSPI block.  Add a
   `TC4X_XSPI_TARGET_HZ` define in
   `arch/tricore/src/tc4xx/hardware/tc4x_clock.h:282-298` next to
   the other domain target frequencies.  Update
   `CONFIG_TC4XX_XSPI_SSI_CLK_HZ` (currently a hard-coded 100 MHz
   in `defconfig:71`) so the IP-level BAUDR math matches reality.

6. **Sanity-check pin-mux alt selection for P16.**  Open the
   TC4D7 user-manual table referenced at `tc4x7_xspi.c:43-52` and
   confirm that `GPIO_ALT0` actually maps each of P16.0/1/3/7/13
   to its xSPI HSFAST function.  If any pin needs `ALT1`/`ALT2`,
   fix `XSPI_PAD()` macro at `tc4x7_xspi.c:54-56`.

7. **After HSPHY locks**, exercise `tc4x7_xspi_smoke_test()`
   (`tc4x7_xspi.c:168-242`) end-to-end and confirm RDID returns a
   plausible Cypress/Infineon JEDEC byte (mfr=0x34 for S26HS).

## 6. Open questions (need TRM or hardware probing)

These cannot be resolved by reading the NuttX tree alone.

- **DLL lock spec.**  TC4xx TRM 17.4 (HSPHY): what is the maximum
  spec'd `t_lock` for `DLL_CFG.LOCKSTAT` after `POWER`/`TXEN`/`RXEN`
  go high?  This sets the floor for the timeout in
  `tc4x_hsphy_xspi_init(timeout_us=...)`.  Today
  `tc4x_xspi.c:169` passes 1000 (1 ms), which is plausible but
  unsupported by a citation.

- **PMS rails actually required for xSPI vs. Ethernet.**  Zephyr's
  ethernet driver enables `VDDHSIF` + `VDDPHY0..2` + `VDDPHPHY0..2`
  unconditionally.  TC4D7 may not require all seven for xSPI-only
  operation.  TRM PMS section needs to spell out the matrix
  (xSPI lanes are a subset of HSPHY pads).

- **Is there an xSPI-side `PHYCTRL` group?**  By analogy with the
  Ethernet `PHYCTRL0/PHYCTRL1` at `0xF200_0210` / `0xF200_0214`
  (`tc4x_geth.h:65-66`), xSPI may have its own per-instance
  PWRDWN / RST controls inside the same HSPHY block.  Need to
  walk `IfxHsphy_regdef.h`
  (`/home/parthiban/work/west/mainline/modules/hal/infineon/tc4x/Sfr/TC4Dx/IfxHsphy_regdef.h`)
  for an XSPI-prefixed group.

- **`CLKSELXSPI` mux source.**  `hardware/tc4x_clock.h:225-243`
  (PERCCUCON1 fields) defines `XSPISLDIV` (bits [7:4]) but does
  not define a `CLKSELXSPI` source mux.  TRM CCU section ... needs
  to confirm whether xSPI is hard-wired to PERPLL/fsource2 or has
  a programmable source like QSPI/MCAN/MSC.

- **Voltage measurements.**  Probe `VDDHSIF` and `VDDPHY*` rails
  with a scope at the moment of the failed DLL-lock poll.  If any
  rail is at 0 V or floating during init, root cause (A) is
  confirmed; if all rails are nominal, root cause (A) is excluded
  and (B)/(C)/(D) move up the list.

- **Triboard stuffing options.**  Confirm with the Triboard
  schematic that the S26HS512 NOR is actually populated and that
  CK_P/CK_N/DQ/CS lines reach pin headers as expected.  An open
  CK net would never let the DLL see a clean clock to lock against
  -- that would mimic root cause (D) without the software being to
  blame.

- **Power-on order vs. Ethernet co-existence.**  If both xSPI and
  Ethernet HSPHY paths are needed in the same image, decide whose
  init owns the shared HSPHY block.  Today Ethernet at
  `tc4x7_appinit.c:91` clobbers `CMNCFG.FSR=1` and xSPI silently
  loses the mux at `tc4x7_appinit.c:99`.  The two drivers must
  either be mutually exclusive at Kconfig time, or share an
  HSPHY-owner state machine that re-routes `FSR` per transaction.

---

End of audit.  All actionable items live in 5.  Owner: TriCore
maintainer.  Re-run Triboard sanity test after step (1)+(2)+(3)+(4)
applied as a single patch series; record the new console line at
the matrix entry "xSPI (HS)" in
`Documentation/platforms/tricore/driver-matrix.md`.
