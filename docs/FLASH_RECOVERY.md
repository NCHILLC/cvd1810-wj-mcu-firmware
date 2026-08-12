# cm68 (CVD1810-WJ) MCU — Flash & Recovery Analysis

Scope: how the head unit flashes the cm68 8051 MCU firmware over `/dev/ttyS3`, and
whether a bad/interrupted flash is recoverable **from software alone**. This gates
whether it is safe to flash a patched image.

Sources analysed:
- Firmware: the decompilation, the disassembly (Ghidra 12.1.2 headless decompile of
  the 12 KB APROM app image; update payload).
- Host app `com.hcn.mcuupgrade` (`UpgradeActivity`) — carved from the device:
  - a CompactDex (`cdex001`) inside `essgoo/system.img` (the shipping build), and
  - a richer standard DEX (`dex\n039`) at eMMC offset `0xC5CB8034` in
    `FW250111/EMMC_USER` (a multi-chip build of the same app — CM68/CM58/APM32/SP05/MS51/…).
- The framing/serial layer `android/carsource/McuService` + `McuManager` (same DEX pool).

---

## EXECUTIVE VERDICT

**RECOVERABLE (high confidence, not 100 % proven).** A bad, partial, or interrupted cm68
flash is re-flashable from software (power-cycle + re-run the updater / let it re-enter
IAP) **provided the residual risk below does not apply**. The design is a classic
**IAP-via-resident-bootloader** with **verify-before-commit**:

- The running application firmware **does not program flash itself** — the flashed 12 KB app
  image contains **zero flash-write code**. The code that erases/writes/verifies is **resident
  in a region the updater never touches** (only the stock firmware image = the 0x3000 APROM is
  written). That resident writer therefore always survives an app update — this is the single
  structural fact that makes recovery possible.
- The writer **checksum-verifies the whole image before committing** (host sends a 32-bit
  checksum; MCU replies `FILE_STATUS_CRC_OK`/`IAP_CRC_FAIL`). A partial/wrong write is
  *rejected*, so it never produces a "confidently running bad app"; the MCU stays in IAP
  announcing `IAP_IS_READY`.
- The MCU can **spontaneously raise IAP**: `McuService.onMcuIAP` **auto-launches** the
  updater when the MCU signals it. So a MCU that comes up in IAP (e.g. after a failed flash)
  triggers an automatic re-flash attempt without the app firmware needing to be alive.
- The host has explicit recovery paths: `CMD_KEY_REDO_IAP`, `MSG_WHAT_REUPLOAD_FILE`,
  `MSG_SEND_REBOOT_AGAIN` ("send reboot cmd again"), per-command resend counters, and a fast
  path that **skips the version/checksum gate when the MCU is already `IAP_IS_READY`**
  ("remove checksum check when receive iap ready status").

**The one residual risk (see §6):** the bootloader binary (LDROM) itself was **not available
in any dump** — it lives in the MCU's internal protected flash, not on eMMC. So the exact
*boot-time entry condition* (auto-enter on invalid APROM vs. serial knock vs. app-set flag)
could not be read directly. Every piece of *host-side* evidence points to an
auto-re-enterable loader, but this must be **proven empirically with a stock image first**
(§7) before trusting it with a patched image — especially against a **power-loss** (not just
a clean software abort) during the erase/write window.

---

## 1. Architecture: IAP is done by a resident bootloader, not the app

Decisive firmware evidence — the 12 KB APROM app contains **no flash self-programming code**:

- The 8051 flash write-enable is gated by the guard key `HKEY = SFR 0x96` (`AA` then `55`).
  **Every** `HKEY` unlock in the entire disassembly is followed *only* by a `HADDR
  (SFR 0x97)` bit operation, never by a data-programming loop:
  - `0x0085`: unlock, `HADDR |= 2`
  - `0x05e4` (`FUN_CODE_05e4`): unlock, `HADDR |= 1` — **watchdog feed**; called every main
    loop iteration in `FUN_CODE_0795` (`while(true){ FUN_CODE_05e4(); … }`).
  - `0x262d` (`FUN_CODE_262d`): unlock, `HADDR &= 0x7f`, unlock, then `while(true)` spin —
    **deliberate watchdog/software reset**.
  Disasm confirms these are the *only* four `MOV 0x96,#0xaa/#0x55` sites (0x0085, 0x05e9,
  0x05f5, 0x2633). No SFR anywhere receives a stream of image bytes.
- The app never transfers control outside its own `0x0000–0x2FFF` region (0 calls/jumps
  ≥ 0x3000; all high addresses are XRAM `0xF0xx` peripheral space). It hands off to the
  loader purely by resetting.
- Host-side, the eMMC only ever stores ~12 KB **APROM app images** (`mcu_found/*.bin`,
  the stock firmware image = 0x30-byte header + 0x3000 code). **No loader/LDROM binary exists on the
  Android side** — confirming the bootloader is resident in the MCU's protected flash.

Therefore the app's *entire* role in flashing is: receive the IAP command → reset → the
resident bootloader takes over the UART and does the erase/program/verify.

### The IAP entry command (firmware ⇄ host, reconciled end-to-end)
- Host constant `GID_IAPMCU = 21 = 0x15`. Host log `requestMcuIAP:1` ⇒ it sends GID `0x15`,
  sub-byte `0x01`.
- Firmware dispatcher `FUN_CODE_0e39` (decomp ~line 2364): reads the command byte
  `FUN_CODE_01c4(3)`; for `0x15`, if the sub-field `FUN_CODE_01c4(4) == 1` it calls
  `FUN_CODE_1ff3(1)` then `FUN_CODE_262d()` (reset). This is exactly the host's
  `requestMcuIAP:1` → MCU reboots into the loader.
- `FUN_CODE_1ff3` writes SFR `0x8e` (bits 5–7). SFR 0x8e is a **multi-field peripheral
  control register** (other routines write bits 3–4), *not* a provable "stay-in-bootloader"
  magic flag — so the app image alone does not reveal the loader-entry handshake.

---

## 2. Host flow (`com.hcn.mcuupgrade.UpgradeActivity`)

The app does **not** touch the serial port directly. It calls
`android/carsource/IMcuManager.requestMcuIAP` (Binder txn 13) into the system **McuService**,
which frames bytes (`A0 55 … XOR`, 115200 8N1) onto `ttyS3`, and receives replies via
`IMcuListener.onByteEvent` (txn 2). Relevant constants (from the DEX static pools):

| Constant | Value | Meaning |
|---|---|---|
| `GID_IAPMCU` | `21` (0x15) | IAP channel / reboot-to-IAP command |
| `GID_VERSION` | `4` | version query (`onGetMcuVersion`) |
| `EVENT_MCU_IAP` | `8` | internal McuManager event id |
| `MCU_MAX_COMMAND_BYTES` | `264` | max frame *capacity* (buffer cap, **not** the block size) |
| `SOCKET_OPEN_RETRY_MILLS` | `4000` | serial reconnect retry period |
| `TRANSACTION_requestMcuIAP` | `13` | McuService Binder txn |

`requestMcuIAP(subCmd, data)` = `McuService.send(0x15, subCmd, data)`; the native `mcud`
daemon adds the `A0 55 … XOR` framing. **Safety interlock:** while `mMcuUpgradeMode` is true,
`send` transmits **only** GID `0x15` frames — all other MCU traffic is dropped. The three IAP
frames (bytecode-confirmed payload layouts):

- **Enter IAP:** `subCmd=1`, empty data (`requestMcuIAP:1`).
- **Write block (`sendFile`):** `data = addr[4 LE] + len[1]=0x40 + 64 data bytes` = **69-byte**
  payload. `addr = (blockIndex-1)*64`. ⇒ **block size = 64 bytes**, absolute byte addressing,
  length byte always `0x40`.
- **Checksum (`sendCheckSum`):** `data = checksum[4 LE] + 2 bytes` = 6-byte payload. A
  **32-bit checksum** accumulated over the whole `.bin` buffer; the MCU re-verifies.

State machine (Handler messages in `UpgradeActivity$2.handleMessage`) and the observed
string flow:

0. **Auto-launch (recovery/entry).** When the MCU signals IAP, `McuService.onMcuIAP`
   **auto-starts `com.hcn.mcuupgrade`** and posts `what=1` ("MCU IAP mode"). This fires even
   if the app was not running — the MCU can pull the host into a flash.
1. **Locate file** — `loadFile` / `readFileToIAP`, searching `/system/data`,
   `/storage/emulated/0`, `/storage/udisk1|2` for the stock firmware image (fallback other
   `*_update.bin`). `MSG_WHAT_NO_CORRECT_FILE` if none. **No version-compare in the flash
   path itself** — once a `.bin` is present and the MCU is in IAP, it flashes whatever it
   finds (any version gate lives in the auto-trigger, not here; `fake_version` also exists).
   The whole file is read into `buffer[]`.
2. **Request IAP** — `MSG_REQUEST_IAP` → `requestMcuIAP(1)`, arm 1000 ms timeout. If no
   response: `MSG_SEND_REBOOT_AGAIN` / "send reboot cmd again" (`resendCmd1Times`, bounded).
3. **Wait for ready** — MCU emits `CM68_IAP_IS_READY` (per-chip codes e.g. `MS51_IAP_IS_READY:3`)
   → `MSG_MCU_READY` → `dealMcuReady` (re-resolves the `.bin`).
   **"remove checksum check when receive iap ready status"** — if the MCU is *already*
   IAP-ready, the host skips the normal gate and proceeds straight to flashing.
4. **Stream data** — `MSG_SEND_MCU_DATA` / `sendFile` sends 64-byte blocks with
   auto-incrementing address; `ACK`/`NACK` per block (`ackLength`), resend on NACK/timeout
   (`resendCmd4Times`, bounded, ~timeout-driven). `sentCount` vs `totalBlocks`.
   File status walks `FILE_STATUS_INIT → FILE_STATUS_REDAY → FILE_STATUS_SEND_DATA_ING`.
5. **Checksum/verify** — `MSG_WHAT_CHECK_SUM` → `sendCheckSum` (32-bit LE). MCU replies
   `FILE_STATUS_CRC_OK` / `IAP_CRC_FAIL`. On fail: "resend check sum" (reposts after 2500 ms
   while under limit) or `MSG_WHAT_REUPLOAD_FILE` = **full restart from block 0** (only after
   confirming the `.bin` still exists — "file Exist"; there is **no mid-stream resume**),
   or `CMD_KEY_REDO_IAP` (`resendCmd6Times`). All retries are **bounded** (`if-ge` limits) —
   not infinite loops.
6. **Commit/reboot** — on CRC OK: `REBOOT` / `mcu_reboot`; done when the reboot-status
   broadcast carries `"YES"` (`receivedRebootStatus`) → `ACTION_MCU_UPGRADE_SUCCESS`.
   `CMD_KEY_ABOOT` = enter/leave boot.

---

## 3. Entry mechanism (Q1)

Two entry directions exist; **neither changes baud** — same `ttyS3` @ 115200 8N1, `A0 55`
framing throughout, no power-cycle:

- **Host-initiated:** app sends `0x15`/sub 1 → MCU resets (firmware `FUN_CODE_262d` watchdog
  reset) → the resident writer comes up and **announces `CM68_IAP_IS_READY`** → host
  (`dealMcuReady`) proceeds. If the command is lost the host resends it ("send reboot cmd
  again"). This needs the app firmware alive only long enough to receive one `0x15` frame.
- **MCU-initiated (spontaneous):** the MCU raises IAP on its own and `McuService.onMcuIAP`
  auto-launches the updater. This does **not** need the running app firmware at all — it is
  the mechanism by which a MCU stuck in IAP (e.g. after a bad flash, *if* it cold-boots into
  IAP) pulls the host into a re-flash.

**Reconciling "app handler" vs "bootloader".** From the host side the peer looks like an
in-app IAP handler (same UART/framing). But the firmware decompile is decisive: the flashed
12 KB app image has **no flash-write code at all** (§1). Whatever erases/programs/verifies the
64-byte blocks is therefore **resident code outside the 0x3000 region the updater writes** —
i.e. a bootloader/IAP-ROM that survives every app update. The host's terminology ("MCU IAP
mode") does not distinguish the two; the firmware evidence does.

---

## 4. Recovery after a bad flash (Q2/Q3)

**Self-brick shape.** The cm68 has a single ~12 KB APROM bank (no A/B staging on this board;
the `cm68a/cm68b` names are for other multi-MCU variants). The loader **erases then programs
that one bank in place**, so during the write window the APROM *is* invalid. There is **no
rollback**; the safety is **verify-before-jump**: the loader only leaves IAP and runs the app
after `IAP_CRC_OK`. A partial/interrupted/CRC-wrong image is therefore **not committed** — the
MCU stays in the loader emitting `IAP_IS_READY`, and the host reflashes (`REDO_IAP` /
`REUPLOAD_FILE`, full restart from block 0). This is the core reason the flow is recoverable
rather than terminal.

**The recovery chain, concretely.** If the MCU is in IAP (bad/absent APROM) → it emits
`IAP_IS_READY` → `McuService.onMcuIAP` auto-launches the updater (§3) → the updater finds
the stock firmware image and re-flashes, *skipping* the version/checksum gate ("remove checksum check
when receive iap ready status"). So as long as (a) the resident writer survives (it does — it
is outside the written region) and (b) the MCU re-enters IAP after the failure, recovery is
automatic from a power-cycle. Point (b) — cold-boot auto-entry on an invalid APROM — is the
one link not directly read from the (unavailable) loader binary; see §6.

**Correction of a prior assumption.** The string *"[verify]: rom are damaged, upgrade == 1"*
is **NOT** an MCU integrity check. It was found (eMMC `@3603650506`) inside the **Android A/B
OTA delta verifier** (`deltaPath`, `file sha256`, `server sha256`, `sha256 is different`,
`unzip fail`) — it concerns the SoC ROM patch, not the cm68. It is not evidence for MCU
auto-recovery and should not be cited as such. Likewise "This car CanBox not support wireless
update!" / "Config file upgrading, do not power off" belong to the **CAN-box** updater
(`com.hcn.canupgrade`), a different chip.

**What the host-side evidence does establish:** the updater is event-driven on
`IAP_IS_READY`, will flash a MCU that reports ready *unsolicited*, and has redo/reupload/
resend-reboot recovery. That behavior only makes engineering sense if the resident loader
**stays in / re-enters IAP whenever the APROM is not valid**. This strongly implies power-on
auto-recovery, but is inferred from the host, not read from the loader.

---

## 5. Block/erase/write/verify protocol & critical windows (Q4)

- **Transport:** `ttyS3` 115200 8N1, host→MCU `A0 55 LEN ~LEN GID SUB … XOR`, MCU→host
  `55 A0 …` (per `re/PRIOR_RE.md` + McuService framing).
- **Block size:** **64 bytes** data per frame (bytecode-confirmed: `sendFile` builds
  `addr[4 LE] + 0x40 + 64 bytes`; `addr=(blockIndex-1)*64`, absolute byte addressing).
  `MCU_MAX_COMMAND_BYTES=264` is only the buffer cap. Blocks stream sequentially from addr 0.
- **Per-block integrity:** `ACK`/`NACK` with bounded resend (`resendCmd4Times`, timeout-driven).
- **Whole-image integrity:** a **32-bit checksum** (accumulated over the whole `.bin`), sent LE
  in the 6-byte `sendCheckSum` frame, verified by the writer before commit
  (`FILE_STATUS_CHECKSUM` → `FILE_STATUS_CRC_OK`/`IAP_CRC_FAIL`). The exact accumulation
  algorithm and whether it covers the 0x30 header are **not fully pinned** (the 0x30 header on
  the stock firmware image begins `f6 11 …`, rest 0xFF, and did not match simple sum/xor/word tests);
  the host computes and sends it, the writer validates.
- **"Do not power off" critical window:** continuous from **enter-IAP (subCmd 1)** through the
  APROM **erase** and every 64-byte block and the checksum, until the reboot-status broadcast
  reads **`"YES"`**. A power cut here leaves the APROM invalid; recovery then relies on the
  MCU re-entering IAP at next power-on (§6). On a clean *software* failure (NACK/CRC-fail) the
  MCU is already left in IAP and the host auto-restarts the upload (`REUPLOAD_FILE`).

---

## 6. Residual uncertainty (stated explicitly)

Not determinable from the available dumps:
1. **The bootloader/LDROM binary** — it is in the MCU's internal protected flash, absent from
   both `system.img` and the 31 GB `EMMC_USER`. So its exact boot decision could not be read:
   (a) always-run-first loader with a serial-listen window, (b) APROM-integrity auto-enter,
   or (c) app-set persistent flag + reset. Host behavior argues for (a)/(b).
2. **Power-loss vs. clean-abort recovery.** A *software* abort (NACK/CRC-fail) demonstrably
   leaves the MCU in IAP (recoverable). A *physical power-loss* mid-erase is only guaranteed
   recoverable if the loader auto-enters IAP on an invalid APROM at cold boot — which is
   exactly the unread part. Treat power-loss as the worst case until proven (§7).
3. **32-bit-checksum accumulation algorithm / header `f6 11` semantics / exact subCmd numbers
   for data vs checksum (4 vs 6, inferred from `resendCmdNTimes` field names)** — not fully
   pinned. The *shape* (64-byte blocks, LE addr, 32-bit checksum, bounded retries) is
   bytecode-confirmed; these last immediates are inferred.

If any of these must be certain before flashing, the only way to close them is the empirical
proof in §7 (or an oscilloscope/logic-analyzer capture of a real flash + a deliberate
CRC-reject cycle).

---

## 7. Safe procedure (prove-with-stock-first) (Q5)

Do **not** flash the patched image until the loop is proven re-enterable on a **stock** image:

1. **Baseline read-back.** Power up normally; confirm the app answers the version query
   (GID `0x02`/sub `0x02` → reply `CVD1810-WJ_25.03.05_399`). This proves the current app +
   serial link are healthy.
2. **Prove a full stock cycle.** Flash the **unmodified** stock firmware image (same version is
   fine; use `fake_version` to force if the gate blocks a same-version reflash). Watch for the
   complete sequence: `requestMcuIAP:1` → `CM68_IAP_IS_READY` → block ACKs →
   `IAP_CRC_OK` → reboot → version reads back. This proves: reset-into-loader works, the
   loader is re-enterable, and CRC-commit works.
3. **Prove rejection/recovery WITHOUT risking a committed bad app.** Flash a **deliberately
   CRC-invalid** stock image (flip one byte so the transmitted checksum won't match). Expected:
   loader returns `IAP_CRC_FAIL`, the unit does **not** boot a bad app, and the updater offers
   `REDO_IAP`/`REUPLOAD_FILE`; re-flash the good image and confirm recovery. This empirically
   demonstrates verify-before-commit and re-entry after an invalid APROM.
   - *If you also need power-loss confidence:* only after step 3 passes, optionally pull power
     during the send window on a **stock** flash, re-power, and confirm the updater re-enters
     IAP (MCU reports `IAP_IS_READY`) and completes. If it does **not** auto-re-enter, treat
     the MCU as **app-triggered-entry only** → power-loss is NOT software-recoverable, and a
     hardware programmer on the CVD1810 IAP/UART pins would be required.
4. **Only then** flash the patched image, over a **stable power supply** (bench PSU, not
   ignition), never interrupting the erase→CRC window. Keep the stock image on a USB stick
   (`/storage/udisk1/<update>.bin`) as the immediate re-flash fallback.

**Bottom line:** software-only recovery is well-supported for the normal failure modes
(NACK, CRC mismatch, lost reboot command, clean abort) because the writer is a resident,
un-erased bootloader that verifies before committing. The only unproven corner is a physical
power-loss mid-erase, which hinges on the (unread) loader's cold-boot entry condition — prove
it with step 3 (and optionally the power-loss test) on a stock image before trusting the
patched flash.
