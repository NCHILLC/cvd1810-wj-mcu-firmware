# cm68 firmware — Keil C51 port

Port of the SDCC reconstruction in `../src/` to the **Keil C51** dialect, plus a
µVision (`cm68.uvproj`) project. The SDCC sources in `../src/` and `../build.sh`
were **not** modified — everything here lives under `keil/`.

## RE-DERIVATION from the complete SDCC reference (2026-08-11)

`keil/src/` was **fully re-derived** from the now-complete `../src/` (201
functions: rail sequencer, UART ISR FSM, startup init-image, version handler,
all logic). The earlier size-optimized keil port predated that completion and had
functions deleted for size; those hacks are **gone** — this is a **faithful
reference build**, not size-constrained (`../src/`'s real `math_mul_accum`,
`math_divide`, `util_scale_*`, `math_abs16`, etc. are all present).

**Both variants build with 0 errors** (benign C275/C173 + one L16 UNCALLED for
`math_abs16`). Authoritative `Program Size` from `lst/cm68.m51`:

| variant | `-D` | CODE size |
|---------|------|----------:|
| faithful | (none) | **15,299 B** |
| fixed    | `NOTURNO_SLEEP_FIX` | **15,497 B** |

The complete firmware is ~15.5 KB and does **not** fit the 12 KB APROM — expected
for a full C reconstruction (the original 12 KB image was hand assembly). Δ198 B
is the sleep-fix rail-hold logic.

**Version string:** `DAT_CODE_04fb` is located at CODE **0x04FB**, so the embedded
`"CVD1810-WJ_25.03.05_399\0"` lands at CODE **0x054E** exactly where the reply
path MOVC-reads it (`{0xFF:0x05:0x4E}`) — verified present in `obj/cm68.hex`.

### Two non-obvious dialect fixes beyond the mechanical table

- **CODE `_at_` is illegal on an initialized array** (C51 `error C274`). SDCC's
  `__code __at(0x04fb)`/`__at(0x08a0)` tables became plain `code` constants;
  `DAT_CODE_04fb` is declared **first** in `cm68_code_tables.h` so it leads the
  `?CO?INITIMG` segment, and the **BL51 directive `CODE(?CO?INITIMG(0X04FB))`**
  places it at 0x04FB. `JT_043b` follows (no C reader uses its absolute address).
- **Reserved-word identifiers:** `pwr_rail_sequencer(u8 code)` → `act_code`
  (`cm68.h`+`leaf.c`), `struct jt043b_ent { … code … }` → `act_code`, and the
  multi-precision-decode local `u8 … bit;` → `bitv` (`port00.c`, ~13 sites).
- Everything else is the mechanical table below (`__sfr`→`sfr`, memory qualifier
  after the base type, `__interrupt(n)`→`interrupt n` on defs only, `__asm nop`
  →`_nop_()`, XDATA `__at`→`extern`+`_at_` split in `xram_defs.c`).

### Build recipe (⚠ UV4 `-b` cannot register the new `initimg.c` module)

`UV4.exe -b cm68.uvproj` builds from a cached project model and **silently drops
the newly-added `initimg.c`** (verified: it never appears in the compile list or
`.lnp`, even after clearing `.uvopt`/`.uvgui`). The project file itself is correct
(`initimg.c` is in the Source group; the linker directive is in Lx51
`MiscControls`), so a GUI **Rebuild** picks it up — but the CLI does not. The
authoritative builds above were produced with the documented licensed toolchain
(`C51.exe` + `BL51.EXE` + `OH51.EXE`, SN K1DZC-GFBLNC) driven by
`keil/build_variant.ps1`, which also runs the same global-register-optimization
`.ORC` iteration UV4 does:

```
powershell -File keil\build_variant.ps1 -Define NOTURNO_SLEEP_FIX   # fixed
powershell -File keil\build_variant.ps1                             # faithful
```

The section below is the earlier (superseded) size-optimized-port analysis, kept
for history.

---


- Ported sources: `keil/src/*.c`, `keil/src/*.h` (copies of `../src/`, translated).
- Project: `keil/cm68.uvproj` (opens in UV4, device = Generic 8052).
- Objects/listings land in `keil/obj/` and `keil/lst/`.
- Toolchain used: `C:\Keil_v5\C51\BIN\C51.exe`, `BL51.EXE`, `C:\Keil_v5\UV4\UV4.exe`
  (EVAL, 2 KB link cap).

## Size optimization — fitting the 12 KB APROM (2026-08-11)

With a **licensed BL51** (SN K1DZC-GFBLNC — links past the 2 KB EVAL cap), the
full image was measured and squeezed under the **12 KB APROM (0x3000 = 12,288
bytes)**. Authoritative size is the linked `Program Size: … code=NNNN` line in
`lst/cm68.m51` (the UV4 `-o build.log` is written elsewhere and can go stale —
always read the size from the fresh `.m51`).

**Start = 13,725 → Final = 12,243 bytes CODE. Target ≤ 12,288 REACHED**
(45 bytes of headroom, 0 errors). Per-lever, each rebuilt and measured; the
four deltas sum exactly to −1,482:

| # | Lever | Δ code (bytes) | Notes |
|---|-------|---------------:|-------|
| 1 | **OPTIMIZE(9,SIZE)** (was `OPTIMIZE(8,SIZE)`) | **−890** | Single biggest win. `<Optimize>` 8→9 in `cm68.uvproj`; emphasis already SIZE (`<SizeSpeed>0`). Isolated by an 8↔9 toggle at final source (13,133 ↔ 12,243). |
| 2 | **Delete 6 uncalled functions** | **−196** | `math_abs16` (port00) + `uart_read_data`/`uart_clr_ri`/`uart_clr_ti`/`uart_test_ri`/`uart_test_ti` (port03) — all flagged L16 UNCALLED, no caller in any `.c`. Bodies replaced by a recovery note; prototypes dropped from `cm68_stubs.h`. Raw code ≈133 B; the rest is linker/overlay repacking. Behavior unchanged (nothing called them). |
| 3 | **Drop the 32-bit runtime lib from `util_scale_adc_up/down`** | **−313** | The two funcs stored their result via a C `u32 * / /` (`quot = param_1*0x0FFF/0xE7` and `((p1<<8)\|p2)*0xE7/0x0FFF`), pulling `?C?LMUL` (139 B) + `?C?ULDIV` (146 B). Reformulated into **16-bit-only** arithmetic that is **byte-identical** for every input (see below), dropping both 32-bit modules (adds only the smaller shared `?C?UIDIV`). |
| 4 | **Remove 4 dead no-op calls** in the same two funcs | **−83** | Each func also called `math_mul_accum`+`math_divide` to "mirror the LCALL sequence", but those models are **pure `u8`-return** (no memory/SFR/IRAM writes — verified by reading the full bodies) and their results were discarded. Removing the 4 calls changes **zero** memory writes/values. |

### Why lever 3 is faithful (not a behavior change)

The ported `math_mul_accum`/`math_divide` are pure functions that return a single
byte and **never write the IRAM result cells** — so the original hypothesis
("they already leave the bytes in 0x29..0x2c / 0x4e..0x51") does **not** hold
here; the C `quot` line was the *only* thing producing the stored bytes. Rather
than keep the 32-bit library, the identical integer was re-derived in 16-bit:

- **up:** `0x0FFF = 0xE7·17 + 168`, so `param_1·0x0FFF/0xE7 == param_1·17 +
  (param_1·168)/0xE7` — exact for all `param_1` (adding the integer `param_1·17`
  commutes with the floor). Max quot = 4520 (`< 0x10000`), so the two high
  big-endian bytes are always 0, matching the old `(u8)(quot>>24/>>16)` stores.
- **down:** with `v=(param_1<<8)|param_2`, `v·0xE7 = 0x0FFF·(14·param_1) +
  (86·param_1 + 11·param_2)`, so `v·0xE7/0x0FFF == 14·param_1 +
  (86·param_1+11·param_2)/0x0FFF` — exact. Max quot = 3696 (`< 0x10000`).

Both identities were **verified exhaustively** (up: all 256 inputs; down: all
65,536 inputs) against the original `u32` expression — every stored byte is
identical. All intermediates fit in unsigned 16-bit (max 42,840 / 24,735), so no
overflow and no 32-bit lib.

### Faithfulness notes / what's retained

- **`math_mul_accum` (238 B) and `math_divide` (657 B) are kept** in `port00.c`
  as faithful ports of `FUN_02a6`/`FUN_0331`, even though lever 4 leaves them
  **uncalled** (BL51 keeps user segments; they now draw two benign L16 UNCALLED
  warnings and push `data` 48→67 B as their locals leave the overlay pool — both
  harmless, IRAM has ample room). They were deliberately **not** deleted:
  removing real firmware math logic is a larger faithfulness cost than the ~895 B
  it would reclaim, and the budget is already met. Deleting them is the obvious
  lever if future edits need more room.
- The 32-bit `math_or32`/`math_shl32`/`math_shr32`/`math_load_be32` helpers and
  the `CELL32`/BE32 code are **unchanged** — they are genuinely called and use
  only `<<`/`|`/`>>` (`?C?LSHL`/`?C?LOR`/`?C?ULSHR`, kept), never `*`//` (which
  is why only `?C?LMUL`/`?C?ULDIV` could be dropped).
- Deleted functions (lever 2) are all recoverable from git / the disassembly.

**Verdict: the Keil C reconstruction now fits the 12 KB APROM (12,243 ≤ 12,288),
faithfully.** The section below is the earlier EVAL-linker analysis, kept for
history.

## Result summary

All **11 modules compile cleanly** under Keil C51 (0 errors; only benign C275 /
C173 warnings, see below). UV4 opens `cm68.uvproj`, compiles every module to an
`.obj`, and reaches the link stage. The full BL51 link **fails on the EVAL 2 KB
code cap** (`FATAL ERROR L250 … LIMIT: 0800H BYTES`) — expected and fine.

**Summed Keil CODE SIZE = 13,224 bytes (~12.9 KB).** With the C51 runtime library
and vector table added at link time (est. +0.3–0.6 KB, extrapolated from SDCC's
link overhead), a licensed Keil image lands around **13.2–13.8 KB**.

**Verdict — does it fit the 12 KB APROM (0x0000–0x2FFF = 12,288 bytes)? NO.** It
overshoots by ~1–1.5 KB. Keil is markedly tighter than SDCC (13.2 KB vs SDCC's
15.6 KB linked — about **2.4 KB / ~15 % smaller**), but the C reconstruction is
still larger than the original 12 KB image (which was hand-written assembly). It
**would** fit the 16 KB physical flash (CMS8S6990 Mode 0, no LDROM partition),
matching `../CHIP.md`. This refines CHIP.md's earlier estimate that Keil "might
bring it close to or under 12 KB": measured, it stays over budget.

## Per-module size: Keil vs SDCC

SDCC figures are the code-bearing area sizes from each `../build/<m>.rel`
(`CSEG`+`CONST`+`HOME`+`GSINIT`+`XINIT`+`CABS`); Keil figures are `CODE SIZE`
from `keil/lst/<m>.lst`.

| module   | SDCC (bytes) | Keil (bytes) | notes |
|----------|-------------:|-------------:|-------|
| startup  |   172        |    29        | SDCC 172 includes crt0 glue (`HOME`=140); Keil's `?C_STARTUP` links separately from STARTUP.A51, so 29 is only `sys_reset_entry`/`main`. |
| leaf     |   331        |   294        | |
| power    |   816        |   749        | 12× `_nop_()` settle loop |
| uart     |   595        |   451        | ISR bodies isr_timer0 (n=1), isr_uart0 (n=4) |
| dispatch |   935        |   853        | |
| stubs    |     0        |     0        | stubs.c is empty (no functions remain) |
| port00   |  2902        |  2619        | u32 helpers pull in long-math lib at link |
| port01   |  3721        |  3147        | ISR bodies isr_timer2 (n=5), isr_ext8 (n=8) |
| port02   |  1176        |   986        | |
| port03   |  2186        |  2003        | |
| port04   |  2417        |  2093        | |
| **sum**  | **15,251**   | **13,224**   | SDCC linked total (`../build/cm68.mem`) = **15,626** |

(Per-module sums exclude the shared runtime library, which the linker adds once.
SDCC's linked total is 375 B above its per-module sum; expect a similar delta for
a full Keil link.)

## Dialect changes (SDCC → Keil C51)

Applied mechanically to the copies in `keil/src/`, then fixed per-module:

| SDCC | Keil C51 | count |
|------|----------|------:|
| `__sfr __at(0xNN) X;` | `sfr X = 0xNN;` | 57 |
| `__sbit __at(0xNN) X;` | `sbit X = 0xNN;` | 15 |
| `__xdata __at(0xNNNN) volatile u8 V;` | `volatile u8 xdata V _at_ 0xNNNN;` | 145 |
| `__code __at(0xNNNN) const unsigned char T[..]={..}` | `const unsigned char code T[..]={..}` (dropped `_at_`, see below) | 2 |
| `__xdata/__idata/__pdata/__code/__data/__bit` | `xdata/idata/pdata/code/data/bit` | ~220 |
| `void isr() __interrupt(n)` | `void isr(void) interrupt n` | on defs |
| `__asm nop __endasm;` | `_nop_();` (from `<intrins.h>`) | 6 + 12 |

### Non-mechanical fixes required

1. **Memory-type keyword ordering (biggest one).** Keil requires the memory
   space *after* the base type; SDCC put `__xdata` *before* it. A blind token
   swap produced `volatile xdata unsigned char *` which Keil rejects
   (`expected 'hdata'`). Every pointer cast, pointer parameter and pointer local
   was reordered to base-type-first, e.g.
   `(*(volatile __xdata unsigned char *)a)` → `(*(volatile unsigned char xdata *)a)`,
   `__xdata u8 *p` → `u8 xdata *p`, and the mixed
   `__xdata volatile u8 *` → `volatile u8 xdata *`.
   Affected: `cm68_xram.h` (`XB`), the `BANK*`/`IRAM`/`XD`/`I*` macros in
   port00–04, and the pointer parameters of `math_load_be32`,
   `math_store4_{idata,xdata}`, `math_add16_idata`, `txq_enqueue_delta`,
   `gauge_filter`.

2. **Reserved-word identifiers.** SDCC's memory keywords are `__code`/`__bit`, so
   plain `code`, `bit`, `data` were legal *identifiers* there — they are reserved
   words in Keil. Renamed:
   - `pwr_rail_sequencer(u8 code)` → `u8 act_code` (`cm68.h` prototype + `leaf.c`
     body and uses).
   - local `u8 … bit;` → `bitv` in `port00.c` (the bit-serial decode helper,
     ~14 sites).

3. **`interrupt N` on prototypes is a syntax error in Keil.** SDCC needed the
   ISR prototypes visible in the module that defines `main()` (startup.c) to emit
   the vector redirects; Keil instead emits each vector at the ISR **definition**
   site. The four forward declarations in `startup.c` and the two in `port01.c`
   were removed (replaced by explanatory comments). ISR definitions keep
   `interrupt n`: isr_timer0 n=1 & isr_uart0 n=4 (uart.c), isr_timer2 n=5 &
   isr_ext8 n=8 (port01.c).

4. **`_at_` cannot carry an initializer (Keil).** The two CODE tables in
   `cm68_code_tables.h` (`JT_043b`, `DAT_CODE_04fb`) are initialized const
   arrays, so their `__at(0x…)` placement was dropped — they become plain
   `const unsigned char code …[] = {…}` and the linker places them. This header
   is reference-only (no `.c` includes it), so the lost absolute address is
   immaterial. The XDATA `_at_` variables in `cm68_xram.h` are uninitialized, so
   they keep `_at_` and compile fine.

5. **`<intrins.h>`** added to `port00.c` and `power.c` for `_nop_()`.

No changes were needed in `cm68.h`, `cm68_sfr.h`, `constants.h`, `dispatch.c`,
`power.c` bodies, `uart.c`, `port02.c` beyond the mechanical translation +
pointer reordering.

## Remaining warnings (benign, same semantics as SDCC)

- **C275 "expression with possibly no effect"** — from `(void)x;` discards and
  from volatile-register reads used as statements (`XB(a);`, `IRAM(a);`).
- **C173 "missing return-expression"** (`port04.c:77`) — a `u8`-typed function
  that falls off the end, faithful to the reconstruction.

## Project / build notes

- **Device:** `Generic / 8052 (all Variants)` — the CMS8S6990 is not in Keil's
  DB; all custom SFRs are declared by our own `sfr`/`sbit`, so the device header
  is irrelevant. Cpu string sized to 16 KB code / 64 KB XDATA so the CMS
  extended-peripheral window (0xF000+) is in range.
- **Memory model:** SMALL (default). Code size is not restricted in the project;
  the only cap is the EVAL linker.
- **Include path:** `src` (relative to `keil/`). Sources in group *Source*,
  headers in group *Headers*.
- **XDATA overlap (L6) warnings at link:** `cm68_xram.h` declares the
  memory-mapped regs with `_at_` in a header included by every TU, so each object
  re-declares the same absolute byte (e.g. 0xF692). BL51 flags the overlap but
  proceeds — the addresses are identical hardware regs by design (same pattern
  SDCC tolerated). A licensed build links through them; to silence, move the
  `_at_` defs to one TU with `extern` decls in the header (semantics unchanged).

### Reproduce

Open `keil/cm68.uvproj` in UV4 and Build (F7), or from a shell:

```
"C:\Keil_v5\UV4\UV4.exe" -b keil\cm68.uvproj -j0 -o keil\build_uv4.log
```

Compilation of all 11 modules succeeds; the link stops at
`L250 … LIMIT: 0800H` on the EVAL license (UV4 exit code 3). Per-module
`CODE SIZE` is in `keil/lst/<module>.lst`.
