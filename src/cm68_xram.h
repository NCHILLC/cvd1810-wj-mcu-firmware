/* cm68_xram.h - AUTHORITATIVE XDATA (external RAM) memory map for the cm68 MCU.
 *
 * Cmsemicon enhanced-8051 head-unit MCU (image base 0x0000).  Every symbol here
 * is a byte in XDATA reached via MOVX @DPTR, i.e. every distinct DAT_EXTMEM_xxxx
 * in the Ghidra decompile (re/cm68_decomp.c).  144 distinct addresses, all
 * enumerated below - this is the FROZEN contract other porters build against.
 *
 * RULES honoured:
 *  - One __at object per byte-address; no two symbols overlap an address.
 *  - Names/addresses defined by the first porter are preserved verbatim
 *    (X_ACC_MODE, X_FLAGS, X_SLEEP_ARMED, ...); they were XB() macros before and
 *    are now real __at objects with the SAME name+address (drop-in for the .c).
 *  - Each line: purpose + the functions (@0xADDR) that read/write it.
 *  - Suspected little-endian byte pairs are noted in comments but still declared
 *    as two individual bytes, so no symbol overlaps another.
 *
 * Addresses 0xF000+ are the CMS "extended peripheral" MOVX window (separate block).
 * The 50-slot inbound RX ring lives at XDATA 0x0000.. (base ~0x0032) and is
 * addressed through computed pointers (not literal DAT_EXTMEM_*), so it has no
 * single __at symbol; its producer/consumer indices are x_rxring_wr/x_rxring_rd.
 */
#ifndef CM68_XRAM_H
#define CM68_XRAM_H

typedef unsigned char  u8;
typedef unsigned short u16;

/* Legacy accessor kept for code still using it (pointer cast, not a symbol) */
#define XB(addr)   (*(volatile unsigned char xdata *)(addr))

/* RX payload buffer base (reached via pointer, not one byte var) */
#define X_RXBUF_BASE  0x01ba   /* RX frame base: offset 0 = X_RX_HDR@0x01ba; sub-cmd at +4 */


/* ---- Low XDATA: RX FSM tail, TX queue, ring bookkeeping (0x00cb-0x0141) ---- */
extern volatile u8 xdata g_rxcopy_wr;            /* rx-copy scratch pointer/index low | fns: 0x1cc7 0x1f8c 0x25c6 */
extern volatile u8 xdata g_rxcopy_rd;            /* rx-copy scratch pointer/index high | fns: 0x1cc7 0x1f8c 0x25c6 */
extern volatile u8 xdata X_RX_STATE;        /* UART RX frame FSM state 0..5 | fns: 0x18eb 0x1f8c 0x284a */
extern volatile u8 xdata g_txframe_len;            /* rx ring / copy scratch | fns: 0x1cc7 0x1d85 */
extern volatile u8 xdata x_rxring_wr;       /* RX ring producer (write) index | fns: 0x18eb 0x1d85 0x1f8c */
extern volatile u8 xdata x_rxring_rd;       /* RX ring consumer (read) index | fns: 0x1cc7 0x1d85 0x1f8c */
extern volatile u8 xdata x_txq_scan;        /* TX-queue scan result (FUN_2145 output) | fns: 0x2145 */
extern volatile u8 xdata g_txq_scan_arg1;            /* TX-queue scan scratch | fns: 0x2145 */
extern volatile u8 xdata g_txq_scan_arg2;            /* TX-queue scan scratch | fns: 0x2145 */
extern volatile u8 xdata x_txq_head;        /* TX message queue head index | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad 0x2145 0x22cd */
extern volatile u8 xdata x_txq_count;       /* TX message queue count/tail (0=empty) | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad 0x2145 0x22cd */
extern volatile u8 xdata x_txq_active;      /* TX queue current-entry active flag | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */
extern volatile u8 xdata x_txq_retry;       /* TX queue retry counter (seeded 10) | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */
extern volatile u8 xdata x_txq_e;           /* TX queue staging: entry/type byte | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */
extern volatile u8 xdata x_txq_arg3;        /* TX queue staging: payload byte3 | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */
extern volatile u8 xdata x_txq_arg1;        /* TX queue staging: payload byte1 | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */
extern volatile u8 xdata x_txq_arg2;        /* TX queue staging: payload byte2 | fns: 0x0a9b 0x0aa5 0x0aa9 0x0aad */

/* ---- RX frame FSM + timers/scratch (0x01b7-0x01ff) ---- */
extern volatile u8 xdata X_RX_XOR;          /* RX running XOR checksum | fns: 0x18eb */
extern volatile u8 xdata X_RX_LEN;          /* RX payload length / write index | fns: 0x18eb */
extern volatile u8 xdata X_RX_B9;           /* RX length byte (ones-complement checked) | fns: 0x18eb */
extern volatile u8 xdata X_RX_HDR;          /* RX header byte | fns: 0x18eb */
extern volatile u8 xdata x_rx_bb;           /* RX FSM scratch | fns: 0x18eb */
extern volatile u8 xdata x_rx_bc;           /* RX FSM scratch | fns: 0x18eb */
extern volatile u8 xdata X_SLEEP_DEB;       /* sleep-emit debounce (fires >1; FUN_27e8) | fns: 0x27e8 */
extern volatile u8 xdata x_notify_pending;  /* 0x50/0x06 notify-pending flag (set 1) | fns: 0x1613 0x1c9f */
extern volatile u8 xdata X_RX_TMO;          /* RX inter-byte timeout (dec FUN_284a) | fns: 0x18eb 0x1f8c 0x284a */
extern volatile u8 xdata g_tx_seq;            /* tx/ack scratch | fns: 0x1a77 0x1ea8 */
extern volatile u8 xdata x_tx_f0;           /* TX pump index/state | fns: 0x18ba 0x1eca */
extern volatile u8 xdata x_tx_f1;           /* TX pump index/state | fns: 0x18ba 0x1eca 0x284a */
extern volatile u8 xdata g_alert_lo_a;            /* FUN_0b59/281d state | fns: 0x0b59 0x281d */
extern volatile u8 xdata g_alert_lo_b;            /* FUN_0b59/281d state | fns: 0x0b59 0x281d */
extern volatile u8 xdata g_frame02_seen;            /* FUN_0e39/18eb/281d shared flag | fns: 0x0e39 0x18eb 0x281d */
extern volatile u8 xdata g_bandA_last_hi;            /* FUN_2b93 block | fns: 0x2b93 */
extern volatile u8 xdata g_bandA_last_lo;            /* FUN_2b93 block | fns: 0x2b93 */
extern volatile u8 xdata g_bandA_zone;            /* FUN_2b93 block | fns: 0x2b93 */
extern volatile u8 xdata g_bandA_debounce;            /* FUN_2b93 block | fns: 0x2b93 */
extern volatile u8 xdata g_bandA_out;            /* FUN_2b93 block | fns: 0x2b93 */
extern volatile u8 xdata g_bandB_last_hi;            /* FUN_2d51 block | fns: 0x2d51 */
extern volatile u8 xdata g_bandB_last_lo;            /* FUN_2d51 block | fns: 0x2d51 */
extern volatile u8 xdata g_bandB_zone;            /* FUN_2d51 block | fns: 0x2d51 */
extern volatile u8 xdata g_bandB_debounce;            /* FUN_2d51 block | fns: 0x2d51 */
extern volatile u8 xdata g_bandB_out;            /* FUN_2d51 block | fns: 0x2d51 */
extern volatile u8 xdata g_pattern_bitcnt;            /* FUN_14a1 scratch | fns: 0x14a1 */

/* ---- Sub-system state blocks (0x0200-0x0221) ---- */
extern volatile u8 xdata g_gaugeC_last;            /* FUN_2a29 state | fns: 0x2a29 */
extern volatile u8 xdata g_gaugeD_last;            /* FUN_2ab3 state | fns: 0x2ab3 */
extern volatile u8 xdata g_bandA_up;            /* FUN_2b61/2b93 state | fns: 0x2b61 0x2b93 */
extern volatile u8 xdata g_gaugeC_cnt;            /* FUN_2a29 state | fns: 0x2a29 */
extern volatile u8 xdata g_gaugeD_cnt;            /* FUN_2ab3 state | fns: 0x2ab3 */
extern volatile u8 xdata g_gauge_hold;            /* shared config/state (0b59,1c9f,2516,28f1,298d) | fns: 0x0b59 0x1c9f 0x2516 0x28f1 0x298d */
extern volatile u8 xdata g_dev_present_mask;            /* FUN_0b59 state | fns: 0x0b59 */
extern volatile u8 xdata g_gaugeX_target;            /* shared (1c39,1c9f,2863,28f1) | fns: 0x1c39 0x1c9f 0x2863 0x28f1 */
extern volatile u8 xdata g_gaugeY_target;            /* shared (1c39,1c9f,2863,298d) | fns: 0x1c39 0x1c9f 0x2863 0x298d */
extern volatile u8 xdata g_dev209_debounce;            /* FUN_0b59/1c39 state | fns: 0x0b59 0x1c39 */
extern volatile u8 xdata g_dev20a_debounce;            /* FUN_0b59/1c39 state | fns: 0x0b59 0x1c39 */
extern volatile u8 xdata g_disp_mode;            /* 0x50/0x06 stored param (FUN_1613) | fns: 0x0b59 0x1613 */
extern volatile u8 xdata g_bandB_up;            /* FUN_2b61/2d51 state | fns: 0x2b61 0x2d51 */
extern volatile u8 xdata g_anim_hold;            /* FUN_14a1/2ee7/2f0e state | fns: 0x14a1 0x2ee7 0x2f0e */
extern volatile u8 xdata g_dev20e_debounce;            /* FUN_0b59 state | fns: 0x0b59 */
extern volatile u8 xdata g_dev20f_debounce;            /* FUN_0b59 state | fns: 0x0b59 */
extern volatile u8 xdata g_decode_val_lo;            /* FUN_14a1/2ee7 state | fns: 0x14a1 0x2ee7 */
extern volatile u8 xdata g_decode_val_hi;            /* FUN_14a1/15c9/2ee7 state | fns: 0x14a1 0x15c9 0x2ee7 */
extern volatile u8 xdata g_decode_sr_lo;            /* FUN_14a1/15c9/2ee7/2f0e state | fns: 0x14a1 0x15c9 0x2ee7 0x2f0e */
extern volatile u8 xdata g_decode_sr_hi;            /* FUN_14a1/15c9/2ee7/2f0e state | fns: 0x14a1 0x15c9 0x2ee7 0x2f0e */
extern volatile u8 xdata g_gaugeX_last;            /* FUN_28f1 state | fns: 0x28f1 */
extern volatile u8 xdata g_gaugeY_last;            /* FUN_298d state | fns: 0x298d */
extern volatile u8 xdata g_gaugeX_raw;            /* FUN_1c39/2863/28f1 state | fns: 0x1c39 0x2863 0x28f1 */
extern volatile u8 xdata g_gaugeY_raw;            /* FUN_1c39/2863/298d state | fns: 0x1c39 0x2863 0x298d */
extern volatile u8 xdata g_decode_state;            /* FUN_1413/14a1/2ee7/2f0e state | fns: 0x1413 0x14a1 0x2ee7 0x2f0e 0xthunk_1413 */
extern volatile u8 xdata g_decode_out_hi;            /* FUN_15c9/2728 state | fns: 0x15c9 0x2728 */
extern volatile u8 xdata g_decode_out_lo;            /* FUN_15c9/2728 state | fns: 0x15c9 0x2728 */
extern volatile u8 xdata g_gaugeX_cnt;            /* FUN_28f1 state | fns: 0x28f1 */
extern volatile u8 xdata g_gaugeY_cnt;            /* FUN_298d state | fns: 0x298d */
extern volatile u8 xdata g_decode_timeout;            /* FUN_14a1/2ee7/2f0e state | fns: 0x14a1 0x2ee7 0x2f0e */
extern volatile u8 xdata g_bandA_dn;            /* FUN_2b61/2b93 state | fns: 0x2b61 0x2b93 */
extern volatile u8 xdata g_bandB_dn;            /* FUN_2b61/2d51 state | fns: 0x2b61 0x2d51 */
extern volatile u8 xdata g_gaugeX_slew;            /* FUN_1c39/2863 state | fns: 0x1c39 0x2863 */
extern volatile u8 xdata g_gaugeY_slew;            /* FUN_1c39/2863 state | fns: 0x1c39 0x2863 */

/* ---- Config / ACC-arbiter / power state (0x024a-0x0268) ---- */
extern volatile u8 xdata g_cfgtbl_wr;            /* FUN_0624/07a5/2f2c state | fns: 0x0624 0x07a5 0x2f2c */
extern volatile u8 xdata g_cfgtbl_rd;            /* FUN_0624/07a5 state | fns: 0x0624 0x07a5 */
extern volatile u8 xdata g_volt_hold;            /* FUN_0cc9 state | fns: 0x0cc9 */
extern volatile u8 xdata g_volt_state;            /* FUN_0cc9 state | fns: 0x0cc9 */
extern volatile u8 xdata g_volt_thr1_hi;            /* FUN_0cc9/247f/24ce (LE word lo with 0x024f) | fns: 0x0cc9 0x247f 0x24ce */
extern volatile u8 xdata g_volt_thr1_lo;            /* FUN_0cc9/247f/24ce (LE word hi) | fns: 0x0cc9 0x247f 0x24ce */
extern volatile u8 xdata x_0250;            /* FUN_24ce init state | fns: 0x24ce */
extern volatile u8 xdata g_tx_kick;            /* FUN_07b9/18ba TX-ready flag | fns: 0x07b9 0x18ba */
extern volatile u8 xdata g_shutdown_delay;            /* FUN_24ce/25a2/25b2 state | fns: 0x24ce 0x25a2 0x25b2 */
extern volatile u8 xdata X_ARB_STATE;       /* ACC-arbiter sub-state 0/2/3 | fns: 0x2239 0x24ce */
extern volatile u8 xdata X_ARB_ACTION;      /* ACC-arbiter action code -> FUN_043b jumptable | fns: 0x009d 0x0899 0x2239 0x24ce 0x25a2 */
extern volatile u8 xdata X_POFF_TMO;        /* poweroff-timeout seed (LE u16 lo with 0x0256) | fns: 0x1613 0x24ce */
extern volatile u8 xdata X_POFF_CNT;        /* poweroff-timeout counter (LE u16 hi) | fns: 0x1613 0x24ce */
extern volatile u8 xdata g_band_inhibit_lo;            /* FUN_1613/2b61/2b93/2d51 (LE word lo with 0x0258) | fns: 0x1613 0x2b61 0x2b93 0x2d51 */
extern volatile u8 xdata g_band_inhibit_hi;            /* FUN_1613/2b61/2b93/2d51 (LE word hi) | fns: 0x1613 0x2b61 0x2b93 0x2d51 */
extern volatile u8 xdata g_railtimer_lo;            /* FUN_0a5d/0a61/0fc3/24ce (LE word lo with 0x025a) | fns: 0x0a5d 0x0a61 0x0fc3 0x24ce */
extern volatile u8 xdata g_railtimer_hi;            /* FUN_0a5d/0a61/0fc3/24ce (LE word hi) | fns: 0x0a5d 0x0a61 0x0fc3 0x24ce */
extern volatile u8 xdata x_025b;            /* FUN_1613/2b61 state | fns: 0x1613 0x2b61 */
extern volatile u8 xdata x_025c;            /* FUN_1613/2b61 state | fns: 0x1613 0x2b61 */
extern volatile u8 xdata g_led_blink_tmo;            /* FUN_0a6a/0a6c/0a70/0fc3 state | fns: 0x0a6a 0x0a6c 0x0a70 0x0fc3 */
extern volatile u8 xdata X_ACC_VALID;       /* ACC mode validity (normally 0) | fns: 0x0624 0x0702 0x070e 0x07d0 0x14a1 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 0x2ee7 */
extern volatile u8 xdata X_ACC_MODE;        /* ACC/power mode 0..3 (3=ACC OFF) | fns: 0x0624 0x0702 0x070e 0x07d0 0x14a1 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 0x2ee7 */
extern volatile u8 xdata g_volt_thr2_hi;            /* FUN_0cc9/247f/24ce (LE word lo with 0x0261) | fns: 0x0cc9 0x247f 0x24ce */
extern volatile u8 xdata g_volt_thr2_lo;            /* FUN_0cc9/247f/24ce (LE word hi) | fns: 0x0cc9 0x247f 0x24ce */
extern volatile u8 xdata X_ACC_PREV_V;      /* previous X_ACC_VALID (edge detect) | fns: 0x1613 0x2b61 */
extern volatile u8 xdata X_ACC_PREV_M;      /* previous X_ACC_MODE (edge detect) | fns: 0x1613 0x2b61 */
extern volatile u8 xdata x_0264;            /* FUN_1613/2b61 state | fns: 0x1613 0x2b61 */
extern volatile u8 xdata x_0265;            /* FUN_1613/2b61 state | fns: 0x1613 0x2b61 */
extern volatile u8 xdata X_FLAGS;           /* bit0 rail-en, bit2 present, bit3 link, bit4 KEEP-ALIVE | fns: 0x0624 0x07a5 0x07d0 0x0a6a 0x0a6c 0x0a70 0x0b59 0x0cc9 0x0e39 0x0fbb 0x0fc3 0x1613 0x1a59 0x1cc7 0x2239 0x24ce 0x2524 0x2728 0x27d5 0x27e8 0x28f1 0x298d 0x2a29 0x2ab3 */
extern volatile u8 xdata x_0267;            /* FUN_24ce init state | fns: 0x24ce */
extern volatile u8 xdata x_0268;            /* FUN_24ce init state | fns: 0x24ce */

/* ---- Work scheduler + edge latches (0x026d-0x0276) ---- */
extern volatile u8 xdata x_work_ph0;        /* FUN_0f24 phase-0 sub-tick counter (mod 5) | fns: 0x0f24 */
extern volatile u8 xdata x_work_ph1;        /* FUN_0f24 phase-1 sub-tick counter (mod 10) | fns: 0x0f24 */
extern volatile u8 xdata x_work_ph2;        /* FUN_0f24 phase-2 sub-tick counter (mod 10) | fns: 0x0f24 */
extern volatile u8 xdata X_WORK;            /* work-request bitfield (set by FUN_208c tick) | fns: 0x0f24 0x208c */
extern volatile u8 xdata g_vmin_hi;            /* FUN_0cc9/1a77/1b04/241c (LE word lo with 0x0272) | fns: 0x0cc9 0x1a77 0x1b04 0x241c */
extern volatile u8 xdata g_vmin_lo;            /* FUN_0cc9/1a77/1b04/241c (LE word hi) | fns: 0x0cc9 0x1a77 0x1b04 0x241c */
extern volatile u8 xdata X_TICKS;           /* Timer0 sub-tick counter 0..0x26 | fns: 0x208c */
extern volatile u8 xdata g_t2cap_hi;            /* FUN_1413 state | fns: 0x1413 0xthunk_1413 */
extern volatile u8 xdata g_t2cap_lo;            /* FUN_1413 state | fns: 0x1413 0xthunk_1413 */
extern volatile u8 xdata X_SLEEP_ARMED;     /* 1 => power-down arm request (FUN_241c) | fns: 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 */

/* ---- CMS extended-peripheral MOVX window (0xF000+) ---- */
extern volatile u8 xdata xp_f000;           /* CMS extended-peripheral reg (FUN_07d0) | fns: 0x07d0 */
extern volatile u8 xdata xp_f002;           /* ext-periph reg (FUN_0587 power-down seq) | fns: 0x0587 */
extern volatile u8 xdata xp_f003;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata xp_f004;           /* ext-periph reg (FUN_2342) | fns: 0x2342 */
extern volatile u8 xdata xp_f005;           /* ext-periph reg (FUN_2342) | fns: 0x2342 */
extern volatile u8 xdata xp_f013;           /* ext-periph reg (FUN_2342) | fns: 0x2342 */
extern volatile u8 xdata xp_f014;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata xp_f015;           /* ext-periph reg (FUN_0773/2342) | fns: 0x0773 0x2342 */
extern volatile u8 xdata xp_f016;           /* ext-periph reg (FUN_1795/2b4f) | fns: 0x1795 0x2b4f */
extern volatile u8 xdata xp_f017;           /* ext-periph reg (FUN_17a7/2b3d) | fns: 0x17a7 0x2b3d */
extern volatile u8 xdata xp_f021;           /* ext-periph reg (FUN_187d) | fns: 0x187d */
extern volatile u8 xdata xp_f022;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata xp_f023;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata xp_f024;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata g_ext_wake_req;           /* ext-periph reg (FUN_07d0/133a/241c) | fns: 0x07d0 0x133a 0x241c 0xthunk_133a */
extern volatile u8 xdata xp_f026;           /* ext-periph reg (FUN_0587) | fns: 0x0587 */
extern volatile u8 xdata xp_f02a;           /* ext-periph reg (FUN_133a) | fns: 0x133a 0xthunk_133a */
extern volatile u8 xdata xp_f031;           /* ext-periph reg (FUN_179e/2b58) | fns: 0x179e 0x2b58 */
extern volatile u8 xdata xp_f032;           /* ext-periph reg (FUN_17db/2b46) | fns: 0x17db 0x2b46 */
extern volatile u8 xdata xp_f035;           /* ext-periph reg (FUN_187d) | fns: 0x187d */
extern volatile u8 xdata xp_f036;           /* ext-periph reg (FUN_2342) | fns: 0x2342 */
extern volatile u8 xdata xp_f0c8;           /* ext-periph reg (FUN_133a) | fns: 0x133a 0xthunk_133a */
extern volatile u8 xdata g_uart_frame_ctl;           /* ext-periph reg (FUN_1eae/1ebf) | fns: 0x1eae 0x1ebf */
extern volatile u8 xdata g_uart_frame_win_lo;           /* ext-periph reg (FUN_1eb6, LE word lo w/ 0xf5c2) | fns: 0x1eb6 */
extern volatile u8 xdata g_uart_frame_win_hi;           /* ext-periph reg (FUN_1eb6, LE word hi) | fns: 0x1eb6 */
extern volatile u8 xdata xp_f692;           /* ext-periph reg (FUN_0819) | fns: 0x0819 */

/* ---- X_FLAGS (0x0266) bit definitions (porter) ---------------------------- */
#define FLAG_RAIL_EN   0x01   /* SoC rail enable                              */
#define FLAG_PRESENT   0x04   /* peer present                                 */
#define FLAG_LINK      0x08   /* link established                            */
#define FLAG_KEEPALIVE 0x10   /* 0x50/0x07 arg==1 -> Android keep-alive       */

#endif /* CM68_XRAM_H */
