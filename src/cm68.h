/* cm68.h - common header for the cm68 MCU C reconstruction (SDCC / mcs51).
 *
 * Faithful re-implementation of cm68_update.bin.  Behaviour matches the
 * original EXCEPT for the deliberate sleep/power fix, which is isolated
 * behind NOTURNO_SLEEP_FIX (see power.c and PORT.md).
 */
#ifndef CM68_H
#define CM68_H

#include "cm68_sfr.h"
#include "cm68_xram.h"

/* u8 and u16 are defined by cm68_xram.h (the frozen memory map); do not
 * re-typedef them here (SDCC flags a duplicate typedef as an error). */
typedef signed   char s8;
typedef unsigned long u32;   /* 32-bit helpers (FUN_03c3/03e1/03f4/0407/...) */

/* ======================================================================
 *  NOTURNO_SLEEP_FIX
 *  ----------------------------------------------------------------------
 *  Undefined  => FAITHFUL build: byte-for-byte the original power logic
 *                (ACC-off arms sleep and lets the SoC rail be cut).
 *  Defined     => FIXED build: on ACC-off the MCU HOLDS the rail (P3.6)
 *                and treats the state as suspend-with-wake, honouring the
 *                keep-alive flag / zero-timeout, while still allowing a
 *                real long-timeout clean shutdown.
 *
 *  Toggle here (or pass -DNOTURNO_SLEEP_FIX to sdcc).
 * ==================================================================== */
/* #define NOTURNO_SLEEP_FIX */

#ifdef NOTURNO_SLEEP_FIX
/* Shared keep-alive guard predicate used by EVERY rail-cut chokepoint.
 * True  => the SoC rail must be HELD HIGH (P3.6=1), not cut: either the Android
 *          keep-alive flag (X_FLAGS bit4) is set, or no auto power-off is
 *          configured (poweroff-timeout seed X_POFF_TMO == 0).
 * False => a genuine auto power-off may proceed (long-timeout clean shutdown).
 * Faithful build (macro undefined) keeps every rail-cut path byte-faithful. */
#define NOTURNO_KEEPALIVE_HOLD  (((X_FLAGS & FLAG_KEEPALIVE) != 0) || (X_POFF_TMO == 0))
#endif

/* ---- Faithfully-ported functions (real signatures) -------------------- */
/* startup.c */
void sys_reset_entry(void);          /* @0x461  crt0/startup                 */
void sys_main_loop(void);          /* @0x795  init + main loop             */

/* leaf.c */
void          sys_ccapm4_enable(void);                 /* @0x005 CCAPM4|=0x80  */
void          flash_wdt_kick(void);                 /* @0x5e4 watchdog kick */
void          sys_pcon1_ccapm4_cfg(u8 pcon1_bits, u8 ccapm4_hi);   /* @0x577      */
void          pwr_sleep_pca_cfg(u8 param);             /* @0x5ce sleep PCA cfg */
void          sys_clr_f692_bit7(void);                 /* @0x819               */
u8            frame_get_off(u8 off);               /* @0x1ab frame get     */
u8            frame_get_cmd(u8 off);               /* @0x1c4 frame get     */
void          frame_put(u8 val, u8 off);       /* @0x224 frame put     */
void          frame_set(u8 val, u8 off);       /* @0x236 frame put     */
u8            frame_get_next(u8 off);               /* @0x26e frame get+1   */
void          math_add16_xdata(u8 hi, u16 addr, u8 add); /* @0x258 16b add    */
void          pwr_rail_sequencer(u8 act_code);              /* @0x43b jumptable     */
void          sys_hpcon_hp_on(void);                 /* @0x4d  HPCON|=0x80   */
void          sys_hpcon_hp_off(void);                 /* @0x75  HPCON&=0x7f   */
void          bl_scan_strobe(u8 on);                /* @0x70e               */
void          bl_scan_pulse(void);                 /* @0x702               */

/* power.c */
void          pwr_powerdown(void);                 /* @0x587 power-down     */
void          sys_bringup(void);                 /* @0x5b0 periph bringup */
u16           pwr_sleep_sample(u8 mode);             /* @0x1bc8 sleep sample (full 12-bit R6:R7) */
void          arb_acc(void);                 /* @0x2239 ACC arbiter   */
void          pwr_sleep_arm(void);                 /* @0x241c sleep arm     */
u8            pwr_sleep_emit(void);                 /* @0x27e8 sleep emit    */
void          pwr_linkdown_hk(void);                 /* @0x7d0                */
void          bl_scan_restart(void);                 /* @0x7a5                */
void          sys_can_int_bringup(u8 param);             /* @0x2342 periph init   */
void          arb_dispatch(void);                 /* @0x899                */
void          pwr_state_init(void);                 /* @0x24ce state init    */
void          pwr_state_reset(void);                 /* @0x2b61 state reset   */

/* uart.c */
void          uart_rx_fsm(void);                 /* @0x18eb RX FSM        */
u8            uart_service(void);                 /* @0x1d85 rx/tx byte    */
void          uart_tx_byte_off(u8 off);               /* @0x1d6f TX one        */
void          uart_tx_byte(u8 val, u8 chan);      /* @0x1d7a TX chan       */
void          isr_uart0_body(void);                 /* @0x20ae UART0 ISR body*/
void          isr_timer0_body(void);                 /* @0x2052 Timer0 ISR    */
void          sys_timer0_tick(void);                 /* @0x208c tick          */

/* dispatch.c */
void          disp_work_sched(void);                 /* @0xf24 fast worker    */
void          uart_tx_pump(void);                 /* @0x18ba tx pump       */
void          frame_build_periodic(u8 param);             /* @0x1a59               */
void          frame_route_status(u8 p1, u8 p2, u8 p3);  /* @0x1a77               */
u8            disp_cmd50(u8 p1, u8 p2, u8 p3);  /* @0x1613 0x50 dispatch */
void          disp_slowtick(void);                 /* @0xfc3                */

#endif /* CM68_H */
