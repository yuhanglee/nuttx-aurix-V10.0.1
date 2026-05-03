# TriCore driver-status matrix

Single-page snapshot of every TriCore driver's state across the two
chip families and the three lab kits.  Updated 2026-04-29.

Legend:
- **N/A** -- not applicable on this family.
- **`-`** -- not implemented yet.
- **build** -- compiles in tree, no hardware-test claim.
- **loopback** -- exercised on hardware via the IP's internal loopback only.
- **physical** -- exercised against an external partner / link.
- **partial** -- works for some operations, fails or untested for others.
-  -- known runtime defect (see Notes column).

## Per-IP matrix

| Subsystem    | Driver source                              | Kconfig flag(s)                                  | TC3xx (TC1.6)              | TC4xx (TC1.8)              | TC397         | TC4D7 Lite     | Triboard       | Notes                                                                                                |
| ------------ | ------------------------------------------ | ------------------------------------------------ | -------------------------- | -------------------------- | ------------- | -------------- | -------------- | ---------------------------------------------------------------------------------------------------- |
| Boot/AMP     | `common/tricore_initialize.c` + `head.S`   | `TRICORE_AMP_NUM_CPUS`                           | physical (6 CPUs)          | physical (4 CPUs)          | physical OK    | partial        | NSH-only       | TC397 6-CPU AMP verified via LMU mailbox 2026-04-29.  TC4D7 AMP unverified on hw.                    |
| IRQ Router   | `common/tricore_irq.[ch]`                  | (always)                                         | physical                   | physical                   | OK             | OK              | NSH boots      | TC3xx uses runtime CORE_ID-as-TOS; CORE_ID==6->logical 5 quirk for TC397XA.                           |
| Watchdog     | `tc3xx/tc3x_wdt.c`, `tc4xx/tc4x_wdt.c`     | `TC4X_WDT` (no `TC3XX_WDT` Kconfig yet)          | always-on (per-CPU disable)| `TC4X_WDT` driver          | disabled      | char-dev OK    | char-dev OK    | TC3xx side has no Kconfig -- `tricore_wdt_disable()` is unconditional.  Char-dev driver TC3xx-side TODO. |
| Serial UART  | `aurix/tricore_serial.c` (ASCLIN0)         | `TRICORE_UART0`                                  | physical                   | physical                   | OK             | OK              | OK              | Shared 115200 8N1 console.                                                                           |
| GPIO         | `tc3xx/tc3xx_gpio.c`, `tc4xx/tc4xx_gpio.c` | (auto)                                           | physical                   | physical                   | OK             | OK              | OK              |                                                                                                      |
| IC          | `aurix/tricore_i2c.c` + per-family Kconfig | `TC3XX_I2Cn`, `TC4XX_I2Cn`                       | build (loopback?)          | build (loopback?)          | unverified    | unverified     | unverified     | `I2C_DRIVER` char-dev now enabled in :nsh defconfigs.  No physical sensor on bus today.              |
| SPI (ASCLIN) | `tc4xx/tc4xx_asclin_spi.c`                 | `TC4XX_ASCLIN_SPI`, `TC4XX_ASCLIN_SPI_CHARDEV`   | **N/A (no driver)**        | loopback                   | n/a           | loopback OK     | loopback?      | TC3xx port not yet written; TC4XX driver tested with `TC4XX_ASCLIN2_SPI_LOOPBACK`.                   |
| xSPI (HS)    | `tc4xx/tc4x_xspi.c` + `tc4x_hsphy.c`       | `TC4XX_XSPI`, `MTD_XSPI_NOR`                     | N/A                        | build (HSPHY DLL fails)    | n/a           | n/a            | NSH boots     | Triboard sanity 2026-04-29: HSPHY DLL lock timeout -> `tc4x7_xspi_initialize` fails -19.  Audit TODO. |
| CAN (MCMCAN) | `aurix/tricore_mcmcan.c` + Bosch MCAN core | `CAN`, `CAN_MCAN`                                | loopback                   | loopback                   | loopback      | loopback     | loopback?      | TC4D7 Lite has CAN1 wired to RPi @192.168.1.8, two-node test pending.  Zephyr also fails physical.    |
| GETH         | `tc3xx/tc3x_geth.c`, `tc4xx/tc4x_geth.c`   | `TC3XX_GETH`, `TC4XX_GETH`                       | build                    | build                    | unverified    | "barely works" | PHY ID wrong | Triboard reads PHYID1=0x0000 vs Realtek 0x001c.  Seitz Zephyr `dev/ifx/add_tc3x_eth` works upstream.  |
| HSPHY        | `tc4xx/tc4x_hsphy.c`                       | (linked when XSPI=y)                             | N/A                        | build                    | n/a           | n/a            | DLL lock fails | New from saravanan's xspi cherry-pick.                                                               |
| Clock        | `tc3xx/tc3xx_clockconfig.c`, tc4xx equiv.  | (always)                                         | static one-shot            | static one-shot            | OK             | OK              | OK              | Not yet a NuttX `clk_ops` framework; `linumiz_gitlab/dev/infineon/tricore-clk` has WIP refactor.     |
| STM (timer)  | `common/tricore_stm.c`                     | (always)                                         | physical                   | physical                   | OK             | OK              | OK              | Per-CPU timer ticks under AMP.                                                                       |
| MTD (DFLASH) | `aurix/tricore_flash.c`                    | `TRICORE_FLASH_MTD`                              | build                      | build                      | wired         | wired          | unwired        | Char-dev `/dev/dflash0` registered; physical R/W/E test pending.                                     |
| MTD (NOR)    | `drivers/mtd/xspi_nor.c`                   | `MTD_XSPI_NOR`                                   | N/A                        | build (gated by xSPI)      | n/a           | n/a            | gated on xSPI  | Stack on top of xSPI DW SSI.                                                                         |
| Crypto / RNG | -                                          | -                                                | -                          | -                          | -             | -              | -              | Not started.                                                                                         |

## Lab kit summary

| Kit          | NuttX board name           | winIDEA instance ID (host 192.168.1.2)              | TCP    | Console (kural 192.168.1.3) | Notes                                                |
| ------------ | -------------------------- | --------------------------------------------------- | ------ | --------------------------- | ---------------------------------------------------- |
| TC397 ADAS   | `a2g-tc397xa-3v3-tft`      | `com.tasking.winIDEA.instance.id-TC397`             | 5315   | TCP 9000 / `/dev/ttyUSB0`   | 6-CPU AMP verified.                                  |
| TC4D7 Lite   | `a3g-tc4d7-lite`           | `com.tasking.winIDEA.instance.id-TC4D7`             | 5316   | TCP 9001 / `/dev/ttyUSB1`   | 4-CPU AMP infra in tree, hw-untested.  CAN1RPi.    |
| TC4D7 COM    | `triboard-tc4x7-com`       | `com.tasking.winIDEA.instance.id-TC4D7 - Triboard`  | 5317   | TCP 9002 / ttyUSB2          | NSH boots; xSPI fails; Ethernet PHYID mismatch.       |

## Outstanding driver gaps (post-AMP)

The audits referenced from this matrix go in the same directory:

- `xspi-hsphy-gaps.md` -- why HSPHY DLL lock times out on Triboard.
- `triboard-phy-gaps.md` -- PHYID1=0x0000 instead of Realtek 0x001c.
- `can-gaps.md` -- physical-bus failure root cause (shared with Zephyr symptom).
- `geth-gaps.md` -- diff against Seitz Zephyr `dev/ifx/add_tc3x_eth`.

External branches that are not yet integrated:

- `linumiz_gitlab/dev/infineon/tricore-clk` -- producer/consumer clock framework, 2,577 LoC WIP.
- `linumiz_gitlab/dev/infineon/tricore-{geth,gpio,i2c,spi,uart,tc16}` -- per-IP iteration branches; rebase order TBD.
- `linumiz/zephyr-aurix:dev/ifx/add_tc3x_eth` (Seitz) -- read-only reference for GETH bring-up.
