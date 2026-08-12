/* power.c - power management: sleep loop, PCON1 power-down, ACC arbiter,
 *           the P3.6 SoC-rail control, and the sleep FIX.
 *
 * This is the bug locus.  Constants below were recovered from THIS image's
 * decompile (cm68_decomp.c) and cross-checked against the raw opcodes; they
 * are NOT assumed from older cm68 revisions.
 *
 * Recovered power-path facts:
 *   - ACC/power mode lives in XDATA:  X_ACC_MODE (0x025f), validity X_ACC_VALID
 *     (0x025e, normally 0).  State test: s=X025e; if(X025e==0) s=X025f^K;
 *     state==K iff s==0.  K=3 => ACC OFF / power-down eligible.
 *   - Keep-alive flag (0x50/0x07): X_FLAGS(0x0266) bit4 (0x10).
 *   - Sleep-armed flag: X_SLEEP_ARMED (0x0276).
 *   - Poweroff-timeout (0x50/0x09): seed -> X_POFF_TMO (0x0255), X_POFF_CNT
 *     (0x0256) reset to 0 at set time.
 *   - Sleep-emit debounce: X_SLEEP_DEB (0x01ec), fires at >1 (i.e. 2 ticks).
 *   - Power-down instruction: PCON1 |= 2  (PCON1 @ SFR 0xDF).
 *   - Rail control bit: P3.6 (bit 0xB6).  CLR P3.6 => rail down.
 */
#include "cm68.h"
#include <intrins.h>
#include "cm68_stubs.h"

/* @0x587 - shutdown/power-down path: park PCA, drop extended enables, then
 * enter PCON1 power-down (bit1).  Faithful. */
void pwr_powerdown(void)
{
    sys_pcon1_ccapm4_cfg(0, 7);       /* PCON1 low=0, CCAPM4 mode nibble=7 */
    XB(0xF014) = 1;
    XB(0xF022) = 1;
    XB(0xF023) = 1;
    XB(0xF026) = 1;
    XB(0xF002) = 1;
    XB(0xF003) = 1;
    XB(0xF024) = 1;
    sys_clr_f692_bit7();
    sys_ccapm4_enable();
    PCON1 |= 2;                /* enter power-down */
}

/* @0x5b0 - clocks / UART / interrupt bring-up (init path). Faithful. */
void sys_bringup(void)
{
    sys_can_int_bringup(1);
    sys_timer1_setup();
    pwr_powerdown();
    sys_sfr8e_field(7);
    sys_eie_set_bit5();
    SFR_BA = (SFR_BA & 0xDF) | 0x20;
    IE |= 0x80;                /* EA */
    flash_wdt_kick();
    flash_unlock_haddr1();
}

/* @0x2342 - peripheral (CAN/aux) interrupt bring-up. Faithful.
 * with_aux_reinit: when set, also re-enables the aux RX interrupt (0xF013/HIE)
 * and re-runs the P13-flag-clear + P3.6 rail step below. sys_bringup() (full
 * init) passes 1; the port01.c re-init call site passes 0 to skip that part. */
void sys_can_int_bringup(u8 with_aux_reinit)
{
    XB(0xF036) = 0;  FIE1  |= 0x40;
    XB(0xF004) = 0;  SFR_9A &= 0xEF;
    if (with_aux_reinit) { XB(0xF013) = 0; HIE |= 0x08; }
    XB(0xF015) = 0;  HIE  &= 0xDF;
    XB(0xF005) = 0;  SFR_9A |= 0x20;
    P0_5 = 1;
    if (with_aux_reinit) {
        bl_p13_flag_clear();
#ifdef NOTURNO_SLEEP_FIX
        /* FIX: the original CLR P3.6 @0x2373 drops the SoC rail during this
         * (re)init path.  Hold it up when keep-alive is active so a peripheral
         * re-init cannot power the SoC off.  Overrides the unconditional cut. */
        if (NOTURNO_KEEPALIVE_HOLD) P3_6 = 1;
        else                        P3_6 = 0;
#else
        P3_6 = 0;                              /* orig "CLR WR" == CLR P3.6 */
#endif
    }
}

/* @0x7d0 - link-down housekeeping; re-enables the CAN/aux RX interrupt when
 * in the linked state. Faithful. */
void pwr_linkdown_hk(void)
{
    u8 b;
    XB(0xF000) = 0;
    SFR_9A |= 1;
    b = (u8)X_ACC_VALID;
    if (b == 0) b = (u8)(X_ACC_MODE ^ 2);
    if (b == 0) {
        X_FLAGS |= 0x40;
        XB(0xF025) = 0;
        FIE |= 0x20;
    }
}

/* @0x7a5 - clear "scan busy" flag and restart the scan-out. Faithful. */
void bl_scan_restart(void)
{
    X_FLAGS &= 0xBF;
    XB(0x024A) = 0;            /* scan head */
    XB(0x024B) = 0;            /* scan tail */
    pwr_linkdown_hk();
    bl_scan_strobe(1);
}

/* ======================================================================
 *  pwr_sleep_sample @0x1bc8 - periodic "sleep sample".
 *
 *  Configures the PCA for the requested channel, enters PCON1 power-down
 *  (idle-with-wake), waits for the wake bit to clear, then reads back a PCA
 *  capture nibble.  Mode 7 (the ACC-off sleep) is GATED: it only proceeds
 *  when state==3 (ACC off) AND the sleep-armed flag is set; otherwise it
 *  returns immediately.
 *
 *  This PCON1 power-down is *suspend-with-wake* by construction - it does NOT
 *  by itself cut the rail.  It is therefore left faithful in both builds; the
 *  fix targets the rail-cut / poweroff-notify path (FUN_241c / FUN_27e8).
 *
 *  WIDENED: returns the full 12-bit capture in R6:R7 (readback @0x07f4) as a
 *  u16 = (R6<<8)|R7, instead of just the high nibble.  Consumers reduce it to
 *  their 8-bit sample via (V>>4) (the SWAP/ANL/ORL idiom at 0x28f1/0x1c39/...)
 *  or use the full 16-bit value (0x0cc9/0x2b93/0x2d51).  The not-eligible
 *  mode-7 gate returns 0x0FFF (the original's R6=0x0f,R7=0xff).
 * ==================================================================== */
u16 pwr_sleep_sample(u8 mode)
{
    u8 pc, cm3, hi, lo;

    if (mode == 7) {
        u8 b = (u8)X_ACC_VALID;
        if (b == 0) b = (u8)(X_ACC_MODE ^ 3);      /* state==3 ? */
        if (b != 0 || X_SLEEP_ARMED == 0)
            return 0x0FFF;                          /* not eligible (R6:R7=0x0f:0xff) */
    }

    /* The channel selector (DAT_INTMEM_4e) maps mode -> PCA channel nibble. */
    {
        u8 chan;
        switch (mode) {
            case 3: chan = 8;  break;
            case 2: chan = 9;  break;
            case 0: chan = 0xC; break;
            case 4: chan = 10; break;
            case 6: chan = 2;  break;
            case 5: chan = 3;  break;
            case 7: chan = 0xB; break;
            default: chan = 7; break;
        }
        pwr_sleep_pca_cfg(chan);
    }

    /* 12 NOPs of settle time in the original. */
        _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();

    PCON1 |= 2;                                     /* power-down */
    while ((PCON1 >> 1) & 1) { }                    /* wait for wake */

    /* Readback @0x07f4: assemble the 12-bit sample into R6:R7.  PCON1.6 selects
     * the capture alignment (CCAPM3=0xDD high, CCAPM2=0xDC low). */
    pc  = PCON1;
    cm3 = CCAPM3;
    if (pc & 0x40) {                                /* JB PCON1.6 (0x07f8) */
        hi = (u8)(cm3 & 0x0F);
        lo = CCAPM2;
    } else {
        u8 cm2 = CCAPM2;
        lo = (u8)((u8)(cm3 << 4) | (u8)(cm2 >> 4));
        hi = (u8)((cm3 >> 4) & 0x0F);
    }
    return (u16)(((u16)hi << 8) | lo);
}

/* ======================================================================
 *  arb_acc @0x2239 - ACC arbiter.
 *
 *  Reads the ACC/IGN line state from the CAN/aux side (FUN_2145(6) then
 *  FUN_01ab / FUN_026e) and drives the rail-enable flag (X_FLAGS bit0) plus
 *  the action code (X_ARB_ACTION) that FUN_043b later dispatches.
 * ==================================================================== */
void arb_acc(void)
{
    s8 cVar1, cVar2;
    txq_scan_pop(6);
    cVar1 = (s8)frame_get_off(0);
    if (cVar1 == 0) return;
    cVar2 = (s8)frame_get_next(1);

    if (cVar1 == 2) {
        if (cVar2 == 1) {
            if (((X_FLAGS & FLAG_RAIL_EN) != 1) && ((X_ARB_STATE ^ 3) == 0)) {
                X_ARB_STATE ^= 3;
                X_FLAGS |= FLAG_RAIL_EN;
                X_ARB_ACTION = 4;
                return;
            }
        } else if ((X_FLAGS & FLAG_RAIL_EN) != 0) {
            X_FLAGS &= (u8)~FLAG_RAIL_EN;
            X_ARB_ACTION = 0x0F;
            X_ARB_STATE  = 3;
            pwr_railtimer_set(0, 0);
            P0_5 = 1;
        }
    } else if (cVar1 == 1) {
        if (cVar2 != 1) {
            X_FLAGS &= (u8)~FLAG_RAIL_EN;
            X_ARB_ACTION = 0x10;
            if (X_ARB_STATE != 3) X_ARB_STATE = 2;
            pwr_railtimer_set(0, 0);
            return;
        }
        if (X_ARB_STATE != 3) {
            X_ARB_ACTION = 1;
            X_FLAGS |= FLAG_RAIL_EN;
        }
    }
}

/* ======================================================================
 *  pwr_sleep_arm @0x241c - ARM sleep on ACC-off.
 *
 *  Original: clears the retry markers, and when state==3 (ACC OFF) sets
 *  X_SLEEP_ARMED=1 and the extended request bit 0xF025.  This unconditional
 *  arm is half of why the unit powers OFF at ACC-off even with keep-alive on.
 * ==================================================================== */
void pwr_sleep_arm(void)
{
    u8 b;
    XB(0x0271) = 0xFF;
    XB(0x0272) = 0xFF;
    b = (u8)X_ACC_VALID;
    if (b == 0) b = (u8)(X_ACC_MODE ^ 3);
    if (b == 0) {
#ifdef NOTURNO_SLEEP_FIX
        /* FIX: on ACC-off, honour the keep-alive flag and the "no auto
         * power-off" case (timeout seed == 0).  In those cases DO NOT arm
         * the sleep/rail-cut path; hold the SoC rail up (P3.6 HIGH) so the
         * SoC can suspend/resume instead of losing power.  A real auto
         * power-off (keep-alive clear AND a non-zero timeout) still arms and
         * proceeds through the normal long-timeout shutdown in FUN_27e8. */
        if ((X_FLAGS & FLAG_KEEPALIVE) || (X_POFF_TMO == 0)) {
            P3_6 = 1;                 /* hold rail up */
            X_SLEEP_ARMED = 0;        /* stay suspended, do not cut power */
            XB(0xF025) = 0;
            return;
        }
#endif
        X_SLEEP_ARMED = 1;
        XB(0xF025) = 1;
    }
}

/* ======================================================================
 *  pwr_sleep_emit @0x27e8 - EMIT power-off notify (the rail-cut trigger).
 *
 *  Original guard: state==3 AND X_SLEEP_ARMED AND link (X_FLAGS bit3).  It
 *  debounces two slow ticks (X_SLEEP_DEB) then sends the 0x37/sub-2 message
 *  (FUN_0aad(0,2,0x37,3)) which tells the SoC to power down; combined with
 *  FUN_2239 dropping the rail-enable, the head unit goes fully OFF.
 *
 *  The original guard notably does NOT test the keep-alive flag - which is
 *  why the Android keep-alive=1 doesn't stop the power-off.
 * ==================================================================== */
u8 pwr_sleep_emit(void)
{
    u8 b, cnt;
    b = (u8)X_ACC_VALID;
    if (b == 0) b = (u8)(X_ACC_MODE ^ 3);          /* state==3 ? */

    if ((b == 0) && (X_SLEEP_ARMED != 0)) {
#ifdef NOTURNO_SLEEP_FIX
        /* FIX: keep-alive / zero-timeout => never emit the power-off notify;
         * hold the rail and remain suspended.  Only a genuine long-timeout
         * shutdown (keep-alive clear AND timeout seed non-zero) is allowed to
         * fall through and, once X_POFF_CNT has counted up to X_POFF_TMO,
         * emit the clean power-off. */
        if ((X_FLAGS & FLAG_KEEPALIVE) || (X_POFF_TMO == 0)) {
            P3_6 = 1;                 /* hold rail up */
            return 0;
        }
        /* long-timeout accounting: count slow ticks up to the seed */
        if (X_POFF_CNT < X_POFF_TMO) {
            X_POFF_CNT++;
            return 0;                 /* not yet time to shut down */
        }
#endif
        if ((X_FLAGS & FLAG_LINK) != 0) {
            cnt = (u8)(X_SLEEP_DEB + 1);
            b   = (u8)(X_SLEEP_DEB - 1);
            X_SLEEP_DEB = cnt;
            if (cnt > 1) {
                X_SLEEP_DEB = 0;
                b = txq_enqueue(0, 2, 0x37, 3);   /* power-off notify */
            }
        }
    }
    return b;
}

/* @0x899 - dispatch the current ACC-arbiter action code. Faithful. */
void arb_dispatch(void)
{
    pwr_rail_sequencer((u8)X_ARB_ACTION);
}

/* @0x24ce - power/ACC state initialisation. Faithful (exact seeds). */
void pwr_state_init(void)
{
    X_ARB_ACTION = 0;
    X_ARB_STATE  = 0;
    XB(0x0259) = 0;
    XB(0x025A) = 0;
    X_FLAGS = FLAG_RAIL_EN;      /* 0x0266 = 1 */
    XB(0x0252) = 0;
    XB(0x024E) = 6;
    XB(0x024F) = 0x19;
    XB(0x0260) = 6;
    XB(0x0261) = 0xD5;
    pwr_railtimer_seed();
    XB(0x0267) = 0;
    XB(0x0268) = 0;
    XB(0x0250) = 0;
    X_POFF_TMO = 0;
    X_POFF_CNT = 0;
}

/* @0x2b61 - reset the ACC/power state vars. Faithful. */
void pwr_state_reset(void)
{
    XB(0x0202) = 0; XB(0x021E) = 0; XB(0x021F) = 0; XB(0x020C) = 0;
    X_ACC_VALID = 0; X_ACC_MODE = 0;
    X_ACC_PREV_V = 0; X_ACC_PREV_M = 0;
    XB(0x025B) = 0; XB(0x025C) = 0; XB(0x0257) = 0; XB(0x0258) = 0;
    XB(0x0264) = 0; XB(0x0265) = 0;
    X_SLEEP_ARMED = 0;
}


/* ---- appended during Phase-C file reorganization (pure code motion from
 * port00-04.c/leaf.c/dispatch.c; see git history for original locations). ---- */

#define I21       (*(volatile u8 idata *)0x21)
#define I22       (*(volatile u8 idata *)0x22)
#define GBIT_20  (*(volatile u8 idata *)0x20)
#define GBIT01  (*(volatile unsigned char idata *)0x20)
#define P0_4_IN  ((P0 & 0x10) != 0)
#define P1_5_IN  ((P1 & 0x20) != 0)
#define IRAM(a)   (*(volatile unsigned char idata *)(a))
#ifdef NOTURNO_SLEEP_FIX
#define RAIL_CUT()  do { if (NOTURNO_KEEPALIVE_HOLD) P3_6 = 1; else P3_6 = 0; } while (0)
#else
#define RAIL_CUT()  do { P3_6 = 0; } while (0)   /* orig "CLR WR" == CLR P3.6 */
#endif

/* ============================================================================
 * @0x9d - return the ACC-arbiter action code (XDATA 0x0254).  Matches stub.
 * ========================================================================== */
u8 arb_get_action(void)
{
    return X_ARB_ACTION;
}


/* ============================================================================
 * @0x7b9 - power-up bring-up: clear TX-ready flag, reset TX queue, seed the TX
 * pump, run the state-init / state-reset, then the ACC-arbiter reset.
 * SIGCHANGE: stub `u8 (void)` -> real `void (void)`.
 * ========================================================================== */
void pwr_bringup(void)
{
    g_tx_kick = 0;
    txq_reset();
    uart_tx_pump_sel(200, 1);
    pwr_state_init();
    pwr_state_reset();
    bl_scan_restart();
}


/* ============================================================================
 * @0xa61 - store a little-endian word into XDATA 0x0259:0x025a.
 * SIGCHANGE: stub `u8 (u8,u8)` -> real `void (u8,u8)`.
 * ========================================================================== */
void pwr_railtimer_set(u8 lo, u8 hi)
{
    g_railtimer_lo = lo;
    g_railtimer_hi = hi;
}


/* ============================================================================
 * pwr_railtimer_seed @0xa5d: store constant 0x0200 little-endian into {g_railtimer_lo:g_railtimer_hi}
 * (g_railtimer_lo = lo = 0, g_railtimer_hi = hi = 2).
 * ==========================================================================*/
u8 pwr_railtimer_seed(void)
{
    g_railtimer_lo = 0;                 /* MOVX 0x259 = R6 (#0) */
    g_railtimer_hi = 2;                 /* MOVX 0x25a = R7 (#2) */
    return 0;
}


/* ============================================================================
 * pwr_volt_arbiter @0xcc9 - ACC/voltage-level arbiter (phase-0 slow worker).
 * Samples a 4-bit level via FUN_1bc8(1) and drives a 4-state (024d) hysteresis
 * machine that raises/cuts the SoC rail and stages emit(2,6)/emit-off(2,6).
 *
 * The 16-bit sample is Ghidra {param_1:bVar1}; disasm keeps the low byte = 1
 * (companion const) and the high byte = the FUN_1bc8 reading.  Comparisons are
 * reproduced as 16-bit against {0271:0272} (running minimum), {024e:024f} and
 * {0260:0261} thresholds - byte-exact with the SUBB chains.
 *
 * *** RAIL-CRITICAL: this function performs CLR P3.6 (rail cut) at orig 0x0df2,
 *     one of the three P3.6 cuts OUTSIDE the FUN_043b table. ***
 * ==========================================================================*/
u8 pwr_volt_arbiter(void)
{
    u16 samp16;

    if (g_volt_hold != 0) {
        g_volt_hold--;
    }

    /* Full 12-bit sample (FUN_1bc8 widened to u16 = R6:R7).  The original stores
     * I21=R6 (hi), I22=R7 (lo) and compares the full 16-bit value (0x0cd6). */
    samp16 = pwr_sleep_sample(1);
    I21 = (u8)(samp16 >> 8);              /* 0x21 = R6 (hi) */
    I22 = (u8)samp16;                     /* 0x22 = R7 (lo) */

    /* running minimum in {0271:0272} */
    if ((((u16)g_vmin_hi << 8) | g_vmin_lo) > samp16) {
        g_vmin_hi = I21;
        g_vmin_lo = I22;
    }

    /* level <= 4  (samp16 < 0x0413) -> "on" state 0 */
    if (samp16 < 0x0413u) {
        if (g_volt_state != 0 || ((X_FLAGS >> 1) & 1) == 0) {
            g_volt_state = 0;
            X_FLAGS |= 2;
            P0_5 = 1;
            txq_enqueue(0, 0, 2, 6);
        }
        g_volt_hold = 200;
        return 0;
    }

    /* level < {024e:024f} -> state 1 */
    if (samp16 < ((((u16)g_volt_thr1_hi) << 8) | g_volt_thr1_lo)) {
        if (g_volt_state == 1 && ((X_FLAGS >> 1) & 1) == 1) {
            g_volt_hold = 200;
            return 0;
        }
        g_volt_state = 1;
        X_FLAGS |= 2;
        P0_5 = 1;
        txq_enqueue(0, 0, 2, 6);
        g_volt_hold = 200;
        return 0;
    }

    if (samp16 < ((((u16)g_volt_thr2_hi) << 8) | g_volt_thr2_lo)) {   /* {0260:0261} > samp16 */
        if (g_volt_state == 1) {
            g_volt_hold = 200;
            return 0;
        }
        if (g_volt_state == 3) {
            if (g_volt_hold != 0) {
                return 0;
            }
            goto LAB_dbe;                 /* -> state 2 (off) */
        }
        return 0;                         /* 024d not 1/3: no change */
    } else {                              /* {0260:0261} <= samp16 */
        if (samp16 < 0x0adcu) {
            if (g_volt_hold != 0) {
                return 0;
            }
            if (g_volt_state == 2) {
                return 0;
            }
            goto LAB_dbe;
        }
        if (samp16 < 0x0b86u) {
            if (g_volt_state == 3) {
                g_volt_hold = 200;
                return 0;
            }
            if (g_volt_state != 1) {
                return 0;
            }
            if (g_volt_hold != 0) {
                return 0;
            }
            goto LAB_dbe;
        }
        /* samp16 >= 0x0b86 -> state 3: rail CUT */
        if (g_volt_state == 3 && ((X_FLAGS >> 1) & 1) == 1) {
            g_volt_hold = 200;
            return 0;
        }
        g_volt_state = 3;
        X_FLAGS |= 2;
        bl_p13_flag_clear();
        pwr_rail_rearm();
#ifdef NOTURNO_SLEEP_FIX
        /* FIX: the low-voltage arbiter cuts the rail here (orig CLR P3.6 @0x0df2).
         * Hold it up when keep-alive is active so a transient low reading cannot
         * power the SoC off.  Overrides the unconditional rail cut. */
        if (NOTURNO_KEEPALIVE_HOLD) P3_6 = 1;
        else                        P3_6 = 0;
#else
        P3_6 = 0;                         /* *** RAIL CUT (orig CLR P3.6 @0x0df2) *** */
#endif
        txq_enqueue(0, 0, 2, 6);
        g_volt_hold = 200;
        return 0;
    }

LAB_dbe:                                  /* state 2: rail off-notify */
    g_volt_state = 2;
    X_FLAGS &= 0xfd;
    txq_enqueue(0, 1, 2, 6);
    return 0;
}


/* ============================================================================
 * util_flags_clr_link @0xfbb - clear X_FLAGS bit3 (link).
 * ==========================================================================*/
u8 util_flags_clr_link(void)
{
    X_FLAGS &= 0xf7;
    return X_FLAGS;
}


/* pwr_poff_reload @0x1200: seed the poweroff down-counter at XDATA 0x0267:0268
 * from X_POFF_TMO (0x0255:0256), or zero it when keep-alive (X_FLAGS bit4) is
 * set; then program the SFR_BD/BC PWM/poff window and set SFR_BD bit7.  Called
 * from FUN_083f. */
void pwr_poff_reload(void)
{
    if (!(X_FLAGS & 0x10)) {            /* JB ACC.4 -> keepalive path          */
        XB(0x0267) = XB(0x0255);        /* copy X_POFF_TMO lo                   */
        XB(0x0268) = XB(0x0256);        /* copy X_POFF_TMO hi                   */
    } else {
        XB(0x0267) = 0;                 /* keepalive: zero the counter          */
        XB(0x0268) = 0;
    }
    sys_bd_field(2);                    /* R7=2; ACALL 0x11f6                   */
    sys_bcbd_load(0x0F, 0xA0);          /* R6=0x0f, R7=0xa0; ACALL 0x122c       */
    sys_bd_set_bit7();                  /* LJMP 0x002e (tail): SFR_BD |= 0x80   */
}


/* ============================================================================
 * pwr_poweroff_monitor @0x10f0: the rail-step-0x17 periodic keep-state loop.
 * Loops until both P1.5 and the global bit0 (IDATA 0x20.0) are high.  Each
 * iteration: pulse PCON bit1; unless keep-alive, run the poweroff down-counter
 * (every 0x78 sub-ticks, subtract 2 from the 16-bit count at 0x0267:0268 and,
 * when it reaches 0, CUT the SoC rail P3.6) and kick the watchdog; then bump the
 * 32-bit tick at 0x0269 with a nonlinear ramp (extra +1 while tick<40 and again
 * while tick<10) and kick the watchdog again.
 *
 * The tick is a big-endian 32-bit counter at XDATA 0x269 (0x269=MSB..0x26c=LSB).
 * On this 8-bit core it is maintained byte-wise: increment is a carry chain and
 * "tick/40==0" / "tick/10==0" are just "tick < 40" / "tick < 10" -- avoiding the
 * 32-bit load/store/divide runtime helpers entirely.
 * ========================================================================== */

/* Byte-wise +1 on the big-endian 32-bit tick counter at XDATA 0x269. */
static void tick0269_inc(void)
{
    if (++XB(0x26c) == 0)
        if (++XB(0x26b) == 0)
            if (++XB(0x26a) == 0)
                ++XB(0x269);
}

/* True when the tick counter is below n (n <= 255): all high bytes zero and LSB < n. */
static u8 tick0269_below(u8 n)
{
    return (u8)(XB(0x269) == 0 && XB(0x26a) == 0 && XB(0x26b) == 0 && XB(0x26c) < n);
}

u8 pwr_poweroff_monitor(void)
{
    for (;;) {
        /* @0x10f0: exit when P1.5 AND global bit0 are both high. */
        if ((P1_5 & 1) && (GBIT_20 & 0x01))
            return 0;

        sys_pcon_set_b1();                     /* @0x10f8 ACALL 0x10e4         */

        if (!(X_FLAGS & 0x10)) {               /* @0x1101 JB ACC.4 -> skip all */
            /* @0x1104: proceed only if X_POFF_TMO (0x0255:0256) is nonzero and
             * the global bit0 is clear. */
            u8 lo = XB(0x0255);
            u8 tv = (lo != 0) ? lo : XB(0x0256);
            if (tv != 0 && !(GBIT_20 & 0x01)) {
                x_0250++;                      /* @0x1111 INC + store          */
                if (x_0250 >= 0x78) {          /* SUBB #0x78; JNC              */
                    u16 v;
                    x_0250 = 0;                /* @0x111d                      */
                    v = (u16)(((u16)XB(0x0267) << 8) | XB(0x0268));  /* R6:R7  */
                    if (v != 0) {              /* @0x1128 JZ -> flash kick     */
                        if (v >= 3)            /* @0x112a (v<3 -> zero)        */
                            math_add16_xdata(0xFF, 0x0267, 0xFE);  /* v -= 2   */
                        else {
                            XB(0x0267) = 0;    /* @0x1140                      */
                            XB(0x0268) = 0;
                        }
                        {                      /* @0x1147: reload; if 0 -> cut */
                            u8 a = XB(0x0267);
                            if (a == 0) a = XB(0x0268);
                            if (a == 0) {
                                /* @0x114f CLR P3.6 - SoC rail-cut chokepoint. */
#ifdef NOTURNO_SLEEP_FIX
                                if (NOTURNO_KEEPALIVE_HOLD) P3_6 = 1;
                                else                        P3_6 = 0;
#else
                                P3_6 = 0;      /* orig CLR P3.6 (rail CUT)     */
#endif
                            }
                        }
                    }
                }
            }
            flash_wdt_kick();                  /* @0x1153 LCALL 0x05e4         */
        }

        tick0269_inc();                        /* @0x1159: tick += 1            */

        /* @0x1172: q = tick/40; q==0 (tick < 40) -> bump once more. */
        if (tick0269_below(0x28))
            tick0269_inc();

        /* @0x11ad: q2 = tick/10; q2==0 (tick < 10) -> bump once more. */
        if (tick0269_below(0x0a))
            tick0269_inc();

        flash_wdt_kick();                      /* @0x11e8 LCALL 0x05e4         */

        /* @0x11eb: exit when bit0 AND P1.5 set; otherwise loop. */
        if ((GBIT_20 & 0x01) && (P1_5 & 1))
            return 0;
    }
}


/* ============================================================================
 * pwr_shutdown_sequence @0x0e04: the rail-step-0x17 shutdown/sleep-entry path.
 * Parks SFR_BD/PCON bits, drops ext-enable AD bit5, brings up the CAN/aux int,
 * re-applies the display peripheral mode, resets the TX queue, pulses the P1.3
 * flag + P0.5, kicks the TX pump, runs the power-down + sleep-arm paths, clears
 * the work scheduler, resets the decode block, then tail-calls the scan restart.
 * Byte-exact.  Called from rail step 0x17.
 * ========================================================================== */
u8 pwr_shutdown_sequence(void)
{
    sys_bd_clr_bit7();                 /* LCALL 0x0035                        */
    sys_pcon_clr_b2();                 /* LCALL 0x0026                        */
    sys_extie_clr(1, 0x20);            /* R5=0x20,R7=1; ACALL 0x0860          */
    sys_can_int_bringup(0);            /* R7=0; LCALL 0x2342                  */
    disp_periph_mode_apply();          /* LCALL 0x175f                        */
    txq_reset();                       /* LCALL 0x22cd                        */
    bl_p13_set_and_p05();              /* ACALL 0x0a6a                        */
    g_tx_kick = 1;                     /* MOVX 0x0251 = 1                     */
    uart_tx_pump_sel(0, 1);            /* R5=0 (a0), R7=1 (a1); LCALL 0x1eca  */
    pwr_powerdown();                   /* LCALL 0x0587                        */
    pwr_sleep_arm();                   /* LCALL 0x241c                        */
    sys_work_clear();                  /* LCALL 0x232c                        */
    util_decode_reset();               /* LCALL 0x2ee7                        */
    bl_scan_restart();                 /* LJMP 0x07a5 (tail)                  */
    return 0;
}


/* =====================================================================
 * @0x2447 - re-arm: strobe P0.5, kick FUN_074b, then FUN_0fbb (tail-call).
 * ===================================================================== */
u8 pwr_rail_rearm(void)
{
    P0_5 = 1;
    util_delay16_wdt4(0, 10);   /* faithful: pre-charge delay 0x000a */
    util_flags_clr_link();
    return 0;
}


/* =====================================================================
 * @0x25a2 - if the arbiter action is 0x0F (idle), latch the poweroff hold
 * count g_shutdown_delay = param_1 and pulse FUN_0a6a.
 * SIGCHANGE @0x25a2: stub `u8(void)` -> `void(u8)`; arg in R7.
 * ===================================================================== */
void pwr_shutdown_hold_set(u8 hold_count)
{
    if (X_ARB_ACTION == 0x0F) {
        g_shutdown_delay = hold_count;
        bl_p13_set_and_p05();
    }
}


/* =====================================================================
 * @0x25b2 - count down g_shutdown_delay; when it hits zero drop P0.5, set global bit
 * (IRAM 0x20 bit1) and run FUN_0a70.
 * ===================================================================== */
u8 pwr_shutdown_tick(void)
{
    if (g_shutdown_delay != 0) {
        g_shutdown_delay--;
        if (g_shutdown_delay == 0) {
            P0_5 = 0;
            IRAM(0x20) |= 0x02;     /* SETB bit 0x01 (byte 0x20, bit1) */
            bl_p13_flag_update(0);       /* faithful: 0a70(r7=0) */
        }
    }
    return 0;
}


/* pwr_present_debounce @0x257c: sample P1.5 exactly 1000 times, AND the samples
 * together, and latch the result into X_FLAGS bit2 (FLAG_PRESENT) - set when
 * P1.5 stayed high for every sample, cleared otherwise.  The original counts a
 * 16-bit index (R4:R5) up to (R4==3, R5==0xE8) = 1000.  Called from rail step
 * 0x00 before it reads FLAG_PRESENT. */
u8 pwr_present_debounce(void)
{
    u8  acc = 1;                        /* R7=1                                */
    u16 i;
    for (i = 0; i < 1000; i++)          /* R4:R5 0..(3,0xE8)                   */
        acc &= (u8)(P1_5 & 1);          /* MOV C,0x95; RLC A; ANL R7,A         */
    if (acc) X_FLAGS |= FLAG_PRESENT;   /* JNZ -> ORL 0x0266,#0x04             */
    else     X_FLAGS &= (u8)~FLAG_PRESENT; /* else ANL 0x0266,#0xFB            */
    return 0;
}


/* @0x5ce - sleep PCA configuration.
 * PCON1 = (PCON1 & 0x7F) | ((param & 0x10) << 3) ;
 * CCAPM4 = (CCAPM4 & 0xF0) | (param & 0x0F). */
void pwr_sleep_pca_cfg(u8 chan)
{
    PCON1  = (PCON1 & 0x7F) | (u8)((chan & 0x10) << 3);
    CCAPM4 = (CCAPM4 & 0xF0) | (chan & 0x0F);
}


/* @0x0a50 - inter-step delay gate.  Reads g_railtimer low byte; if it is
 * non-zero the timer is still running, otherwise it re-reads the high byte.
 * The original returns carry set ("expired") only when the whole 16-bit value
 * is 0.  Gating handlers do ACALL 0x0a50 then "JC proceed / else fall to the
 * default RET (wait)". */
static u8 railtimer_expired(void)
{
    u8 a = g_railtimer_lo;              /* MOVX 0x0259                        */
    if (a == 0) a = g_railtimer_hi;     /* JNZ .; INC DPTR; MOVX 0x025a       */
    return (u8)(a == 0);                /* CLR C; JNZ .; SETB C -> C=(a==0)   */
}


/* Forward decls - several handlers tail-transfer into another (the original
 * uses SJMP/AJMP/fall-through; each target ends in RET, so a C call is exact). */
static void h_00(void); static void h_01(void); static void h_02(void);
static void h_03(void); static void h_04(void); static void h_05(void);
static void h_06(void); static void h_07(void); static void h_08(void);
static void h_0d(void); static void h_10(void); static void h_11(void);
static void h_12(void); static void h_13(void); static void h_14(void);
static void h_15(void); static void h_16(void); static void h_17(void);

/* code 0x00 @0x08da - RAIL CUT.  Full power-off entry. */
static void h_00(void)
{
    u8 present;
    bl_p13_set_and_p05();               /* ACALL 0x0a6a: SETB P0.5 + latch    */
    RAIL_CUT();                         /* CLR P3.6 @0x08dc - rail CUT        */
    X_ARB_STATE = 0;                    /* MOVX 0x0253 = 0                    */
    pwr_present_debounce();             /* LCALL 0x257c: latch FLAG_PRESENT from P1.5 */
    present = (X_FLAGS & FLAG_PRESENT) ? 1 : 0;   /* MOVX 0x0266; JB ACC.2    */
    txq_enqueue(present, 1, 0, 6);      /* ACALL 0x0aad; regs R7=6,R5=1,R3:R2=present */
                                        /* PARTIAL: emit payload-arg mapping approx */
    X_ARB_ACTION = 0x0f;                /* next = 0x0f (idle / unmatched)     */
    pwr_railtimer_set(3, 0);            /* R7=3,R6=0 -> 3 ticks               */
}


/* code 0x06 @0x0903 - RAIL CUT after delay; next = 0x07. */
static void h_06(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JC ; else default    */
    RAIL_CUT();                         /* CLR P3.6 @0x0909 - rail CUT        */
    X_ARB_ACTION = 0x07;
    pwr_railtimer_set(3, 0);            /* R7=3,R6=0                          */
}


/* code 0x07 @0x0915 - after delay, next = 0x01. */
static void h_07(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JC ; else default    */
    X_ARB_ACTION = 0x01;
}


/* code 0x05 @0x0943 - RAIL UP.  After delay: next = 0x02, raise P3.6. */
static void h_05(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JC ; else default    */
    X_ARB_ACTION = 0x02;                /* MOVX 0x0254 = 2                    */
    P3_6 = 1;                           /* SETB P3.6 @0x094f - rail UP        */
    pwr_railtimer_set(0xb8, 0x0b);      /* R7=0xb8,R6=0x0b -> 0x0bb8 ticks    */
}


/* code 0x01 @0x0932 - arm rail-up: set _0_1, latch, seed timer, next = 0x05,
 * then FALL THROUGH into handler 0x05 (0x0943). */
static void h_01(void)
{
    GBIT01 |= 0x02;                     /* SETB 0x01 (byte 0x20 bit1)         */
    bl_p13_flag_update(6);              /* R7=6; ACALL 0x0a70                 */
    pwr_railtimer_seed();               /* ACALL 0x0a5d -> g_railtimer=0x0200 */
    X_ARB_ACTION = 0x05;                /* MOVX 0x0254 = 5                    */
    uart_tx_wake_burst();               /* LCALL 0x1f35: 4x SBUF=0 + short delay */
    h_05();                             /* fall through 0x0943                */
}


/* code 0x04 @0x0922 - if PRESENT set, run the rail-up arm (handler 0x01);
 * otherwise just stage a notify (action stays 0x04). */
static void h_04(void)
{
    if (X_FLAGS & FLAG_PRESENT) {       /* MOVX 0x0266; JB ACC.2 -> 0x0932    */
        h_01();
        return;
    }
    /* R3:R2=0, R5=1, R7=6 */
    txq_enqueue(0, 1, 0, 6);            /* AJMP 0x0aad; PARTIAL emit args     */
}


/* code 0x02 @0x0957 - branch on P0.4:
 *   P0.4=1 -> next = 0x03 (short re-arm);
 *   P0.4=0 -> (after gate) seed timer, next = 0x06, raise P0.5 & clear _0_1. */
static void h_02(void)
{
    if (P0_4_IN) {                      /* JNB 0x84 not taken                 */
        X_ARB_ACTION = 0x03;
        pwr_railtimer_set(2, 0);        /* R7=2,R6=0                          */
        return;
    }
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JC ; else default    */
    pwr_railtimer_seed();               /* ACALL 0x0a5d                       */
    X_ARB_ACTION = 0x06;                /* shared tail @0x09e9 with A=6       */
    P0_5 = 1;                           /* SETB P0.5 @0x09ea                  */
    GBIT01 &= (u8)~0x02;                /* CLR 0x01 @0x09ec                   */
    bl_p13_flag_update(0);              /* R7=0; SJMP 0x0a70                  */
}


/* code 0x03 @0x0975 - branch on P0.4:
 *   P0.4=1 -> gate; if expired jump into handler 0x08;
 *   P0.4=0 -> next = 0x02, arm timer. */
static void h_03(void)
{
    if (P0_4_IN) {                      /* JNB 0x84 not taken (P0.4=1)        */
        if (!railtimer_expired()) return;  /* ACALL 0x0a50; JC 0x097e else def */
        h_08();                         /* JB 0x84 taken -> 0x098c            */
        return;
    }
    /* P0.4=0: 0x0975 JNB -> 0x097e, JB 0x84 not taken -> set action 0x02     */
    X_ARB_ACTION = 0x02;
    pwr_railtimer_set(0x58, 0);         /* R7=0x58; R6=A residual (PARTIAL @0x0989 hi) */
}


/* code 0x08 @0x098c - next = 0x0d, then FALL THROUGH into handler 0x0d. */
static void h_08(void)
{
    sys_periph_mode_apply();            /* LCALL 0x0095: disp mode + gauge hold */
    X_ARB_ACTION = 0x0d;
    pwr_railtimer_set(0x0a, 0);         /* R7=0x0a,R6=0 -> 10 ticks           */
    h_0d();                             /* fall through 0x099b                */
}


/* code 0x0d @0x099b - after delay, drop P0.5, next = 0x0f (idle). */
static void h_0d(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50 gate                  */
    P0_5 = 0;                           /* CLR P0.5 @0x09a1                   */
    X_ARB_ACTION = 0x0f;
}


/* code 0x10 @0x09aa - clear _0_1, latch, arm timer, next = 0x11. */
static void h_10(void)
{
    GBIT01 &= (u8)~0x02;                /* CLR 0x01                           */
    bl_p13_flag_update(0x3c);           /* R7=0x3c; ACALL 0x0a70              */
    pwr_railtimer_set(0x3c, 0);         /* R7=0x3c (residual),R6=0            */
    X_ARB_ACTION = 0x11;
}


/* code 0x11 @0x09ba - after delay, arm long timer, next = 0x12. */
static void h_11(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JC ; else default    */
    pwr_railtimer_set(0x70, 0x17);      /* 0x1770 ticks                       */
    X_ARB_ACTION = 0x12;
}


/* code 0x12 @0x09cd - branch on P0.4:
 *   P0.4=0 -> clear link flag, next = 0x14, arm timer;
 *   P0.4=1 -> (after gate) seed timer, next = 0x13, raise P0.5 & clear _0_1. */
static void h_12(void)
{
    if (!P0_4_IN) {                     /* JB 0x84 not taken (P0.4=0)         */
        util_flags_clr_link();          /* ACALL 0x0fbb: X_FLAGS &= 0xf7      */
        X_ARB_ACTION = 0x14;
        pwr_railtimer_set(0x1e, 0);     /* R7=0x1e,R6=0                       */
        return;
    }
    if (!railtimer_expired()) return;   /* 0x09de: ACALL 0x0a50; JNC default  */
    pwr_railtimer_seed();               /* ACALL 0x0a5d                       */
    X_ARB_ACTION = 0x13;                /* shared tail @0x09e9 with A=0x13     */
    P0_5 = 1;                           /* SETB P0.5                          */
    GBIT01 &= (u8)~0x02;                /* CLR 0x01                           */
    bl_p13_flag_update(0);              /* R7=0                               */
}


/* code 0x13 @0x09f2 - RAIL CUT after delay; next = 0x15. */
static void h_13(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JNC default          */
    RAIL_CUT();                         /* CLR P3.6 @0x09f6 - rail CUT        */
    X_ARB_ACTION = 0x15;
}


/* code 0x14 @0x09ff - branch on P0.4:
 *   P0.4=1 -> next = 0x12, arm timer;
 *   P0.4=0 -> (after gate) raise P0.5, FALL THROUGH into handler 0x15. */
static void h_14(void)
{
    if (P0_4_IN) {                      /* JB 0x84 taken -> 0x0a06, JNB not taken -> 0x0a09 */
        X_ARB_ACTION = 0x12;
        pwr_railtimer_set(0xb0, 0x04);  /* 0x04b0 ticks                       */
        return;
    }
    if (!railtimer_expired()) return;   /* P0.4=0: ACALL 0x0a50; JNC default  */
    P0_5 = 1;                           /* SETB P0.5 @0x0a15                  */
    h_15();                             /* fall through 0x0a17                */
}


/* code 0x15 @0x0a17 - next = 0x16, arm short timer (no gate). */
static void h_15(void)
{
    X_ARB_ACTION = 0x16;
    pwr_railtimer_set(0x64, 0);         /* 100 ticks                          */
}


/* code 0x16 @0x0a23 - after delay: clear link, seed timer, and CUT P3.6 ONLY
 * when X_FLAGS bit4 is set (byte-exact original: JNB ACC.4 skips CLR P3.6);
 * next = 0x17.  Note bit4 == FLAG_KEEPALIVE, so the faithful build cuts here
 * precisely when keep-alive is flagged; the fixed build's RAIL_CUT() holds. */
static void h_16(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JNC default          */
    util_flags_clr_link();              /* ACALL 0x0fbb                       */
    pwr_railtimer_seed();               /* ACALL 0x0a5d                       */
    if (X_FLAGS & 0x10) {               /* MOVX 0x0266; JNB ACC.4 skips CLR   */
        RAIL_CUT();                     /* CLR P3.6 @0x0a32 - rail CUT        */
    }
    X_ARB_ACTION = 0x17;
}


/* code 0x17 @0x0a3b - periodic keep-state: after delay, when P1.5 is low run
 * the (un-ported) service calls; re-arm a 2-tick timer.  Action stays 0x17
 * (self-loop; the original writes no X_ARB_ACTION here). */
static void h_17(void)
{
    if (!railtimer_expired()) return;   /* ACALL 0x0a50; JNC default          */
    if (!P1_5_IN) {                     /* JB 0x95 -> skip these when P1.5 set */
        sys_periph_recycle();           /* ACALL 0x0882: periph deinit + reinit */
        pwr_poweroff_monitor();         /* LCALL 0x10f0: poweroff-countdown loop */
        pwr_shutdown_sequence();        /* ACALL 0x0e04: shutdown/sleep-entry    */
    }
    pwr_railtimer_set(2, 0);            /* R7=2,R6=0 -> re-arm 2 ticks        */
}


void pwr_rail_sequencer(u8 act_code)
{
    switch (act_code) {
    case 0x00: h_00(); break;
    case 0x01: h_01(); break;
    case 0x02: h_02(); break;
    case 0x03: h_03(); break;
    case 0x04: h_04(); break;
    case 0x05: h_05(); break;
    case 0x06: h_06(); break;
    case 0x07: h_07(); break;
    case 0x08: h_08(); break;
    case 0x0d: h_0d(); break;
    case 0x10: h_10(); break;
    case 0x11: h_11(); break;
    case 0x12: h_12(); break;
    case 0x13: h_13(); break;
    case 0x14: h_14(); break;
    case 0x15: h_15(); break;
    case 0x16: h_16(); break;
    case 0x17: h_17(); break;
    default:
        /* @0x0a4f DEFAULT handler is a bare RET: unmatched codes (e.g. 0x0f,
         * which has no table entry) do nothing. */
        break;
    }
}

