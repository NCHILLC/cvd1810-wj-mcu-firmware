/* mathutil.c - 8051 multi-precision math helpers and low-level block-copy /
 * delay-loop leaves.  Moved verbatim (pure code motion, no logic changes)
 * from port00.c/port01.c during the Phase-C file reorganization: every
 * math_* function plus the util_block_copy / util_delay16_* leaves that were
 * grouped alongside them in the original decompilation batches.
 */
#include "cm68.h"
#include "cm68_stubs.h"
#include <intrins.h>

#define BANK0_R0  (*(volatile u8 idata *)0x00)
#define BANK0_R1  (*(volatile u8 idata *)0x01)
#define BANK0_R2  (*(volatile u8 idata *)0x02)
#define BANK0_R4  (*(volatile u8 idata *)0x04)
#define BANK0_R6  (*(volatile u8 idata *)0x06)
#define BANK0_R7  (*(volatile u8 idata *)0x07)

/* ============================================================================
 * @0x1f1 - add-in-place byte through a selectable addressing mode.
 *   value=param_1, base16=param_2, off=param_3, off_hi=param_4, mode=param_5.
 *   mode  1 : far  XDATA, addr = base + (off_hi:off)      (MOVX @DPTR)
 *   mode  0 : near IDATA, addr = (u8)(base_lo + off)      (@R0)
 *   mode -2 : 8-bit XDATA, addr = (u8)(base_lo + off)     (MOVX @R0)
 *   default : CODE read (MOVC), returns codebyte+value, no store.
 * SIGCHANGE: stub `u8 (void)` -> `u8 (u8,u16,u8,u8,s8)`.
 * ========================================================================== */
u8 math_addbyte_mode(u8 value, u16 base, u8 off, u8 off_hi, s8 mode)
{
    if (mode == 1) {
        u8 xdata *p = (u8 xdata *)(base + (((u16)off_hi << 8) | off));
        value = (u8)(*p + value);
        *p = value;
        return value;
    }
    if (mode == 0) {
        u8 idata *p = (u8 idata *)(u8)((u8)base + off);
        value = (u8)(value + *p);
        *p = value;
        return value;
    }
    if (mode == -2) {
        u8 xdata *p = (u8 xdata *)(u8)((u8)base + off);
        value = (u8)(*p + value);
        *p = value;
        return value;
    }
    {   /* default: MOVC read-only */
        u8 code *p = (u8 code *)((u16)((base & 0xFF) | ((u16)(off_hi + (base >> 8)) << 8)) + off);
        return (u8)(*p + value);
    }
}


/* ============================================================================
 * @0x41f - store 4 bytes to an XDATA pointer (@DPTR).
 * SIGCHANGE: stub `u8 (void)` -> `void (u8 xdata *, u8,u8,u8,u8)`.
 * ========================================================================== */
void math_store4_xdata(u8 xdata *p, u8 b0, u8 b1, u8 b2, u8 b3)
{
    p[0] = b0;
    p[1] = b1;
    p[2] = b2;
    p[3] = b3;
}


/* ============================================================================
 * @0x42b - 16-bit add-in-place at a near/IDATA record: p[2]+=lo, then
 * p[1]=hi+p[1]+carry.  (@R0 near pointer, `hi`=A, `lo`=B.)
 * SIGCHANGE: stub `u8 (void)` -> `void (u8,u8,u8 idata *)`.
 * ========================================================================== */
void math_add16_idata(u8 hi, u8 lo, u8 idata *p)
{
    u8 old = p[2];
    u8 sum = (u8)(old + lo);
    p[2] = sum;
    p[1] = (u8)(hi + p[1] + (sum < old ? 1 : 0));
}


/* ============================================================================
 * @0x5ff - blocking delay of a 16-bit count (hi:lo) with a per-outer-tick
 * watchdog kick.  hi->IDATA 0x28, lo->IDATA 0x29 in the original.
 * SIGCHANGE: stub `u8 (void)` -> `void (u8 hi, u8 lo)`.
 * ========================================================================== */
void util_delay16_wdt(u8 hi, u8 lo)
{
    for (;;) {
        u8 old = lo;
        u8 oldhi = hi;                 /* snapshot hi BEFORE decrement (orig R6/R2) */
        lo = (u8)(lo - 1);
        if (old == 0) hi = (u8)(hi - 1);
        if ((u8)(old | oldhi) == 0) break;   /* test pre-decrement {old_lo|old_hi} */
        {
            u8 a = 0, b = 0;
            do {
                _nop_();
                a = (u8)(a + 1);
                if (a == 0) b = (u8)(b + 1);
            } while (b != 4 || a != 0x58);
        }
        flash_wdt_kick();
    }
}


/* ============================================================================
 * @0x72c - short busy-wait: 16-bit down-counter (hi:lo) with a 0x14-nop inner
 * delay per outer tick (no watchdog).
 * SIGCHANGE: stub `u8 (u8,u8)` -> real `void (u8 hi, u8 lo)`.
 * ========================================================================== */
void util_delay16_spin(u8 hi, u8 lo)
{
    for (;;) {
        u8 old = lo;
        u8 oldhi = hi;                 /* snapshot hi BEFORE decrement (orig R6/R2) */
        lo = (u8)(lo - 1);
        if (old == 0) hi = (u8)(hi - 1);
        if ((u8)(old | oldhi) == 0) break;   /* test pre-decrement {old_lo|old_hi} */
        {
            u8 i;
            for (i = 0; i < 0x14; i++) {
                _nop_();
            }
        }
    }
}


/* ============================================================================
 * @0x74b - blocking delay of a 16-bit count (hi:lo) with a 4-nop inner loop
 * and a per-outer-tick watchdog kick.  hi->IDATA 0x26, lo->IDATA 0x27.
 * SIGCHANGE: stub `u8 (void)` -> real `void (u8 hi, u8 lo)`.
 * ========================================================================== */
void util_delay16_wdt4(u8 hi, u8 lo)
{
    for (;;) {
        u8 old = lo;
        u8 oldhi = hi;                 /* snapshot hi BEFORE decrement (orig R6/R2) */
        lo = (u8)(lo - 1);
        if (old == 0) hi = (u8)(hi - 1);
        if ((u8)(old | oldhi) == 0) break;   /* test pre-decrement {old_lo|old_hi} */
        {
            u8 a = 0, b = 0;
            do {
                _nop_();
                _nop_();
                _nop_();
                _nop_();
                a = (u8)(a + 1);
                if (a == 0) b = (u8)(b + 1);
            } while (b != 6 || a != 0x36);
        }
        flash_wdt_kick();
    }
}


/* @0x258 - 16-bit little-endian add-in-place: [addr+1] += add with carry
 * into [addr] (plus the hi term).  Faithful to the two-byte accumulate. */
void math_add16_xdata(u8 hi, u16 addr, u8 add)
{
    u8 lo = XB(addr + 1);
    u8 sum = (u8)(lo + add);
    XB(addr + 1) = sum;
    XB(addr) = (u8)(XB(addr) + hi + (sum < lo ? 1 : 0));
}

