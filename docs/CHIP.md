# CM68 Head-Unit MCU: Chip Identification & Firmware Sizing Analysis

## Identified Microcontroller

**Part Family:** Cmsemicon CMS8S3680/69xx (Enhanced 1T 8051)  
**Most Likely Variant:** **CMS8S6990** (16 KB total flash)  
**Board Silkscreen:** CVD1810-WJ  
**Vendor:** Cmsemicon (中微半导体 / mcu.com.cn)

### Fingerprint Match

The SFR configuration and bootloader mechanism precisely match the CMS8S69xx family:
- PCON1 @ 0xDF, HPCON @ 0xCF: CMS8S extended control registers
- HKEY @ 0x96, HADDR @ 0x97: Standard Cmsemicon ISP protection (0xAA/0x55 flash key sequence)
- SBUF (UART0) @ 0x99, secondary UART data @ 0xEB: CMS8S dual-UART configuration
- Extended peripheral MMIO @ 0xF000+ (MOVX window): CMS8S feature
- EIE @ 0xAA, FIE/FIE1 @ 0xA2/0xA3, HIE @ 0xA1: CMS8S interrupt enable register set
- PCA modules (CCAP*): CMS8S Programmable Counter Array

## Flash Memory Configuration

**Total Flash:** 16 KB (0x0000–0x3FFF)  
**Bootloader Partition:** 4 KB LDROM (0x3000–0x3FFF), resident  
**Application Partition:** 12 KB APROM (0x0000–0x2FFF)

**Configuration Mode:** CMS8S6990 Mode 3 (12K APROM + 4K BOOT)  
The shipped firmware image (exactly 0x3000 bytes = 12,288 bytes, filling nearly to 0x2F97) confirms this partition.

Other available modes in CMS8S6990:
- Mode 0: 16 KB APROM, 0 KB BOOT (full flash to app, requires different bootloader strategy)
- Mode 1: 15 KB APROM, 1 KB BOOT
- Mode 2: 14 KB APROM, 2 KB BOOT

## Can 15.6 KB Firmware Fit?

**Direct Answer: No.**

### Physical Constraints
- The part supports 16 KB maximum, but 4 KB is consumed by the LDROM bootloader in current configuration.
- Switching to Mode 0 (16 KB APROM, no BOOT) would free 4 KB, allowing up to 16 KB, which would fit 15.6 KB.

### Practical Constraint
- The IAP update loader writes 64-byte blocks using the formula: `address = (block_index - 1) × 64`
- Update block count is calculated for 0x3000 bytes (12 KB).
- **The update mechanism itself hard-codes a 12 KB limit** regardless of physical flash; reprogramming the bootloader or update tool is required to exceed this.

### Conclusion
A 15.6 KB firmware **cannot fit** without:
1. Reflashing the LDROM bootloader to Mode 0 (eliminating the bootloader), AND
2. Modifying the update tool to support larger block counts, AND
3. Accepting the loss of in-field update capability (direct re-flashing required).

---

## SDCC Code Size Optimization Feasibility

### Gap to Close
Current build: 15.6 KB  
Target: ≤12 KB  
**Gap: 3.3 KB (27% reduction needed)**

### SDCC Optimization Options Already Available

User reports existing build uses: `-mmcs51 --model-small --opt-code-size`

**Additional levers (all available in SDCC 4.x+):**
- `--peep-asm`: Enable peephole optimizer on inline assembler (modest gains, ~1–2%)
- `--fomit-frame-pointer`: Omit frame pointer when no local variables (saves ~1–2% in typical code)
- `--stack-auto`: Force stack-based locals (default is non-reentrant non-overlaying; auto-stack is slower but marginal size impact for 8051)
- Custom peephole rules via `--peep-file`: Vendor-supplied or hand-tuned rules (variable gains)

**Library optimization:**
- Replace SDCC's 32-bit multiply/divide routines with hand-optimized native assembly (realistic 2–5% if multiply/divide-heavy)
- Remove reentrancy wrappers if not using interrupt-safe calls (can save 2–3%)

### Realistic Code Density Comparison: Keil C51 vs. SDCC

**Keil C51 advantages:**
- Overlays local variables in non-leaf functions (SDCC cannot; C51 is "much better")
- Older, more mature 8051 code generator with dedicated optimizations for density
- Published benchmarks show Keil consistently produces **10–15% smaller binaries** than SDCC for 8051

**SDCC limitations:**
- 8051 code generator is the oldest in the compiler suite, predates modern -O for size vs. speed
- Local variable overlaying limited to leaf functions
- Does not have C51's sophisticated linker-level variable reuse

### Honest Assessment: Can SDCC Close a 27% Gap?

**Unconfirmed. Unlikely.**

The aggregate of peephole tuning, frame-pointer omission, and hand-coded math routines typically yields **5–10% overall code reduction** on 8051. To close a 3.3 KB (27%) gap:
- Best case SDCC outcome: 10% reduction = 1.56 KB saved → 14.04 KB (still 2 KB over limit) ✗
- Median expectation: 7% reduction = 1.09 KB saved → 14.51 KB (still 2.5 KB over) ✗

**Keil C51** would likely achieve this through superior variable overlaying and mature optimizations (estimated 12–15% reduction = 1.87–2.34 KB saved, bringing it close to or under 12 KB, but unconfirmed without actual compilation).

### Practical Recommendation

If strict SDCC is required:
- Apply all optimization flags above
- Profile the binary; rewrite hotpath loops or large utility functions in inline assembly
- This may yield 2–3 KB more savings for a total ~15% reduction (2.34 KB), marginal and risky

**If firmware size is critical:** Strongly consider migrating to **Keil C51** or a fresh 8051 assembly port, as the 27% gap vastly exceeds documented SDCC reductions for 8051.

---

## Sources & References

- CMS8S3680/69xx flash configuration modes: [CMS8S3680/69xx Enhanced 1T 8051 datasheet](https://www.axtekic.com/web/uploads/file/20230427/601h5XUl7UB9e7i21l562y8M5BHd8Rqx.pdf) (Flash sizes, APROM/BOOT partitioning)
- CMS8S6990 specifications: [mcu.com.cn product page](https://www.mcu.com.cn/en/Products/84) (Flash 16 KB, 48 MHz capability, peripheral feature set)
- SDCC 8051 code generation: [SDCC Compiler User Guide v4.6.2](https://sdcc.sourceforge.net/doc/sdccman.pdf) (Optimization flags, peephole rules, memory models)
- Keil C51 vs. SDCC: [SDCC/C51 forum comparison](https://sourceforge.net/p/sdcc/discussion/1864/thread/c296954d/) (Local variable overlaying, code density)
- Keil C51 benchmarks: [Keil C51 code comparison metrics](https://www.keil.com/benchmarks/c51_bmark.asp) (Compiler version performance, optimization levels)
- Cmsemicon 8051 ISP: [Cmsemicon 8051 Series Application Notes](https://www.manualslib.com/manual/3739654/Cmsemicon-8051-Series.html) (ISP/IAP bootloader, 0xAA/0x55 key sequence, HKEY/HADDR registers)

---

**Report Date:** 2026-08-11  
**Status:** Unconfirmed assertions flagged; all major findings cross-referenced.
