/* cm68_stubs.h - canonical prototypes for the functions that live OUTSIDE the
 * first-porter set (cm68.h).  After Phase-B convergence this splits into:
 *
 *   (1) FAITHFULLY-PORTED prototypes (port00.c..port04.c own the bodies) - the
 *       signatures here are the reconciled, faithful ones (matching the SDCC
 *       definitions).  Where a batch tagged a `SIGCHANGE`, the faithful arity
 *       won here and every call site was updated to match (defaults marked
 *       `TODO faithful-arg` where a caller cannot yet supply the real value).
 *
 *   (2) STILL-STUBBED prototypes (stubs.c owns the bodies, return 0) - the 24
 *       functions not yet faithfully ported.  These keep the u8(void) arity.
 *
 * The two ISR bodies isr_ext8 (@0x109a, vector 8) and isr_timer2
 * (@0x1413, vector 5) are NOT declared here: they are `__interrupt` handlers
 * defined in port01.c and never called from C (their prototypes live in
 * startup.c, the main module, so SDCC emits the vector redirect).
 */
#ifndef CM68_STUBS_H
#define CM68_STUBS_H
#include "cm68.h"

/* ======================================================================
 * (1) Faithfully-ported functions (bodies in port00.c .. port04.c).
 * ==================================================================== */

/* ---- port00.c ---- */
u8   sys_clear_ccap1h(void);
u8   math_abs16(void);
u8   sys_et2_disable(void);
u8   sys_store_acc_debounce(void);
u8   sys_t2con_clr_lowbits(void);
u8   sys_eie_set_bit5(void);
void flash_unlock_haddr1(void);
u8   arb_get_action(void);
void rx_pump_step(void);
s8   util_block_copy(u8 dst_lo, u8 dst_mode, u8 src_mode, u8 count_hi, u8 count_lo);
u8   math_addbyte_mode(u8 value, u16 base, u8 off, u8 off_hi, s8 mode);
u32  math_or32(u32 a, u32 b);
u32  math_shr32(u8 count, u32 v);
u32  math_shl32(u8 count, u32 v);
u32  math_load_be32(u8 xdata *p);
void math_store4_idata(u8 idata *p, u8 b0, u8 b1, u8 b2, u8 b3);
void math_store4_xdata(u8 xdata *p, u8 b0, u8 b1, u8 b2, u8 b3);
void math_add16_idata(u8 hi, u8 lo, u8 idata *p);
void util_delay16_wdt(u8 hi, u8 lo);
void bl_scan_refresh(void);
void util_delay16_spin(u8 hi, u8 lo);
void util_delay16_wdt4(u8 hi, u8 lo);
void pwr_bringup(void);
void bl_p13_flag_update(u8 blink_tmo);
u8   txq_enqueue_delta(u8 xdata *p, u8 r7);
/* rail-path sub-tree leaves/services (reached only via pwr_rail_sequencer) */
void sys_ccapm4_disable(void);        /* @0x000e */
void sys_pcon_set_b2(void);           /* @0x001d */
void sys_pcon_clr_b2(void);           /* @0x0026 */
void sys_bd_set_bit7(void);           /* @0x002e */
void sys_bd_clr_bit7(void);           /* @0x0035 */
void sys_periph_mode_apply(void);     /* @0x0095 */
void sys_extie_set(u8 sel, u8 mask);  /* @0x0821 */
void sys_periph_reinit(void);         /* @0x083f */
void sys_extie_clr(u8 sel, u8 mask);  /* @0x0860 */
void sys_periph_recycle(void);        /* @0x0882 */

/* ---- port01.c ---- */
u8   bl_p13_set_and_p05(void);
u8   bl_p13_flag_clear(void);
u8   txq_enqueue_i21(void);
u8   txq_enqueue_i5c(void);
u8   txq_enqueue(u8 a0, u8 a1, u8 a2, u8 a3);
u8   util_scan_inputs(void);
u8   pwr_volt_arbiter(void);
u8   disp_frame_router(u8 a0, u8 a1, u8 a2);
u8   sys_timer1_setup(void);
u8   sys_timer2_setup(void);
u8   util_pattern_decode(u8 cap_hi, u8 cap_lo);
u8   sys_comms_cfg(u8 channel);
/* rail-path sub-tree services */
void sys_pcon_set_b1(void);           /* @0x10e4 */
void sys_bd_field(u8 val);            /* @0x11f6 */
void sys_bcbd_load(u8 a, u8 b);       /* @0x122c */
void pwr_poff_reload(void);           /* @0x1200 */
u8   pwr_poweroff_monitor(void);      /* @0x10f0 */
u8   pwr_shutdown_sequence(void);     /* @0x0e04 */

/* ---- port02.c ---- */
u8   sys_hpcon_clrbit(u8 bit_n);
u8   sys_serial_clk_cfg(u8 mode, u8 clk_src, u8 chan);
u8   uart_bringup(void);
u8   util_gauge_scheduler(void);
u8   uart_clr_seq(void);
u8   uart_tx_pump_sel(u8 a0, u8 a1);
u8   sys_pcon_mode(u8 sel);
/* rail-path sub-tree services */
void disp_periph_mode_apply(void);    /* @0x175f */
void sys_periph_disable_all(void);    /* @0x17b0 */

/* ---- port03.c ---- */
u8   rx_reset(void);
u8   sys_sfr8e_field(u8 param_1);
u8   txq_retry_tick(void);
u8   txq_scan_pop(u8 param_1);
u8   txq_reset(void);
void frame_pack_be32(u8 param_1, u8 param_2, u8 param_3);
u8 util_scale_adc_down(u8 param_1, u8 param_2);
u8   util_gauge_hold_set(void);
u8   sys_acc_debounce_dec(void);
u8   util_present_detect(void);
u8   sys_acc_debounce_zero(void);
void pwr_shutdown_hold_set(u8 param_1);
u8   pwr_shutdown_tick(void);
u8   uart_tx_stage(u8 param_1, u8 param_2);
/* rail-path sub-tree services */
u8   pwr_present_debounce(void);      /* @0x257c */
u8   uart_tx_wake_burst(void);        /* @0x1f35 */
u8   sys_int_clr_pair(u8 chan);       /* @0x1fd3 */
u8   sys_int_disable_pair(void);      /* @0x1fe0 */
u8   sys_pd_wait(void);               /* @0x243a */
u8   sys_work_clear(void);            /* @0x232c */

/* ---- port04.c ---- */
u8   uart_txwin_dec(void);
u8   isp_system_reset(void);
u8   txq_notify_link(void);
u8   frame_build_status(void);
u8   util_band_track_a(void);
u8   util_band_track_b(void);
u8   util_decode_reset(void);
u8   util_decode_tick(void);

/* ======================================================================
 * (2) Still-stubbed functions - NONE REMAIN.
 *
 * All 13 former placeholders that were real callable functions are now
 * faithfully ported (relocated to sections above under port00.c/port01.c).
 * The 11 interrupt-vector RETI fillers (FUN_CODE_0009 000a 0012 0019 001a
 * 0021 0022 002a 0032 0039 003a) were dropped entirely - they had no call
 * sites and the vector table is owned by SDCC __interrupt handlers.
 * ==================================================================== */

void bl_precharge(u8 hi, u8 lo);
u8 bl_pwm_mux_clear(u8 channel);
void bl_strobe_pulse(void);
u8 disp_cfgtbl_store(void);
void disp_slowtick(void);
void disp_work_sched(void);
u8 frame_build_04_1a(u8 base_lo, u8 base_hi, u8 base_sel);
u8 frame_build_37sub2(u8 base_lo, u8 base_hi, u8 base_sel);
u8 frame_build_37sub3(u8 base_lo, u8 base_hi, u8 base_sel);
u8 frame_build_counter(u8 base_memtype, u8 base_hi, u8 base_lo);
u8 frame_build_send(u8 base_lo, u8 base_hi, u8 base_sel, u8 seq);
u8 frame_build_small(u8 base_memtype, u8 base_hi, u8 base_lo);
u8 frame_build_ui(u8 base_lo, u8 base_hi, u8 base_sel, u8 kind, u8 val);
void frame_memset(u8 ptr_lo, u8 ptr_hi, u8 space_sel, u8 fill_val);
void frame_strcpy(u8 dst_memtype, u8 dst_hi, u8 dst_lo);
u8 pwr_poweroff_monitor(void);
u8 pwr_present_debounce(void);
u8 pwr_rail_rearm(void);
u8 pwr_railtimer_seed(void);
void pwr_railtimer_set(u8 lo, u8 hi);
u8 pwr_shutdown_sequence(void);
void pwr_sleep_pca_cfg(u8 chan);
void pwr_state_init(void);
void pwr_state_reset(void);
void sys_can_int_bringup(u8 with_aux_reinit);
u8 sys_clocksrc_cfg(u8 val, u8 sel);
u8 sys_et2_enable(void);
void sys_extie_set(u8 sel, u8 mask);
u8 sys_hpcon_setbit(void);
u8 sys_int_clr_pair(u8 chan);
u8 sys_int_disable_pair(void);
void sys_main_loop(void);
u8 sys_pd_wait(void);
u8 sys_periph_ack_f016(void);
u8 sys_periph_ack_f017(void);
u8 sys_periph_ack_f031(void);
u8 sys_periph_ack_f032(void);
u8 sys_periph_dis_f016(void);
u8 sys_periph_dis_f017(void);
u8 sys_periph_dis_f031(void);
u8 sys_periph_dis_f032(void);
void sys_periph_disable_all(void);
void sys_periph_reinit(void);
u8 sys_t2mod_bit7(void);
u8 sys_t2mod_load7f(void);
u8 sys_t2mod_setmask(u8 bit_n);
u8 sys_t2mod_testbit(u8 bit_n);
u8 sys_tick_decrements(void);
u8 sys_timer2_mode_low(u8 clk_bits, u8 mode_bits);
u8 sys_timer2_pwm_cfg(u8 val, u8 mode);
u8 sys_timer2_reload_load(void);
u8 sys_timer2_topbit(u8 bit_n);
u8 sys_timer_int_enable(u8 sel);
u8 sys_timer_mode_cfg(u8 val_lo, u8 val_hi, u8 sel);
u8 sys_timer_reload_load(u8 lo, u8 hi, u8 sel);
void uart_clr_ri(u8 chan);
void uart_clr_ti(u8 chan);
u8 uart_frame_ctl_low(u8 a0);
u8 uart_frame_ctl_set(void);
u8 uart_frame_win_load(u8 a0, u8 a1);
u8 uart_read_data(u8 chan);
u8 uart_rx_enable(u8 chan);
void uart_rx_int_enable(u8 param_1);
u8 uart_test_ri(u8 chan);
u8 uart_test_ti(u8 chan);
void uart_tx_byte_off(u8 off);
u8 uart_tx_drain(void);
void uart_tx_pump(void);
u8 util_flags_clr_link(void);
u8 util_gauge_c(void);
u8 util_gauge_d(void);
u8 util_gauge_sample_thr(void);
u8 util_gauge_set_thr(u8 param_1, u8 param_2);
u8 util_gauge_x(void);
u8 util_gauge_y(void);
u8 util_needle_chase(void);
u8 util_pattern_latch(void);
u8 util_port_clear_bit(u8 port_sel);
u8 util_port_dummy_read(u8 port_sel);
void util_scale_adc_up(u8 adc_raw);

#endif /* CM68_STUBS_H */
