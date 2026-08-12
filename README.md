# CVD1810-WJ MCU Firmware — a clean, documented reimplementation

A readable, verified, from-decompile **reimplementation** of the
power-management microcontroller (`CM68`) firmware on the **CVD1810-WJ** board —
a **MediaTek MT8163**-based Android head-unit board used across a range of
aftermarket units. The firmware is specific to this board/MCU, not to any car
brand. The MCU is a **Cmsemicon CMS8S6990** — an enhanced 1T 8051 (8-bit) —
whose primary job is **managing the head unit's power**: the SoC power rail
(P3.6), ignition/ACC sequencing, and sleep/wake. Over the same UART it also
relays some vehicle data and steering-wheel controls to the Android SoC. (It is
*not* the separate "CAN-box" update chip.)

The stock firmware is a hand-tuned assembly blob with no source. This project
rebuilds the same behaviour as **clean, domain-organised C** that anyone can
read, audit, build and extend — and, along the way, **fixes a power-management
bug** that made the unit *power off* instead of *sleeping* when the car's ACC
line drops.

---

## Why this exists

Two goals:

1. **A source you can actually read.** The original is ~12 KB of undocumented
   8051 machine code. Here it is ~2,000 lines of commented C, split by domain
   (`power`, `uart`, `frame`, `cluster`, `hwconfig`, …), with every SFR, XDATA
   cell and frame field named and explained.

2. **The sleep fix.** On ACC-off the stock MCU cuts the SoC power rail (P3.6)
   instead of holding it. The Android side has no way to keep the unit alive
   across a short ACC blip, so it cold-boots every time. This build adds a
   **keep-alive rail hold** (`NOTURNO_SLEEP_FIX`) that keeps P3.6 asserted while
   the SoC has requested keep-alive — so the unit *sleeps* and *resumes* instead
   of dying.

## Highlights

- **Behaviour-equivalent to the stock firmware**, verified path-by-path against
  the decompile (see `docs/PORT.md`), not a guess.
- **Fits the stock 12 KB APROM in pure C** — no inline assembly. Getting there
  meant respecting the 8-bit core: the byte-wise / 16-bit arithmetic the original
  used, instead of the 32-bit C-runtime helpers a naive port pulls in.
  Final size: **12,047 B** (faithful) / **12,242 B** (with the sleep fix).
- **Comprehensively simulator-tested** on the real built binary (ucsim/`s51`):
  boot, the full frame FSM, command dispatch, every control sub-command, the NAK
  path, the XOR checksum, and the sleep fix — see `docs/SIMULATION` notes below.
- **Recoverable to flash.** The chip has a resident 4 KB LDROM bootloader, so a
  bad APROM write can always be re-flashed. `docs/FLASH_RECOVERY.md` documents
  the "prove-with-stock-first" bench procedure.

## The chip

| | |
|---|---|
| Part | Cmsemicon **CMS8S6990** (CMS8S69xx family) |
| Core | Enhanced 1T 8051, **8-bit**, 24 MHz core / 48 MHz peripherals |
| Flash | 16 KB total, **APROM + BOOT configurable** (this build = 12 KB APROM + 4 KB BOOT) |
| RAM | 256 B IRAM + 1 KB XRAM; XSFR block at 0xF000–0xFFFF |

See `docs/CHIP.md` and `docs/MEMORY_MAP.md` for the full register/memory contract.

## Repository layout

```
src/                 The reimplementation, organised by domain:
  main.c               reset entry, main loop, scheduler
  power.c              rail sequencer, sleep/keep-alive (the fix lives here)
  uart.c               UART transport, RX frame FSM, TX queue
  frame.c              frame builders / parsers / checksum encoder
  cluster.c            gauges, needle/band tracking, backlight, 0x50 dispatch
  hwconfig.c           timer / peripheral / clock SFR configuration
  mathutil.c           math + delay helpers
  initimg.c            startup data-init image unpacker
  cm68_*.h             SFR / XDATA / code-table / constants contracts
cm68.uvproj          Keil C51 (µVision) project
build_variant.ps1    Command-line build driver (Keil C51 + BL51, no IDE needed)
docs/                Deep-dive documentation (see index below)
```

## Building

**Keil µVision (GUI):** open `cm68.uvproj` and Build. Requires a Keil C51
toolchain. Notes on the required project settings (the generic 8052 device needs
`ROM(LARGE)` and a couple of linker controls) are in `docs/KEIL.md`.

**Command line (recommended, deterministic):**

```powershell
# faithful build
.\build_variant.ps1

# with the sleep fix
.\build_variant.ps1 -Define NOTURNO_SLEEP_FIX
```

This invokes `C51` + `BL51` directly (3-pass global register optimisation) and
prints the final `code=` size and produces `obj/cm68.hex`.

## Verification

Every non-trivial transformation was proven, not assumed:

- Rewritten arithmetic (ADC scaling, the tick counter, the big-endian packers)
  was checked **exhaustively or over 100k+ vectors** against the original form.
- The whole binary was run in **ucsim/`s51`**: it boots to the main loop; all
  serial commands (`0x04/05/09/15/50/02`) route to the correct handler; the
  `0x50` control sub-commands (keep-alive, power-off timeout, ACC state, …) apply
  the right state; a bad checksum is rejected (NAK) while a good one dispatches;
  the version reply emits the exact embedded string; and the sleep fix holds
  P3.6 under keep-alive and cuts it otherwise.

What a simulator **cannot** prove: real peripheral timing (the vendor UART1 /
timer / XSFR blocks aren't modelled) and the physical flash write. The LDROM
recovery is the real safety net for the latter.

## Flashing & recovery — read first

⚠️ Flashing the wrong image can leave the unit unresponsive until re-flashed.
The APROM is **recoverable** via the resident LDROM bootloader, so this is not a
permanent brick — but treat it seriously. **`docs/FLASH_RECOVERY.md` is required
reading**, including the "prove-with-stock-first" procedure: flash the *stock*
image, confirm recovery works, *then* flash a modified build.

Nothing here flashes automatically. Do it on the bench, with a recovery path
ready.

## Documentation

| Doc | What |
|---|---|
| `docs/CHIP.md` | The CMS8S6990: core, flash partitioning, SFRs |
| `docs/MEMORY_MAP.md` | The frozen SFR / XDATA / IDATA / CODE contract |
| `docs/PORT.md` | Reimplementation notes, the sleep fix, fidelity decisions |
| `docs/KEIL.md` | Keil project setup, build levers, size analysis |
| `docs/FLASH_RECOVERY.md` | Flash internals, recovery, bench procedure |

## Disclaimer

This is an independent, interoperability/repair-oriented **reimplementation**.

- The **original vendor firmware binary and its disassembly/decompilation are NOT
  included** in this repository — they are third-party copyrighted material.
  Documentation refers to them only in generic terms (the stock image, the
  disassembly, the decompilation) for reverse-engineering traceability; those
  artifacts are not distributed here.
- The C in `src/` is original work: a clean re-expression of the observed
  behaviour, written to be readable and to be behaviourally equivalent.
- Provided **as-is, with no warranty**. Flashing firmware to hardware is done at
  your own risk. This project is not affiliated with, endorsed by, or supported
  by Cmsemicon, MediaTek, or any head-unit vendor.

## License

[MIT](LICENSE) © 2026 Noturno
