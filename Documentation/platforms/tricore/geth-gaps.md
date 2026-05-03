# NuttX TriCore GETH driver gaps

Audit-only document. No code changes proposed inline. Citations are
`file:line` against the working tree of branch
`dev/infineon/tricore-i2c-working` and the Zephyr Aurix branch
`aurix/dev/ifx/add_tc3x_eth` (Christoph Seitz, Infineon).

Both Zephyr trees in
`/home/parthiban/work/west/mainline/zephyr` and
`/home/parthiban/work/west/mainline/zephyr-tc4xx` were inspected;
the Seitz reference branch is present as
`remotes/aurix/dev/ifx/add_tc3x_eth` in the first one, so a real diff
was possible.

---

## 1. Symptom recap -- what is evidenced in code/commits

The user statement is "barely works". What is concretely visible in
the tree:

1. **Single squashed commit** -- `7b405182476 arch: tricore: all changes
   in one commit` rolled the whole TriCore work, including ethernet,
   into one patch. There is no commit-message history that pinpoints a
   specific known bug; the audit therefore relies on code evidence.

2. **Driver appears to be a port-in-progress of the Zephyr
   `dwc_ether_qos` driver, with parts still missing.** Indicators:

   - `xgmac_send()` only supports a single linear buffer -- see
     `/home/parthiban/work/west/mainline/nuttx/drivers/net/dwxgmac/dwxgmac.c:266-298`.
     Zephyr's equivalent walks `pkt->frags` and skips zero-length
     frags (Zephyr commit `e11c8037cfa`,
     `drivers/ethernet/dwc_ether_qos/eth_qos.c:732-758`). NuttX's
     IP/ARP path passes a single buffer, so this is not directly fatal,
     but means the TX path has had no fragment-handling work.

   - `set_link_state()` is **hard-coded to 100 Mbps full-duplex on
     XGMAC (SS=4)** --
     `drivers/net/dwxgmac/dwxgmac.c:246-248`:

     ```c
     /* 100Mbps full duplex: SS=4 for XGMAC 100M */
     eth_dwmac_mac_set_link(priv, 4, true);
     ```

     The PHY result from auto-negotiation is read in board code
     (`tc397_ethernet.c:231-241`) and printed but **never fed back to
     the MAC**. So even when a PHY negotiates 1000 Mbps, the MAC stays
     at SS=4. With the RTL8211F (which generally negotiates 1000-FD on
     a switch) the MAC and PHY will disagree on speed and the link
     will visibly come up at PHY level (`MII_MSR_LINKSTATUS` set,
     auto-neg complete) but no frames will flow.

   - **MAC speed encoding does not match what the QoS variant
     expects.** The XGMAC build path uses
     `MAC_TX_CONFIGURATION_SS` (XGMAC, see
     `drivers/net/dwxgmac/xgmac_priv.h:450-459`) and the QoS build path
     uses `MAC_CONFIGURATION_FES|PS` (`qos_priv.h:525-534`). The
     literal `4` passed in `dwxgmac.c:248` is only a valid SS value in
     the XGMAC encoding, not in the QoS encoding. A TC3xx defconfig
     (`boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/configs/nsh/defconfig:65`
     sets `CONFIG_DWXGMAC_QOS=y`) therefore links against `qos_priv.h`
     with an XGMAC-style speed value -- root-cause class bug.

   - **No PHY -> MAC linkage at all.** There is no
     `set_link_state()` call after `tc397_init_phy()` returns. PHY
     auto-negotiation runs once at boot in board code, the result is
     printed, then `xgmac_initialize()` is called with the hard-coded
     SS=4 path. There is no PHY state-change callback, no carrier
     event from the PHY, and `dwxgmac_ifup()` blindly calls
     `set_link_state(priv, true)` without consulting the PHY
     (`dwxgmac.c:478-500`).

   - **MAC address is randomly generated every boot**
     (`dwxgmac.c:549-561`) -- this alone will break ARP caches at the
     peer between reboots and is a usability regression vs. Zephyr,
     which takes the address from devicetree and from
     `phy_get_link_state` callbacks.

   - **TC3xx PHY board file uses PHY address 0**
     (`boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_ethernet.c:49`,
     `BOARD_PHY_ADDR 0`), but the Zephyr DTS for the same board uses
     `reg = <1>`
     (`boards/infineon/kit_a2g_tc397xa_3v3_tft/kit_a2g_tc397xa_3v3_tft.dtsi`
     in the Seitz tree). RTL8211F default strap on the TC3x97 TFT
     application kit puts the PHY at address 1. With `BOARD_PHY_ADDR
     0`, MDIO reads will return `0xFFFF` and the `MII_MCR_RESET`
     poll succeeds because `0xFFFF & MII_MCR_RESET` is non-zero only
     transiently. Note the `printf("phy val %x\n", phyval)` debug
     line at `tc397_ethernet.c:139` -- that is a left-over from the
     reset-poll timing out repeatedly on the wrong PHY address.

3. **TC3xx GPCTL setup is half-finished and inconsistent with the SoC
   manual.**
   `arch/tricore/src/tc3xx/tc3x_geth.c:61-70` does:
   ```c
   xgmac_modreg(TC3XX_ETH_PHY_MODE(1), TC3XX_ETH_PHY_MODE_MASK, TC3XX_ETH_GPCTL);
   /* mdio pins in alt1 */
   xgmac_modreg(BIT(1), GENMASK(1,0), TC3XX_ETH_GPCTL);
   /* all pins in alt0 */
   xgmac_modreg(0, GENMASK(10,2), TC3XX_ETH_GPCTL);
   ```
   The function is declared `static int` but **returns no value**, and
   the call site does `if (ret < 0)`
   (`tc3x_geth.c:132-134`) -- undefined behaviour. The PHY-mode value
   `1` here corresponds to RGMII (in the Zephyr wrapper, value 1 is
   RGMII; values 0,3,4 mean RMII/MII variants -- see
   `drivers/ethernet/eth_tc3x_geth.c:73-78` of the Seitz branch).
   Zephyr's wrapper writes the **interface** value `<< 22`; NuttX uses
   `TC3XX_ETH_PHY_MODE(mode) (mode << 22)` with mode=1, so this part
   matches.

   What is **missing** vs. Zephyr:

   - GPCTL and **SKEWCTL** are not zeroed before reset
     (Zephyr's wrapper writes `0` to both *before* kernel reset to
     avoid an erratum, then re-writes the mode after reset:
     `eth_tc3x_geth.c:54-56` and `:65-69`).
   - `SKEWCTL = 9` for RGMII is **not written** in NuttX
     (`eth_tc3x_geth.c:62-64` of the Seitz branch:
     ```c
     if (cfg->interface == 1) {       /* RGMII */
         sys_write32(9, cfg->base + GETH_SKEWCTL);
     }
     ```
     ). NuttX has no `TC3XX_ETH_SKEWCTL` definition at all
     (`arch/tricore/src/tc3xx/tc3x_geth.h:30-44`).
     This is the canonical RGMII timing fix on TC3xx.

   - **DMA software reset** is not applied to the QoS DMA after the
     TC3x kernel reset. Zephyr does
     `sys_write32(DMA_MODE_SWR, cfg->base + DMA_MODE)`
     unconditionally in the wrapper (`eth_tc3x_geth.c:71`) and again
     in `eth_qos_init` if `do_reset` is true
     (`eth_qos.c:947-950`). NuttX never writes
     `DMA_MODE.SWR` (`drivers/net/dwxgmac/dwxgmac.c:216-234`,
     `dma_init`). After a warm reset the DMA is in an undefined state.

4. **TC4xx HSPHY init looks plausible but pinctrl is wrong.**
   `arch/tricore/src/tc4xx/tc4x_geth.c:100-158` configures the HSPHY
   for *RMII* (the comment `// rmii` and value `BIT(29)` in
   ETH register matches an RMII selector), but
   `boards/tricore/tc4xx/triboard-tc4x7-com/src/tc4x7_ethernet.c:63-79`
   defines only **two** TX peripheral pins
   (`P16.11`, `P16.14`) and **one** RX input (`P10.2`). RMII needs
   TX_EN + TXD0 + TXD1 + RX_DV/CRS_DV + RXD0 + RXD1 + REFCLK +
   MDC/MDIO -- i.e. 7-8 pins. So either: (a) the MAC is being told
   "RMII" but the board pins are not configured, or (b) the board
   relies on a non-standard internal HSPHY routing that bypasses
   pinmux (the integrated HSPHY at `0xF2000000` does have internal
   PHY interface). In either case the asymmetry between the two TC4x
   boards (a3g-tc4d7-lite has 7 RMII pins,
   `tc4d7_ethernet.c:65-81`; triboard-tc4x7 has 4) is suspicious.

5. **CONFIG_TRICORE_GETH_BRIDGE_IRQ is mandatory on TC4xx but absent
   from the TC4D7 NSH defconfig.**
   `tc4x_geth.c:253` calls
   `irq_attach(CONFIG_TRICORE_GETH_BRIDGE_IRQ, ...)`. The Kconfig
   default is in `arch/tricore/src/tc4xx/Kconfig:164`, but
   `boards/tricore/tc4xx/a3g-tc4d7-lite/configs/nsh/defconfig` does
   not set it explicitly -- relies on default. If the default is `0`,
   `irq_attach(0, ...)` will silently grab the spurious vector and
   the bridge ISR is never invoked -> no DMA RX completion -> "ping
   never returns" symptom even though MDIO and PHY look fine.

---

## 2. NuttX TC3xx GETH bring-up -- step-by-step (file:line)

Order as actually executed at boot. Numbers refer to the path
TC397 NSH defconfig -> `tc397_appinit.c:92` ->
`tc397_ethernet_initialize()`.

| # | Step | NuttX implementation | File:line |
|---|------|----------------------|-----------|
| 1 | Pin mux for RMII/RGMII + MDIO | `tc397_config_geth_pins()` -- `gpio_pinset_t` array, all `ALT6/ALT7`, drive-strength field is **`3`** (4th positional arg) | `boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_ethernet.c:64-107` |
| 2 | Enable GETH module clock (CLC bit 0) | `init_geth()`, polls CLC.DISS bit 1 to clear, with EndInit unlock | `arch/tricore/src/tc3xx/tc3x_geth.c:76-89` |
| 3 | Set PHY-mode in GPCTL **before** reset | `init_phy_mode_input_cfg()` -- writes `mode<<22`, sets MDIO alt1, "all other pins alt0" | `arch/tricore/src/tc3xx/tc3x_geth.c:61-70` |
| 4 | Kernel reset GETH (CTRLA + CTRLB bit 0) | poll RST_CTRLA bit 1, clear RST_STAT.RSTSTAT0, EndInit dance | `arch/tricore/src/tc3xx/tc3x_geth.c:91-112` |
| 5 | Open access-protection ACCEN0/1 + per-channel ACCEN_CH | writes `0xFFFFFFFF` everywhere | `arch/tricore/src/tc3xx/tc3x_geth.c:114-122` |
| 6 | (Missing) DMA software reset | **NOT performed**. No write to DMA_MODE.SWR. | -- |
| 7 | (Missing) GETH SKEWCTL for RGMII | **NOT performed**. No `TC3XX_ETH_SKEWCTL` symbol in `tc3x_geth.h:30-44`. | -- |
| 8 | DMA TX/RX channel data init (ring head/tail, semaphores) | `dma_tx_data_init`, `dma_rx_data_init` -- descriptors zeroed, RX buffers from static `g_rxbuf_pool` | `drivers/net/dwxgmac/dwxgmac.c:146-176` |
| 9 | MAC interrupt enable (TSIE only) | `xgmac_write(MAC_INTERRUPT_ENABLE_TSIE, ...)` | `drivers/net/dwxgmac/dwxgmac.c:589-590` |
| 10 | Mask MMC counter IRQs | three `0xFFFFFFFF` writes | `drivers/net/dwxgmac/dwxgmac.c:591-593` |
| 11 | DMA bus config -- `dma_set_sysbus`, per-ch `dma_tx_init`/`dma_rx_init`, IRQ enable | uses **`cfg->ubl ? FB : 0`** but **no AAL bit and no UBL bit set** in QoS path | `drivers/net/dwxgmac/qos_priv.h:501-512` |
| 12 | MTL queue init -- RXQ, TXQ size, set_rxq_ctrl, set_mode | uses computed RQS = `size/256`, TQS in TXQ formula. RXQ size = `ALIGN_UP(CONFIG_NET_ETH_PKTSIZE, 256)` = 1792 bytes -> RQS = 7. | `drivers/net/dwxgmac/dwxgmac.c:201-214`, `qos_priv.h:583-607` |
| 13 | MAC config -- IPC + CST + ACS, packet filter PM (promisc), ext-config 0 | | `drivers/net/dwxgmac/qos_priv.h:514-523` |
| 14 | Generate random MAC, install into addr0 | new MAC every boot | `drivers/net/dwxgmac/dwxgmac.c:549-561, 599-600` |
| 15 | Attach DMA IRQ | `priv->irq = CONFIG_DWXGMAC_DMA_IRQ` (=352 in defconfig) | `drivers/net/dwxgmac/dwxgmac.c:574, 603-607` |
| 16 | `netdev_register` | | `drivers/net/dwxgmac/dwxgmac.c:609-616` |
| 17 | Init MDIO bus | `dwxgmac_mdio_bus_initialize(0, base)` | `boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_ethernet.c:279` |
| 18 | PHY: hard reset via MCR.RESET, poll, read PHYID1, expect `0x1C` (RTL8211F) | `tc397_init_phy()` | `boards/.../tc397_ethernet.c:109-148` |
| 19 | PHY: page-switch to `0xd08`, write reg 0x11 OR 0x100 -> **enable TX delay** | RTL8211F MMD page write | `tc397_ethernet.c:165-168` |
| 20 | PHY: page `0xd04`, write LCR=`0x8170`, EEELCR=`0x0` | LED config | `tc397_ethernet.c:171-174` |
| 21 | (Missing) PHY: enable RX delay | **NOT performed**. Only TX delay is set. | -- |
| 22 | PHY: poll MSR for LINKSTATUS, then write MCR with `MII_MCR_ANENABLE` (only) and poll `MII_MSR_ANEGCOMPLETE` | conditioned on `CONFIG_BOARD_PHY_AUTONEG` | `tc397_ethernet.c:178-241` |
| 23 | (Missing) Pass negotiated speed/duplex to MAC | only **printed**, never used | `tc397_ethernet.c:231-241` |
| 24 | When `dwxgmac_ifup` is called by net stack: kick RX descs, start RX DMA, start TX DMA, enable IRQ | `set_link_state(priv, true)` with hard-coded SS=4 | `drivers/net/dwxgmac/dwxgmac.c:478-500` |

### TC4xx delta (where it differs from TC3xx)

`arch/tricore/src/tc4xx/tc4x_geth.c`:

* `pms_enable_supply()` for HSIF, PHY0/1/2 -- TC3xx has no PMS/HSPHY
  step.
* `init_hsphy_for_rmii()` -- drives integrated HSPHY into RMII mode,
  including DLLCFG `BIT(31|24|23|22|16)` (POWER, RXEN, PMODE, 3.3V,
  TXEN). Comment in code says "rmii".
* `init_geth()` enables MAC port via `TC4XX_ETH_MACEN`, accepts
  per-MAC ACCEN registers, programs bridge `FWDCTRL=0` and
  `PORT0_CTRL = BIT(8) | BIT(24)` (RX ch0 + TX ch0 enable).
* Installs **bridge** ISR
  `xgmac_mac_bridge_isr` on `CONFIG_TRICORE_GETH_BRIDGE_IRQ`
  which forwards to `dwxgmac_mac_isr` only if the bridge interrupt
  status bit 16 is set
  (`tc4x_geth.c:160-172`).

### Where IRQ vs. polling lives

The driver is fully **interrupt-driven**:

* TC3xx -- direct DMA IRQ on `CONFIG_DWXGMAC_DMA_IRQ` (defconfig
  shows 352).
* TC4xx -- bridge IRQ which gates the MAC ISR; the DMA IRQ value of
  `CONFIG_DWXGMAC_DMA_IRQ` is *also* attached
  (`dwxgmac.c:602-607`), so on TC4xx **two different vectors are both
  attached to the same `dwxgmac_mac_isr`**. The bridge IRQ is the
  intended one; the direct DMA IRQ may be unused/spurious.

### MTU and descriptor sizing

* `CONFIG_NET_ETH_PKTSIZE = 1514`
  (`boards/.../nsh/defconfig:57`).
* RX/TX rings are 8 descriptors
  (`CONFIG_DWXGMAC_NUM_RX_DMA_DESCRIPTORS=8`,
  `=TX=8` -- `boards/.../nsh/defconfig:67-68`).
* RX desc buffer size programmed via
  `FIELD_PREP(DMA_CHi_RX_CONTROL_RBSZ, CONFIG_NET_ETH_PKTSIZE)`
  = 1514 (QoS path: `qos_priv.h:320`). The XGMAC path uses
  `CONFIG_NET_ETH_PKTSIZE / 8`
  (`xgmac_priv.h:260-262`) -- **two different units in the two
  variants**. The XGMAC RBSZ field is in 8-byte units, so `/8` is
  correct there; the QoS RBSZ field is in **bytes** but only valid
  values are multiples of 4, with the lowest 2 bits reserved.
  `1514 % 4 = 2`, so this is also wrong on the QoS path. The MTU
  effectively delivered to DMA is undefined.
* `aligned_data(64)` is correct for the descriptor cacheline
  (`dwxgmac.c:67-73`). No cache flush/invalidate around descriptor
  writes (NuttX TriCore is non-coherent for DMA on
  some TC4xx zones; for TC3xx the GETH bridge is in PSPR/DSPR
  region -- needs verification per chip).

---

## 3. Zephyr Seitz reference -- same structure

Source: `aurix/dev/ifx/add_tc3x_eth` (commit `ab95451071f` "drivers:
ethernet: add TC3x GETH support" + DTS additions
`df89c64377a`).

| # | Step | Zephyr implementation | File:line |
|---|------|------------------------|-----------|
| 1 | Pinmux for RGMII | DTS `eth0_rgmii_default` group sets `drive-strength = "rgmii"` on every RGMII pin (rgmii drives encoded as `pad_driver_mode = 3` in `pinctrl_soc.h`) | `boards/infineon/kit_a2g_tc397xa_3v3_tft/kit_a2g_tc397xa_3v3_tft-pinctrl.dtsi:18-44`, `soc/infineon/aurix/tc3x/pinctrl_soc.h:33-49` |
| 2 | Enable GETH module clock | `aurix_enable_clock(base + GETH_CLC, 1000)` in wrapper | `drivers/ethernet/eth_tc3x_geth.c:46-49` |
| 3 | **Zero GPCTL and SKEWCTL** before reset | erratum workaround | `eth_tc3x_geth.c:51-53` |
| 4 | Kernel reset GETH | `aurix_kernel_reset(base + GETH_KRST0, 1000)`, then `k_busy_wait(1)` | `eth_tc3x_geth.c:54-58` |
| 5 | Apply pinctrl state | `pinctrl_apply_state(eth_pins, DEFAULT)` -- pin alt-functions land **after** reset | `eth_tc3x_geth.c:59` |
| 6 | Re-write GPCTL with `interface << 22` | RGMII = 1, RMII variants = 0/3/4 | `eth_tc3x_geth.c:60-61` |
| 7 | **`SKEWCTL = 9` for RGMII** | absolute key bring-up step | `eth_tc3x_geth.c:62-64` |
| 8 | DMA software reset (`DMA_MODE.SWR`) | wrapper does it once | `eth_tc3x_geth.c:67` |
| 9 | `eth_qos_init`: optional second SWR via `cfg->do_reset`, then poll | proper reset sequence | `drivers/ethernet/dwc_ether_qos/eth_qos.c:944-953` |
| 10 | Read `MAC_HW_FEATURE0` to discover RX/TX checksum offload | sets `data->rxcoe_available`, `txcoe_available` | `eth_qos.c:955-957` |
| 11 | Init DMA channel state, DMA SYSBUS_MODE, per-ch `dma_tx_init/dma_rx_init`, IRQ enable | identical structure to NuttX, but pulls `blen`, `aal`, `fb` from DTS (`snps,burst-length=8`, `snps,address-aligned-burst`, `snps,fixed-burst-length`) | `eth_qos.c:271-303`, `eth_qos_priv.h:529-540` |
| 12 | MTL queue config | `RQS = size/256`, `TQS = (size+255)/256 - 1`, `TXQEN` set | `eth_qos_priv.h:793-844` |
| 13 | MAC config -- IPC+CST+ACS, packet filter PM, EXT_CONFIG (`PDC` for DA-duplication if requested), VLAN tag default | `eth_qos_priv.h:645-661` |
| 14 | MAC address from DTS (or driver-provided), installed into `MAC_ADDRESSi_HIGH/LOW` with optional per-DMA-channel routing | `eth_qos_priv.h:847-877`, `eth_qos.c:107-140` |
| 15 | **PHY device** is a separate Zephyr driver bound via `phy-handle` and registered as a `phy_link_callback` | `eth_qos_link_state_changed()` is called whenever the PHY status changes; it calls `eth_qos_set_link_state()` which calls `eth_qos_mac_set_link(dev, ss, fd)` with the *correct* `ss` decoded from `link_speed` | `eth_qos.c:606-633`, `eth_qos_priv.h:670-678, 710-727` |
| 16 | Per-channel TX/RX IRQ handlers via `LISTIFY(8, ...)` macros, plus a `eth_qos_common_isr` for shared/MAC/MTL | proper per-channel demux | `eth_qos.c:866-895` |
| 17 | RX path: `net_pkt_rx_alloc_on_iface` -> `net_buf` fragments -> `eth_qos_dma_rx_fill_desc` after each consumed batch | `eth_qos.c:362-475` |
| 18 | TX path: walks `pkt->frags`, **skips zero-length**, locks per-channel mutex, sets first/last bits per fragment | `eth_qos.c:691-790` |

### Zephyr DTS facts to take note of

* `geth0` at `0xF001D000`, `mdio0` at `0xF001D200`, `ptp` at
  `0xF001DB00` -- same base NuttX uses
  (`tc3x-common.dtsi:241-300`).
* `phy-connection-type = "rgmii"`, `phy-handle = <&phy>`, **PHY at
  reg `1`** (not 0).
* `snps,burst-length = <8>`, `address-aligned-burst`,
  `fixed-burst-length` -- these flow into `cfg->blen / aal / fb` for
  `DMA_SYSBUS_MODE`.

---

## 4. Diff table -- NuttX vs. Zephyr step-by-step

| Step | NuttX (file:line) | Zephyr does this differently? | Evidence |
|------|-------------------|--------------------------------|----------|
| GPCTL/SKEWCTL zeroed before reset | **No** (`tc3x_geth.c:64-69` writes mode + alt-mux first) | Yes -- writes 0 to both **before** kernel reset | Seitz `eth_tc3x_geth.c:51-53` |
| GETH SKEWCTL = 9 for RGMII | **No symbol, no write** (`tc3x_geth.h:30-44`) | Yes -- `if interface==1: SKEWCTL=9` | Seitz `eth_tc3x_geth.c:62-64` |
| DMA software reset (`DMA_MODE.SWR`) after kernel reset | **No** (`dwxgmac.c:216-234`) | Yes -- wrapper writes SWR, eth_qos_init re-asserts and polls | Seitz `eth_tc3x_geth.c:67`, `eth_qos.c:944-953` |
| MDIO clock divider (`CR` field of `MAC_MDIO_ADDRESS`) | Hard-coded `0x4` (CSR 150-250 MHz) (`dwxgmac_mdio.c:81`) -- only correct if `fgeth/CSR` actually 150-250 MHz | Sets per-clock from DTS `clocks` `csr` (Zephyr clock-control) | DTS `tc3x-common.dtsi:251`: `clocks = <&ccu CLOCK_FSPB>; clock-names="csr"` |
| PHY address | **0** (`tc397_ethernet.c:49`) | **1** (DTS `phy: ethernet-phy@1`) | `kit_a2g_tc397xa_3v3_tft.dtsi:60` |
| PHY RX delay enable | **No** -- only TX delay (`tc397_ethernet.c:165-168`) | RTL8211F enable usually requires both for RGMII unless board has on-PCB RX delay; Zephyr's PHY config is in a separate PHY driver and is generally TX+RX-delay together (RTL8211F errata: page 0xd08 reg 0x15 = RX delay) | RTL8211F datasheet; NuttX writes only reg 0x11 (TX) but never reg 0x15 (RX) |
| PHY -> MAC speed/duplex feedback | **None** -- printed only (`tc397_ethernet.c:231-241`) | Yes -- `phy_link_callback_set` registers `eth_qos_link_state_changed` which calls `eth_qos_mac_set_link(dev, ss, fd)` | Seitz `eth_qos.c:611-633` |
| Hard-coded MAC speed | `eth_dwmac_mac_set_link(priv, 4, true)` (`dwxgmac.c:248`) | Decoded from PHY: `eth_qos_mac_get_speed(link_speed)` returns 0/1/2/3 for 1G/2.5G/10/100 | Seitz `eth_qos_priv.h:710-727` |
| MAC speed encoding for QoS variant | **Mixed** -- `qos_priv.h` uses `MAC_CONFIGURATION_FES|PS` (3-bit field), but `dwxgmac.c:248` passes XGMAC value `4` | QoS-correct value used | Seitz `eth_qos_priv.h:670-678` |
| MTU/RBSZ unit | XGMAC: `RBSZ = pktsize/8` OK ; QoS: `RBSZ = pktsize` (1514) -- value invalid (lower 2 bits reserved) | `RBSZ = CONFIG_NET_BUF_DATA_SIZE` (typically 256/512/...) -- multiple of 4 | NuttX `qos_priv.h:320`, Zephyr `eth_qos_priv.h:374` |
| MAC address source | Random per boot (`dwxgmac.c:549-561`) | DTS / `data->mac_addr` filled by board init | Seitz `eth_qos.c:594-602` |
| TX path zero-length frag handling | TX is single-buffer, no frag loop (`dwxgmac.c:266-298`) | Walks `pkt->frags`, skips len==0 | Seitz `eth_qos.c:732-758` (commit `e11c8037cfa` "skip zero-length fragments in TX path") |
| RX descriptor refill | Refill loop in ISR (`dwxgmac.c:380-423`) -- refills *every* descriptor after each ISR | Refills only consumed ones, with semaphore credit, after each RX-process pass | Seitz `eth_qos.c:325-365` |
| `DMA_SYSBUS_MODE` flags | QoS path **never sets UBL** (`qos_priv.h:501-512`: `cfg->ubl ? FB : 0` -- wrong bit name; comment says UBL but it ORs FB instead). XGMAC path sets UBL correctly. | Sets `aal`/`fb`/`blen` from DTS properties | Seitz `eth_qos_priv.h:529-540` |
| MAC interrupt enable mask | `MAC_INTERRUPT_ENABLE_TSIE` only (`dwxgmac.c:589-590`) | Same TSIE for timestamp; per-DMA-channel IRQ enables include `RIE/TIE/NIE/FBEE/CDEE/AIE` | Seitz `eth_qos_priv.h:393-401, 542-552` |
| Carrier event -> net stack | Always asserts `netdev_carrier_on` in `dwxgmac_ifup` (`dwxgmac.c:497`) regardless of PHY | Driven by PHY callback | Seitz `eth_qos.c:629-632` |
| TC3xx PHY-mode selector value | `1` for RGMII (`tc3x_geth.c:64`, `TC3XX_ETH_PHY_MODE(1)`) | `1` for RGMII (`#define INTERFACE_TYPE_3 1` in `eth_tc3x_geth.c:78`) | match |
| `init_phy_mode_input_cfg` return value | Declared `int`, returns nothing (`tc3x_geth.c:61-70`) -- UB at `:132-134` | n/a (Zephyr wrapper returns 0 explicitly) | `eth_tc3x_geth.c:43-72` |
| TC4xx Bridge IRQ | Attached on `CONFIG_TRICORE_GETH_BRIDGE_IRQ`, but defconfig leaves it at default -- possibly 0 | n/a (TC3x reference)  | `tc4x_geth.c:253-257`, `boards/tricore/tc4xx/a3g-tc4d7-lite/configs/nsh/defconfig` |
| TC4xx HSPHY config | RMII-only (`tc4x_geth.c:141`: `BIT(29)` ETH register) | n/a in TC3x reference; TC4x not in scope of Seitz branch | `tc4x_geth.c:100-158` |

---

## 5. Probable root causes -- ranked

Confidence based on direct code evidence in the tree.

### Tier 1 -- these will prevent any traffic flowing

1. **PHY address mismatch on TC3xx (HIGH)** --
   `BOARD_PHY_ADDR = 0` (`tc397_ethernet.c:49`) vs. Zephyr DTS
   `reg = <1>`. Symptoms: MDIO reads return `0xFFFF`, the
   `MII_MCR_RESET` poll appears to "succeed" because the bit
   happens to be cleared in the noise pattern, PHYID1 read returns
   `0xFFFF`, then the `if (phyval != BOARD_PHYID1)` check at
   `tc397_ethernet.c:156-161` rejects with "Wrong PHYID1". If the
   `printf("phy val %x\n", phyval)` is firing repeatedly, that is
   precisely this.
   *Fix order: 1.*

2. **MAC speed not driven from PHY auto-neg (HIGH)** --
   `set_link_state` hard-codes `ss=4` (`dwxgmac.c:248`). On the QoS
   build (`CONFIG_DWXGMAC_QOS=y`) this writes `FES=0, PS=4` which is
   a reserved combination. PHY may auto-negotiate 1000-FD on the
   switch; MAC stays in some undefined / 100M state -> no frames.
   *Fix order: 2.*

3. **Missing GETH SKEWCTL=9 for RGMII on TC3xx (HIGH)** --
   `tc3x_geth.h` does not even define the register. The chip will
   power up with default skew; on most TC3x97 boards this fails RGMII
   timing validation and either no frames cross the MII or only short
   frames make it. This is the single most-cited TC3x bring-up issue.
   *Fix order: 3.*

4. **Missing PHY RX delay** --
   `tc397_ethernet.c:165-168` sets bit `0x100` of page-0xd08 reg
   0x11 (TX delay) but never writes RTL8211F page-0xd08 reg 0x15
   bit 3 (RX delay). On boards without a PCB RX-delay trace, RX
   data sample timing is wrong -> 1 in N frames received, often
   appears as "ping works once after reset, then stops".
   *Fix order: 4.*

### Tier 2 -- degrade or break specific paths

5. **DMA RBSZ field on QoS path is set in wrong units**
   (`qos_priv.h:320` writes raw `CONFIG_NET_ETH_PKTSIZE` instead of
   in 4-byte units). Effective RX buffer programmed into DMA is some
   garbage value. Either the DMA writes a too-short fragment and
   completion bit never fires, or it overruns into the next
   descriptor.
   *Fix order: 5.*

6. **`init_phy_mode_input_cfg` returns no value (UB)** --
   `tc3x_geth.c:61-70`. With certain compiler flags
   `ret < 0` may be true and the whole bring-up aborts. Easy
   one-liner.
   *Fix order: 6.*

7. **TC4xx bridge IRQ uses Kconfig default** -- if default is 0,
   `irq_attach(0, ...)` traps an unrelated vector and DMA RX
   completions are lost. Need to verify
   `arch/tricore/src/tc4xx/Kconfig:164` default value vs. the actual
   bridge SR number on TC4D7.
   *Fix order: 7.*

8. **DMA `SYSBUS_MODE` UBL/FB confusion in QoS path** --
   `qos_priv.h:501-512` ORs `FB` into the register on `cfg->ubl`,
   but the comment field is named "UBL". The current
   `g_xgmac_cfg[].ubl = true` (`dwxgmac.c:128`) therefore enables
   fixed-burst, not undefined-burst-length. Side effect: every burst
   is 8 beats (or whatever `blen=0`-default decodes to) regardless of
   DMA boundary, which is fine functionally but degrades performance
   and may trip MTL FIFO underrun/overrun on small queues.
   *Fix order: 8.*

9. **DMA software reset never asserted** -- `dma_init` does not write
   `DMA_MODE.SWR` (`dwxgmac.c:216-234`). After warm reset (e.g.,
   debugger restart, watchdog) the DMA carries stale state.
   *Fix order: 9.*

### Tier 3 -- quality-of-life / will not cause "cannot ping"

10. **MTU < 1500 not the problem** -- `CONFIG_NET_ETH_PKTSIZE = 1514`
    is correct. RXQ size is `ALIGN_UP(1514, 256) = 1792`, RQS = 7,
    valid. TXQ same.

11. **IRQ vs. polling** -- driver is fully IRQ-driven on both TC3xx
    and TC4xx; no polling fallback present. There is no mismatch
    here per se, but on TC4xx the bridge IRQ wiring is the only
    actual delivery path and it depends on Kconfig.

12. **MAC address randomised every boot** -- annoying, not blocking.
    The peer ARP cache is the user-visible symptom.

13. **MDIO clock divider `0x4` hard-coded** --
    `dwxgmac_mdio.c:81`. For a 100 MHz `fgeth` (Zephyr DTS:
    `clock-frequency = <DT_FREQ_M(150)>`,
    `tc3x-common.dtsi:248`) with the QoS divider table, `0x4` =
    "CSR 150-250 MHz" which matches. So this happens to be correct
    on TC3xx; on TC4xx the CSR clock is different and this should be
    reviewed but is not the main culprit.

14. **No cache coherency hints around descriptors** -- TC3xx DSPR is
    not cached for DMA, and the GETH base
    `0xF001D000` is in non-cacheable peripheral space. NuttX's lack
    of `up_clean_dcache` / `up_invalidate_dcache` is therefore not a
    practical blocker, but it would become one if descriptors are
    moved to DSRAM via a future refactor.

---

## 6. Recommended fix order

In order of "smallest change with biggest unblock first", and only
the items where the audit found real evidence of breakage:

1. **Fix `BOARD_PHY_ADDR`** on TC3x97 to `1`
   (`boards/tricore/tc3xx/a2g-tc397xa-3v3-tft/src/tc397_ethernet.c:49`).
   Verify with the Zephyr DTS and MDIO scan output. If PHY answers at
   address 1, ID will read `0x001C` and the `printf("phy val ...")`
   spam disappears.

2. **Drive MAC link from PHY result.** Replace the hard-coded `ss=4`
   in `set_link_state` with a value computed from the PHY status
   register read in `tc397_init_phy()` (or move that read into the
   driver and call `set_link_state(priv, true)` from there). Use the
   Zephyr `eth_qos_mac_get_speed()` table
   (`eth_qos_priv.h:710-727`) for the QoS variant.

3. **Add GETH `SKEWCTL = 9`** for RGMII on TC3xx --
   define `TC3XX_ETH_SKEWCTL` in `tc3x_geth.h` (offset 0x040), zero
   it before kernel reset, then set 9 after, conditional on RGMII.

4. **Add RTL8211F RX-delay write** --
   page `0xd08`, register `0x15`, set bit 3. Same sequence as the
   existing TX-delay write. Cross-check with Realtek datasheet for
   the exact bit (0x08 in some revisions).

5. **Fix RBSZ field unit on the QoS path** --
   `qos_priv.h:320` should program `RBSZ` as
   `(buf_size & ~0x3)` in bytes; for 1514 the value should be e.g.
   1536 (next multiple of 4 above 1514), not 1514. Better: round up
   to next 4-byte multiple or use `ALIGN_UP(CONFIG_NET_ETH_PKTSIZE,
   4)`.

6. **Make `init_phy_mode_input_cfg` actually return `int`** --
   `tc3x_geth.c:61-70`.

7. **Verify `CONFIG_TRICORE_GETH_BRIDGE_IRQ` default and add an
   explicit value to TC4D7 NSH defconfig.** Cross-check the SR number
   from the TC4D7 IR table.

8. **Replace `cfg->ubl ? FB` with the correct `UBL` bit** in
   `qos_priv.h:501-512`. Compare the bit positions in
   `dwxgmac_reg.h` to confirm `DMA_SYSBUS_MODE_UBL` is defined and
   distinct from `_FB`.

9. **Add `DMA_MODE.SWR` reset and poll** at the start of `dma_init`
   (`dwxgmac.c:216-234`). Mirrors Seitz
   `eth_qos.c:944-953`.

10. (Optional, after 1-9 work) **Take MAC address from a board
    `tc397_get_mac()` (efuse/UID derived) instead of randomising it
    every boot.**

11. (Optional, longer-term) **Wire a proper PHY driver** with a
    callback path so link state changes propagate after boot, not
    only at init time.

---

## 7. Open questions

* **TC4xx RMII pinmux discrepancy.** Why does
  `triboard-tc4x7-com/src/tc4x7_ethernet.c:63-79` define only 4
  pins while `a3g-tc4d7-lite/src/tc4d7_ethernet.c:65-81` defines 7?
  Either the TriBoard relies on the integrated HSPHY's internal
  routing (no external RMII pins), in which case the pin entries are
  legacy debug remnants, or the TriBoard config is missing pins. Need
  schematic confirmation.

* **Is the TC3x97 board actually wired for RGMII or RMII?** The
  code at `tc3x_geth.c:64` writes `mode=1` (RGMII) and the Zephyr DTS
  uses `phy-connection-type = "rgmii"`. The board pin entries
  (`tc397_ethernet.c:64-101`) include both 4 RX data pins and TXCLK,
  consistent with RGMII. But the board file comment says
  "RMII TX outputs" / "RMII RX inputs" -- the comment is wrong, the
  code is RGMII. Cosmetic.

* **`CONFIG_DWXGMAC_DMA_IRQ = 352` on TC3xx -- what SR is that?**
  From the TC397 IR table, GETH0 SR0 ("common") is what Zephyr
  attaches as `IRQ_GETH0_SR0`. Need to confirm 352 is the same
  vector after NuttX's IRQ-numbering offset. If it is offset to
  another SR, the wrong status register is read.

* **Cache coherency for descriptors on TC4xx.** Descriptors live in
  `g_rx0_descs` / `g_tx0_descs` which are static BSS. Where does
  the linker put them on the TC4D7 build? If they end up in cached
  DSPR, the lack of cache flush around descriptor ownership transfer
  will cause sporadic ownership-bit confusion. Same question for the
  `g_rxbuf_pool` (frame buffers).

* **MDIO clock divider value on TC4xx.** `0x4` (CSR 150-250 MHz) is
  hard-coded. TC4D7's GETH CSR clock is different from TC3xx's. Need
  to compute the correct divider class.

* **Missing PTP clock.** Zephyr exposes a PTP clock via
  `snps,dwc-ether-qos-ptp-clock` at `0xF001DB00`. NuttX has no
  corresponding driver. Not blocking for ping, but blocks any
  time-sync usecase.

* **`MAC_PACKET_FILTER_PM`** is set unconditionally in both
  variants (`qos_priv.h:521`, `xgmac_priv.h:446`) -- i.e., the MAC is
  put in **promiscuous mode**. This is fine for bring-up but will
  hide a class of bugs (e.g., if `eth_dwmac_set_mac_addr` is wrong,
  packets still arrive). Recommend disabling promisc once link comes
  up to validate the address path.

* **Single squashed commit history hides the bring-up trail.** The
  commit `7b405182476` is one blob; bisecting or attributing
  individual ethernet regressions in the future will need that to be
  split before the driver is upstreamed.
