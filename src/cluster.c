/* cluster.c - instrument-cluster gauge/needle/pattern-decode and backlight
 * (bl_*) domain.  Moved verbatim (pure code motion, no logic changes) from
 * port00.c/port01.c/port02.c/port03.c/port04.c/leaf.c during the Phase-C file
 * reorganization.
 */
#include "cm68.h"
#include "cm68_stubs.h"

#define BANK0_R7  (*(volatile u8 idata *)0x07)
#define I21       (*(volatile u8 idata *)0x21)
#define I22       (*(volatile u8 idata *)0x22)
#define I23       (*(volatile u8 idata *)0x23)
#define I24       (*(volatile u8 idata *)0x24)
#define I25       (*(volatile u8 idata *)0x25)
#define I5A       (*(volatile u8 idata *)0x5a)
#define I5B       (*(volatile u8 idata *)0x5b)
#define I5C       (*(volatile u8 idata *)0x5c)
#define I5D       (*(volatile u8 idata *)0x5d)
#define IRAM(a)   (*(volatile unsigned char idata *)(a))
#define XD(a)  (*(volatile unsigned char xdata *)(a))

/* ============================================================================
 * @0x624 - scan-out / refresh cycle: on config-index change, emits a strobe
 * sequence and shifts out a 4x8 record read from the XDATA config table.
 * IDATA 0x21..0x24 hold the 4-byte record, 0x25..0x27 the loop counters
 * (modelled here as locals; the strobe helpers do not touch that IDATA range).
 * Matches stub `void (void)` arity.  SIGCHANGE: stub return `u8` -> `void`.
 *
 * MISSING-XDATA: the 4-byte record table at 0x0222 + 4*g_cfgtbl_rd (indices 0..9,
 * covering 0x0222..0x0249) is NOT in cm68_xram.h; accessed inline via XB().
 * ========================================================================== */
void bl_scan_refresh(void)
{
    if (g_cfgtbl_wr != g_cfgtbl_rd) {
        u8 rec[4];
        u8 idx = g_cfgtbl_rd;
        u8 i25, i26, i27;

        u8 s = X_ACC_VALID;
        if (s == 0) s = (u8)(X_ACC_MODE ^ 2);
        if (s == 0 && ((X_FLAGS >> 6) & 1) != 1) {
            X_FLAGS |= 0x40;
            pwr_linkdown_hk();
            bl_scan_strobe(1);
            util_delay16_wdt(0, 8);
        }

        /* read the 4-byte record from XDATA 0x0222 + 4*idx */
        rec[0] = XB(0x0222 + 4u * (u16)idx);
        rec[1] = XB(0x0223 + 4u * (u16)idx);
        rec[2] = XB(0x0224 + 4u * (u16)idx);
        rec[3] = XB(0x0225 + 4u * (u16)idx);

        g_cfgtbl_rd = (u8)(g_cfgtbl_rd + 1);
        if (g_cfgtbl_rd > 9) g_cfgtbl_rd = 0;

        bl_scan_strobe(0);
        util_delay16_wdt(0, 0x12);
        bl_scan_strobe(1);
        util_delay16_wdt(0, 9);

        for (i26 = 0; i26 < 4; i26++) {
            i27 = rec[i26];
            for (i25 = 0; i25 < 8; i25++) {
                bl_scan_pulse();
                if (i27 & 1) {
                    util_delay16_wdt(0, 2);
                    util_delay16_spin(0, 0x3d);
                } else {
                    util_delay16_spin(0, 0x32);
                }
                i27 = (u8)(i27 >> 1);
            }
        }
        bl_scan_pulse();
    }
}


/* ============================================================================
 * @0x743 - LED/strobe pre-charge: delay(hi:lo), drop P1.5, delay(0,0x17).
 * SIGCHANGE: stub `u8 (void)` -> real `void (u8 hi, u8 lo)`.
 * ========================================================================== */
void bl_precharge(u8 hi, u8 lo)
{
    util_delay16_wdt4(hi, lo);
    P1_5 = 0;
    util_delay16_wdt4(0, 0x17);
}


/* ============================================================================
 * @0x773 - clear ext-periph 0xF015, enable HIE bit5, pulse P1.5 with two
 * pre-charge delays, then clear 0xF015 and disable HIE bit5.
 * SIGCHANGE: stub `u8 (void)` -> real `void (void)`.
 * ========================================================================== */
void bl_strobe_pulse(void)
{
    xp_f015 = 0;
    HIE |= 0x20;
    P1_5 = 1;
    bl_precharge(0, 0x2d);
    P1_5 = 1;
    bl_precharge(0, 0x2d);
    P1_5 = 1;
    xp_f015 = 0;
    HIE &= 0xDF;
}


/* ============================================================================
 * @0xa70 - latch bit7 of X_FLAGS from the global bit flag _0_1, store r7 into
 * g_led_blink_tmo, and (when r7==0) mirror that flag onto P1.3.
 *   _0_1 = bit address 0x01 = bit1 of IDATA byte 0x20 (read-only here; it is
 *   set/cleared by FUN_0a6a/0a6c which live in another batch).
 * SIGCHANGE: stub `u8 (void)` -> real `void (u8 r7)`.
 * ========================================================================== */
void bl_p13_flag_update(u8 blink_tmo)
{
    u8 flag = (u8)((*(volatile u8 idata *)0x20) >> 1) & 1;   /* _0_1 */

    X_FLAGS = (u8)((X_FLAGS & 0x7F) | (flag << 7));
    g_led_blink_tmo = blink_tmo;
    if (blink_tmo == 0) {
        if (X_FLAGS & 0x80) {
            P1_3 = 1;
            return;
        }
        P1_3 = 0;
    }
}


/* ============================================================================
 * bl_p13_set_and_p05 @0xa6a: SETB P0.5, then run the shared tail @0xa70 with the
 * global bit flag (IDATA byte 0x20, bit1 = bit-addr 0x01) cleared: mask bit7 of
 * X_FLAGS, store g_led_blink_tmo = BANK0 R7, and (when R7==0) clear P1.3.
 * The original CLR A;MOV R7,A writes only the current-bank R7 (scratch); the
 * tail reads bank-0 cell 0x07 directly, so g_led_blink_tmo = BANK0_R7 (caller value).
 * ==========================================================================*/
u8 bl_p13_set_and_p05(void)
{
    P0_5 = 1;                                      /* SETB 0x85 */
    (*(volatile u8 idata *)0x20) &= (u8)~0x02;   /* CLR bit-addr 0x01 (byte 0x20 bit1) */
    X_FLAGS &= 0x7f;                               /* (X_FLAGS & 0x7f) | (bit0x01<<7) == &0x7f */
    g_led_blink_tmo = BANK0_R7;                             /* MOVX 0x25d = R4 (= bank-0 R7) */
    if (BANK0_R7 == 0) {                           /* JNZ skip; bit7 now 0 -> SETB path dead */
        P1_3 = 0;                                  /* CLR 0x93 */
    }
    return 0;
}


/* ============================================================================
 * bl_p13_flag_clear @0xa6c: shared tail @0xa70 with the global bit flag (bit-addr
 * 0x01) cleared - identical to FUN_0a6a but without the leading SETB P0.5.
 * ==========================================================================*/
u8 bl_p13_flag_clear(void)
{
    (*(volatile u8 idata *)0x20) &= (u8)~0x02;   /* CLR bit-addr 0x01 (byte 0x20 bit1) */
    X_FLAGS &= 0x7f;                               /* mask bit7 (bit0x01 now 0) */
    g_led_blink_tmo = BANK0_R7;                             /* g_led_blink_tmo = bank-0 R7 */
    if (BANK0_R7 == 0) {
        P1_3 = 0;                                  /* CLR 0x93 */
    }
    return 0;
}


/* ============================================================================
 * util_scan_inputs @0xb59 - per-slow-tick peer/device presence scanner.
 * Only runs when X_FLAGS bit2 (present) is set.  Samples four sub-channels via
 * FUN_2a29/2ab3/28f1/298d (CARRY = valid, value in I23) and stages up/down
 * notifications through FUN_0aa5 (which consumes I21=chan, I22=value).
 * ==========================================================================*/
u8 util_scan_inputs(void)
{
    u8 r = X_FLAGS;

    if (((X_FLAGS >> 2) & 1) == 1) {
        /* --- channel via FUN_2a29 -> device 0x20e / flag bit2 --- */
        if (util_gauge_c()) {
            if (I23 < 0xfe) {
                if (g_dev20e_debounce == 0) {
                    I21 = 1; I22 = I23; txq_enqueue_i21();
                    g_dev20e_debounce = 4; g_dev_present_mask |= 4;
                } else {
                    g_dev20e_debounce--;
                }
            } else if ((g_dev_present_mask >> 2) & 1) {
                g_dev_present_mask &= 0xfb;
                I21 = 1; I22 = 0xff; txq_enqueue_i21();
                g_dev20e_debounce = 0;
            }
        }

        /* --- channel via FUN_2ab3 -> device 0x20f / flag bit3 --- */
        if (util_gauge_d()) {
            if (I23 < 0xfe) {
                if (g_dev20f_debounce == 0) {
                    I21 = 2; I22 = I23; txq_enqueue_i21();
                    g_dev20f_debounce = 4; g_dev_present_mask |= 8;
                } else {
                    g_dev20f_debounce--;
                }
            } else if ((g_dev_present_mask >> 3) & 1) {
                g_dev_present_mask &= 0xf7;
                I21 = 2; I22 = 0xff; txq_enqueue_i21();
                g_dev20f_debounce = 0;
            }
        }

        r = g_gauge_hold;
        if (g_gauge_hold == 0) {
            /* --- channel via FUN_28f1 -> device 0x209 / flag bit0 --- */
            if (util_gauge_x()) {
                if (I23 < 0xfe) {
                    if (g_dev209_debounce == 0) {
                        I21 = 4; I22 = I23; txq_enqueue_i21();
                        g_dev209_debounce = 4; g_dev_present_mask |= 1;
                        if (g_disp_mode == 0) {
                            if (I23 < 0x31) {
                                g_alert_lo_a = 1;
                            }
                        }
                    } else {
                        g_dev209_debounce--;
                    }
                } else if (g_dev_present_mask & 1) {
                    g_dev_present_mask &= 0xfe;
                    I21 = 4; I22 = 0xff; txq_enqueue_i21();
                    g_dev209_debounce = 0; g_alert_lo_a = 0;
                }
            }

            /* --- channel via FUN_298d -> device 0x20a / flag bit1 --- */
            if (util_gauge_y()) {
                if (I23 < 0xfe) {
                    if (g_dev20a_debounce == 0) {
                        I21 = 5; I22 = I23; txq_enqueue_i21();
                        g_dev20a_debounce = 4; g_dev_present_mask |= 2;
                    } else {
                        g_dev20a_debounce--;
                    }
                    if (g_disp_mode == 0 && I23 < 0x31) {
                        g_alert_lo_b = 1;
                        return 1;
                    }
                } else if ((g_dev_present_mask >> 1) & 1) {
                    g_dev_present_mask &= 0xfd;
                    I21 = 5; I22 = 0xff; txq_enqueue_i21();
                    g_dev20a_debounce = 0; g_alert_lo_b = 0;
                }
            }
        }
    }
    return r;
}


/* ============================================================================
 * disp_frame_router @0xe39 - inbound-frame dispatcher (header at frame offset 3).
 * Saves the 3-byte frame pointer to I25:I24:I23 then routes on the sub-header.
 * ==========================================================================*/
u8 disp_frame_router(u8 a0, u8 a1, u8 a2)
{
    u8 h;
    u8 t;
    u8 lo, hi;

    I23 = a2;   /* param_3 */
    I24 = a1;   /* param_2 */
    I25 = a0;   /* param_1 */

    h = frame_get_cmd(3);
    switch (h) {
    case 0x04:
        txq_enqueue(0, 0, 4, 3);
        return 0;
    case 0x05:
        txq_enqueue(0, 0, 5, 3);
        return 0;
    case 0x09:
        /* orig @0x0f00: LJMP FUN_2f2c (tail call) with far frame pointer
         * {R1:R2:R3} = {I25+5, I24+(I25>0xfa?1:0), I23}.  The frozen prototype
         * is u8(void) and leaf FUN_01ab/01c4 read a fixed near base
         * (X_RXBUF_BASE), so the +5 far-pointer cannot be threaded through the
         * near model and is dropped (fidelity gap - see REPORT). */
        disp_cfgtbl_store();
        return 0;
    case 0x15:
        if (frame_get_cmd(4) == 1) {
            sys_sfr8e_field(1);
            isp_system_reset();
        }
        return 0;
    case 0x50:                            /* 'P' - 0x50 command block */
        disp_cmd50(I25, I24, I23);
        return 0;
    case 0x02:
        t = frame_get_cmd(4);
        if (t == 0xfe) {
            if (frame_get_cmd(5) == 0) {
                pwr_shutdown_hold_set(0x46);
            } else if (frame_get_cmd(5) == 1) {
                P0_5 = 1;
                util_delay16_wdt4(4, 0x65);
                P0_5 = 0;
                for (;;) {                /* orig: do-nothing infinite halt */
                }
            }
        }
        if (frame_get_cmd(4) == 2) {
            txq_enqueue(0, 0, 4, 3);
            txq_enqueue(0, 0, 3, 3);
        }
        lo = I25;
        hi = I24;
        if (frame_get_cmd(4) == 4) {
            /* SIGCHANGE @0x2376: faithful sig is void(u8,u8,u8) = the R1:R2:R3
             * frame-pointer triple; here {I25+5 : I24+carry : I23}. */
            frame_pack_be32((u8)(lo + 5), (u8)(hi + ((I25 > 0xfa) ? 1 : 0)), I23);
        }
        X_FLAGS |= 8;
        g_frame02_seen = 1;
        return 0;
    default:
        return 0;
    }
}


/* ============================================================================
 * util_pattern_decode @0x14a1 - 16-bit sample windowing / bit-accumulation FSM.
 * SIGCHANGE @0x14a1: void -> (u8 param_1, u8 param_2).  The pair is a 16-bit
 * value V = (param_1<<8)|param_2 (param_1=high).  Gated by X_ACC_VALID/MODE.
 * All comparisons are byte-exact 16-bit forms from the SUBB chains.
 * ==========================================================================*/
u8 util_pattern_decode(u8 cap_hi, u8 cap_lo)
{
    u16 v;
    u8  g;

    I5A = cap_hi;
    I5B = cap_lo;

    g = X_ACC_VALID;
    if (X_ACC_VALID == 0) {
        g = (u8)(X_ACC_MODE ^ 1);
    }
    if (g != 0) {
        return g;
    }

    v = ((u16)cap_hi << 8) | cap_lo;

    switch (g_decode_state) {
    case 1:
        if (v >= 0x0104u && v < 0x0119u) {
            g_pattern_bitcnt = 0;
            g_decode_state = 2;
            return 2;
        }
        if (v < 0x00d2u) {
            return cap_hi;               /* no state change */
        }
        if (v >= 0x00f1u) {
            return cap_hi;
        }
        /* 0x00d2 <= v < 0x00f1 */
        if (g_anim_hold == 0) {
            util_pattern_latch();
        }
        g_decode_state = 0;
        g_decode_timeout = 0;
        return 0;

    case 2:
        if (v < 0x0012u || v >= 0x0031u) {
            g_decode_state = 0;
            g_decode_sr_lo = 0;
            g_decode_sr_hi = 0;
            g_decode_timeout = 0;
            return 0;
        }
        if (v < 0x0013u || v >= 0x001bu) {
            if (v >= 0x0029u && v < 0x0030u) {
                /* shift {0212:0213} right, set top bit */
                g_decode_sr_hi = (u8)((g_decode_sr_hi >> 1) | (g_decode_sr_lo << 7));
                g_decode_sr_lo = (u8)((g_decode_sr_lo >> 1) | 0x80);
                g_pattern_bitcnt++;
            }
            /* else: no shift */
        } else {
            /* 0x0013 <= v < 0x001b: shift {0212:0213} right, zero fill */
            g_decode_sr_hi = (u8)((g_decode_sr_hi >> 1) | (g_decode_sr_lo << 7));
            g_decode_sr_lo = (u8)(g_decode_sr_lo >> 1);
            g_pattern_bitcnt++;
        }

        if (g_pattern_bitcnt == 0x10) {
            g_decode_val_lo = g_decode_sr_lo;
            g_decode_val_hi = g_decode_sr_hi;
            return g_decode_sr_hi;
        }
        if (g_pattern_bitcnt != 0x20) {
            return g_pattern_bitcnt;
        }
        util_pattern_latch();
        g_anim_hold = 0x1e;
        g_pattern_bitcnt = 0;
        g_decode_state = 0;
        g_decode_timeout = 0;
        return 0;

    case 0:
        g_pattern_bitcnt = 0;
        g_decode_state = 1;
        g_decode_timeout = 0x32;
        return 0x32;

    default:
        return g_decode_state;
    }
}


/* ============================================================================
 * util_pattern_latch @0x15c9 - if {0212:0213} sums to 0xFF (complete pattern),
 * latch it to {0219:021a}/{0211} and stage an ack (I5C=10,I5D=1) via FUN_0aa9.
 * ==========================================================================*/
u8 util_pattern_latch(void)
{
    if ((u16)((u16)g_decode_sr_lo + (u16)g_decode_sr_hi) == 0xffu) {   /* sum == 0xFF, no carry */
        g_decode_out_hi = g_decode_val_hi;
        g_decode_out_lo = g_decode_sr_hi;
        I5C = 10;
        I5D = 1;
        txq_enqueue_i5c();                 /* consumes I5C/I5D (a.k.a. 0aa9(10,1)) */
    }
    return 0;
}


/* ============================================================================
 * bl_pwm_mux_clear @0x15fa - clear a 2-bit PWM-mux field in SFR_CE per channel.
 * ==========================================================================*/
u8 bl_pwm_mux_clear(u8 channel)
{
    if (channel == 0) {
        SFR_CE &= 0xfc;
    }
    if (channel == 1) {
        SFR_CE &= 0xf3;
    }
    if (channel == 2) {
        SFR_CE &= 0xcf;
    }
    if (channel == 3) {
        SFR_CE &= 0x3f;
    }
    return 0;
}


/* ======================================================================
 *  @0x1c39 - sample two PCA sleep channels (modes 2 and 3), scale each into
 *  X_0216/X_0217, then derive the clamped thresholds X_0207/X_0208 and reset
 *  the seed counters.
 *  FUN_1bc8 is widened to the full 12-bit sample V (R6:R7); the stored byte is
 *  the faithful (R6<<4)|(R7>>4) = (u8)(V>>4).
 * ==================================================================== */
u8 util_gauge_sample_thr(void)
{
    u8 v;

    g_gaugeX_raw = (u8)(pwr_sleep_sample(2) >> 4);
    g_gaugeY_raw = (u8)(pwr_sleep_sample(3) >> 4);

    v = g_gaugeX_raw;
    g_gaugeX_target = (v < 2) ? v : (u8)(v - 2);
    v = g_gaugeY_raw;
    g_gaugeY_target = (v < 2) ? v : (u8)(v - 2);

    g_gaugeX_slew = 100;
    g_gaugeY_slew = 100;
    g_dev209_debounce = 0;
    g_dev20a_debounce = 0;
    return 0;
}


/* ======================================================================
 *  @0x1c9f - notify-pending debounce/scheduler.
 *  When no notify is pending, park the thresholds; otherwise count the
 *  pending window down and, on expiry, refresh the samples (FUN_1c39).
 *  Faithful.
 * ==================================================================== */
u8 util_gauge_scheduler(void)
{
    if (x_notify_pending == 0) {
        g_gauge_hold = x_notify_pending;   /* = 0 */
        g_gaugeX_target = 0xFE;
        g_gaugeY_target = 0xFE;
        return 0;
    }
    if (g_gauge_hold == 0) {
        util_needle_chase();
        return 0;
    }
    g_gauge_hold--;
    if (g_gauge_hold == 0)
        util_gauge_sample_thr();
    return 0;
}


/* ============================================================================
 * disp_periph_mode_apply @0x175f: load the current display mode g_disp_mode
 * (XDATA 0x020b) into R7 and fall through into FUN_1764 (sys_comms_cfg), which
 * (re)configures the four ext-peripheral blocks (0xF016/F017/F031/F032) and
 * their P1/P3 strobe lines for that mode.  Reached from the rail sequencer via
 * FUN_0095 and FUN_0e04.  Byte-exact: 0x175f is just the load + fall-through.
 * ========================================================================== */
void disp_periph_mode_apply(void)
{
    sys_comms_cfg(g_disp_mode);         /* R7 = MOVX 0x020b; fall into 0x1764  */
}


/* =====================================================================
 * util_scale_adc_up @0x2453: scale param_1 into IRAM 0x29..0x2c (32-bit BE).
 *   [0x29:0x2a:0x2b:0x2c] = param_1 * 0x0FFF / 0xE7
 * FUN_02a6 multiplies (0,0,0,param_1) by (0,0,0x0f,0xff) = 0x0FFF into
 * R4:R5:R6:R7; FUN_0331 divides that product by (0,0,0,0xe7) = 0xE7; the
 * quotient (R4:R5:R6:R7) is stored back big-endian to 0x29..0x2c.
 * SIGCHANGE @0x2453: stub `u8(void)` -> `void(u8)`; arg R7.
 * PARTIAL @0x2453: the u8-return FUN_02a6/FUN_0331 model cannot thread the
 * 32-bit product/quotient between the calls, so the equivalent arithmetic is
 * computed for a byte-faithful store-back; the calls mirror the LCALL sequence.
 * ===================================================================== */
void util_scale_adc_up(u8 adc_raw)
{
    /* result = adc_raw * 0x0FFF / 0xE7, exact-decomposed to stay 16-bit on this
     * 8-bit core (avoids the 32-bit LMUL/ULDIV runtime helpers):
     *   adc_raw*4095/231 == adc_raw*17 + (adc_raw*8)/11   (proven for all 0..255)
     * The quotient is <= 4520, so the two high bytes of the BE store are 0. */
    u16 quot = (u16)(adc_raw * 17u) + (u16)(adc_raw * 8u) / 11u;

    IRAM(0x29) = 0;                  /* R4 (MSB, always 0) */
    IRAM(0x2a) = 0;                  /* R5 (always 0)      */
    IRAM(0x2b) = (u8)(quot >> 8);    /* R6 */
    IRAM(0x2c) = (u8)quot;           /* R7 (LSB) */
}


/* =====================================================================
 * @0x247f - if param_2 in [0x46,0x64): stage it via FUN_2453 and record the
 * pair {param_1,param_2} into g_volt_thr1_hi/g_volt_thr1_lo plus a +0xB1 shifted copy into
 * g_volt_thr2_hi/g_volt_thr2_lo.  Returns param_2+0xB1 (else param_2 unchanged).
 * SIGCHANGE @0x247f: stub `u8(u8)` -> `u8(u8,u8)`; reads R6 (param_1) & R7
 * (param_2).  Caller FUN_1613 currently passes only the R7 value.
 * ===================================================================== */
u8 util_gauge_set_thr(u8 param_1, u8 param_2)
{
    u8 ret = param_2;
    if ((param_2 >= 0x46) && (param_2 < 0x65)) {
        u8 sum = (u8)(param_2 + 0xB1);
        u8 carry = (param_2 + 0xB1 > 0xFF) ? 1 : 0;
        util_scale_adc_up(param_2);
        g_volt_thr1_hi = param_1;                       /* 0x024e */
        g_volt_thr1_lo = param_2;                       /* 0x024f */
        g_volt_thr2_hi = (u8)(param_1 + carry);         /* 0x0260 */
        g_volt_thr2_lo = sum;                           /* 0x0261 */
        ret = sum;
    }
    return ret;
}


/* =====================================================================
 * util_scale_adc_down @0x24a2: scale the 16-bit value (param_1<<8)|param_2 into
 * IRAM 0x4e..0x51 (32-bit BE).
 *   [0x4e:0x4f:0x50:0x51] = ((param_1<<8)|param_2) * 0xE7 / 0x0FFF
 * FUN_02a6 multiplies (0,0,param_1,param_2) by (0,0,0,0xe7) = 0xE7 into
 * R4:R5:R6:R7; FUN_0331 divides that product by (0,0,0x0f,0xff) = 0x0FFF; the
 * quotient (R4:R5:R6:R7) is stored back big-endian to 0x4e..0x51.
 * SIGCHANGE @0x24a2: stub `u8(void)` -> `void(u8,u8)`; args R6,R7.
 * PARTIAL @0x24a2: as FUN_2453 - the u8-return FUN_02a6/FUN_0331 model cannot
 * thread the 32-bit intermediate, so the equivalent arithmetic is computed for
 * a byte-faithful store-back; the calls mirror the LCALL sequence.
 * ===================================================================== */
u8 util_scale_adc_down(u8 hi, u8 lo)
{
    /* result = ((hi<<8)|lo) * 0xE7 / 0x0FFF, exact-decomposed to stay 16-bit on
     * this 8-bit core (avoids 32-bit LMUL/ULDIV):
     *   val*231/4095 == hi*14 + (hi*86 + lo*11)/195   (proven for all 0..65535)
     * The quotient is <= 3696, so the two high bytes of the BE store are 0. */
    u16 quot = (u16)(hi * 14u) + (u16)(hi * 86u + lo * 11u) / 195u;

    IRAM(0x4e) = 0;                  /* R4 (MSB, always 0) */
    IRAM(0x4f) = 0;                  /* R5 (always 0)      */
    IRAM(0x50) = (u8)(quot >> 8);    /* R6 */
    IRAM(0x51) = (u8)quot;           /* R7 (LSB) */
    return (u8)quot;                 /* R7 low byte left in A at RET (used by 0x1b04) */
}


/* =====================================================================
 * @0x2516 - g_gauge_hold = 5.
 * ===================================================================== */
u8 util_gauge_hold_set(void)
{
    g_gauge_hold = 5;
    return 0;
}


/* =====================================================================
 * @0x2524 - ACC-present detect + debounce.  Samples P1.5, ages the debounce
 * counter, and on elapse latches X_FLAGS bit2 (FLAG_PRESENT) and notifies via
 * FUN_0aad; then drives FUN_0070 with 3 (present) or 0x32 (absent).
 * ===================================================================== */
u8 util_present_detect(void)
{
    u8 lvl = (u8)(P1_5 & 1);
    u8 act;

    sys_acc_debounce_dec();                 /* age debounce   */
    if (sys_acc_debounce_zero()) {           /* debounce elapsed */
        if (lvl != 0) {
            bl_strobe_pulse();
            X_FLAGS |= FLAG_PRESENT;     /* bit2 */
        } else {
            X_FLAGS &= (u8)~FLAG_PRESENT;
        }
        txq_enqueue(0, (u8)(lvl != 0), 1, 6);
    }

    if ((lvl == 0) || (((X_FLAGS >> 2) & 1) == 0)) {
        if (lvl != 0) return 0;
        if (((X_FLAGS >> 2) & 1) == 1) return 0;
        act = 0x32;
    } else {
        act = 3;
    }
    IRAM(0x07) = act;                         /* bank0 R7 = act (FUN_0070 arg) */
    sys_store_acc_debounce();                 /* orig: FUN_0070(act) */
    return 0;
}


/* ==========================================================================
 * @0x2863 - two-axis needle "chase": move g_gaugeX_target/g_gaugeY_target toward the target
 * (g_gaugeX_raw-2 / g_gaugeY_raw-2), snapping immediately when below target and slewing
 * with a 0x96-count hold when above.  |diff| via abs (LCALL 0x0055).
 * ======================================================================== */
u8 util_needle_chase(void)
{
    u8 tx = (u8)(g_gaugeX_raw - 2);
    u8 ty = (u8)(g_gaugeY_raw - 2);

    /* --- X axis --- */
    if (g_gaugeX_target == tx) {
        g_gaugeX_slew = 0;
    } else if (g_gaugeX_target < tx) {
        g_gaugeX_target = tx;
    } else {
        g_gaugeX_slew++;
        if (g_gaugeX_slew > 0x96)
            g_gaugeX_target = tx;
    }

    /* --- Y axis --- */
    if (g_gaugeY_target == ty) {
        g_gaugeY_slew = 0;
        return 0;
    } else if (g_gaugeY_target < ty) {
        g_gaugeY_target = ty;
        return ty;
    } else {
        g_gaugeY_slew++;
        if (g_gaugeY_slew > 0x96) {
            g_gaugeY_target = ty;
            return ty;
        }
        return (u8)(g_gaugeY_slew + 0x69);
    }
}


/* --- shared debounce/clamp body for the four single-axis gauge filters ----
 * (28f1/298d/2a29/2ab3).  `limit` is the upper bound the raw sample is tested
 * against (a companion axis value, or the fixed 0xfa); `cur`/`last`/`cnt` are
 * the XDATA state cells.  Returns the accepted position byte written to the
 * frame at offset 0x23.  Sample low byte is unavailable (see header note). */
static u8 gauge_filter(u8 s, u8 limit,
                       volatile u8 xdata *last,
                       volatile u8 xdata *cnt)
{
    u8 out;
    int d;

    if (s < limit) {
        d = (int)s - (int)*last;
        if (d < 0) d = -d;
        if (d < 3) {
            (*cnt)++;
            if (*cnt < 4)
                return (u8)(*cnt - 4);   /* not yet confirmed */
            *cnt  = 0;
            *last = s;
            out   = s;
        } else {
            *cnt  = 0;
            *last = s;
            out   = 0xff;
        }
    } else {
        *cnt  = 0;
        *last = 0;
        out   = 0xff;
    }
    frame_put(out, 0x23);
    return out;
}


/* @0x28f1 - gauge filter, X axis (sample mode 2, bound g_gaugeX_target). */
u8 util_gauge_x(void)
{
    u8 s;
    if (!(X_FLAGS & FLAG_PRESENT))
        return X_FLAGS;
    if (g_gauge_hold != 0)
        return g_gauge_hold;
    s = (u8)(pwr_sleep_sample(2) >> 4);     /* faithful 8-bit sample = (R6<<4)|(R7>>4) */
    g_gaugeX_raw = s;
    return gauge_filter(s, g_gaugeX_target, &g_gaugeX_last, &g_gaugeX_cnt);
}

/* @0x298d - gauge filter, Y axis (sample mode 3, bound g_gaugeY_target). */
u8 util_gauge_y(void)
{
    u8 s;
    if (!(X_FLAGS & FLAG_PRESENT))
        return X_FLAGS;
    if (g_gauge_hold != 0)
        return g_gauge_hold;
    s = (u8)(pwr_sleep_sample(3) >> 4);     /* faithful 8-bit sample = (R6<<4)|(R7>>4) */
    g_gaugeY_raw = s;
    return gauge_filter(s, g_gaugeY_target, &g_gaugeY_last, &g_gaugeY_cnt);
}

/* @0x2a29 - gauge filter (sample mode 5, fixed bound 0xfa).  No g_gauge_hold gate. */
u8 util_gauge_c(void)
{
    u8 s;
    if (!(X_FLAGS & FLAG_PRESENT))
        return X_FLAGS;
    s = (u8)(pwr_sleep_sample(5) >> 4);     /* faithful 8-bit sample = (R6<<4)|(R7>>4) */
    return gauge_filter(s, 0xfa, &g_gaugeC_last, &g_gaugeC_cnt);
}

/* @0x2ab3 - gauge filter (sample mode 6, fixed bound 0xfa).  No g_gauge_hold gate. */
u8 util_gauge_d(void)
{
    u8 s;
    if (!(X_FLAGS & FLAG_PRESENT))
        return X_FLAGS;
    s = (u8)(pwr_sleep_sample(6) >> 4);     /* faithful 8-bit sample = (R6<<4)|(R7>>4) */
    return gauge_filter(s, 0xfa, &g_gaugeD_last, &g_gaugeD_cnt);
}


/* --- band classifier shared by 2b93/2d51 (contiguous 16-bit thresholds) --- */
static u8 gauge_zone(u16 v)
{
    if (v < 0x0739) return 1;
    if (v < 0x0929) return 2;
    if (v < 0x0bc7) return 3;
    if (v < 0x0c90) return 1;
    if (v < 0x0dc0) return 2;
    if (v < 0x0f00) return 3;
    return 4;
}


/* ==========================================================================
 * @0x2b93 - pressure/position band tracker (A).  Acquires a 16-bit sample,
 * applies a 0x31 dead-band vs the last accepted value, classifies it into a
 * band, drives a two-sided up/down counter (g_bandA_up / g_bandA_dn), then
 * enqueues the resulting delta.
 * ======================================================================== */
u8 util_band_track_a(void)
{
    u8  st, zone, r7;
    u16 cur, last;

    st = arb_get_action();
    if ((st ^ 0x0f) != 0)
        return (u8)(st ^ 0x0f);

    cur = pwr_sleep_sample(0);              /* full 12-bit sample */

    if (g_bandA_debounce != 0)
        g_bandA_debounce--;

    last = (u16)((u16)g_bandA_last_hi << 8) | g_bandA_last_lo;
    if (cur != last) {
        u16 diff = (cur >= last) ? (u16)(cur - last) : (u16)(last - cur);
        g_bandA_last_hi = (u8)(cur >> 8);
        g_bandA_last_lo = (u8)cur;
        if (diff > 0x30)                    /* big jump -> track and bail */
            return (u8)cur;
    }

    zone = gauge_zone(cur);
    switch (zone) {
    case 1:
        g_bandA_zone = 0;
        break;
    case 2:
        if (g_bandA_zone == 3)      g_bandA_up++;
        else if (g_bandA_zone == 0) g_bandA_dn++;
        g_bandA_zone = 1;
        break;
    case 3:
        if (g_bandA_zone == 0) {
            if (g_band_inhibit_lo == 0 && g_band_inhibit_hi == 0) g_bandA_up++;
        } else if (g_bandA_zone == 3) {
            if (g_band_inhibit_lo == 0 && g_band_inhibit_hi == 0) g_bandA_dn++;
        }
        g_bandA_zone = 2;
        break;
    default: /* 4 */
        g_bandA_zone = 3;
        break;
    }

    if (g_bandA_up > g_bandA_dn) {
        r7 = g_bandA_dn;
        g_bandA_up = (u8)(g_bandA_up - g_bandA_dn - 1);
        g_bandA_dn = 0;
        if (g_bandA_debounce == 0) g_bandA_out = 1;
    } else if (g_bandA_dn > g_bandA_up) {
        r7 = g_bandA_up;
        g_bandA_dn = (u8)(g_bandA_dn - g_bandA_up - 1);
        g_bandA_up = 0;
        if (g_bandA_debounce == 0) g_bandA_out = 2;
    } else {
        return (u8)(g_bandA_dn - (g_bandA_up + 1));   /* balanced -> no enqueue */
    }

    return txq_enqueue_delta((u8 xdata *)0x1f8, r7);
}

/* ==========================================================================
 * @0x2d51 - pressure/position band tracker (B): identical machine to (A) on
 * the parallel g_bandB_* state cells.  Sample mode 4; output codes 3/4 (vs
 * 1/2 in A); the up/down decision is mirrored relative to A.
 * ======================================================================== */
u8 util_band_track_b(void)
{
    u8  st, zone, r7;
    u16 cur, last;

    st = arb_get_action();
    if ((st ^ 0x0f) != 0)
        return (u8)(st ^ 0x0f);

    cur = pwr_sleep_sample(4);

    if (g_bandB_debounce != 0)
        g_bandB_debounce--;

    last = (u16)((u16)g_bandB_last_hi << 8) | g_bandB_last_lo;
    if (cur != last) {
        u16 diff = (cur >= last) ? (u16)(cur - last) : (u16)(last - cur);
        g_bandB_last_hi = (u8)(cur >> 8);
        g_bandB_last_lo = (u8)cur;
        if (diff > 0x30)
            return (u8)cur;
    }

    zone = gauge_zone(cur);
    switch (zone) {
    case 1:
        g_bandB_zone = 0;
        break;
    case 2:
        if (g_bandB_zone == 3)      g_bandB_dn++;
        else if (g_bandB_zone == 0) g_bandB_up++;
        g_bandB_zone = 1;
        break;
    case 3:
        if (g_bandB_zone == 0) {
            if (g_band_inhibit_lo == 0 && g_band_inhibit_hi == 0) g_bandB_dn++;
        } else if (g_bandB_zone == 3) {
            if (g_band_inhibit_lo == 0 && g_band_inhibit_hi == 0) g_bandB_up++;
        }
        g_bandB_zone = 2;
        break;
    default: /* 4 */
        g_bandB_zone = 3;
        break;
    }

    if (g_bandB_dn > g_bandB_up) {
        r7 = g_bandB_up;
        g_bandB_dn = (u8)(g_bandB_dn - g_bandB_up - 1);
        g_bandB_up = 0;
        if (g_bandB_debounce == 0) g_bandB_out = 3;
    } else if (g_bandB_up > g_bandB_dn) {
        r7 = g_bandB_dn;
        g_bandB_up = (u8)(g_bandB_up - g_bandB_dn - 1);
        g_bandB_dn = 0;
        if (g_bandB_debounce == 0) g_bandB_out = 4;
    } else {
        return (u8)(g_bandB_up - (g_bandB_dn + 1));
    }

    return txq_enqueue_delta((u8 xdata *)0x1fd, r7);
}


/* ==========================================================================
 * @0x2ee7 - reset the animation/timer block; if ACC state == 1, kick 0x133a.
 * ======================================================================== */
u8 util_decode_reset(void)
{
    u8 b;
    g_decode_timeout = 0;
    g_anim_hold = 0;
    g_decode_val_lo = 0;
    g_decode_val_hi = 0;
    g_decode_sr_lo = 0;
    g_decode_sr_hi = 0;
    g_decode_state = 0;

    b = X_ACC_VALID;
    if (b == 0)
        b = (u8)(X_ACC_MODE ^ 1);
    if (b == 0)
        sys_timer2_setup();
    return 0;
}


/* ==========================================================================
 * @0x2f0e - per-tick decrements for the animation block; when the g_decode_timeout
 * timer expires, clear the g_decode_sr_lo/g_decode_sr_hi/g_decode_state latches.
 * ======================================================================== */
u8 util_decode_tick(void)
{
    if (g_anim_hold != 0)
        g_anim_hold--;
    if (g_decode_timeout != 0) {
        g_decode_timeout--;
        if (g_decode_timeout == 0) {
            g_decode_sr_lo = 0;
            g_decode_sr_hi = 0;
            g_decode_state = 0;
        }
    }
    return 0;
}


/* ==========================================================================
 * @0x2f2c - copy a 4-byte record from the payload frame (offsets 0..3) into
 * the ring table at XDATA 0x0222 (stride 4, 10 slots indexed by g_cfgtbl_wr),
 * advancing the slot index mod 10.
 * NOTE: XDATA 0x0222..0x0249 is a computed-pointer table, not in cm68_xram.h.
 * ======================================================================== */
u8 disp_cfgtbl_store(void)
{
    u8  idx = g_cfgtbl_wr;                    /* slot index */
    u16 base = (u16)(0x0222 + (u16)idx * 4);
    s8  ret  = (s8)(idx - 9);

    XD(base + 0) = frame_get_off(0);
    XD(base + 1) = frame_get_cmd(1);
    XD(base + 2) = frame_get_cmd(2);
    XD(base + 3) = frame_get_cmd(3);

    g_cfgtbl_wr = (u8)(idx + 1);
    if (g_cfgtbl_wr > 9) {
        ret = 0;
        g_cfgtbl_wr = 0;
    }
    return (u8)ret;
}


/* @0x702 - drive P0.0 / P2.5 pulse used by the scan-out routine. */
void bl_scan_pulse(void)
{
    bl_scan_strobe(0);
    util_delay16_spin(0, 0x32);
    P0_0 = 1;
    {
        u8 b = (u8)X_ACC_VALID;             /* DAT_EXTMEM_025e */
        if (b == 0) b = (u8)(X_ACC_MODE ^ 2);
        if (b == 0) P2_5 = 1;
    }
}


/* @0x70e - set/clear the P0.0 (+ mirrored P2.5 when in state 2) strobe. */
void bl_scan_strobe(u8 on)
{
    P0_0 = on ? 1 : 0;
    {
        u8 b = (u8)X_ACC_VALID;
        if (b == 0) b = (u8)(X_ACC_MODE ^ 2);
        if (b == 0) P2_5 = on ? 1 : 0;
    }
}


/* ======================================================================
 *  disp_cmd50 @0x1613 - 0x50 control sub-dispatch.
 *
 *  Sub-command byte at frame offset 4; args at 5/6/7.
 *    0x06  set X020b, (re)enable, X01ed=1
 *    0x03  brightness/param FUN_247f(arg)
 *    0x07  KEEP-ALIVE flag: X_FLAGS bit4 = (arg==1)
 *    0x09  POWEROFF-TIMEOUT: seed X_POFF_TMO=byte6, X_POFF_CNT=0
 *    0x0A  ACC/POWER STATE: X_ACC_MODE = arg (0..3); on change run wake seq;
 *          clear X_SLEEP_ARMED; FUN_241c (arm); FUN_2ee7; FUN_07a5
 *    0x0B  misc paired config
 * ==================================================================== */
u8 disp_cmd50(u8 unused_i25, u8 unused_i24, u8 unused_i23)
{
    u8 sub, arg, b;
    /* Caller (port01.c) forwards I25/I24/I23 but the reachable original code
     * never reads R6/R5/R4 here (confirmed against re/cm68_disasm.txt) -
     * genuinely unused on both dialects; kept only to preserve the call-site
     * signature (see cm68.h). */
    (void)unused_i25; (void)unused_i24; (void)unused_i23;

    sub = frame_get_cmd(4);

    if (sub == 6) {
        arg = frame_get_cmd(5);
        if (arg < 3) {
            XB(0x020B) = arg;
            sys_comms_cfg(0);
            util_gauge_hold_set();
            XB(0x01ED) = 1;
            return 1;
        }
        return (u8)(arg - 3);
    }

    if (sub == 3) {
        /* @0x247f faithful sig is u8(param_1=R6, param_2=R7).  Operand recovery
         * from re/cm68_disasm.txt: the 0x50/0x03 tail-path @0x1650-0x1657 loads
         * only R7 (= FUN_01c4(5)) and LJMPs to 0x247f; R6 is NEVER written on
         * the whole reachable path (caller @0x19ff sets R3/R2/R1 only; router
         * FUN_0e39 @0x0e39->0x0f03->0x1613 never touches R6 - the sole R6 write
         * @0x0e94 is on the unrelated 0x65 error-halt branch).  So R6 is a
         * genuinely INDETERMINATE leftover register in the original firmware; it
         * is unrepresentable in the near-C model with no defined source value.
         * FUN_247f consumes R6 only inside its guard 0x46 <= R7 < 0x64 (stores
         * it to X[0x24e] and as the high byte of R6:R7+0x00b1 at X[0x260]); on
         * that window the original itself propagates undefined data.  Passing 0
         * is therefore as faithful as any concrete value, and is byte-exact
         * whenever R7 falls outside [0x46,0x64) (the common case). */
        return (u8)util_gauge_set_thr(0, frame_get_cmd(5));
    }

    if (sub == 7) {                          /* keep-alive */
        if (frame_get_cmd(5) != 1) {
            X_FLAGS &= (u8)~FLAG_KEEPALIVE;
        } else {
            X_FLAGS |= FLAG_KEEPALIVE;
        }
        return X_FLAGS;
    }

    if (sub == 9) {                          /* poweroff-timeout */
        X_POFF_TMO = frame_get_cmd(6);
        X_POFF_CNT = 0;
        math_add16_xdata(0, 0x255, frame_get_cmd(5));
        return 0;
    }

    if (sub == 0x0A) {                       /* ACC / power state */
        arg = frame_get_cmd(5);
        if (arg < 4) {
            X_ACC_VALID = 0;
            X_ACC_MODE  = arg;
        }
        b = (u8)(X_ACC_PREV_M ^ X_ACC_MODE);
        if (b == 0) b = (u8)(X_ACC_PREV_V ^ X_ACC_VALID);
        if (b != 0) {
            if (X_ACC_PREV_M == 1 && X_ACC_PREV_V == 0) {   /* was ACC ON */
                sys_hpcon_clrbit(0);
                bl_pwm_mux_clear(0);
                sys_hpcon_hp_off();
                sys_et2_disable();
                sys_t2con_clr_lowbits();
            }
            X_ACC_PREV_V = X_ACC_VALID;
            X_ACC_PREV_M = X_ACC_MODE;
        }
        X_SLEEP_ARMED = 0;
        pwr_sleep_arm();       /* arm sleep (fix chokepoint) */
        util_decode_reset();
        bl_scan_restart();
        return 0;
    }

    if (sub == 0x0B) {
        u8 a5 = frame_get_cmd(5);
        if (a5 < 2) { XB(0x025B) = 0; XB(0x025C) = a5; }
        {
            u8 a6 = frame_get_cmd(6);
            if (a6 < 2) { XB(0x0257) = 0; XB(0x0258) = a6; }
        }
        b = frame_get_cmd(7);
        XB(0x0264) = 0;
        XB(0x0265) = b;
        return b;
    }

    return 0;
}

