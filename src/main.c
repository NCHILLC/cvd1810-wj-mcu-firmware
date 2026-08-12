/* main.c - reset entry / crt0 hand-off, main loop, and the work-scheduler /
 * slow-tick / TX-pump top level.  Moved verbatim (pure code motion, no logic
 * changes) from startup.c and dispatch.c during the Phase-C file
 * reorganization.
 */
#include "cm68.h"
#include "cm68_stubs.h"

/* src/initimg.c - faithful DAT_CODE_04fb (FUN_0461) data-init decompressor. */
void initimg_apply(void);

/* ---- Interrupt vector reconciliation --------------------------------------
 * SDCC emits the hardware vector redirect (at CODE 3+8n) for every __interrupt
 * function whose prototype is visible in the module that defines main().  The
 * ISR bodies live in other TUs (uart.c, hwconfig.c), so no local prototypes
 * are required here (Keil forbids an `interrupt N` prototype). */

/* ==========================================================================
 * @0x284a - per-tick timer decrements: TX pump timer (0x01f1) and RX inter-
 * byte timeout (X_RX_TMO); when the RX timeout expires, reset X_RX_STATE.
 * ======================================================================== */
u8 sys_tick_decrements(void)
{
    if (x_tx_f1 != 0)
        x_tx_f1--;
    if (X_RX_TMO != 0) {
        X_RX_TMO--;
        if (X_RX_TMO == 0)
            X_RX_STATE = 0;
    }
    return 0;
}


/* @0xf24 - fast/slow work dispatcher, driven by the X_WORK bitfield that the
 * Timer0 tick and other producers raise.  Faithful structure. */
void disp_work_sched(void)
{
    u8 w;

    w = X_WORK;
    if (w & 1) {
        X_WORK &= 0xFE;
        if (++XB(0x026D) > 4) { XB(0x026D) = 0; X_WORK |= 2; }
        pwr_volt_arbiter();
        util_band_track_a();
        util_band_track_b();
    }
    w = X_WORK;
    if ((w >> 1) & 1) {
        X_WORK &= 0xFD;
        if (++XB(0x026E) > 9) { XB(0x026E) = 0; X_WORK |= 4; }
        util_scan_inputs();
        uart_txwin_dec();
        sys_tick_decrements();
        util_present_detect();
        util_decode_tick();
    }
    w = X_WORK;
    if ((w >> 2) & 1) {
        X_WORK &= 0xFB;
        if (++XB(0x026F) > 9) { XB(0x026F) = 0; X_WORK |= 8; }
        txq_retry_tick();
        disp_slowtick();
        util_gauge_scheduler();
        pwr_shutdown_tick();
        bl_scan_refresh();
        pwr_sleep_emit();       /* <-- sleep-emit (fix chokepoint) */
        frame_build_status();
    }
    w = X_WORK;
    if ((w >> 3) & 1) {
        X_WORK &= 0xF7;
        txq_notify_link();
    }
}


/* @0xfc3 - slow-tick housekeeping (LED/backlight fade + rail bit). Faithful. */
void disp_slowtick(void)
{
    s8 c;
    arb_acc();           /* ACC arbiter */
    arb_dispatch();           /* dispatch arbiter action code */
    c = (s8)XB(0x0259);
    if (c == 0) c = (s8)XB(0x025A);
    if (c != 0) math_add16_xdata(0, 0x259, 0xFF);  /* 16-bit counter@0x259 += 0xff */
    if ((XB(0x025D) != 0) && (--XB(0x025D) == 0)) {
        if ((s8)X_FLAGS < 0) {            /* bit7 set */
            P1_3 = 1;
            return;
        }
        P1_3 = 0;
        P0_5 = 1;
    }
}


/* @0x18ba - outgoing message pump. Faithful. */
void uart_tx_pump(void)
{
    if (XB(0x01F1) == 0) {
        if (XB(0x01F0) == 3) {
            frame_build_periodic(0);
            rx_pump_step();
        } else if (XB(0x01F0) == 1) {
            uart_clr_seq();
            rx_reset();
            uart_bringup();
            uart_tx_pump_sel(0, 3);
            if (XB(0x0251) != 0) {
                XB(0x0251) = 0;
                uart_tx_byte(0xFF, 0);
            }
        }
    }
}


/* @0x795 - top-level init then the forever main loop. */
void sys_main_loop(void)
{
    sys_clear_ccap1h();           /* early low-level init (stub)           */
    pwr_bringup();           /* subsystem reset chain (stub)          */
    sys_bringup();           /* clocks / UART / interrupts bring-up   */
    for (;;) {
        flash_wdt_kick();       /* kick watchdog                         */
        disp_work_sched();       /* fast/slow work dispatcher             */
        uart_tx_pump();       /* outgoing message pump                 */
    }
}


/* SDCC entry point. */
void main(void)
{
    sys_reset_entry();
}


#if 0  /* reference only - original startup @0x461 */
void sys_reset_entry(void)
{
    /* 1) zero IDATA 0xFF..0x00 */
    /* 2) zero XDATA 0x000..0x3FF (outer=4, inner=256) */
    /* 3) SP = 0x5D */
    /* 4) decompress init image from &DAT_CODE_04fb into DATA/XDATA/bit */
    /* 5) sys_main_loop();  (never returns) */
}
#endif

void sys_reset_entry(void)
{
    /* Faithful behavioural entry.  Steps 1-2 (zero IDATA / XDATA 0..0x3FF) are
     * already performed by SDCC crt0 + __at BSS init.  We reproduce the SP
     * choice, then the DAT_CODE_04fb data-init decompressor, then hand off. */
    SP = 0x5D;                 /* original startup sets SP=0x5D  @0x461 */
    initimg_apply();           /* step 4: apply DAT_CODE_04fb presets   */
    sys_main_loop();           /* init + main loop, never returns       */
}

