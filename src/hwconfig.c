/* hwconfig.c - SFR/peripheral configuration leaves (sys_*), flash/WDT and
 * ISP reset.  Moved verbatim (pure code motion, no logic changes) from
 * port00.c/port01.c/port02.c/port03.c/port04.c/leaf.c during the Phase-C file
 * reorganization.
 */
#include "cm68.h"
#include "cm68_stubs.h"
#include <intrins.h>

#define BANK0_R4  (*(volatile u8 idata *)0x04)
#define BANK0_R5  (*(volatile u8 idata *)0x05)
#define BANK0_R7  (*(volatile u8 idata *)0x07)
#define BANK1_R4  (*(volatile u8 idata *)0x0C)   /* IDATA 0x0C = bank1 R4 */
#define GBIT_20  (*(volatile u8 idata *)0x20)
#define IRAM(a)   (*(volatile unsigned char idata *)(a))
#define XD(a)  (*(volatile unsigned char xdata *)(a))

/* SFR 0xFB (CCAP1H, PCA module-1 capture high) - not present in cm68_sfr.h;
 * declared TU-locally here (an __sfr declaration creates no linker symbol).
 * Written only by sys_clear_ccap1h. */
sfr CCAP1H = 0xFB;

/* ============================================================================
 * Low-address leaf functions and interrupt-vector fillers (CODE 0x09..0x8f).
 *
 * sys_clear_ccap1h @0x15: `CLR A; MOV 0xFB,A` - clear SFR 0xFB (CCAP1H).  A real
 *   leaf (ends in RET); called from the early low-level init path.
 * ========================================================================== */
u8 sys_clear_ccap1h(void)
{
    CCAP1H = 0;
    return 0;
}


/* sys_et2_enable @0x3d: `SETB 0xAD` - set IE.5 (ET2, Timer2 interrupt enable). */
u8 sys_et2_enable(void)
{
    SFR_IE_AD = 1;
    return 0;
}


/* sys_t2mod_load7f @0x65: `MOV 0xC9,#0x7F` - load SFR_C9 (T2MOD) with 0x7F. */
u8 sys_t2mod_load7f(void)
{
    SFR_C9 = 0x7F;
    return 0;
}


/* sys_et2_disable @0x6d: `CLR 0xAD` - clear IE.5 (ET2, Timer2 interrupt enable). */
u8 sys_et2_disable(void)
{
    SFR_IE_AD = 0;
    return 0;
}


/* sys_store_acc_debounce @0x70: `MOV 0x0C,R7` - store R7 into IDATA 0x0C (bank1 R4),
 * the ACC-detect debounce counter.  Argument is register-passed (R7). */
u8 sys_store_acc_debounce(void)
{
    BANK1_R4 = BANK0_R7;
    return 0;
}


/* sys_t2con_clr_lowbits @0x7d: `ANL 0xC8,#0xFC` - clear T2CON bits 0..1. */
u8 sys_t2con_clr_lowbits(void)
{
    T2CON = (u8)(T2CON & 0xFC);
    return 0;
}


/* sys_eie_set_bit5 @0x8f: `ORL 0xAA,#0x20` - set EIE bit5 (secondary int enable). */
u8 sys_eie_set_bit5(void)
{
    EIE = (u8)(EIE | 0x20);
    return 0;
}


/* ============================================================================
 * @0x85 - flash/WDT unlock key then HADDR bit1 (no EA guard, cf. FUN_05e4).
 * SIGCHANGE: stub `u8 (void)` -> real `void (void)`.
 * ========================================================================== */
void flash_unlock_haddr1(void)
{
    HKEY  = 0xAA;
    HKEY  = 0x55;
    HADDR |= 2;
}


/* sys_ccapm4_disable @0x000e: CCAPM4 &= 0x7F (clear ECOM bit7 - the inverse of
 * leaf.c sys_ccapm4_enable).  Tail-jumped into by FUN_243a. */
static void sys_ccapm4_disable(void) { CCAPM4 &= 0x7F; }


/* sys_pcon_set_b2 @0x001d: PCON |= 0x04.  Tail of FUN_1200 (LJMP 0x002e path is
 * separate); paired clear is FUN_0026. */
static void sys_pcon_set_b2(void) { PCON |= 0x04; }


/* sys_pcon_clr_b2 @0x0026: PCON &= 0xFB (clear bit2). */
void sys_pcon_clr_b2(void) { PCON &= 0xFB; }


/* sys_bd_set_bit7 @0x002e: SFR_BD |= 0x80.  Tail-jumped into by FUN_1200. */
void sys_bd_set_bit7(void) { SFR_BD |= 0x80; }


/* sys_bd_clr_bit7 @0x0035: SFR_BD &= 0x7F. */
void sys_bd_clr_bit7(void) { SFR_BD &= 0x7F; }


/* sys_extie_set @0x0821: set `mask` bits in the ext-enable byte selected by
 * `sel` (1->SFR_AD, 2->SFR_AE, 3->SFR_AF, 0/else->SFR_AC).  Byte-exact: the
 * original does MOV A,R5; ORL <sel-reg>,A. */
void sys_extie_set(u8 sel, u8 mask)
{
    switch (sel) {
    case 1:  SFR_AD |= mask; break;
    case 2:  SFR_AE |= mask; break;
    case 3:  SFR_AF |= mask; break;
    default: SFR_AC |= mask; break;   /* sel==0; sel>3 returns without effect  */
    }
}


/* sys_extie_clr @0x0860: clear `mask` bits in the ext-enable byte selected by
 * `sel` (mirror of FUN_0821).  Original: MOV A,R5; CPL A; ANL <sel-reg>,A. */
void sys_extie_clr(u8 sel, u8 mask)
{
    switch (sel) {
    case 1:  SFR_AD &= (u8)~mask; break;
    case 2:  SFR_AE &= (u8)~mask; break;
    case 3:  SFR_AF &= (u8)~mask; break;
    default: SFR_AC &= (u8)~mask; break;
    }
}


/* sys_periph_mode_apply @0x0095: FUN_175f (apply display/peripheral mode from
 * g_disp_mode) then tail-call FUN_2516 (gauge-hold = 5).  Called by rail step
 * 0x08. */
void sys_periph_mode_apply(void)
{
    disp_periph_mode_apply();      /* LCALL 0x175f */
    util_gauge_hold_set();         /* LJMP 0x2516 (tail) */
}


/* sys_periph_reinit @0x083f: park ext block 0xF015, drop HIE bit5, arm 0xF08D,
 * set ext-enable AD bit5, reconfigure SFR_B9 to mode 2, enable global ints
 * (EA), load the power-off window (FUN_1200), then tail into FUN_002e via
 * FUN_1200 -> set PCON bit2 (FUN_001d).  Byte-exact. */
void sys_periph_reinit(void)
{
    XB(0xF015) = 0;                /* MOVX F015 = 0                             */
    HIE &= 0xDF;                   /* ANL 0xA1,#0xDF (clear bit5)               */
    XB(0xF08D) = 1;                /* MOVX F08D = 1                             */
    sys_extie_set(1, 0x20);        /* R5=0x20,R7=1 -> SFR_AD |= 0x20            */
    SFR_B9 = (u8)((SFR_B9 & 0xFD) | 0x02);  /* ANL #0xFD then ORL #0x02        */
    IE |= 0x80;                    /* ORL 0xA8,#0x80 -> EA = 1                  */
    pwr_poff_reload();             /* LCALL 0x1200                             */
    sys_pcon_set_b2();             /* LJMP 0x001d (tail)                        */
}


/* sys_periph_recycle @0x0882: rail-step-0x17 peripheral cycle - drop P0.5, wait
 * for power-down to clear (FUN_243a), clear the P1.3 flag (FUN_0a6c), disable
 * the int pair (FUN_1fe0), disable all ext blocks (FUN_17b0), re-init the
 * peripherals (FUN_083f), then clear global bit0 and zero x_0250.  Byte-exact. */
void sys_periph_recycle(void)
{
    P0_5 = 0;                      /* CLR 0x85 = P0.5                           */
    sys_pd_wait();                 /* LCALL 0x243a                             */
    bl_p13_flag_clear();           /* ACALL 0x0a6c                             */
    sys_int_disable_pair();        /* LCALL 0x1fe0                             */
    sys_periph_disable_all();      /* LCALL 0x17b0                             */
    sys_periph_reinit();           /* ACALL 0x083f                            */
    GBIT_20 &= (u8)~0x01;          /* CLR 0x00 (bit0 of IDATA 0x20)            */
    x_0250 = 0;                    /* MOVX 0x0250 = 0                          */
}


/* ============================================================================
 * util_port_dummy_read @0xff6 - busy-delay then dummy-read a CMS port (P4/P5/P6/IPH0).
 * SIGCHANGE @0xff6: void -> (u8 param_1).  Delay count is BANK0_R5 (case 0/1/2)
 * or BANK0_R7 (case 3), read here from the register bank (byte-faithful).
 * The port reads are side-effect reads (result discarded in the original).
 * ==========================================================================*/
u8 util_port_dummy_read(u8 port_sel)
{
    volatile u8 dummy = 0;   /* side-effect port read sink (init to silence W84) */
    s8 c;

    if (port_sel == 1) {
        c = (s8)(BANK0_R5 + 1);
        do { c = (s8)(c - 1); } while (c != 0);
        dummy = P5;
    } else if (port_sel == 2) {
        c = (s8)(BANK0_R5 + 1);
        do { c = (s8)(c - 1); } while (c != 0);
        dummy = P6;
    } else if (port_sel == 3) {
        c = (s8)(BANK0_R7 + 1);
        do { c = (s8)(c - 1); } while (c != 0);
        dummy = IPH0;
    } else if (port_sel == 0) {
        c = (s8)(BANK0_R5 + 1);
        do { c = (s8)(c - 1); } while (c != 0);
        dummy = P4;
    }
    (void)dummy;
    return 0;
}


/* ============================================================================
 * util_port_clear_bit @0x104e - write ~(1<<n) to a CMS port bit-mask register.
 * SIGCHANGE @0x104e: void -> (u8 param_1).  Shift count is BANK0_R5 (case
 * 0/1/2) or BANK0_R7 (case 3), read from the register bank.  Returns the mask.
 * ==========================================================================*/
u8 util_port_clear_bit(u8 port_sel)
{
    u8 mask;
    s8 c;

    if (port_sel == 1) {
        mask = 1;
        c = (s8)(BANK0_R5 + 1);
        while ((c = (s8)(c - 1)) != 0) { mask <<= 1; }
        P5 = (u8)~mask;
        return (u8)~mask;
    }
    if (port_sel == 2) {
        mask = 1;
        c = (s8)(BANK0_R5 + 1);
        while ((c = (s8)(c - 1)) != 0) { mask <<= 1; }
        P6 = (u8)~mask;
        return (u8)~mask;
    }
    if (port_sel == 3) {
        mask = 1;
        c = (s8)(BANK0_R7 + 1);
        while ((c = (s8)(c - 1)) != 0) { mask <<= 1; }
        IPH0 = (u8)~mask;
        return (u8)~mask;
    }
    if (port_sel == 0) {
        mask = 1;
        c = (s8)(BANK0_R5 + 1);
        while ((c = (s8)(c - 1)) != 0) { mask <<= 1; }
        P4 = (u8)~mask;
        return (u8)~mask;
    }
    return port_sel;
}


/* ============================================================================
 * isr_ext8 @0x109a - INTERRUPT SERVICE ROUTINE (RETI @0x10e3).
 * Body: read P5 bit via FUN_0ff6(5-count,sel=1); if the sampled port is nonzero
 * set bit-flag 0x00 and clear it via FUN_104e(sel=1).  The full register
 * save/restore + RETI in the original is the SDCC __interrupt prologue/epilogue.
 * Vector: original slot 0x0043 (LJMP 0x109a) -> n = (0x43-3)/8 = 8.
 * ==========================================================================*/
void isr_ext8(void) interrupt 8
{
    u8 f = 1;                             /* orig R7 seed, preserved across 0ff6 */

    BANK0_R5 = 5;                         /* delay/shift count seed (R5) */
    util_port_dummy_read(1);                     /* dummy-read P5 (sel=1) */
    if (f != 0) {                         /* orig test is effectively constant */
        P0_0 = 1;                         /* SETB bit 0x00 */
        BANK0_R5 = 5;
        util_port_clear_bit(1);
    }
}


/* ============================================================================
 * sys_timer_mode_cfg @0x1239 - configure TMOD / SOFL timer-mode nibble.
 * SIGCHANGE @0x1239: void -> (u8 param_1, u8 param_2, u8 param_3).  param_3 is
 * the selector (1=T1 mode, 3=SOFL low, 4=SOFL bits4-5, 0=T0 mode).
 * ==========================================================================*/
u8 sys_timer_mode_cfg(u8 val_lo, u8 val_hi, u8 sel)
{
    u8 v;

    if (sel == 1) {
        TMOD &= 0x8f;
        v = (u8)((val_hi << 6) | (val_lo << 4));
        TMOD |= v;
        return v;
    }
    if (sel != 3) {
        if (sel == 4) {
            SOFL &= 0xcf;
            sel = (u8)(val_lo << 4);
            SOFL |= sel;
        } else if (sel == 0) {
            TMOD &= 0xf8;
            val_lo = (u8)((val_hi << 2) | val_lo);
            TMOD |= val_lo;
            return val_lo;
        }
        return sel;
    }
    SOFL &= 0xfc;
    SOFL |= val_lo;
    return val_lo;
}


/* ============================================================================
 * sys_timer1_setup @0x1277 - Timer1 setup: mode, prescale, reload 0x3a3a, enable.
 * ==========================================================================*/
u8 sys_timer1_setup(void)
{
    sys_timer_mode_cfg(2, 0, 0);
    sys_clocksrc_cfg(0, 0);
    sys_timer_reload_load(0x3a, 0x3a, 0);
    sys_timer_int_enable(0);
    IP  &= 0xfd;
    IE  |= 0x80;
    TCON |= 0x10;
    return 0;
}


/* ============================================================================
 * sys_timer_int_enable @0x12b6 - enable a timer interrupt (ET0/ET1/EIE bits).
 * SIGCHANGE @0x12b6: void -> (u8 param_1) selector.
 * ==========================================================================*/
u8 sys_timer_int_enable(u8 sel)
{
    if (sel == 1) {
        ET1 = 1;
        return 0;
    }
    if (sel != 3) {
        if (sel == 4) {
            EIE |= 2;
        } else if (sel == 0) {
            ET0 = 1;
            return 0;
        }
        return 0;
    }
    EIE |= 1;
    return 0;
}


/* ============================================================================
 * sys_timer_reload_load @0x12d3 - load a timer/PCA/2nd-UART reload pair.
 * SIGCHANGE @0x12d3: void -> (u8 param_1, u8 param_2, u8 param_3).  param_3 is
 * the selector (1=T1, 3=PCA0/1, 4=RXSTAT/RXDAT, 0=T0).  param_1=low, param_2=high.
 * ==========================================================================*/
u8 sys_timer_reload_load(u8 lo, u8 hi, u8 sel)
{
    if (sel == 1) {
        TH1 = hi;
        TL1 = lo;
        return 0;
    }
    if (sel != 3) {
        if (sel == 4) {
            RXDAT  = hi;
            RXSTAT = lo;
        } else if (sel == 0) {
            TH0 = hi;
            TL0 = lo;
            return 0;
        }
        return 0;
    }
    CCAPM1 = hi;
    CCAPM0 = lo;
    return 0;
}


/* ============================================================================
 * sys_clocksrc_cfg @0x12f6 - configure SFR_8E / SOFL clock-source bits.
 * SIGCHANGE @0x12f6: void -> (u8 param_1, u8 param_2) selector in param_2.
 * ==========================================================================*/
u8 sys_clocksrc_cfg(u8 val, u8 sel)
{
    if (sel == 1) {
        SFR_8E &= 0xef;
        SFR_8E |= (u8)(val << 4);
        return (u8)(val << 4);
    }
    if (sel != 3) {
        if (sel == 4) {
            SOFL &= 0xbf;
            sel = (u8)(val << 6);
            SOFL |= sel;
        } else if (sel == 0) {
            SFR_8E &= 0xf7;
            SFR_8E |= (u8)(val << 3);
            return (u8)(val << 3);
        }
        return sel;
    }
    SOFL &= 0xfb;
    SOFL |= (u8)(val << 2);
    return (u8)(val << 2);
}


/* ============================================================================
 * sys_timer2_mode_low @0x1332 - set Timer2 clock/mode low bits in T2CON.
 * SIGCHANGE @0x1332: void -> (u8 param_1, u8 param_2).
 * ==========================================================================*/
u8 sys_timer2_mode_low(u8 clk_bits, u8 mode_bits)
{
    T2CON &= 0xe4;
    T2CON |= (u8)(mode_bits | clk_bits);
    return (u8)(mode_bits | clk_bits);
}


/* ============================================================================
 * sys_timer2_setup @0x133a - Timer2 / 2nd-serial / capture bring-up + int enable.
 * ==========================================================================*/
u8 sys_timer2_setup(void)
{
    sys_timer2_mode_low(0, 1);
    sys_timer2_pwm_cfg(0, 0);
    sys_timer2_topbit(0);
    sys_timer2_reload_load();
    sys_hpcon_hp_on();
    sys_hpcon_setbit();
    IP &= 0xdf;
    sys_et2_enable();
    IE |= 0x80;
    g_ext_wake_req = 0;
    FIE &= 0xdf;
    xp_f0c8 = 0x25;
    xp_f02a |= 0x20;
    if ((T2CON & 3) == 0) {
        T2CON |= 1;
    }
    return 0;
}


/* ============================================================================
 * sys_timer2_topbit @0x137a - set T2CON top bit field.
 * SIGCHANGE @0x137a: void -> (u8 param_1).
 * ==========================================================================*/
u8 sys_timer2_topbit(u8 bit_n)
{
    T2CON &= 0x7f;
    T2CON |= bit_n;
    return bit_n;
}


/* ============================================================================
 * sys_timer2_reload_load @0x1381 - load Timer2 count/reload from register-bank R7:R4.
 * Kept void per stub.  BANK0_R7 (low) / BANK0_R4 (high) are read from the
 * register bank exactly as the original (MOV direct 0x07 / 0x04).
 * ==========================================================================*/
u8 sys_timer2_reload_load(void)
{
    TL2 = BANK0_R7;
    TH2 = BANK0_R4;
    if (((T2CON & 0x18) == 0x10) || ((T2CON & 0x18) == 0x18)) {
        RCAP2L = BANK0_R7;
        RCAP2H = BANK0_R4;
    }
    return 0;
}


/* ============================================================================
 * sys_timer2_pwm_cfg @0x13a4 - configure T2CON clock-out + SFR_CE PWM mux per mode.
 * SIGCHANGE @0x13a4: void -> (u8 param_1, u8 param_2).  param_2 = mode 0..3.
 * ==========================================================================*/
u8 sys_timer2_pwm_cfg(u8 val, u8 mode)
{
    u8 v = mode;

    if (mode == 0) {
        T2CON &= 0xbf;
        T2CON |= (u8)((val & 1) << 6);
        SFR_CE &= 0xfc;
        v = (u8)(val | 1);
        SFR_CE |= v;
    }
    if (mode == 1) {
        T2CON &= 0xdf;
        if (val == 0) {
            T2CON |= 0x20;
        }
        SFR_CE &= 0xf3;
        v = (u8)((val | 1) << 2);
        SFR_CE |= v;
    }
    if (mode == 2) {
        T2CON &= 0xdf;
        if (val == 0) {
            T2CON |= 0x20;
        }
        SFR_CE &= 0xcf;
        v = (u8)((val | 1) << 4);
        SFR_CE |= v;
    }
    if (mode == 3) {
        T2CON &= 0xdf;
        if (val == 0) {
            T2CON |= 0x20;
        }
        SFR_CE &= 0x3f;
        v = (u8)((val | 1) << 6);
        SFR_CE |= v;
    }
    return v;
}


/* ============================================================================
 * sys_hpcon_setbit @0x1405 - OR (1<<BANK0_R7) into HPCON.
 * Kept void-param per stub; shift count read from register bank R7.
 * ==========================================================================*/
u8 sys_hpcon_setbit(void)
{
    u8 b = 1;
    s8 c = (s8)(BANK0_R7 + 1);

    while ((c = (s8)(c - 1)) != 0) {
        b <<= 1;
    }
    HPCON |= b;
    return b;
}


/* ============================================================================
 * isr_timer2 @0x1413 - INTERRUPT SERVICE ROUTINE (RETI @0x14a0).
 * Slow-tick housekeeping: if FUN_17e4 armed, service Timer2 reload and advance
 * the {0274:0275} debounce counter (cap 0x015e); if FUN_17fd flags a completed
 * capture, run FUN_14a1 over {0274:0275} and reset it.  Register save/restore +
 * RETI is the SDCC __interrupt frame.
 * Vector: original slot 0x002B (LJMP 0x1413) -> n = (0x2B-3)/8 = 5.
 * SIGCHANGE @0x17fd/@0x1819: called with a bit index R7; the original 1413
 * uses index 0 (CLR A; MOV R7,A @0x1469/@0x1470) for the T2MOD test/clear. */
void isr_timer2(void) interrupt 5
{
    if (sys_t2mod_bit7()) {
        sys_timer2_reload_load();
        sys_t2mod_load7f();
        if (g_decode_state != 0) {
            if ((((u16)g_t2cap_hi << 8) | g_t2cap_lo) < 0x015eu) {
                math_add16_xdata(0, 0x0274, 1);      /* {0274:0275} += 1 */
            }
        } else {
            if (g_t2cap_hi != 0 || g_t2cap_lo != 0) {
                g_t2cap_hi = 0;
                g_t2cap_lo = 0;
            }
        }
    }

    if (sys_t2mod_testbit(0)) {                        /* T2MOD.0 capture-complete */
        sys_t2mod_setmask(0);                         /* T2MOD = ~(1<<0) = 0xFE */
        util_pattern_decode(g_t2cap_hi, g_t2cap_lo);
        g_t2cap_hi = 0;
        g_t2cap_lo = 0;
    }
}


/* ============================================================================
 * sys_comms_cfg @0x1764 - (re)configure a comms channel (0/1/2) - GPIO + the
 * FUN_17xx/2bxx peripheral register block.
 * ==========================================================================*/
u8 sys_comms_cfg(u8 channel)
{
    if (channel == 1) {
        sys_periph_dis_f017();
        sys_periph_ack_f016();
        P1_6 = 1;
        sys_periph_dis_f032();
        sys_periph_ack_f031();
        TXD = 1;
        return 0;
    }
    if (channel == 2) {
        sys_periph_dis_f016();
        sys_periph_dis_f017();
        sys_periph_dis_f031();
        sys_periph_dis_f032();
    } else if (channel == 0) {
        sys_periph_ack_f017();
        P1_7 = 1;
        sys_periph_dis_f016();
        sys_periph_ack_f032();
        INT0 = 1;
        sys_periph_dis_f031();
    }
    return 0;
}


/* sys_pcon_set_b1 @0x10e4: PCON |= 0x02 (the original has NOP padding around
 * the ORL).  Called at the top of each pwr_poweroff_monitor iteration. */
void sys_pcon_set_b1(void) { PCON |= 0x02; }


/* sys_bd_field @0x11f6: SFR_BD[5:4] = val[1:0] (byte-exact: clear bits 5:4 then
 * OR in SWAP(val)&0xF0, so bits 7:6 are OR-set from val[3:2]). */
void sys_bd_field(u8 val)
{
    SFR_BD = (u8)((SFR_BD & 0xCF) | (u8)((val & 0x0F) << 4));
}


/* sys_bcbd_load @0x122c: SFR_BD low nibble = a (R6), SFR_BC = b (R7). */
void sys_bcbd_load(u8 a, u8 b)
{
    SFR_BD = (u8)((SFR_BD & 0xF0) | (a & 0x0F));
    SFR_BC = b;
}


/* @0x1795 - disable ext block 0xF016 and clear HIE bit6. Faithful. */
u8 sys_periph_dis_f016(void)
{
    xp_f016 = 0;
    HIE &= 0xBF;
    return 0;
}


/* @0x179e - disable ext block 0xF031 and clear FIE1 bit1. Faithful. */
u8 sys_periph_dis_f031(void)
{
    xp_f031 = 0;
    FIE1 &= 0xFD;
    return 0;
}


/* @0x17a7 - disable ext block 0xF017 and clear HIE bit7. Faithful. */
u8 sys_periph_dis_f017(void)
{
    xp_f017 = 0;
    HIE &= 0x7F;
    return 0;
}


/* @0x17db - disable ext block 0xF032 and clear FIE1 bit2. Faithful. */
u8 sys_periph_dis_f032(void)
{
    xp_f032 = 0;
    FIE1 &= 0xFB;
    return 0;
}


/* @0x17e4 - return bit7 of T2MOD (SFR_C9).  Disasm: MOV A,0xC9; R7 = ACC.7.  */
u8 sys_t2mod_bit7(void)
{
    return (u8)((SFR_C9 >> 7) & 1);
}


/* @0x17ee - clear bit a0 of HPCON, return the applied mask (~(1<<a0)).
 * a0 arrives in R7 (matches stub). Faithful (disasm-derived).                 */
u8 sys_hpcon_clrbit(u8 bit_n)
{
    u8 m = (u8)(1u << bit_n);       /* 1<<bit_n, 8-bit (bit_n>=8 -> 0, as RLC loop) */
    HPCON &= (u8)~m;
    return (u8)~m;
}


/* @0x17fd - test bit a0 of T2MOD (SFR_C9); return 1 if set, else 0.
 * SIGCHANGE @0x17fd: stub (void) -> (u8 a0).  a0 in R7 builds a 16-bit mask;
 * only the low byte is ANDed with T2MOD (disasm 1801-1817).                    */
u8 sys_t2mod_testbit(u8 bit_n)
{
    u16 mask = (u16)1u << bit_n;
    return ((u8)mask & SFR_C9) ? 1 : 0;
}


/* @0x1819 - T2MOD (SFR_C9) = ~(1<<a0); return that mask.
 * SIGCHANGE @0x1819: stub (void) -> (u8 a0).  a0 in R7 (disasm-derived).       */
u8 sys_t2mod_setmask(u8 bit_n)
{
    u8 m = (u8)(1u << bit_n);
    SFR_C9 = (u8)~m;
    return (u8)~m;
}


/* ======================================================================
 *  @0x1828 - configure a serial channel (UART0 SCON or PCA/2nd-UART CCAP0L)
 *  clock-source + mode via aux SFR 0x91.
 *  Params (registers): a0=R3 mode(1/2/3), a1=R5 2-bit source, a2=R7 channel.
 *  SIGCHANGE @0x1828: stub (void) -> (u8 a0, u8 a1, u8 a2).
 *  (Decompile showed a spurious 4th arg = R6; the real body reads only R3/R5/R7.)
 * ==================================================================== */
u8 sys_serial_clk_cfg(u8 mode, u8 clk_src, u8 chan)
{
    if (chan == 0) {
        SCON = (u8)((SCON & 0x3F) | (u8)((clk_src & 3) << 6));
        SFR_91 &= 0xFA;
        if (mode == 2)      SFR_91 |= 4;
        else if (mode == 3) SFR_91 |= 5;
        else if (mode == 1) SFR_91 |= 1;
    }
    if (chan == 1) {
        CCAP0L = (u8)((CCAP0L & 0x3F) | (u8)((clk_src & 3) << 6));
        SFR_91 &= 0xF5;
        if (mode == 2)      SFR_91 |= 8;
        else if (mode == 3) SFR_91 |= 0x0A;
        else if (mode == 1) SFR_91 |= 2;
    }
    return 0;
}


/* @0x1ed5 - power/clock mode select via PCON: a0==0 -> PCON|=0x80,
 * a0==1 -> PCON|=0x40.  SIGCHANGE @0x1ed5: stub (void) -> (u8 a0). */
u8 sys_pcon_mode(u8 sel)
{
    if (sel == 0) PCON |= 0x80;
    if (sel == 1) PCON |= 0x40;
    return 0;
}


/* ============================================================================
 * sys_periph_disable_all @0x17b0: full ext-peripheral shutdown - disable the
 * four blocks then clear each block's XDATA control cell and the matching
 * enable bits in SFR_9A/FIE/FIE1.  Reached from the rail sequencer via
 * FUN_0882.  Byte-exact.
 * ========================================================================== */
void sys_periph_disable_all(void)
{
    sys_periph_dis_f016();              /* ACALL 0x1795                        */
    sys_periph_dis_f031();              /* ACALL 0x179e                        */
    sys_periph_dis_f017();              /* ACALL 0x17a7                        */
    sys_periph_dis_f032();              /* ACALL 0x17db                        */
    xp_f005 = 0;         SFR_9A &= 0xDF;    /* F005=0 ; ANL 0x9A,#0xDF         */
    xp_f035 = 0;         FIE1   &= 0xDF;    /* F035=0 ; ANL 0xA3,#0xDF         */
    xp_f024 = 0;         FIE    &= 0xEF;    /* F024=0 ; ANL 0xA2,#0xEF         */
    g_ext_wake_req = 0;  FIE    &= 0xDF;    /* F025=0 ; ANL 0xA2,#0xDF         */
    xp_f02a &= 0xDF;                        /* F02A &= 0xDF (read-modify-write) */
}


/* =====================================================================
 * @0x1ff3 - SFR_8E = (SFR_8E & 0x1F) | ((param<<5) & 0xE0); returns the field.
 * ===================================================================== */
u8 sys_sfr8e_field(u8 param_1)
{
    u8 field = (u8)((u8)(param_1 << 5) & 0xE0);
    SFR_8E = (u8)((SFR_8E & 0x1F) | field);
    return field;
}


/* =====================================================================
 * @0x251d - decrement the ACC-detect debounce counter (IRAM 0x0c, bank1 R4)
 * if it is non-zero.
 * ===================================================================== */
u8 sys_acc_debounce_dec(void)
{
    if (IRAM(0x0C) != 0) IRAM(0x0C)--;
    return 0;
}


/* =====================================================================
 * @0x2575 - predicate: returns 1 when the debounce counter (IRAM 0x0c) is 0.
 * (Original returns the result in carry; modelled as a byte return.)
 * ===================================================================== */
u8 sys_acc_debounce_zero(void)
{
    return (u8)(IRAM(0x0C) == 0);
}


/* sys_int_clr_pair @0x1fd3: clear the RX-enable bit (0x10) of the serial block
 * selected by `chan` (0 -> SCON, 1 -> CCAP0L).  Byte-exact. */
u8 sys_int_clr_pair(u8 chan)
{
    if (chan == 0) SCON   &= 0xEF;      /* R7==0 -> ANL 0x98,#0xEF             */
    if (chan == 1) CCAP0L &= 0xEF;      /* R7==1 -> ANL 0xEA,#0xEF             */
    return 0;
}


/* sys_int_disable_pair @0x1fe0: clear UART0 RX-enable then drop IE bit4.  The
 * original passes a channel register that is hardcoded to 0 at both call points,
 * so the IE-bit6 branch (CJNE R7,#1) is never taken.  Called from FUN_0882. */
u8 sys_int_disable_pair(void)
{
    sys_int_clr_pair(0);                /* R7=0 -> ACALL 0x1fd3                */
    IE &= 0xEF;                         /* R7==0 -> ANL 0xA8,#0xEF (clear bit4) */
    /* CJNE R7,#1 branch (ANL IE,#0xBF) is unreachable here: R7 is 0.          */
    return 0;
}


/* sys_pd_wait @0x243a: clear the sleep-arm request (X_SLEEP_ARMED), spin until
 * PCON1 power-down bit1 clears, then disable the CCAPM4 output (tail FUN_000e).
 * Called from FUN_0882. */
u8 sys_pd_wait(void)
{
    X_SLEEP_ARMED = 0;                  /* MOVX 0x0276 = 0                     */
    while (PCON1 & 0x02) { }            /* MOV A,PCON1; JB ACC.1 -> re-read     */
    sys_ccapm4_disable();              /* LJMP 0x000e (tail)                  */
    return 0;
}


/* sys_work_clear @0x232c: reset the sub-tick counter (X_TICKS) and clear the low
 * nibble of the work-request bitfield X_WORK (four successive read-modify-write
 * masks, byte-faithful to the original).  Called from FUN_0e04. */
u8 sys_work_clear(void)
{
    u8 a;
    X_TICKS = 0;                        /* MOVX 0x0273 = 0                     */
    a = X_WORK;                         /* MOVX A,0x0270                       */
    a &= 0xFE; X_WORK = a;              /* ANL #0xFE; MOVX                     */
    a &= 0xFD; X_WORK = a;              /* ANL #0xFD; MOVX                     */
    a &= 0xFB; X_WORK = a;              /* ANL #0xFB; MOVX                     */
    a &= 0xF7; X_WORK = a;              /* ANL #0xF7; MOVX                     */
    return 0;
}


/* ==========================================================================
 * @0x262d - system reset: flash-key unlock, clear HADDR bit7, spin to reset.
 * Writes 0x55 to XDATA 0xF691 first (ext-periph, not a cm68_xram.h symbol).
 * The final infinite loop lets the watchdog/reset controller take the part.
 * ======================================================================== */
u8 isp_system_reset(void)
{
#ifdef NOTURNO_SLEEP_FIX
    /* FIX: this is a hard reset (flash-key unlock + spin-to-reset).  A spurious
     * reset would drop the SoC rail and defeat the keep-alive.  When keep-alive
     * is active, hold the rail HIGH and skip the reset entirely.  Overrides the
     * original unconditional hard reset @0x262d. */
    if (NOTURNO_KEEPALIVE_HOLD) {
        P3_6 = 1;
        return 0;
    }
#endif
    XD(0xf691) = 0x55;
    HKEY  = 0xaa;
    HKEY  = 0x55;
    HADDR = (u8)(HADDR & 0x7f);
    HKEY  = 0xaa;
    HKEY  = 0x55;
    for (;;) {
        /* spin until reset */
    }
}


/* ==========================================================================
 * @0x2b3d - ack ext-periph 0xF017 and re-enable its interrupt (HIE bit7).
 * ======================================================================== */
u8 sys_periph_ack_f017(void)
{
    xp_f017 = 0;
    HIE = (u8)(HIE | 0x80);
    return 0;
}


/* ==========================================================================
 * @0x2b46 - ack ext-periph 0xF032 and re-enable its interrupt (FIE1 bit2).
 * ======================================================================== */
u8 sys_periph_ack_f032(void)
{
    xp_f032 = 0;
    FIE1 = (u8)(FIE1 | 0x04);
    return 0;
}


/* ==========================================================================
 * @0x2b4f - ack ext-periph 0xF016 and re-enable its interrupt (HIE bit6).
 * ======================================================================== */
u8 sys_periph_ack_f016(void)
{
    xp_f016 = 0;
    HIE = (u8)(HIE | 0x40);
    return 0;
}


/* ==========================================================================
 * @0x2b58 - ack ext-periph 0xF031 and re-enable its interrupt (FIE1 bit1).
 * ======================================================================== */
u8 sys_periph_ack_f031(void)
{
    xp_f031 = 0;
    FIE1 = (u8)(FIE1 | 0x02);
    return 0;
}


/* @0x005 - CCAPM4 |= 0x80 (enable PCA module 4 output used by sleep timing) */
void sys_ccapm4_enable(void)
{
    CCAPM4 |= 0x80;
}


/* @0x5e4 - watchdog/flash "kick": unlock key 0xAA,0x55 then HADDR bit0.
 * Guards EA around the sequence exactly like the original. */
void flash_wdt_kick(void)
{
    if (EA) {
        EA = 0;
        HKEY = 0xAA;
        HKEY = 0x55;
        HADDR |= 1;
        EA = 1;
        return;
    }
    HKEY = 0xAA;
    HKEY = 0x55;
    HADDR |= 1;
}


/* @0x577 - configure PCON1 low bits + CCAPM4 mode nibble.
 * PCON1 = (PCON1 & 0xBF) | pcon1_bits ;  CCAPM4 = (CCAPM4 & 0x8F) | (hi<<4). */
void sys_pcon1_ccapm4_cfg(u8 pcon1_bits, u8 ccapm4_hi)
{
    PCON1  = (PCON1 & 0xBF) | pcon1_bits;
    CCAPM4 = (CCAPM4 & 0x8F) | (u8)(ccapm4_hi << 4);
}


/* @0x819 - clear bit7 of extended reg 0xF692. */
void sys_clr_f692_bit7(void)
{
    XB(0xF692) &= 0x7F;
}


/* @0x4d - HPCON |= 0x80 (enter a high-power/clock mode). */
void sys_hpcon_hp_on(void) { HPCON |= 0x80; }


/* @0x75 - HPCON &= 0x7f. */
void sys_hpcon_hp_off(void) { HPCON &= 0x7F; }

