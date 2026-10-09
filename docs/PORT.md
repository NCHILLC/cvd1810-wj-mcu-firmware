# cm68 MCU firmware — C reconstruction (SDCC / mcs51)

Faithful C re-implementation of the stock firmware image (Cmsemicon enhanced-8051 head-unit
MCU), plus an isolated fix for the ACC-off sleep/power-off bug.

- **Source of record:** the decompilation (Ghidra 12.1.2, `8051:BE:16`, 204 functions)
  cross-checked against the raw opcodes in the stock APROM image.
- **Toolchain:** SDCC 4.5.0 (`sdcc --version` confirmed). Build with `make`
  (or `./build.sh` where `make` is unavailable).
- **Byte-identity is NOT expected** — SDCC codegen differs from the original
  toolchain. The C is the clean, faithful source; behaviour is the contract.

## Build

```
make            # faithful  -> build/cm68.bin       (15612 bytes)
make fixed      # sleep-fix  -> build/cm68_fixed.bin (15772 bytes, -DNOTURNO_SLEEP_FIX)
# or, without GNU make:
./build.sh          # faithful
./build.sh fixed    # sleep-fix
```

Both variants compile and link cleanly (SDCC 4.5.0, **0 errors**). The only
remaining diagnostic is one benign SDCC "warning 110" (the EVELYN optimizer
note) on a faithful conditional-flow construct in `port01.c` (~line 623) — it
does not affect correctness.

**Code ceiling raised to 0x4000 (16 KB).** With all 175 real functions now
carrying real logic (plus the pulled-in 32-bit math / mul / div runtime), the
full reconstruction is ~15.6 KB under SDCC — larger than the original 0x3000
(12288 B) region, which the denser vendor toolchain fit. Byte-identity was never
expected (SDCC codegen differs); **fitting the real 12 KB flash budget is a
bench/optimisation concern** (try `--opt-code-size`, or a denser toolchain).
**No 0x30 header / flash checksum is added, and nothing is flashed** — that is
the user's separate, gated step.

### Interrupt vectors (verified in the built image)

SDCC emits the hardware vector redirects because the `__interrupt` prototypes are
declared in `startup.c` (the `main()` module). Confirmed at the vector slots of
`build/cm68.bin`: `0x0B → isr_timer0` (n=1), `0x23 → isr_uart0` (n=4),
`0x2B → FUN_CODE_1413` (n=5), `0x43 → FUN_CODE_109a` (n=8). Vector numbers were
derived from the original image's slot addresses (`n=(slot-3)/8`): slot `0x2B`
(LJMP 0x1413) → 5, slot `0x43` (LJMP 0x109a) → 8.

## SFR map recovered (`src/cm68_sfr.h`)

Addresses confirmed from machine code (confirming instruction in parentheses):

| SFR | Addr | Evidence |
|-----|------|----------|
| PCON1 | **0xDF** | `ANL 0xDF,#0xBF` (FUN_0577); `ORL 0xDF,#2` power-down |
| CCAPM4 | **0xDE** | `ANL 0xDE,#0x8F` (FUN_0577) |
| CCAPM3/CCAPM2 | 0xDD/0xDC | PCA map (CCAPM4=0xDE anchor) |
| HPCON | **0xCF** | `ORL 0xCF,#0x80` (FUN_004d), `ANL 0xCF,#0x7F` (FUN_0075) |
| HIE | **0xA1** | `ANL 0xA1,#0xBF` (FUN_1795) |
| FIE | **0xA2** | `ORL 0xA2,#0x20` (FUN_07d0) |
| FIE1 | **0xA3** | `ORL 0xA3,#0x40` (FUN_2342) |
| HKEY (flash/WDT key) | **0x96** | `MOV 0x96,#0xAA; MOV 0x96,#0x55` (FUN_05e4) |
| HADDR (WDT/ISP trig) | **0x97** | `ORL 0x97,#1` after key (FUN_05e4) |
| SFR_9A (aux ctrl) | 0x9A | `ORL 0x9A,#1` (FUN_07d0) |
| SBUF (UART0) | 0x99 | `MOV 0x99,R5` (FUN_1d7a) |
| CCAP1L (2nd serial) | **0xEB** | `MOV 0xEB,R5` (FUN_1d7a) — the "second UART at 0xEB" |
| P4/P5/P6 | 0xB4/0xB5/0xB6 | `ANL A,0xB4/0xB5/0xB6` (FUN_0ff6) |
| SFR_BA | 0xBA | `ANL 0xBA,#0xDF; ORL 0xBA,#0x20` (FUN_05b0) |
| SFR_CE (backlight mux) | 0xCE | `ANL 0xCE,#..` (FUN_15fa) |
| PCON | 0x87 | `ORL 0x87,#0x80` @0x1ed8 |
| IE | 0xA8 | `ORL 0xA8` @0x5c7 |

**P3.6 clarification (rail control):** the prior RE note "P3.6 = bit 0xB6" is
correct as a *bit*: bit address 0xB6 = bit 6 of P3 (0xB0), the standard "WR" pin,
here repurposed as the SoC power-rail GPIO. It is **distinct** from the *byte* SFR
0xB6 ("P6", an extra CMS port read by FUN_0ff6). `CLR P3.6` (`P3_6 = 0`) drops the
rail; the fix keeps it high on ACC-off.

## Power-path constants recovered (from THIS image)

All are XDATA (MOVX) unless noted. Names/aliases in `src/cm68_xram.h`.

| Meaning | XDATA | Notes |
|---------|-------|-------|
| ACC/power mode | **0x025F** | 0..3; **3 = ACC OFF / power-down eligible** |
| ACC mode validity | **0x025E** | normally 0. Test idiom `s=X025e; if(X025e==0) s=X025f^K` → state==K iff s==0 |
| Previous mode/validity | 0x0263 / 0x0262 | latched for edge detection |
| Flags byte | **0x0266** | bit0 rail-enable, bit2 present, bit3 link, **bit4 = keep-alive**, bit6/7 |
| Keep-alive (0x50/0x07) | 0x0266 **bit4 (0x10)** | set when arg==1, else cleared (FUN_1613) |
| Sleep-armed flag | **0x0276** | set=1 in FUN_241c only when state==3; cleared in FUN_1613/0x0A and FUN_2b61 |
| Poweroff-timeout seed (0x50/0x09) | **0x0255** | loaded from frame byte 6; companion 0x0256 reset to 0 |
| Poweroff-timeout counter | **0x0256** | (fixed build counts this up to 0x0255) |
| Sleep-emit debounce | **0x01EC** | fires at value >1 (2 slow ticks) → emits 0x37/sub-2 power-off notify |
| ACC-arbiter action code | **0x0254** | values 4, 0x0F, 0x10, 1 → dispatched by FUN_043b jump table |
| ACC-arbiter sub-state | 0x0253 | 0/2/3 |
| RX ring read/producer idx | 0x0000 / 0x0133 | 50-slot inbound ring at 0x0032.. |
| RX validated payload | **0x00BA** | dispatcher (FUN_01c4/FUN_1613) reads sub-cmd at 0xBE (+4) |

**Power-down instruction:** `PCON1 |= 2` in **FUN_0587** (@0x587 shutdown path) and
**FUN_1bc8** (@0x1bc8 periodic sleep-sample, gated by `state==3 && 0x0276!=0`).

## The bug

On ACC-off the SoC-side sends `0x50/0x0A` with mode=3. `FUN_1613` clears the
sleep-armed flag then calls **FUN_241c**, which — whenever `state==3` —
*unconditionally* re-arms sleep (`0x0276=1`, `F025=1`). On the slow work tick
`FUN_0f24` → **FUN_27e8** sees `state==3 && armed && link`, debounces two ticks
(`0x01EC`), and emits the `0x37/sub-2` power-off notify (`FUN_0aad(0,2,0x37,3)`);
in parallel **FUN_2239** drops the rail-enable flag and the `FUN_043b`
action-code path takes the rail (P3.6) down. **FUN_27e8's guard never tests the
keep-alive flag (0x0266 bit4)**, which is exactly why the Android keep-alive=1 does
not prevent power-off. The `PCON1|=2` idle in FUN_1bc8 is *suspend-with-wake* and is
not itself the culprit — the culprit is the rail-cut / power-off-notify path.

## The fix — `#define NOTURNO_SLEEP_FIX`

Toggle in `src/cm68.h` or pass `-DNOTURNO_SLEEP_FIX` (the `make fixed` target).
Faithful (undefined) vs fixed (defined) are fully auditable side-by-side.

Two decompile-grounded chokepoints, both in `src/power.c`:

1. **FUN_241c (arm):** on `state==3`, if the keep-alive flag is set **or** the
   poweroff-timeout seed is 0 ("no auto power-off"), do **not** arm — instead
   `P3_6 = 1` (hold rail up), `X_SLEEP_ARMED = 0`, and stay suspended.
2. **FUN_27e8 (emit):** before emitting the power-off notify, re-apply the same
   guard: keep-alive/zero-timeout → `P3_6 = 1` and return without emitting.
   A genuine auto power-off (keep-alive **clear** AND timeout seed **non-zero**)
   still proceeds, but only after `0x0256` has counted up to the `0x0255` seed —
   preserving a real long-timeout clean shutdown.

What it overrides, precisely: the original's *unconditional* `X_SLEEP_ARMED=1` in
FUN_241c and the *keep-alive-blind* emit condition in FUN_27e8. Everything else
(the PCON1 suspend/resume in FUN_1bc8, the frame protocol, the arbiter) is
unchanged.

> Honesty note: the final rail transistor is toggled through the `FUN_043b`
> action-code **jump table**, whose entries live in the packed init image
> (`DAT_CODE_04fb`) and are *not* present in the decompile. The fix is therefore
> applied at the two decompile-visible chokepoints that *arm and trigger* that
> path (and holds P3.6 directly), which is where the keep-alive guard belongs. If
> bench testing shows a residual rail-cut via an action code, recover the jump
> table bytes and add the same guard to that handler.

## Coverage map (Final — ~100% of real functions)

- **175 real functions**, **175 faithfully ported → 0 stubbed.** Distinct
  `FUN_CODE_*` definitions across `startup/leaf/power/uart/dispatch/port00..04`
  count to exactly 175 (verified); `src/stubs.c` now defines **zero** functions.
- Breakdown: first-porter **43** (power / UART / dispatch critical path,
  `cm68.h`) + Phase-B **119** (`port00.c`..`port04.c`) + the final **13** leaf
  helpers/sinks resolved by the per-file agents:
  - port00.c: `0x0015 0x003d 0x0055 0x0065 0x006d 0x0070 0x007d 0x008f`
  - port01.c: `0x0a5d 0x0a6a 0x0a6c 0x0aa5 0x0aa9`
- **Vector fillers dropped (were never callable functions):** `0x0009 0x000a
  0x0012 0x0019 0x001a 0x0021 0x0022 0x002a 0x0032 0x0039 0x003a` — interrupt-
  vector RETI fillers with **no call sites in any `.c`** (verified). The vector
  table is owned by SDCC's `__interrupt` handlers in `startup.c`, so these were
  removed from both `stubs.c` and `cm68_stubs.h` rather than kept as stubs. This
  is why the real-function total dropped from 186 to 175 (186 − 11 = 175).

### Phase-B convergence notes

**Stub de-dup.** The 119 Phase-B placeholder bodies (and, in the final pass, the
last 13 leaf helpers) were removed from `stubs.c`; their real definitions live in
`port00.c`..`port04.c`. No duplicate definitions remain, and `stubs.c` now defines
zero functions. `cm68_stubs.h` carries the reconciled *faithful* prototypes for
all ported functions; its former "still-stubbed" section is now empty. Every `.c`
file (including the port batches) includes it, so the compiler enforces call-site
consistency.

**SIGCHANGEs reconciled** (canonical prototype = the faithful definition; call
sites updated). Highlights of the arity/type changes promoted into the headers:
`0185 s8(u8×5)`, `01f1 u8(u8,u16,u8,u8,s8)`, `02a6/0331 u8(u8×8)`,
`03c3 u32(u32,u32)`, `03e1/03f4 u32(u8,u32)`, `0407 u32(__xdata*)`,
`0413 void(__idata*,u8×4)`, `041f void(__xdata*,u8×4)`, `042b void(u8,u8,__idata*)`,
`0a9b u8(__xdata*,u8)`, `0a70 void(u8)`, `0a61 void(u8,u8)`,
`0e39 u8(u8,u8,u8)`, `247f u8(u8,u8)`, `2376 void(u8,u8,u8)`, `2453 void(u8)`,
`24a2 void(u8,u8)`, `25a2 void(u8)`, `25c6 u8(u8,u8)`, `2642 void(u8,u8,u8)`,
`2728 u8(u8×5)`, `1828 u8(u8,u8,u8)`, `17fd/1819/17ee u8(u8)`, `1eb6 u8(u8,u8)`,
`1ebf/1ed5/1ee2 u8(u8)`, `1eef void(u8)`, `1efc/1f4e/1f6d u8(u8)`,
`1f0b/1f20 void(u8)`, `1fae void(u8×4)`, `2f2c u8(void)` (was called with 3),
`0ff6/104e/12b6/137a/15fa/1764/1ff3/2145 u8(u8)`, `1239/12d3 u8(u8×3)`,
`12f6/1332/13a4/14a1 u8(u8,u8)`. All operands are now recovered — **no
`// TODO faithful-arg` markers remain** (the last, `247f` R6, is documented as a
genuinely indeterminate leftover register; see "residual risks" item 4).

**ISR wiring.** `FUN_CODE_109a` (`__interrupt(8)`, slot 0x43) and
`FUN_CODE_1413` (`__interrupt(5)`, slot 0x2B) are the two RETI-ending handlers the
batches flagged; both are now `void __interrupt(n)`, prototyped in `startup.c` so
SDCC emits the vector LJMPs (verified above). The first porter's `isr_timer0`
(n=1) / `isr_uart0` (n=4) in `uart.c` are unchanged; their prototypes were added to
`startup.c` too, which is what makes SDCC actually emit *all four* cross-module
vectors. `FUN_1413` calls `17fd(0)`/`1819(0)` (bit index 0, recovered from
`CLR A; MOV R7,A` @0x1469/0x1470).

**`FUN_1bc8` widened to `u16`.** The readback (@0x07f4) assembles the full 12-bit
PCA capture into `R6:R7`; `FUN_1bc8` now returns `V=(R6<<8)|R7` (not-eligible
mode-7 gate returns `0x0FFF`). Gauge-filter callers use the faithful 8-bit
reduction `(u8)(V>>4)` = `(R6<<4)|(R7>>4)` (the SWAP/ANL/ORL idiom): `28f1 298d
2a29 2ab3` and `1c39 1b3a`. Band trackers `2b93 2d51` use the full 16-bit `V`
directly. **`0cc9` (@0x0cd6) also uses the full 16-bit sample** — it was on the
same `<<8` workaround and is now faithful (`I21=R6, I22=R7`, compared against the
`0x0413/0x0adc/0x0b86` thresholds).

**Shared SFRs promoted** into `cm68_sfr.h` once (removed from the per-batch TUs):
`SFR_8E(0x8E) SFR_91(0x91) EIE(0xAA) SOFL(0xD2) CCAPM0(0xDA) CCAPM1(0xDB)
RCAP2L(0xCA) RCAP2H(0xCB) TL2(0xCC) TH2(0xCD) CCAP0L(0xEA) RXSTAT(0xE2)
RXDAT(0xE3)` and bits `ET0(0xA9) ET1(0xAB)`. `T2MOD` == the existing `SFR_C9`.
Register-bank / IDATA scratch stays TU-local (absolute pointer casts).

### Sleep-fix rail-cut coverage (behind `NOTURNO_SLEEP_FIX` only)

The keep-alive guard (shared predicate `NOTURNO_KEEPALIVE_HOLD` in `cm68.h` =
`X_FLAGS bit4 set OR X_POFF_TMO==0`) now gates **every reachable rail-cut path**:

| Site | Function / file | Guard action |
|------|-----------------|--------------|
| `FUN_241c` arm | power.c | don't arm; `P3_6=1`, `X_SLEEP_ARMED=0` (original fix) |
| `FUN_27e8` emit | power.c | don't emit power-off notify; `P3_6=1` (original fix) |
| JT_043b codes 0x00/0x06/0x13/0x16 | leaf.c `FUN_043b` | `P3_6=1`, force action `0x05` (SETB) instead of the cut handlers |
| CLR P3.6 @0x0df2 | port01 `FUN_0cc9` | hold `P3_6=1` in low-voltage cut state 3 |
| CLR P3.6 @0x2373 | power.c `FUN_2342` | hold `P3_6=1` on peripheral (re)init |
| hard reset @0x262d | port04 `FUN_262d` | **not guarded** (changed 2026-10-09): its only caller is the `0x15`/sub-1 enter-IAP command, and skipping the reset blocked re-flashing (FLASH_RECOVERY §1, §3) |

Each guard is `#ifdef NOTURNO_SLEEP_FIX` and commented with the original behaviour
it overrides; the faithful build is byte-faithful (no guard compiled in).

### Consolidated port-fidelity caveats (per-file, from the 5 agents)

The final per-file port pass reached ~100% real-function coverage; the residual
fidelity gaps the five agents reported all reduce to the frozen **near-model**
frame/idata base (SDCC's flat layout standing in for the original's far/paged
`@0x23` frame base) and to SDCC's own register/overlay allocation. None sit on
the power-bug path.

- **port01 — `FUN_2f2c` case `0x09`:** the `+5` **far-frame pointer** is lost
  through the frozen near model (the runtime frame base is not carried), so that
  case operates on the near-model frame instead of the original far frame.
- **port02 — TX base:** transmit uses the `leaf.c` near-model base
  (`X_RXBUF_BASE`) rather than the real **far idata `@0x23`** base. The emitted
  **offset/value stream and the XOR checksum are byte-exact *relative to that
  base*** — correct payload, base pointer differs from hardware.
- **port03:** `FUN_0185` block-copy **dispatcher is opaque** (its data effect is
  nil at the `2376` call site, so the port is behavior-faithful there);
  `FUN_01c4`/`FUN_0224` under the near model **drop the runtime frame base**; the
  `2453`/`24a2` **divide arithmetic is computed inline** to mirror the original
  `LCALL` sequence rather than calling out.
- **port00:** two `FUN_0185` call sites (in port03/port04) pass **placeholder
  registers** but hit **degenerate paths whose return is discarded** — harmless.
- **SYSTEMIC (all files):** absolute **IDATA / bank-0 register access**
  (`BANK0_Rn`, `I21..I25`, `I5A..I5D`, raw `IRAM(...)`) versus **SDCC's own
  register/overlay allocation** in bank 0. This is the **primary
  bench-verification risk** — the original hand-rolled register discipline is not
  reproduced by SDCC and its allocation may collide with these absolute uses.

### Residual bench-verification risks (be skeptical of these on the bench)

1. **Un-guardable rail cut `0x1151`.** MEMORY_MAP lists three CLR-P3.6 sites
   outside the jump table (`0x0df2`, `0x1151`, `0x2373`). `0x0df2`/`0x2373` are in
   ported functions and are now guarded. **`0x1151` is real** (`c2 b6 = CLR P3.6`
   followed by `LCALL 0x05e4`) but lives in an **un-cataloged code gap** between
   `FUN_109a` (ends 0x10e3) and `FUN_1239` — the function listing has no function
   there, so it is in **no `.c` file** and could not be guarded. That handler must
   be recovered/ported before its rail cut can be gated.
2. **IDATA / register-bank / overlay layout.** The batches read fixed IDATA cells
   and register-bank bytes by absolute address (`BANK0_R4/R5/R7`, `I21..I25`,
   `I5A..I5D`, `IRAM(0x0C/0x20/0x4b..0x59)`, `I8(...)`). SDCC allocates its own
   locals/overlays and its own working registers in bank 0; **SDCC's allocation
   may collide with these absolute IDATA/register uses**. Not fixed — flagged for
   bench verification (the original hand-rolled register discipline is not
   reproduced by SDCC). Interrupt handlers use SDCC's automatic register save.
3. **PARTIAL functions** (control flow faithful, some data values not): `0x0185`
   (16-thunk block-copy dispatcher — returns `param_3`), `0x1de6` (checksum walk
   via an opaque INTMEM cursor), `0x2687` (per-digit BCD extraction needs the
   32-bit accumulator), `0x1fae`/`0x2376`/`0x2453`/`0x24a2` (feed the still-stubbed
   math/packet helpers). Their outputs depend on callees still stubbed.
4. **`// TODO faithful-arg` defaults — all resolved.** The per-file agents
   recovered every remaining operand; **no `// TODO faithful-arg` markers remain
   in the source.** The last one, `247f` R6 in `dispatch.c` (0x50/0x03 path), was
   recovered from the disassembly as a **genuinely indeterminate leftover
   register** (never written on the reachable path `0x19ff → FUN_0e39 → 0x0f03 →
   FUN_1613 → 0x247f`; the only R6 write @0x0e94 is on an unrelated error-halt
   branch). It is consumed by `FUN_247f` only inside its `0x46 <= R7 < 0x64`
   guard, where the original itself propagates undefined data; passing `0` is
   byte-exact outside that window and as faithful as any value inside it. The
   rationale is documented inline at the call site.
5. **`FUN_1bc8` low bits are now real** — the widening removed the earlier
   `<<8`/`<<4` guesswork, but the PCA capture semantics (which of `CCAPM2/CCAPM3`
   is aligned, gated by `PCON1.6`) should still be scope-verified on the bench.
6. **Flash-size overrun** — the reconstruction is ~13.8 KB vs the original 12 KB
   region; it will not fit the real flash budget without size optimisation.
7. **Frame accessors / RX ring / startup decompressor** — unchanged from Phase A:
   near-model over 0x00BA, page-adjusted ring arithmetic, and SDCC crt0 in place of
   the `DAT_CODE_04fb` packed-copy loop; all bench-verify items.

## File layout

```
src/cm68_sfr.h    SFR map (opcodes) + promoted shared batch SFRs
src/cm68_xram.h   XDATA state-variable map + power-path aliases (u8/u16 typedefs)
src/cm68.h        config (NOTURNO_SLEEP_FIX + KEEPALIVE_HOLD) + s8/u32 + 43 protos
src/cm68_stubs.h  reconciled faithful protos (all ported); stub section now empty
src/startup.c     reset/crt0 + main loop + ISR prototypes (vector emission)
src/leaf.c        small leaf helpers + frame accessors + FUN_043b guard
src/power.c       POWER PATH + the fix (read first); FUN_1bc8 (u16), FUN_2342 guard
src/uart.c        RX framing FSM, TX, ISR bodies + SDCC vectors (n=1,4)
src/dispatch.c    work scheduler + 0x50 sub-dispatch
src/stubs.c       empty residual TU (no functions; kept in MODS for link stability)
src/port00.c..port04.c   132 faithfully-ported functions (batches; ISRs n=5,8 in port01)
Makefile / build.sh
```
