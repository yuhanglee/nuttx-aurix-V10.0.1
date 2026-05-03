# MCMCAN bring-up audit (TC4D7 Lite, TC3x reference)

Engineering audit of the NuttX MCMCAN (Bosch M_CAN on AURIX) driver,
comparing it to Zephyr's `can_mcmcan` driver to explain why both
stacks work in loopback but fail on a physical bus on the TC4D7 Lite
kit wired to a Raspberry Pi `can1` interface.

All file:line citations are absolute.

## 1. Symptom recap

* SoC node    : AURIX TC4D7 Lite (`a3g-tc4d7-lite`)
* On-board PHY: Infineon TLE9371VSJ, wired to MCMCAN0 **NODE 1**
                (TX `P01.3`, RX `P01.4`)
* PHY STB pin : `P03.5`, ACTIVE_HIGH (LOW = normal mode)
* Refs        :
  `/home/parthiban/work/west/mainline/zephyr/boards/infineon/kit_a3g_tc4d7_lite/kit_a3g_tc4d7_lite.dtsi:42-69`,
  `/home/parthiban/work/west/mainline/zephyr/dts/tricore/infineon/tc4x/tc4dx.dtsi:942-1002`
* Bus partner : Raspberry Pi `can1` (SPI-attached controller)
* Termination : 120 R on the Lite kit

NuttX `/dev/can0` opens, `CANIOC_SET_CONNMODES bm_loopback=1` round
trips frames; normal mode produces no edges on the bus.  Zephyr
exhibits the same symptom.  Two independent stacks share the failure,
so the gap is at the board / silicon / electrical boundary, not in
code paths exercised by loopback (which short-circuits the BSP inside
the M_CAN core).

## 2. NuttX MCMCAN init sequence (TC4D7 Lite)

Entry point: `mcmcan_board_initialize()`, called from
`board_app_initialize()`.

* `/home/parthiban/work/west/mainline/nuttx/boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_appinit.c:94-100`
* `/home/parthiban/work/west/mainline/nuttx/boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_can.c:146-161`

Sequence:

```
tc4d7_config_can_pins()                     ; tc4d7_can.c:123-140
mcmcan_module_init(&g_mcmcan_module)        ; tc4d7_can.c:152
mcmcan_node_initialize(&g_node0_priv, ...)  ; tc4d7_can.c:158
can_register("/dev/can0", ...)              ; tc4d7_can.c:160
```

### 2.1 Pin-mux

`/home/parthiban/work/west/mainline/nuttx/boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_can.c:123-140`

* TX  : `P01.3 ALT5`, peripheral output (correct mux for CAN01_TX).
* RX  : `P01.4` plain digital input, no alt -- the M_CAN selects the
        actual RX path through `Ni_NPCR.RXSEL`.
* STB : **not driven**.  No GPIO is configured for the TLE9371's
        `STB` pin (`P03.5`).  The sister board file
        `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_can.c:120-140`
        *does* configure and drive a transceiver-enable on P00.3; the
        pattern simply was not ported to the Lite board.

### 2.2 NPCR (`Ni_NPCR.RXSEL`)

`tc4d7_can.c:63`: `CONFIG_MCMCAN_NODE0_NPCR = 0x2` -- a magic constant.

Programmed at `tricore_mcmcan.c:692-704`:

```c
for (i = 0; i < 4; i++) {
    if (!config->node_pin[i].present) continue;
    mcr |= (0x3 << (i * 2));
    putreg32(config->node_pin[i].npcr_val,
             config->base + MCMCAN_NPCR(i));
    mcmcan_configure_grint(config->base, i);
}
```

`MCMCAN_NPCR(0) = 0x14C`
(`/home/parthiban/work/west/mainline/nuttx/arch/tricore/src/tc4xx/hardware/tc4xx_mcanreg.h:39`).
NuttX programs the RXSEL of **NODE 0**.  The Lite board has the on-
board PHY wired to **NODE 1**'s RX.

### 2.3 Module clock / MRAM clear

`/home/parthiban/work/west/mainline/nuttx/arch/tricore/src/aurix/tricore_mcmcan.c:629-705`

* `aurix_enable_clock()` (629-652): clears `CLC.DISR`, spins on
  `CLC.DISS == 0`.  TC4x path **does not** toggle ENDINIT; line
  633-639 only does so on TC3x.
* MCR sequence (667-703): `CLE|CLKSEL` -> wait MRAM_BUSY -> add
  `INIT_MRAM` -> wait MRAM_BUSY -> 0.  Then per node enable CLE+
  CLKSEL bits, write NPCR, call `mcmcan_configure_grint(base, n)`
  (614-627) which writes two registers `MCMCAN_GRINT1(n)`,
  `MCMCAN_GRINT2(n)` at offsets `+0x12C+n*0x400`, `+0x130+n*0x400`
  (`tc4xx_mcanreg.h:37-38`).  See 3.3 for why the field layout is
  wrong on TC4x.

### 2.4 ACCEN write

NuttX does **not** program `ACCEN_WRA` or any per-master write
enable on the MCMCAN parent module.  No instance of `ACCEN`,
`MASTER_TAG`, or "tag id" exists in the NuttX MCMCAN code paths.

### 2.5 Per-node M_CAN core init (`can_mcan_init`)

`/home/parthiban/work/west/mainline/nuttx/drivers/can/can_mcan.c:1584-1759`

1. `exit_sleep_mode` clears `CCCR.CSR` (1611).
2. `enter_init_mode` (1618).
3. `enable_configuration_change` sets `CCCR.CCE` (1625).
4. Read CCCR; mask off `FDOE | BRSE | TEST | MON | ASM`; write back
   (1649-1657).  **CCCR.TEST is dropped here, before TEST.LBCK is
   written.**  The M_CAN spec write-protects the `TEST` register
   unless `CCCR.TEST=1` at the time of the write.
5. Read TEST, clear `TEST.LBCK`, write back (1659-1671) -- per (4)
   this write may be silently NACKed.
6. GFC: reject non-matching frames (1673-1690).
7. Compute timing -- see 2.6 (stub).
8. `set_timing` writes `NBTP` (1714).  No `DBTP`/`TDCR` path is
   compiled because `CONFIG_CAN_FD_MODE` is not set; defconfig only
   sets `CAN_FD`, the upper-half framing knob -- see 5.7.
9. Program `IE`/`ILS`/`ILE` for line-0/line-1 split (1730-1755).
10. (in `mcmcan_hw_init_once` 277-300) `can_mcan_configure_mram`
    programs SIDFC, XIDFC, RXF0C, RXF1C, RXBC, TXEFC, TXBC.
11. `irq_attach` lines 0/1 (284-300).
12. Default RX filters (302-328).

`mcmcan_setup` (333-365) then enables IRQs and calls `can_mcan_start`.
`can_mcan_start` (`drivers/can/can_mcan.c:488-524`) calls
`can_transceiver_enable(priv, mode)` only if `config->phy_present`,
then `can_mcan_leave_init_mode`.  `tc4d7_can.c:102` sets
`phy_present = false`, so the transceiver-enable hook is never
entered, and the static stub (`drivers/can/can_mcan.c:92-104`) is
itself a no-op.

### 2.6 Bit-timing stub

`drivers/can/can_mcan.c:55-69`:

```c
static int can_mcan_calc_timing(... uint32_t bitrate, uint16_t sample_pnt)
{
  /* TODO: implement bit timing calculation for nominal phase */
  UNUSED(bitrate); UNUSED(sample_pnt);
  timing->sjw = 1; timing->phase_seg1 = 13;
  timing->phase_seg2 = 2; timing->prescaler = 1;
  return 0;
}
```

The function ignores both `config->bitrate` and
`config->sample_point` and produces a 16-tq @ prescaler-1 setting.
On TC4D7 with `f_MCAN` of 80-160 MHz this puts nominal bitrate at
5-10 Mbit/s -- silicon-illegal for classical CAN.  Loopback masks it
because the M_CAN BSP is bypassed under `TEST.LBCK`.

The board's `CONFIG_MCMCAN_NODE0_BITRATE = 875` and `..._SAMPLE_POINT
= 875` (`tc4d7_can.c:55-56`) are also wrong: the
`mcan_config.bitrate` field is in Hz, not kbit/s, and a
sample-point should be per-mille.  Two compounding bugs that cancel
because the calc routine is a TODO.

### 2.7 IRQ wiring

`tc4d7_can.c:51-53`:

```
CONFIG_MCMCAN_NODE0_IRQ_LINE0 = 788
CONFIG_MCMCAN_NODE0_IRQ_LINE1 = 0     /* "TC4X has one ISR for TX & RX" */
```

NuttX collapses both lines: `mcmcan_line0_isr` calls *both*
`can_mcan_line_0_isr` and `can_mcan_line_1_isr` when `irq_line1==0`
(`tricore_mcmcan.c:183-196`).  Functionally fine, but it depends on
the routing that `mcmcan_configure_grint` writes -- see 3.3.

## 3. Zephyr equivalent init sequence

### 3.1 Devicetree wiring (TC4D7 Lite)

`/home/parthiban/work/west/mainline/zephyr/boards/infineon/kit_a3g_tc4d7_lite/kit_a3g_tc4d7_lite.dtsi:49-69`

```dts
&mcmcan0 { status = "okay"; };
can_phy0: can-phy0 {
    compatible = "infineon,tle9371", "can-transceiver-gpio";
    standby-gpios = <&p03 5 GPIO_ACTIVE_HIGH>;
    max-bitrate = <1000000>;
    #phy-cells = <0>;
};
&can01 {                              /* NODE 1, base 0xF4710600 */
    bitrate = <500000>;
    bitrate-data = <2000000>;
    pinctrl-0 = <&can1_default>;
    phys = <&can_phy0>;
    status = "okay";
};
```

* Uses `can01` (NODE 1, base `0xF4710600`) -- *not* `can00`
  (`tc4dx.dtsi:963-975`).
* `phys = <&can_phy0>` binds the M_CAN to a transceiver driver that
  knows about `P03.5`.
* Bitrate is in Hz.

`kit_a3g_tc4d7_lite-pinctrl.dtsi:27-35`:

```dts
can1_default {
    group1 { pinmux = <CAN01_TX_P1_3>; output-enable; };
    group2 { pinmux = <CAN01_RX_P1_4>; };
};
```

The `CAN01_*` macros encode IOCR alt index for TX *and* the
input-select index that the driver writes into `Ni_NPCR.RXSEL`.

### 3.2 Transceiver driver

`/home/parthiban/work/west/mainline/zephyr/drivers/can/transceiver/can_transceiver_gpio.c:33-71,94-107`:

* Init drives `P03.5` high (`GPIO_OUTPUT_ACTIVE`) -- PHY in standby.
* `can_transceiver_enable()` drives `standby_gpio` to 0 (inverted
  ACTIVE_HIGH) bringing the PHY into normal operating mode.

`/home/parthiban/work/west/mainline/zephyr/drivers/can/can_mcan.c:292-329`
calls `can_transceiver_enable(...)` **before**
`can_mcan_leave_init_mode`.  By the time `CCCR.INIT` is cleared the
PHY is guaranteed to be active.

### 3.3 MCMCAN parent-module init

`/home/parthiban/work/west/mainline/zephyr/drivers/can/can_mcmcan.c:276-398`

TC4x order:

1. `clock_control_on()` for `CLOCK_FMCAN`, `CLOCK_FMCANH` (291-298).
2. `aurix_enable_clock(base + 0x0, 1000)` (300).
   `/home/parthiban/work/west/mainline/zephyr/soc/infineon/aurix/tc4x/soc.h:17-25`
   short-circuits if `CLC.DISS` is already cleared.  NuttX always
   writes and spins.
3. **TC4x-only**: `sys_write32(0xFFFFFFFF, base + 0x30)` -- i.e.
   `ACCEN_WRA` set to "all master TAG IDs allowed" (305-311).
   Quoting the source comment (219-226):

   > Allow all master TAG IDs to write the M_CAN node registers;
   > without this CPU0 writes to NBTP/CCCR/etc. are silently
   > dropped because the reset value of ACCEN_WRA on TC4Dx silicon
   > does not always include tag 0.  Without this, NBTP/CCCR
   > writes succeed at the bus level but the register state never
   > changes, and the controller cannot leave INIT.

4. MRAM clear (314-327) -- same `0xC000_0000`/`+BIT(29)` dance as
   NuttX.
5. Per-node loop:
   * Set CLE+CLKSEL bits in MCR.
   * Apply pinctrl (`mcmcan_set_node_pinctrl`, 247-274) -- walks
     pinctrl entries and writes RX `pin->alt` into `Ni_NPCR` at
     `+0x4C`.  No board-side magic constant.
   * **TC3x** writes `GRINT1`/`GRINT2` (343-354).
   * **TC4x** writes `G0INTR @ +0x2C`, `G1INTR @ +0x30`, leaves
     `G2INTR @ +0x34` at reset (355-389).  The TC4x routing
     register is a 3-register set with a *different field layout*
     from TC3x's two-register set.

The TC4x routing comment (`can_mcmcan.c:355-366`) states:

> Reset value is 0, which routes all groups to SRC_CANINT0 -- that
> means a node whose DT "interrupts" property points anywhere else
> never sees its IRQ fire, even though the M_CAN core sets IR.

### 3.4 Per-node M_CAN core init

`/home/parthiban/work/west/mainline/zephyr/drivers/can/can_mcan.c:1397-1594`

Two relevant differences from NuttX:

* `1460-1490`: **first** `CCCR |= TEST`, **then** clear `TEST.LBCK`,
  **then** drop `CCCR.TEST` again.  This is the protocol the M_CAN
  spec mandates because TEST writes are ignored unless `CCCR.TEST=1`
  at the moment of the write.
* `1524-1556`: real timing search via Zephyr's CAN core helper
  `can_calc_timing()`
  (`/home/parthiban/work/west/mainline/zephyr/drivers/can/can_common.c:251-329`),
  which reads the M_CAN core clock through the driver's
  `get_core_clock` callback (`can_mcmcan.c:82-90`) and finds a
  valid `(prescaler, tseg1, tseg2)` triple for the requested
  bitrate and sample point.

## 4. Diff table

| Aspect | NuttX | Zephyr | Severity |
|---|---|---|---|
| **Node used on TC4D7 Lite** | NODE 0, base `0xF4710200`, IRQ 788 (`tc4d7_can.c:47,51`) | **NODE 1**, base `0xF4710600`, `IRQ_CAN0_INT2/INT3` (`tc4dx.dtsi:963-975`, `kit_a3g_tc4d7_lite.dtsi:62-69`) | **CRITICAL** |
| **TX pad alt** | `P01.3 ALT5` (`tc4d7_can.c:128`) | `CAN01_TX_P1_3` ALT5 (`kit_a3g_tc4d7_lite-pinctrl.dtsi:29`) | match |
| **RX pad / NPCR.RXSEL source** | hard-coded `0x2` written to NODE 0 NPCR (`tc4d7_can.c:63`, `tricore_mcmcan.c:692-704`) | derived from `CAN01_RX_P1_4` macro by `mcmcan_set_node_pinctrl` and written to NODE 1 NPCR (`can_mcmcan.c:247-274`) | **HIGH** -- wrong node and unverified value |
| **`ACCEN_WRA` (TC4D7 master-tag write enable)** | not programmed | `0xFFFFFFFF` at `module_base+0x30` (`can_mcmcan.c:305-311`) | **CRITICAL on TC4D7** |
| **TC4x interrupt routing regs** | uses `MCMCAN_GRINT1/GRINT2` at `+0x2C/+0x30` with TC3x field packing (`tc4xx_mcanreg.h:37-38`, `tricore_mcmcan.c:614-627`) | uses `G0INTR/G1INTR/G2INTR` at `+0x2C/+0x30/+0x34` with TC4x policy (`can_mcmcan.c:355-389`) | **HIGH** -- IRQ routing wrong on TC4x |
| **CAN_TEST write protocol** | clears `TEST.LBCK` *after* dropping `CCCR.TEST` (`can_mcan.c:1649-1671`) | sets `CCCR.TEST`, clears `TEST.LBCK`, then drops `CCCR.TEST` (`can_mcan.c:1460-1490`) | **MEDIUM** -- explains warm-boot loopback-stuck |
| **Bit-timing calc** | hard-coded stub `sjw=1 ts1=13 ts2=2 presc=1` (`can_mcan.c:55-69`) | `can_calc_timing()` reading `core_clock` and searching (`can_common.c:251-329`) | **CRITICAL** -- yields ~5-10 Mbit/s on TC4D7 |
| **`bitrate` units in board file** | `875` (`tc4d7_can.c:55`) | `500000` Hz in DT (`kit_a3g_tc4d7_lite.dtsi:63`) | masked by stub |
| **Transceiver standby pin** | not driven, no GPIO config (`tc4d7_can.c:123-140`) | `&p03 5 GPIO_ACTIVE_HIGH`, init `OUTPUT_ACTIVE`, low on enable (`kit_a3g_tc4d7_lite.dtsi:55-59`, `can_transceiver_gpio.c:94-107`,`33-71`) | **CRITICAL** -- TLE9371 stuck in standby |
| **`config->phy_present`** | `false` (`tc4d7_can.c:102`) | DT `phys` property | **HIGH** |
| **`can_transceiver_enable` body** | no-op stub (`can_mcan.c:92-104`) | dispatches to platform `can_transceiver_api` (`can_mcan.c:303`) | **HIGH** |
| **CCCR FD bits** | `FDOE`/`BRSE` cleared (`can_mcan.c:1649`) | same | match |
| **Loopback bit (`TEST.LBCK`) cleared on each init?** | yes (1665) but write may not stick on warm boot | yes (1477-1481), with proper protocol | medium |
| **Bus-off recovery** | manual via `CANIOC_BUSOFF_RECOVERY` only (`tricore_mcmcan.c:538-542`) | `CONFIG_CAN_MANUAL_RECOVERY_MODE` knob, otherwise auto | low |

## 5. Probable root causes (ranked)

### 5.1 (CRITICAL) Wrong CAN node / wrong RXSEL on TC4D7 Lite

The Lite board wires the on-board TLE9371 PHY to **NODE 1**
(`P01.3` TX, `P01.4` RX); confirmed by the Zephyr DT
(`kit_a3g_tc4d7_lite.dtsi:49-51,62-69` plus `tc4dx.dtsi:963-975`).
NuttX (`tc4d7_can.c:46-50,77`) uses NODE 0 (`0xF4710200`) and only
enables `node_pin[0]`.  `Ni_NPCR(0).RXSEL` cannot reach `P01.4`
because that pad is silicon-tied to NODE 1's RXSEL mux.  TX side is
analogous: the pad-mux ALT5 on `P01.3` selects NODE 1's TX driver.

Symptom matches: NODE 0 TX runs internally, the pad never gets
driven externally, the Pi sees no edges, and NODE 0's RX cannot see
the bus.

### 5.2 (CRITICAL) Transceiver still in standby

The TLE9371 powers up in standby.  Both the data sheet and the
Zephyr comment in `kit_a3g_tc4d7_lite.dtsi:46-47` are explicit:

> The CAN_STB pin (P03.5) must be driven low to enter normal
> operating mode.

NuttX never configures `P03.5` as an output and never drives it.
The pad floats / weakly pulls high -- i.e. standby.  In standby the
PHY's TX driver is off (line stays recessive) and RX is in low-power
wake mode and does not faithfully reflect the bus.  Loopback bypasses
the transceiver, so the symptom is exactly what we see.

### 5.3 (CRITICAL) Bit-timing stub

`can_mcan_calc_timing` (`drivers/can/can_mcan.c:55-69`) ignores
`bitrate` and writes `NBTP` for 16 tq @ prescaler-1.  On a TC4D7
`f_MCAN` of 80-160 MHz this means a 5-10 Mbit/s nominal rate.  No
classical CAN node will sync to that.  Loopback masks it because the
LBCK path short-circuits the BSP before the sample point.

### 5.4 (HIGH) Missing `ACCEN_WRA` write on TC4D7

Per the Zephyr driver comment (`can_mcmcan.c:219-226`) the reset
value of `ACCEN_WRA` on TC4Dx silicon does not always include
master tag 0; without `0xFFFFFFFF -> base+0x30` CPU0 writes to
per-node `NBTP`/`CCCR`/`IE` etc. are silently dropped.  This can
make loopback "look" like it works because `CCCR.INIT` is cleared
by reset and `TEST.LBCK` may have been left set by a previous boot
or debugger session -- symptom: "loopback works after debug attach,
fails after power cycle".

### 5.5 (HIGH) TC4x interrupt-routing field layout wrong

NuttX's `MCMCAN_GRINT1/GRINT2` macros resolve to the right
*addresses* on TC4x (`+0x2C/+0x30`) but the *field layout*
changed: TC4x uses three registers `G0INTR/G1INTR/G2INTR` with a
different group-to-nibble mapping (`can_mcmcan.c:355-389`).  Once
the bit timing and PHY are fixed, NuttX will see no IRQs (or the
wrong IRQ) on RxFIFO-new-message because the routing target nibble
holds the wrong line index.

### 5.6 (MEDIUM) `TEST.LBCK` clear may be silently dropped

The M_CAN spec write-protects `TEST` unless `CCCR.TEST=1` at the
write.  NuttX's `can_mcan_init` clears `CCCR.TEST` first
(1649-1657), then writes `TEST.LBCK=0` (1659-1671).  On a fresh
power-on this works because `TEST.LBCK` was never set.  After a
warm boot from a previous run that *had* loopback configured,
`TEST.LBCK` survives and the controller boots into loopback --
matching the original report that "loopback mode works".

### 5.7 (MEDIUM) FD vs Classic mode mismatch (next failure)

Zephyr DT requests FD with `bitrate-data=2000000`.  NuttX defconfig
sets `CONFIG_CAN_FD=y` (upper-half framing knob) but **not**
`CONFIG_CAN_FD_MODE`, so `can_mcan_init` never programs
`CCCR.FDOE | BRSE` (`drivers/can/can_mcan.c:1590,1703-1728`).
Result: AURIX would send Classic CAN frames; a Pi-side configured
for CAN-FD would refuse them with stuff errors.  Not the current
"no traffic at all" symptom -- becomes the next failure once 5.1-5.4
are resolved.

### 5.8 (LOW) Bus-off recovery state machine

NuttX exposes `CANIOC_BUSOFF_RECOVERY` (`tricore_mcmcan.c:539-541`)
but only when `CONFIG_CAN_MANUAL_RECOVERY_MODE` is set.  Otherwise
the M_CAN core auto-recovers per spec (128*11 recessive bits) --
same as Zephyr.  Not implicated in the current bug.

## 6. Recommended fix order

Each step should leave a verifiable state on the bus.

1. **Drive the transceiver enable pin.**  In `tc4d7_can.c::
   tc4d7_config_can_pins`, configure `P03.5` as a GPIO output and
   drive low *before* registering the M_CAN device.  Confirm with
   a scope: `P03.5` low.

2. **Re-target NuttX to NODE 1.**  In `tc4d7_can.c`:
   * `CONFIG_MCMCAN_NODE0_BASE_ADDR = 0xF4710600`
   * IRQ_LINE0 must point at the SRC line that NODE 1's line-0 maps
     to (Zephyr's `IRQ_CAN0_INT2`).  Pick the right number from the
     AURIX SRC table; 788 is for NODE 0.
   * `node_pin[1].present = true; node_pin[0].present = false;`
   * Confirm with the scope: a recessive-to-dominant edge on
     CAN_H/CAN_L when a TX is attempted.

3. **Add `ACCEN_WRA` write on TC4x.**  In
   `arch/tricore/src/aurix/tricore_mcmcan.c::mcmcan_module_init`,
   under `#if defined(CONFIG_ARCH_CHIP_FAMILY_TC4XX)`, write
   `0xFFFFFFFF` to `config->base + 0x30` after CLC enable but
   before the MRAM clear.  Confirm with a register read-back that
   subsequent NBTP/CCCR writes stick.

4. **Replace the bit-timing stub.**  Either implement the
   prescaler search in `drivers/can/can_mcan.c::can_mcan_calc_timing`
   (and the data-phase variant) using a `get_core_clock` op the SoC
   glue must provide, or hard-code a verified `(prescaler, ts1,
   ts2, sjw)` for 500 kbit/s @ the actual `f_MCAN` of TC4D7.  Per
   Zephyr's algorithm at `f_MCAN=80 MHz`, 500 kbit/s @ 87.5%
   sample point resolves to `prescaler=10, ts1=13, ts2=2, sjw=1`.

5. **Fix the TEST register write order.**  In
   `drivers/can/can_mcan.c::can_mcan_init`:
   1. Read CCCR, write back with `CCCR.TEST` *set*.
   2. Read TEST, clear `TEST.LBCK`, write back.
   3. Read CCCR, clear `CCCR.TEST` (along with FDOE/BRSE/MON/ASM),
      write back.

6. **Split TC4x routing register defs from TC3x.**  In
   `arch/tricore/src/tc4xx/hardware/tc4xx_mcanreg.h`, drop
   GRINT1/GRINT2; add `MCMCAN_G0INTR/G1INTR/G2INTR`.  In
   `tricore_mcmcan.c::mcmcan_configure_grint`, branch on chip
   family and program the three TC4x registers using the same
   policy as `can_mcmcan.c:355-389` (line0 = SRC_CANINT(i*2),
   line1 = SRC_CANINT(i*2+1)).

7. **Wire up a real PHY abstraction.**  Turn `phy_present=true`,
   replace the `can_transceiver_enable/disable` stubs with a
   function-pointer in `can_mcan_config` that dispatches to the
   board hook.  TC4D7 hook drives `P03.5` low on enable and high
   on disable.

8. **Resolve the FD/Classic mismatch.**  Either set
   `CONFIG_CAN_FD_MODE=y` and supply `bitrate_data` /
   `sample_point_data` in the board file, or run the bus in
   Classic mode end-to-end.  Pick one and verify both sides match.

After (1)+(2)+(3)+(4) the bus should at least carry a recognisable
bit pattern.  Steps (5)-(8) are correctness hardening.

## 7. Open questions

* **Zero CAN-related Kconfig knobs in `arch/tricore/src/tc4xx/Kconfig`
  and `tc3xx/Kconfig`.**  Every parameter (base, IRQ, NPCR, MRAM
  element counts) is a magic `#define` in `tc4d7_can.c`.  The
  `// TODO all in Kconfig` (line 45) is still TODO.  This is the
  reason the "wrong node" bug (5.1) escaped review.

* **AURIX SRC_CANINT IRQ ID 788 unverified.**  No SRC table is
  present in `arch/tricore/include/tc4xx/irq.h` (it is empty
  beyond `NR_IRQS`).  `788` was apparently lifted from a vendor
  library; whether it really is CAN0 NODE0 line-0 needs cross-
  check against the TC4D7 user-manual chapter on the IR (Service
  Request) module.  Same caveat for `364/365` on the TC397 board.

* **Zephyr's TC4x `aurix_enable_clock` short-circuits if CLC is
  already enabled** (`soc/infineon/aurix/tc4x/soc.h:17-25`).
  NuttX's version writes `CLC.DISR=0` unconditionally and spins.
  On TC4D7, if the CLC is ENDINIT-protected the unconditional
  write becomes a silent no-op and the module is never clocked.
  NuttX only toggles ENDINIT on TC3x (`tricore_mcmcan.c:633-639`).
  Whether the TC4D7 CLC is ENDINIT-protected at this register
  needs a user-manual cross-check.

* **`Ni_NPCR.RXSEL` value `0x2` for NODE 0 has no documented source.**
  Zephyr derives it from the pinctrl macro `CAN01_RX_P1_4` which
  encodes the silicon-correct RXSEL for NODE 1 P1.4 -- but that
  header (`tc4x7_com-pinctrl.h`) is not itself committed in the
  Zephyr tree we have.  Recovering the full RXSEL table from the
  AURIX user manual ("MCMCAN Module Pin Selection") is a
  prerequisite to step 6.2.

* **NuttX has no `can_mcan_set_state_change_callback` registration**
  in `tricore_mcmcan.c`, so the upper half is blind to bus-off
  transitions.  Once the bit timing is wrong (5.3) the M_CAN goes
  bus-off within microseconds and spins in the recessive-bit
  recovery loop with no feedback to userland.

* **TC397 (`tc397_can.c`) uses the same NODE 0 / hard-coded NPCR /
  no-PHY pattern.**  If it has been verified working on the TC397
  dev kit, that kit's PHY topology must differ (likely
  `P20.7/P20.8` is wired to NODE 0 directly with no STB pin), and
  the NuttX driver was implicitly tuned to the TC3x kit, not the
  TC4D7 one.  Worth sanity-checking before changing the shared
  driver.
