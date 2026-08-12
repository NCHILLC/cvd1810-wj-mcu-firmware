/* constants.h - PROPOSAL: named magic numbers for the cm68 MCU firmware port.
 *
 * NOT YET INCLUDED anywhere.  This is a rename-companion to rename.tsv: it
 * gives readable names to the protocol / rail / state magic numbers that are
 * currently spelled as bare hex literals across port00.c..port04.c, power.c,
 * dispatch.c, uart.c and leaf.c.  Each group lists the files/functions (by the
 * NEW names from rename.tsv, original @0xADDR in parentheses) where the literal
 * appears, so it can be applied by hand.
 *
 * The X_FLAGS bit masks FLAG_RAIL_EN / FLAG_PRESENT / FLAG_LINK / FLAG_KEEPALIVE
 * are already defined in cm68_xram.h and are NOT redefined here (only the two
 * still-unnamed X_FLAGS bits are added below, guarded).
 */
#ifndef CM68_CONSTANTS_H
#define CM68_CONSTANTS_H

/* ===========================================================================
 * ACC-arbiter rail action codes (X_ARB_ACTION @0x0254).
 *
 * Dispatched by pwr_rail_sequencer (FUN_043b @0x43b, leaf.c) through the opaque
 * JT_043b jump table @0x08a0.  The four CUT codes route to handlers that
 * CLR P3.6 (raw sites 0x08dc / 0x0909 / 0x09f6 / 0x0a32); RAISE routes to the
 * SETB-P3.6 handler @0x0943.  The NOTURNO_SLEEP_FIX guard in FUN_043b keys off
 * exactly {CUT_0, CUT_6, CUT_13, CUT_16} and forces RAISE.
 *
 * Producers of X_ARB_ACTION: arb_acc (FUN_2239 @0x2239, power.c),
 * pwr_state_init (FUN_24ce), pwr_shutdown_hold_set (FUN_25a2 tests IDLE).
 * Consumers: arb_dispatch (FUN_0899) -> pwr_rail_sequencer (FUN_043b);
 * arb_get_action (FUN_009d) returns it.
 * =========================================================================== */
#define ACT_RAIL_CUT_0   0x00   /* rail-cut handler (FUN_043b guard) */
#define ACT_ACC_ON       0x01   /* arb_acc: cVar1==1 & linked -> rail on         */
#define ACT_RAIL_ON      0x04   /* arb_acc: transition into rail-enabled         */
#define ACT_RAIL_RAISE   0x05   /* SETB-P3.6 handler @0x0943 (fix target)        */
#define ACT_RAIL_CUT_6   0x06   /* rail-cut handler (FUN_043b guard)             */
#define ACT_RAIL_CUT_13  0x13   /* rail-cut handler (FUN_043b guard)             */
#define ACT_IDLE         0x0F   /* arb_acc off-notify; pwr_shutdown_hold_set gate*/
#define ACT_ACC_OFF      0x10   /* arb_acc: cVar1==1 & not linked -> rail off    */
#define ACT_RAIL_CUT_16  0x16   /* rail-cut handler (FUN_043b guard)             */

/* ===========================================================================
 * MCU <-> SoC command IDs (frame header byte at payload offset 3).
 *
 * Routed by disp_frame_router (FUN_0e39 @0xe39, port01.c) switch on FUN_01c4(3).
 * =========================================================================== */
#define CMD_HEARTBEAT    0x02   /* heartbeat / boot handshake (sub at offset 4)  */
#define CMD_ACK          0x04   /* ACK                                           */
#define CMD_ACK5         0x05   /* secondary ACK                                 */
#define CMD_CONFIG       0x09   /* config block -> disp_cfgtbl_store (FUN_2f2c)  */
#define CMD_RESET        0x15   /* reset / ISP request (sub 1 -> isp_system_reset)*/
#define CMD_CONTROL      0x50   /* 'P' control block -> disp_cmd50 (FUN_1613)    */

/* CMD_HEARTBEAT (0x02) sub-command byte at offset 4 (disp_frame_router). */
#define HB_SUB_HALT      0xFE   /* 0x02/0xFE: sub5==1 -> P0.5 pulse + infinite halt */

/* ===========================================================================
 * CMD_CONTROL (0x50) sub-command byte at payload offset 4.
 *
 * Handled by disp_cmd50 (FUN_1613 @0x1613, dispatch.c); args at offsets 5/6/7
 * read via frame_get_cmd (FUN_01c4).
 * =========================================================================== */
#define SUB_ENABLE       0x06   /* store g_disp_mode(0x020b), (re)enable, g_gauge_hold */
#define SUB_BRIGHTNESS   0x03   /* brightness/param -> util_gauge_set_thr (FUN_247f) */
#define SUB_KEEPALIVE    0x07   /* arg==1 -> set X_FLAGS FLAG_KEEPALIVE, else clear */
#define SUB_POFF_TIMEOUT 0x09   /* seed X_POFF_TMO=byte6, X_POFF_CNT=0            */
#define SUB_ACC_STATE    0x0A   /* X_ACC_MODE = arg (0..3); run wake seq         */
#define SUB_CFG_PAIR     0x0B   /* misc paired config (0x0257/0x0258, 0x0264/5)  */

/* ===========================================================================
 * TX message type bytes (the `type`/`arg3` operands of txq_enqueue FUN_0aad).
 * =========================================================================== */
#define MSG_POWEROFF     0x37   /* power-off / status-3x notify (pwr_sleep_emit   */
                                /* FUN_27e8, txq_notify_link FUN_27d5,            */
                                /* frame_build_37sub2/3 FUN_1b3a/1b04)           */

/* ===========================================================================
 * UART frame sync headers (uart.c).
 *
 * SoC->MCU stream leads with A0 55; MCU->SoC frames lead with 55 A0.
 * RX side: uart_rx_fsm (FUN_18eb @0x18eb) hunts SYNC_RX_0 then SYNC_RX_1.
 * TX side: frame_build_send (FUN_1de6 @0x1de6, port02.c) writes SYNC_TX_0/1.
 * =========================================================================== */
#define SYNC_RX_0        0xA0   /* SoC->MCU first sync byte (uart_rx_fsm state 0)*/
#define SYNC_RX_1        0x55   /* SoC->MCU second sync byte (state 1)           */
#define SYNC_TX_0        0x55   /* MCU->SoC frame[0] (frame_build_send)          */
#define SYNC_TX_1        0xA0   /* MCU->SoC frame[1]                             */
#define RX_ISP_MARKER    0x42   /* 'B' in state 0 -> set ISP/reset marker 0x01F4 */

/* ===========================================================================
 * ACC / power mode (X_ACC_MODE @0x025f, validity X_ACC_VALID @0x025e).
 *
 * State test idiom (power.c and leaf.c): s = X_ACC_VALID; if (s==0)
 * s = X_ACC_MODE ^ K;  state == K  iff  s == 0.
 *   K = ACC_MODE_OFF (3): ACC OFF / power-down eligible
 *       - pwr_sleep_sample (FUN_1bc8 mode-7 gate), pwr_sleep_arm (FUN_241c),
 *         pwr_sleep_emit (FUN_27e8), arb path.
 *   K = ACC_MODE_ON  (1): util_pattern_decode gate (FUN_14a1), util_decode_reset
 *         (FUN_2ee7 -> kick sys_timer2_setup).
 *   K = ACC_MODE_2   (2): bl_scan_pulse/bl_scan_strobe (FUN_0702/070e),
 *         pwr_linkdown_hk (FUN_07d0), bl_scan_refresh (FUN_0624).
 * =========================================================================== */
#define ACC_MODE_ON      0x01   /* ACC / ignition ON                            */
#define ACC_MODE_2       0x02   /* mode 2 (scan-out / link housekeeping gate)    */
#define ACC_MODE_OFF     0x03   /* ACC OFF -> power-down eligible                */

/* ===========================================================================
 * X_FLAGS (@0x0266) - additional bits not already named in cm68_xram.h.
 * (FLAG_RAIL_EN 0x01, FLAG_PRESENT 0x04, FLAG_LINK 0x08, FLAG_KEEPALIVE 0x10
 *  live in cm68_xram.h.)  KEEP-ALIVE is bit4 (0x10) = mask FLAG_KEEPALIVE.
 * =========================================================================== */
#ifndef FLAG_LEVEL_OK
#define FLAG_LEVEL_OK    0x02   /* bit1: voltage-level "on" latch (pwr_volt_arbiter*/
                                /* FUN_0cc9, disp_slowtick FUN_0fc3)             */
#endif
#ifndef FLAG_SCAN_BUSY
#define FLAG_SCAN_BUSY   0x40   /* bit6: scan-out busy (bl_scan_refresh FUN_0624, */
                                /* pwr_linkdown_hk FUN_07d0; cleared bl_scan_restart)*/
#endif
#ifndef FLAG_P13_STATE
#define FLAG_P13_STATE   0x80   /* bit7: P1.3 LED/backlight mirror state          */
                                /* (bl_p13_flag_update FUN_0a70, disp_slowtick)   */
#endif

#endif /* CM68_CONSTANTS_H */
