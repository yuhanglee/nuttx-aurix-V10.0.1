# Triboard (kit_tc4x7_com_trb) Ethernet PHY-ID Mismatch -- Audit

Status: read-only audit. Captures the gap analysis between the Triboard
ethernet bring-up code in NuttX (`boards/tricore/tc4xx/triboard-tc4x7-com`)
and the Triboard hardware as described by the Zephyr reference DTS
(`zephyr/boards/infineon/kit_tc4x7_com_trb`). No code changes have been
made.

## 1. Symptom recap

Boot log on Triboard sanity test:

```
XGMAC0 initialized, MAC 02:00:00:c5:76:f3
PHY reset complete in 0 ms
ERROR: Wrong PHYID1: 0x0000 expected 0x001c
```

What the log tells us, mechanically:

- `XGMAC0 initialized` -- `tc4x_geth_init(0)` returned OK
  (`arch/tricore/src/tc4xx/tc4x_geth.c:235`). PMS supplies, HSPHY clock,
  HSPHY kernel reset, GETH clock + kernel reset, access-enable, and
  bridge port0 enable all completed without `nerr()`.
- `PHY reset complete in 0 ms` -- the first `mdio_read(MII_MCR)` after
  writing `MII_MCR_RESET` returned a value with the RESET bit clear
  (`tc4x7_ethernet.c:109-123`). The most plausible reading is that the
  read returned `0x0000` (RESET bit happens to be clear in that value),
  not that a real PHY honoured the reset in <10 ms.
- `Wrong PHYID1: 0x0000 expected 0x001c` -- the next read of
  `MII_PHYID1` (offset 0x02) at the same address returned `0x0000`
  (`tc4x7_ethernet.c:133-145`).

The DWXGMAC MDIO read returning `0x0000` is what the bus looks like
when **no PHY drives the MDIO line during the turn-around / data
phase**: the MAC clocks SBUSY low after its own internal timeout / on
SCCD completion, the data-register field is the bus idle level (0).
Compare `dwxgmac_mdio.c:158-217` -- `dwxgmac_c22_read()` waits for
`SBUSY` to clear and then samples `SCCD[15:0]`. There is no
"no-acknowledge" status bit checked in the driver, so a silent `0x0000`
is returned to the caller as `OK`. That makes "no PHY at this address"
indistinguishable from a real read of zero.

So the symptom is consistent with **the MAC is talking on MDIO, but no
PHY at address 0 is replying**.

## 2. MDIO probe sequence as coded

### Triboard board file

`boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c`

| Concern             | Location              | Value                                  |
|---------------------|-----------------------|----------------------------------------|
| PHY name            | line 47               | `"RTL8221B-CG"`                        |
| PHY address         | line 48               | `BOARD_PHY_ADDR = 0`                   |
| Expected PHYID1     | line 49               | `MII_PHYID1_RTL8211F` = `0x001C`       |
| MDC pin             | line 67               | `P16.11 PERIPH ALT10`                  |
| MDIO pin            | line 68               | `P16.14 PERIPH ALT10`                  |
| PHY reset GPIO      | lines 70, 83-90       | `P00.10` push-pull, low-pulse 10 ms then high, settle 50 ms |
| Spare input         | line 72               | `P10.2 INPUT no-pull` (intended PHY0 INT) |
| MDIO probe          | lines 92-145          | Hardcoded single address `BOARD_PHY_ADDR`; **no scan**; no PHYID2 cross-check |

So the code never scans 0..31 -- it issues exactly one `MII_MCR` write, a
small read-loop on the same address, then `MII_PHYID1` on the same
address, and bails out with `-ENXIO` if PHYID1  0x1C
(`tc4x7_ethernet.c:140-145`).

### Lite Kit board file (counter-example)

`boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_ethernet.c`

Identical structure, single hardcoded address (`BOARD_PHY_ADDR = 0`,
line 49), but expects `MII_PHYID1_DP83825I = 0x2000` (line 50).
The Lite Kit pin set covers a full RMII bundle (P16.0/1/2/4/6/8/13)
plus MDIO on **P21.2 ALT8 / P21.3 input** (lines 67-81). It also
includes an explicit `MII_ADVERTISE` write (lines 149-154) that the
Triboard file omits.

Note that the Zephyr Lite Kit DTS
(`zephyr/boards/infineon/kit_a3g_tc4d7_lite/kit_a3g_tc4d7_lite.dtsi`)
does **not** describe an Ethernet PHY at all -- there is no `&mdio0`
node, no `phy@N`, only CAN/I2C/UART/LEDs. So the Lite Kit ethernet
code is currently the un-corroborated reference; whether it has ever
actually returned a non-zero PHYID is itself an open question worth
treating as a precondition rather than a baseline.

### MDIO low-level driver

`drivers/net/dwxgmac/dwxgmac_mdio.c`

- Uses TC4xx XGMAC MDIO regs at `base + 0x200/0x204/0x220` with port
  stride `0x2000` (`dwxgmac_mdio.c:44-46`).
- For each transfer it writes `BIT(phyaddr)` into the
  `MDIO_CLAUSE_22_PORT` register (offset 0x220, see
  `dwxgmac_reg.h:447`) so that the addressed PHY is treated as
  Clause-22 (`dwxgmac_mdio.c:177, 238`).
- The MDC clock divider is **never written** in this code path -- the
  driver leaves MDIO_SCCD bits CR[5:2] / CR[1:0] at their reset value.
  Only the `_QOS` branch (TC3xx EQOS) sets a clock divider
  (`EQOS_MDIO_CR_DEFAULT = 0x4`, `dwxgmac_mdio.c:81`).
- Silently returns `0x0000` on read of a non-responding PHY: there is
  no NACK / read-error check after `dwxgmac_mdio_wait_idle()`
  (`dwxgmac_mdio.c:184-192`).

## 3. Probable root causes -- ranked

### 3.1 Wrong MDIO address (highest confidence)

The Zephyr Triboard DTS places the two RTL822x PHYs at **address 1 and
address 2**, with reset GPIOs P00.10 (PHY @ addr 1) and P00.11 (PHY @
addr 2):

```
zephyr/boards/infineon/kit_tc4x7_com_trb/kit_tc4x7_com_trb.dtsi:65-84
  &mdio0 {
      pinctrl-0 = <&geth0_p0_mdio>;
      geth0_phy: phy@1 {
          compatible = "realtek,rtl822x";
          reg = <1>;
          reset-gpios = <&p00 10 GPIO_ACTIVE_LOW>;
          int-gpios   = <&p10 2  GPIO_ACTIVE_LOW>;
          ...
      };
      geth1_phy: phy@2 {
          compatible = "realtek,rtl822x";
          reg = <2>;
          reset-gpios = <&p00 11 GPIO_ACTIVE_LOW>;
          int-gpios   = <&p10 3  GPIO_ACTIVE_LOW>;
          ...
      };
  };
```

NuttX uses `BOARD_PHY_ADDR = 0`
(`tc4x7_ethernet.c:48`) and has only one reset GPIO (P00.10, the PHY-0
strap line) -- meaning even the GPIO that asserts and de-asserts is the
reset of the PHY at addr 1, not addr 0. Combined with "MDIO read of a
non-responding address returns 0", the symptom is exactly what would
be expected.

This is rank-1 because the Zephyr DTS is upstream board-vendor-authored
metadata, and because every other observable (`PHYID1=0x0000`, MCR
"reset" bit clearing instantly) is consistent with no PHY at address 0.

### 3.2 Wrong PHY part assumed (RTL8211F vs RTL822x) (medium confidence -- tail risk)

`BOARD_PHY_NAME` is `"RTL8221B-CG"` (RTL822x family, used in 2.5G PHYs)
but `BOARD_PHYID1` is `MII_PHYID1_RTL8211F = 0x001C`
(`include/nuttx/net/mii.h:920-922`, `tc4x7_ethernet.c:47-49`).

Realtek RTL8211F and RTL822x do **share** PHYID1 = 0x001C (the prefix
register only carries the upper 22 bits of the OUI, which is Realtek's
00-E0-4C in either case), so the constant happens to be acceptable for
both parts. However:

- After PHY ID matches, the code unconditionally programs RTL8211F
  paged registers (`tc4x7_ethernet.c:149-157`). Those page numbers
  (0xd08, 0xd04) and the LCR/EEELCR offsets are RTL8211F-specific.
  RTL8221B (Clause-45 capable, 2.5G) uses a different MMD model. So
  even if PHY-ID matched today, the configuration writes that follow
  are wrong for RTL822x.
- The Zephyr DTS uses `phy-connection-type = "usxgmii"` with the link
  going through an XPCS (`kit_tc4x7_com_trb.dtsi:86-100`). The Triboard
  RTL8221B is a 2.5GBASE-T PHY connected via USXGMII / XPCS -- *not*
  via RGMII or RMII -- yet `tc4x_geth.c:100-158` calls
  `init_hsphy_for_rmii(intf)` and selects RMII via
  `TC4XX_HSPHY_ETH(port)` bits (`BIT(29)`).

So even after fixing the MDIO address, the PHY-side configuration and
the MAC-PHY interface mode are mismatched. PHYID1 is incidental; the
deeper issue is that this driver was clearly cloned from an RTL8211F /
RGMII-or-RMII path and has not been adapted to RTL822x / USXGMII.

### 3.3 PHY power / reset GPIO timing (medium confidence)

Reset sequence in `tc4x7_phy_reset()` (`tc4x7_ethernet.c:81-90`):

```
aurix_gpio_write(rstpin, false);   /* assert */
up_mdelay(10);
aurix_gpio_write(rstpin, true);    /* deassert */
up_mdelay(50);
```

RTL8221B-CG datasheet (per analogous Realtek 1G/2.5G family): minimum
RST# low time is in the order of 10 ms but the PHY needs ~70-150 ms
**after** reset deassertion before MDIO is responsive (XTAL stabilise,
LDO ramp, internal POR). 50 ms post-deassertion is borderline -- and
the PHY-MCR poll loop has a 10 ms granularity with a 100 ms total
budget (`tc4x7_ethernet.c:51, 109-123`), which can mask whether the
PHY came up at all.

This is rank-3 because, even with this timing, an RTL8211F at address
0 would still answer; the symptom is "PHYID1 = 0x0000", not "PHYID1 =
0xFFFF" (which is what a still-resetting PHY can look like) or a flaky
intermittent value. Reset timing is a likely contributing factor but
not the headline cause.

Additional concern in the same function: the reset pin is driven by
`aurix_gpio_write()` immediately after `tc4x7_config_geth_pins()` ran;
the configure step put it as `GPIO_OUTPUT, GPIO_ALT0` with no defined
initial level (`tc4x7_ethernet.c:70`). If the AURIX P00.10 OUT register
default is 0, the reset is **already asserted** during `config_gpio()`
and remains asserted through the entire `tc4x_geth_init()` MAC-bring-up
window -- many tens of ms -- before `tc4x7_phy_reset()` even runs and
delivers another low-pulse. That is a long unattended assert and
possibly a glitch when the asserting thread changes. Use
`AURIX_GPIO_FULL(...)` with an explicit `GPIO_OUTPUT_HIGH` initial
value to keep the PHY out of reset between configure and the
controlled pulse.

### 3.4 MDC/MDIO pin-mux ALT mismatch (low-medium confidence)

NuttX selects:

```
P16.11 PERIPH ALT10  (intended GETH0_P0_MDC)
P16.14 PERIPH ALT10  (intended GETH0_PX_MDIO)   <-- output? input?
```

Two concerns:

- P16.14 is the bidirectional MDIO line. Coding it as `GPIO_PERIPH,
  GPIO_ALT10` selects an output ALT -- for an open-drain bidirectional
  signal it must be configured as either an open-drain peripheral
  output (PCx mode that lets PHY drive low) or as `GPIO_PERIPH` *with*
  the input direction enabled. Compare the Lite Kit, which configures
  MDIO as `GPIO_INPUT, GPIO_PULL_NONE` (`tc4d7_ethernet.c:80`) -- i.e.
  the OUT-direction is controlled by the IOCR but the input path
  remains enabled. The Triboard wording (`PERIPH ALT10` only, no input
  fall-back) may have the input buffer disabled, making MDIO read
  always return 0.
- The Zephyr pinctrl group is named `geth0_p0_mdio` for **port 0** and
  `geth0_p1_mdio` for **port 1**; both share `MDIO_P16_14` but use
  different MDC pins (`GETH0_P0_MDC_P16_11` vs
  `GETH0_P1_MDC_P16_11`). This implies the same physical MDC pin can
  be muxed to either MAC port's MDIO controller via different ALT
  selectors. ALT10 is what NuttX uses today; without the AURIX TC4D7
  port-output-select table (not present in this NuttX tree under
  `arch/tricore/src/tc4xx/hardware/`) the assertion that ALT10 ==
  GETH0_P0_MDC is unverified inside this repo. Treat ALT10 as
  "matches what works on the Lite Kit board file's analogous port"
  rather than "verified from datasheet".

### 3.5 MDIO clock divider too aggressive (low confidence)

`dwxgmac_mdio.c:158-217` does not write the CR (clock range) field of
SCCD before kicking a transaction. Reset value is implementation
defined; the IEEE max MDC frequency is 2.5 MHz and most XGMAC
implementations default to a divider that keeps MDC  2.5 MHz only
when the CR field is correctly programmed for the **current** APB /
CSR clock. After the system PLL bring-up that this driver assumes is
already in place, the reset value of CR may correspond to an MDC
frequency above the PHY's tolerance.

This is rank-5 because:
- Realtek RTL822x family tolerates MDC up to 25 MHz, so even an
  aggressive divider would normally still be readable.
- The same driver at the same divider works on the Lite Kit board file
  (per user report), strongly suggesting the bus speed is not the
  blocker.

## 4. Pointers to verify each cause

### 4.1 Wrong MDIO address

- Cross-check by sweeping all 32 addresses in `tc4x7_init_phy()`:
  loop `phyaddr = 0..31`, read `MII_PHYID1`/`MII_PHYID2`, log every
  non-zero / non-0xFFFF. The address that returns `0x001C, 0xC8nn` is
  the live PHY. Expected outcome based on Zephyr DTS: address **1**
  (port 0 PHY) and possibly **2** (port 1 PHY).
- Read from the `MDIO_PORT_CONNECT_DISCONNECT_STATUS` register at
  `base + 0x21C` (`dwxgmac_reg.h:445-447` neighbourhood). XGMAC
  IP variants populate this with a per-port bitmask of "PHY detected"
  if the driver enables MDIO link-detect; this can confirm where
  hardware sees a PHY without explicit reads.
- Check the AURIX TC4D7 ESS / TC4x7 COM TRB schematic for the
  PHYAD0..PHYAD2 strap pins on the RTL8221B and whether they are tied
  to result in address 1 (PHYAD = 001b).

### 4.2 PHY part / interface

- Read `MII_PHYID2` (register 0x03) at the resolved address. RTL8211F
  is `0xC916`; RTL8221B is `0xC8nn` (varies by silicon revision).
- If the read confirms RTL822x, replace the page-0xd08/0xd04 setup at
  `tc4x7_ethernet.c:149-157` with the RTL822x MMD-based equivalents
  (LED, EEE, page select via 0x1F is removed in 822x -- uses C45 /
  MMD).
- Re-evaluate the MAC-PHY mode: `tc4x_geth.c:100-158` configures HSPHY
  for RMII (`init_hsphy_for_rmii`, `BIT(29)` in
  `TC4XX_HSPHY_ETH(port)`). The Triboard wires the RTL8221B over
  USXGMII through the on-die XPCS (`kit_tc4x7_com_trb.dtsi:86-100`),
  which means the HSPHY_ETH bits and `TC4XX_HSPHY_CMNCFG` must select
  the XPCS / 10G-USXGMII path, not RGMII/RMII.
- Grep targets:
  - `grep -nE 'init_hsphy_for_rmii|HSPHY_ETH|HSPHY_CMNCFG|USXGMII|XPCS' arch/tricore/src/tc4xx/`
  - Confirm whether the AURIX HSPHY has an `init_hsphy_for_usxgmii()`
    sibling or whether one needs to be authored.

### 4.3 PHY reset timing / GPIO default level

- Add a 200 ms post-deassert delay in `tc4x7_phy_reset()`
  (`tc4x7_ethernet.c:89`) and re-test. If PHYID1 starts reading back
  correctly, the issue was reset-recovery time.
- Change the reset-pin descriptor at `tc4x7_ethernet.c:70` to use
  `AURIX_GPIO_FULL(...)` with an explicit `GPIO_OUTPUT_HIGH` initial
  level so that the PHY is **not** held in reset across
  `tc4x_geth_init()`. Verify on a scope: P00.10 must be high through
  all of `init_pms_supplies -> init_hsphy_for_rmii -> init_geth ->
  xgmac_initialize` and only pulse low inside `tc4x7_phy_reset()`.
- Probe RST# on the Triboard PHY with a logic analyser; verify both
  width and absolute level vs the PHY datasheet.

### 4.4 Pin-mux verification

- For P16.14, change the descriptor at `tc4x7_ethernet.c:68` to mirror
  the Lite Kit MDIO-input pattern:

  ```
  AURIX_GPIO(GPIO_PORT16, GPIO_PIN14, GPIO_INPUT, GPIO_PULL_NONE)
  ```

  ...if the AURIX IOCR uses input-direction-with-peripheral-driven
  output for bidirectional signals (this is the TC3xx/TC4xx pattern
  for I2C/MDIO).
- Cross-reference the AURIX TC4D7 user manual port-output-select table
  for `GETH0_P0_MDC` / `GETH0_PX_MDIO` to confirm that `ALT10` is the
  correct PCx selector. Without that table in-tree, this requires the
  vendor datasheet PDF.
- Check on a scope: MDC must toggle at the configured rate during a
  read; MDIO must show preamble + ST + OP + PHYAD + REGAD. If MDC is
  silent, the mux is wrong; if MDC toggles but MDIO never gets driven
  low by the PHY in the data phase, the PHY is at a different address
  (case 4.1).

### 4.5 MDIO clock divider

- After fixing the MDIO address, if reads are still 0x0000 instrument
  the SCCD register: `xgmac_read(base + DWXGMAC_MDIO_SCCD(port))` and
  log its CR field. Compare against the datasheet table for
  CSR-clock-to-MDC-divider. Add an explicit divider write
  (`dwxgmac_mdio.c:158-217`) before the first transaction.

## 5. Recommended fix order (concrete, code-level)

Each step is defensive: do them in order; stop when the symptom changes.

1. **Make `tc4x7_init_phy()` scan, then bind**
   File: `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c`
   Lines: 92-145.
   - Iterate `phyaddr = 0..31`.
   - For each, read `MII_PHYID1` and `MII_PHYID2`.
   - Log every (`addr`, `id1`, `id2`) tuple where `id1 != 0x0000 &&
     id1 != 0xFFFF`.
   - On first match, set the working `phyaddr` and continue the
     existing init flow.
   - This is a temporary diagnostic -- but the boot log it produces is
     the ground-truth needed to set `BOARD_PHY_ADDR` deliberately.

2. **Make the reset pin drive high by default**
   Same file, line 70:

   - Change `AURIX_GPIO(GPIO_PORT0, GPIO_PIN10, GPIO_OUTPUT,
     GPIO_ALT0)` to use `AURIX_GPIO_FULL(0, 10, GPIO_OUTPUT, GPIO_ALT0,
     0, GPIO_OUTPUT_HIGH, 0, 0, 0)` so the PHY is held out of reset
     during the rest of board init.
   - Bump the post-deassert delay at line 89 from `up_mdelay(50)` to
     `up_mdelay(200)`.

3. **Set `BOARD_PHY_ADDR = 1`**
   Same file, line 48: change to the value confirmed by step 1
   (expected `1` per the Zephyr DTS).

4. **Add PHYID2 cross-check**
   Same file, after line 138: read `MII_PHYID2` too and compare both
   against the chosen part. Log both. This makes future regressions
   noisy instead of silent.

5. **MDIO pin descriptor**
   Same file, line 68: if step 1 still reads `0x0000` everywhere,
   change the MDIO pin descriptor to match the Lite Kit's bidirectional
   MDIO pattern:
   `AURIX_GPIO(GPIO_PORT16, GPIO_PIN14, GPIO_INPUT, GPIO_PULL_NONE)`.

6. **HSPHY mode**
   File: `arch/tricore/src/tc4xx/tc4x_geth.c`, lines 100-158.
   - `init_hsphy_for_rmii()` is misnamed for the Triboard. Once
     PHY-ID is being read correctly, audit the bit pattern in
     `TC4XX_HSPHY_ETH(port)` and `TC4XX_HSPHY_CMNCFG` against the
     AURIX TC4D7 spec for USXGMII via XPCS, and either gate the RMII
     setup behind a `Kconfig CONFIG_TC4XX_GETH_RMII` or factor a
     parallel `init_hsphy_for_usxgmii()` and call it from
     `tc4x_geth_init()` based on board choice.
   - Until the Triboard MAC-PHY interface is configured for USXGMII,
     even with valid MDIO transactions the data plane will not link.

7. **Replace RTL8211F page writes with RTL822x equivalents**
   File: `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c`,
   lines 149-157.
   - Either gate them behind a check that the resolved part is in
     fact RTL8211F-compatible (PHYID2 == 0xC916), or replace them with
     the RTL822x MMD register sequence the part actually requires.

8. **(separate audit) MDIO no-ack detection in driver**
   File: `drivers/net/dwxgmac/dwxgmac_mdio.c`, lines 158-217. After
   `dwxgmac_mdio_wait_idle()` succeeds, check whether the IP
   exposes a "MDIO transfer error" / "no-ack" status bit (some
   Synopsys XGMAC variants put one in `MDIO_INTERRUPT_STATUS` or in
   SCCD bit 22 once SBUSY clears). Plumb that into a real `-EIO`
   return so the upper layers can no longer mistake "no PHY" for
   "PHY returned 0".

## 6. Open questions (need datasheet / scope / schematic)

1. **PHYAD strapping on the Triboard.** The Zephyr DTS asserts addr 1
   and addr 2; confirm with the Infineon kit_tc4x7_com_trb schematic
   that the RTL8221B PHYAD pins are strapped to those values (and
   identify whether MDIO0 sees both or only one).

2. **MAC-PHY interface mode.** The Triboard wires the RTL8221B to
   the AURIX through XPCS / USXGMII per Zephyr. Confirm with the
   board schematic what RGMII/RMII pins (if any) are populated. The
   NuttX driver currently configures RMII; if the board has no RMII
   wiring this can never link even after PHY-ID reads correctly.

3. **AURIX TC4D7 port-output-select for `GETH0_P0_MDC` /
   `GETH0_PX_MDIO`.** Confirm `ALT10` is correct for both pins
   (P16.11 MDC, P16.14 MDIO) and whether MDIO requires open-drain
   enable in the IOCR. The `arch/tricore/src/tc4xx/hardware/`
   directory in this NuttX tree does not include the port-mux table.

4. **Reset GPIO polarity.** Both the Zephyr DTS
   (`reset-gpios = <&p00 10 GPIO_ACTIVE_LOW>`) and the NuttX code
   (assert by `aurix_gpio_write(rstpin, false)`) treat RST# as active
   low. Confirm against schematic that no inverter is on the board.

5. **MDC clock**
   Capture MDC on a scope after `dwxgmac_mdio_bus_initialize()` and a
   single `mdio_read()`. If MDC is silent, the SCCD CR field is
   misconfigured or the MDIO clock gate is closed; this is a
   dwxgmac_mdio.c-level fix, not a board fix.

6. **Lite Kit ground truth.** The Zephyr Lite Kit DTS does not have
   any ethernet PHY described. Confirm with hardware whether the
   tc4d7_ethernet.c bring-up has actually reported a non-zero PHYID
   on real silicon, or whether the Lite Kit ethernet code is itself
   un-validated. If un-validated, it is a poor reference for any
   "the Lite Kit pattern works" assumption used to debug Triboard.

## 7. References (file:line)

NuttX source:

- `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c:47-90`
  PHY constants, pin set, reset routine.
- `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c:92-145`
  PHY reset / PHYID1 read / mismatch error path.
- `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c:238-263`
  `tc4x7_ethernet_initialize()` -- order of pin-config, reset, MAC
  init, MDIO init, PHY init.
- `boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_ethernet.c:47-87`
  Lite Kit pin set -- note `P21.2 ALT8 / P21.3 INPUT` for MDIO and
  full RMII bundle on P16.
- `boards/tricore/tc4xx/a3g-tc4d7-lite/src/tc4d7_ethernet.c:89-230`
  Lite Kit PHY init -- `MII_ADVERTISE` write at 149-154 absent on
  Triboard.
- `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_appinit.c:90-92`
  Calls `tc4x7_ethernet_initialize()` from `board_app_initialize()`.
- `arch/tricore/src/tc4xx/tc4x_geth.c:100-158`
  `init_hsphy_for_rmii()` -- RMII bit pattern in `TC4XX_HSPHY_ETH`.
- `arch/tricore/src/tc4xx/tc4x_geth.c:235-260`
  `tc4x_geth_init()` order: PMS supplies, HSPHY, GETH, MAC, IRQ.
- `arch/tricore/src/tc4xx/tc4x_geth.h:30-68` Register bases for
  GETH (`CONFIG_TRICORE_GETH_BASE`, default `0xf9000000`), HSPHY
  (`0xF2000000`), bridge.
- `drivers/net/dwxgmac/dwxgmac_mdio.c:42-52` MDIO register layout
  for the non-QOS XGMAC.
- `drivers/net/dwxgmac/dwxgmac_mdio.c:158-217` `dwxgmac_c22_read()` --
  silent `0x0000` on no-ack.
- `drivers/net/dwxgmac/dwxgmac_mdio.c:274-289`
  `dwxgmac_mdio_bus_initialize()` -- port range, bus register.
- `drivers/net/dwxgmac/dwxgmac_reg.h:447`
  `MDIO_CLAUSE_22_PORT = 0x220`.
- `include/nuttx/net/mii.h:920-938` RTL8211F constants.
- `include/nuttx/net/mii.h:429` `MII_PHYID1_DP83825I = 0x2000`.
- `boards/tricore/tc4xx/triboard-tc4x7-com/configs/nsh/defconfig:34-52`
  `CONFIG_NET_DWXGMAC=y`, `CONFIG_NET_DWXGMAC_MDIO=y`,
  `CONFIG_TC4XX_GETH=y`. No `CONFIG_BOARD_PHY_AUTONEG` -- the autoneg
  branch in `tc4x7_ethernet.c:159-223` is dead in this build.
- `arch/tricore/src/tc4xx/Kconfig:160-167` `CONFIG_TRICORE_GETH_BASE`
  (`0xf9000000` default), `CONFIG_TRICORE_GETH_BRIDGE_IRQ` (760).
- `drivers/net/dwxgmac/Kconfig:17-19` `CONFIG_DWXGMAC_BASE`
  (`0xf9010000` default = GETH base + 0x10000, MAC0).

Zephyr reference (read-only, off-tree):

- `zephyr/boards/infineon/kit_tc4x7_com_trb/kit_tc4x7_com_trb.dtsi:65-100`
  PHY at addr 1 / 2, RTL822x, USXGMII, XPCS.
- `zephyr/boards/infineon/kit_tc4x7_com_trb/kit_tc4x7_com_trb-pinctrl.dtsi:20-38`
  MDC = P16.11, MDIO = P16.14 -- same pin numbers as NuttX uses.
- `zephyr/boards/infineon/kit_a3g_tc4d7_lite/kit_a3g_tc4d7_lite.dtsi`
  No ethernet / PHY / MDIO node defined (cross-check ground for the
  "Lite Kit ethernet works" assertion).
