/* uart.c - UART framing FSM (SoC <-> MCU), byte TX, and the ISR bodies.
 *
 * Frame protocol (recovered): sync header A0 55 (SoC->MCU) / 55 A0 (MCU->SoC),
 * length byte + 1's-complement length check, payload, XOR checksum trailer.
 * The parser is the classic multi-state FSM in FUN_18eb.
 *
 * Ring model: incoming bytes are buffered in an XDATA ring; the read index is
 * at XDATA 0x0000 (bEXTMEM0000) and the producer index at 0x0133.  The parser
 * assembles the validated payload starting at X_RXBUF_BASE (0x00BA), which is
 * exactly where the command dispatcher (FUN_1613 / FUN_01c4) reads it from.
 */
#include "cm68.h"
#include "cm68_stubs.h"

#define X_RX_RD     XB(0x0000)      /* bEXTMEM0000 - ring read index         */
#define X_RX_WR     XB(0x0133)      /* producer index                        */
#define RX_RING(i)  XB((u16)(0x00CE + (u8)(i)))
/* ^ 50-slot inbound ring at XDATA 0x00CE..0x00FF. Original adds #0xCE to the
 *   ring index at both endpoints (consumer @0x18ff, producer @0x1dcc). */

/* @0x18eb - inbound byte FSM.  Faithful transcription of the state machine. */
void uart_rx_fsm(void)
{
    u8 wr = X_RX_WR;
    u8 rd, b;

    if (X_RX_RD == wr) return;            /* ring empty */

    do {
        rd = X_RX_RD;
        b  = RX_RING(rd);                 /* DAT_INTMEM_21 = next byte */

        switch (X_RX_STATE) {
        case 1:
            X_RX_STATE = (b == 0x55) ? 2 : 0;
            X_RX_LEN = 0;
            break;
        case 2:
            X_RX_STATE = 3;
            X_RX_XOR = b;
            X_RX_HDR = b;
            break;
        case 3:
            X_RX_XOR ^= b;
            X_RX_STATE = 4;
            X_RX_B9 = b;
            XB(0x01BB) = b;
            break;
        case 4:
            X_RX_LEN = 3;
            X_RX_XOR ^= b;
            XB(0x01BC) = b;
            /* length 1's-complement check: B9 + b must be 0xFF (no carry) */
            if ((u8)(X_RX_B9 + b) != 0xFF) {
                X_RX_STATE = 0;
                X_RX_TMO = 0;
            } else {
                X_RX_STATE = 5;
            }
            break;
        case 5:
            /* accumulate payload into 0x00BA + idx until length reached */
            if (X_RX_LEN <= 0x45) {
                XB((u16)(X_RXBUF_BASE + X_RX_LEN)) = b;
                X_RX_LEN++;
                X_RX_XOR ^= b;
            } else {
                XB((u16)(X_RXBUF_BASE + X_RX_LEN)) = b;
                X_RX_STATE = 0;
                X_RX_TMO = 0;
                if (X_RX_XOR == b) {
                    disp_frame_router(0xBA, 1, 1);      /* good frame -> dispatch */
                } else {
                    txq_enqueue(X_RX_HDR, 0xF0, 0xFE, 3); /* NAK */
                }
            }
            break;
        default: /* state 0: hunt for A0 header (or 0x42 special) */
            if (X_RX_STATE != 0) {
                X_RX_STATE = 0;
                X_RX_LEN = 0;
                break;
            }
            if (b == 0xA0) {
                X_RX_STATE = 1;
                X_RX_TMO = 0x19;
            } else if (b == 0x42) {
                XB(0x01F4) = 1;             /* ISP/reset request marker */
            }
            break;
        }

        rd = (u8)(X_RX_RD + 1);
        if (rd > 0x31) rd = 0;
        X_RX_RD = rd;
    } while (X_RX_RD != wr);
}

/* Outbound TX frame staging area (SoC -> UART0), XDATA 0x0100:
 *   g_txframe_len (0x0100) = frame length; payload bytes live at 0x0101.. .
 *   x_rxring_rd   (0x0134) doubles as the TX transmit cursor (the frozen name
 *   is misleading here: in FUN_1d85 this cell is the outbound byte index,
 *   0xFF = idle / nothing to send).
 *   XB(0x0001) is a status/handshake cell (no frozen symbol) raised to 3 once
 *   the whole frame has been shifted out. */
#define TX_LEN       g_txframe_len              /* 0x0100 */
#define TX_IDX       x_rxring_rd                /* 0x0134 (0xFF = idle)          */
#define TX_FRAME(o)  XB((u16)(0x0100 + (u8)(o)))
#define X_TX_DONE    XB(0x0001)

/* @0x1d6f - transmit one TX-frame byte on UART0.  In the original this block
 * loads R5 = X[0x0100 + off], forces channel 0 (R7=0) and *falls through* into
 * FUN_1d7a, so the net effect is SBUF = X[0x0100 + off].  DPTR is built as
 * 0x0100 + off (DPH = 1 + carry), i.e. the payload area at 0x0101 upward. */
void uart_tx_byte_off(u8 off)
{
    SBUF = TX_FRAME(off);
}

/* @0x1d7a - transmit `val` on the selected channel: 0=UART0(SBUF), 1=UART1. */
void uart_tx_byte(u8 val, u8 chan)
{
    if (chan == 0) SBUF   = val;
    if (chan == 1) CCAP1L = val;           /* 2nd serial data @0xEB */
}

/* @0x1d85 - UART0 RX/TX service, run from the UART0 serial ISR (FUN_20ae,
 * vector 0x23).  It is a *polled* handler in the sense that it is a normal
 * function; the ISR body just saves R0..R7/PSW/DPTR and LCALLs it.  Two halves:
 *
 *   TX half: if TI is set (channel 0 finished shifting a byte), clear TI and,
 *   unless the TX cursor is idle (0xFF), push the next frame byte.  The byte at
 *   TX_FRAME(idx+1) goes out, the cursor advances, and when it reaches the frame
 *   length the cursor is parked at 0xFF and X[0x0001] is set to 3 (TX-complete
 *   handshake).
 *
 *   RX half: if RI is set (channel 0 received a byte), clear RI, read the data
 *   register and store it into the 50-slot inbound ring at 0x00CE + producer,
 *   then advance the producer index (x_rxring_wr @0x0133) wrapping at 0x32.
 *
 * Byte-faithful to 1d85: primitives uart_test_ti/clr_ti/test_ri/clr_ri/
 * read_data are @0x1f6d/1f20/1f4e/1f0b/1efc (defined in port03.c), all invoked
 * on channel 0.  The 1d85 return register is a leftover (callers discard it). */
u8 uart_service(void)
{
    /* ---- TX: did UART0 just finish transmitting a byte? (TI) ---------- */
    if (uart_test_ti(0)) {                 /* @0x1f6d test TI (SCON.1)      */
        uart_clr_ti(0);                    /* @0x1f20 clear TI              */
        if (TX_IDX != 0xFF) {              /* 0xFF = idle, nothing queued   */
            uart_tx_byte_off(TX_IDX + 1);  /* @0x1d6f SBUF = TX_FRAME(idx+1)*/
            TX_IDX++;
            if (TX_IDX >= TX_LEN) {        /* whole frame shifted out       */
                TX_IDX = 0xFF;             /* park cursor idle              */
                X_TX_DONE = 3;             /* raise TX-complete handshake   */
            }
        }
    }

    /* ---- RX: did UART0 receive a byte? (RI) --------------------------- */
    if (uart_test_ri(0)) {                 /* @0x1f4e test RI (SCON.0)      */
        uart_clr_ri(0);                    /* @0x1f0b clear RI              */
        RX_RING(x_rxring_wr) = uart_read_data(0);  /* @0x1efc -> ring @0xCE */
        x_rxring_wr++;                     /* advance producer @0x0133      */
        if (x_rxring_wr >= 0x32) x_rxring_wr = 0;  /* 50-slot ring wrap     */
    }

    return 0;                              /* R7 leftover in original; unused */
}

/* @0x20ae - UART0 serial ISR body (vector 0x23).  Saves R0..R7 in the
 * original; under SDCC register save is automatic, so we just call the body. */
void isr_uart0_body(void)
{
    uart_service();
}

/* @0x208c - Timer0 tick: every 0x27 ticks raise work bit0 and age a counter. */
void sys_timer0_tick(void)
{
    if (X_TICKS < 0x27) {
        X_TICKS++;
        return;
    }
    X_TICKS = 0;
    X_WORK |= 1;
    if (XB(0x0001) != 0) XB(0x0001)--;     /* cEXTMEM0001 */
}

/* @0x2052 - Timer0 ISR body (vector 0x0B). */
void isr_timer0_body(void)
{
    sys_timer0_tick();
}

/* ---- SDCC interrupt vectors ------------------------------------------- *
 * Wire the hardware vectors to the ported ISR bodies.  Vector numbers:
 *   1 = Timer0 (0x000B), 4 = UART0 serial (0x0023).                        */
void isr_timer0(void) interrupt 1 { isr_timer0_body(); }
void isr_uart0(void)  interrupt 4 { isr_uart0_body(); }


/* ---- appended during Phase-C file reorganization (pure code motion from
 * port00-04.c/leaf.c/dispatch.c; see git history for original locations). ---- */

#define BANK0_R7  (*(volatile u8 idata *)0x07)
#define I21       (*(volatile u8 idata *)0x21)
#define I22       (*(volatile u8 idata *)0x22)
#define I5C       (*(volatile u8 idata *)0x5c)
#define I5D       (*(volatile u8 idata *)0x5d)
#define IRAM(a)   (*(volatile unsigned char idata *)(a))
#define XD(a)  (*(volatile unsigned char xdata *)(a))
#define TXQ(i,f)  XB((u16)(0x013c + (u8)(i) * 6 + (f)))
#define TXQ_LINK  0   /* +0x3c next-index / active            */
#define TXQ_RETRY 1   /* +0x3d retry countdown               */
#define TXQ_TYPE  2   /* +0x3e message type                  */
#define TXQ_A3    3   /* +0x3f arg3                          */
#define TXQ_A1    4   /* +0x40 arg1                          */
#define TXQ_A2    5   /* +0x41 arg2                          */
#define TXQ1(n)    ((volatile u8 xdata *)(0x013c + (u16)(n) * 6u))

/* ============================================================================
 * @0xad - pump one RX copy + one RX-FSM step.
 * SIGCHANGE: stub `u8 (void)` -> real `void (void)`.
 * ========================================================================== */
void rx_pump_step(void)
{
    uart_tx_drain();
    uart_rx_fsm();
}


/* ============================================================================
 * @0xa9b - enqueue a fixed TX message (type=r7, arg3=6, arg1=7, arg2=*(p+1))
 * into the 20-slot x 6-byte TX queue, writing *p=0x32 first.  On empty queue
 * (count==r7) seeds record 0 (the x_txq_* staging symbols); otherwise appends
 * at record `count` and links it in, returning the new tail index.
 * SIGCHANGE: stub `u8 (void)` -> real `u8 (u8 xdata *p, u8 r7)`.
 *
 * MISSING-XDATA: TX-queue entry array records 1..19 span 0x0142..0x01AD
 * (base 0x013c + 6*idx); only record 0 (0x013a..0x0141) is named in the map.
 * Accessed inline via XB().
 * ========================================================================== */
u8 txq_enqueue_delta(u8 xdata *p, u8 r7)
{
    u8 arg2, found;

    p[0] = 0x32;
    /* IDATA 0x21=7, 0x22=p[1] in the original; only p[1] survives as data. */
    arg2 = p[1];

    if (x_txq_count == r7) {                 /* queue empty -> seed record 0 */
        x_txq_count  = 1;
        x_txq_head   = 0;
        x_txq_arg3   = 6;
        x_txq_e      = r7;
        x_txq_arg1   = 7;
        x_txq_arg2   = arg2;
        x_txq_active = 1;
        x_txq_retry  = 10;
        return 10;
    }

    if (x_txq_count < 0x14) {                 /* append at record `count` */
        u16 base = (u16)(0x013c + 6u * (u16)x_txq_count);
        XB(base + 3) = 6;                     /* arg3 */
        XB(base + 2) = r7;                    /* type */
        XB(base + 4) = 7;                     /* arg1 */
        XB(base + 5) = arg2;                  /* arg2 */
        XB(base + 1) = 10;                    /* retry */

        found = 0;
        do {                                  /* find first free (type==0) slot */
            if (XB(0x013e + 6u * (u16)found) == 0) break;
            found = (u8)(found + 1);
        } while (found != 0x14);

        XB(base + 0) = found;                 /* link */
        x_txq_count = found;
        return found;
    }

    return (u8)(x_txq_count - 0x14);
}


/* ============================================================================
 * txq_enqueue_i21 @0xaa5: enqueue a TX message into the 20-entry x 6-byte queue
 * @0x013c (same tail as FUN_0aad).  Fields: arg1=I21, arg2=I22, arg3=6 (const),
 * type=BANK0_R7.  If x_txq_count==BANK0_R7 seed entry 0 (return 10); else if
 * count<0x14 append at entry [count] and return the first free index; else
 * return count-0x14.
 * ==========================================================================*/
u8 txq_enqueue_i21(void)
{
    /* Field-for-field identical to txq_enqueue(a0,a1,a2,a3) with
     * a0=I21, a1=I22, a2=6 (const arg3), a3=BANK0_R7 (type/match key):
     * every write and branch below matches txq_enqueue's body exactly once
     * those four inputs are substituted, so this is a pure call-through. */
    return txq_enqueue(I21, I22, 6, BANK0_R7);
}


/* ============================================================================
 * txq_enqueue_i5c @0xaa9: enqueue a TX message (same tail as FUN_0aad/0aa5).
 * arg1=param_1 (R2, loaded from I5C by the sole caller FUN_15c9), arg2=param_2
 * (R3, from I5D), arg3=6 (const), type=BANK0_R7.
 * ==========================================================================*/
u8 txq_enqueue_i5c(void)
{
    /* Field-for-field identical to txq_enqueue(a0,a1,a2,a3) with
     * a0=I5C, a1=I5D, a2=6 (const arg3), a3=BANK0_R7 (type/match key):
     * every write and branch below matches txq_enqueue's body exactly once
     * those four inputs are substituted, so this is a pure call-through. */
    return txq_enqueue(I5C, I5D, 6, BANK0_R7);
}


/* ============================================================================
 * txq_enqueue @0xaad - enqueue a TX message (staged emit).
 * Stub arity (a0,a1,a2,a3) preserved.  Decompile/disasm mapping:
 *   a0 = param_1 (0x0140 arg1)   a1 = param_2 (0x0141 arg2)
 *   a2 = param_3 (0x013f arg3)   a3 = R7 (0x013e type/e, and count match key).
 * ==========================================================================*/
u8 txq_enqueue(u8 a0, u8 a1, u8 a2, u8 a3)
{
    u8 n;
    u8 idx;

    if (x_txq_count == a3) {
        x_txq_count  = 1;
        x_txq_head   = 0;
        x_txq_arg3   = a2;     /* 0x013f */
        x_txq_e      = a3;     /* 0x013e */
        x_txq_arg1   = a0;     /* 0x0140 */
        x_txq_arg2   = a1;     /* 0x0141 */
        x_txq_active = 1;      /* 0x013c */
        x_txq_retry  = 10;     /* 0x013d */
        return 10;
    }

    if (x_txq_count < 0x14) {
        n = x_txq_count;
        TXQ1(n)[3] = a2;        /* +0x3f arg3  */
        TXQ1(n)[2] = a3;        /* +0x3e type  */
        TXQ1(n)[4] = a0;        /* +0x40 arg1  */
        TXQ1(n)[5] = a1;        /* +0x41 arg2  */
        TXQ1(n)[1] = 10;        /* +0x3d retry */

        idx = 0;
        do {
            if (TXQ1(idx)[2] == 0) {   /* first slot with empty type field */
                break;
            }
            idx++;
        } while (idx != 0x14);

        TXQ1(n)[0]   = idx;     /* +0x3c active field := found index */
        x_txq_count = idx;
        return idx;
    }

    return (u8)(x_txq_count - 0x14);
}


/* ======================================================================
 *  @0x187d - UART0 / 2nd-serial bring-up (mode, break/idle bits, framing
 *  window regs 0xF5C1:F5C2, ext regs 0xF035/0xF021), then enable UART0 IRQ.
 *  Faithful.  The original's DAT_INTMEM 0x21..0x24 pre-loads are dead scratch
 *  here (FUN_1828 reads none of them; they are re-seeded per user), so omitted.
 * ==================================================================== */
u8 uart_bringup(void)
{
    sys_serial_clk_cfg(3, 1, 0);       /* mode3, source1, channel0 (UART0)          */
    uart_rx_enable(0);             /* SCON |= 0x10 (REN)                         */
    uart_frame_ctl_low(0);             /* 0xF5C0 low3 = 0                            */
    sys_pcon_mode(0);             /* PCON |= 0x80                               */
    uart_frame_win_load(0xFF, 0xE6);    /* 0xF5C1=0xE6, 0xF5C2=0xFF (framing window)  */
    uart_frame_ctl_set();              /* 0xF5C0 |= 0x80                             */
    xp_f035 = 8;
    xp_f021 = 9;
    uart_rx_int_enable(0);             /* IE |= 0x10 (UART0 RX irq enable)           */
    IP &= 0xEF;
    IE |= 0x80;                   /* EA                                        */
    return 0;
}


/* ======================================================================
 *  @0x1cc7 - drain one queued TX record from the XDATA ring (base 0x0000, slot
 *  stride via producer/consumer g_rxcopy_wr/g_rxcopy_rd) into the scratch frame at
 *  0x0100 (length) / 0x0101.. (payload), then kick the byte transmitter
 *  (FUN_1d6f).  Gated on: link bit (X_FLAGS bit3), pending data (cb!=cc), the
 *  0x0001 busy latch clear, and the 0x0134/0x0002 tx-window guard.  Faithful.
 * ==================================================================== */
u8 uart_tx_drain(void)
{
    u8 i;

    if ((((X_FLAGS >> 3) & 1) == 1) && (g_rxcopy_wr != g_rxcopy_rd) &&
        (XB(0x0001) == 0) &&
        ((x_rxring_rd == 0xFF) || (XB(0x0002) == 0))) {

        g_txframe_len = XB((u16)(g_rxcopy_rd + 3));
        if (g_txframe_len >= 0x32) {          /* over-long -> drop, reset copy cursor  */
            g_rxcopy_wr = 0;
            g_rxcopy_rd = 0;
            return 0;
        }
        g_rxcopy_rd++;
        if (g_rxcopy_rd >= 0xC8) g_rxcopy_rd = 0;

        for (i = 0; i < g_txframe_len; i++) {
            XB((u16)(0x0100 + i + 1)) = XB((u16)(g_rxcopy_rd + 3));
            g_rxcopy_rd++;
            if (g_rxcopy_rd >= 0xC8) g_rxcopy_rd = 0;
        }

        x_rxring_rd = 0;
        XB(0x0002) = 0x1E;             /* re-arm the inter-byte tx window        */
        uart_tx_byte_off(1);              /* transmit first byte                    */
        x_rxring_rd++;
    }
    return 0;
}


/* @0x1ea8 - clear the TX ack/seq scratch (0x01EF). Faithful. */
u8 uart_clr_seq(void)
{
    g_tx_seq = 0;
    return 0;
}


/* @0x1eae - set bit7 of framing-control reg 0xF5C0. Faithful. */
u8 uart_frame_ctl_set(void)
{
    g_uart_frame_ctl |= 0x80;
    return 0;
}


/* @0x1eb6 - load the 16-bit framing window (0xF5C1:0xF5C2).
 * a0 arrives in R6, a1 in R7:  0xF5C1 = a1, 0xF5C2 = a0.
 * SIGCHANGE @0x1eb6: stub (void) -> (u8 a0, u8 a1). */
u8 uart_frame_win_load(u8 a0, u8 a1)
{
    g_uart_frame_win_lo = a1;
    g_uart_frame_win_hi = a0;
    return 0;
}


/* @0x1ebf - set the low 3 bits of framing-control reg 0xF5C0 from a0.
 * SIGCHANGE @0x1ebf: stub (void) -> (u8 a0). */
u8 uart_frame_ctl_low(u8 a0)
{
    g_uart_frame_ctl = (u8)((g_uart_frame_ctl & 0xF8) | a0);
    return 0;
}


/* @0x1eca - select the TX pump source: 0x01F1 = a0 (R5), 0x01F0 = a1 (R7).
 * Faithful (stub arity matches). */
u8 uart_tx_pump_sel(u8 a0, u8 a1)
{
    x_tx_f1 = a0;
    x_tx_f0 = a1;
    return 0;
}


/* @0x1ee2 - enable receive on a channel: a0==0 -> SCON|=0x10 (UART0 REN),
 * a0==1 -> CCAP0L|=0x10 (2nd serial).  SIGCHANGE @0x1ee2: stub (void) -> (u8 a0). */
u8 uart_rx_enable(u8 chan)
{
    if (chan == 0) SCON   |= 0x10;
    if (chan == 1) CCAP0L |= 0x10;
    return 0;
}


/* =====================================================================
 * @0x1eef - enable one of two UART RX interrupts.
 * SIGCHANGE @0x1eef: stub `u8(void)` -> `void(u8)`; reads R7 (chan).
 * ===================================================================== */
void uart_rx_int_enable(u8 param_1)
{
    if (param_1 == 0) IE |= 0x10;   /* chan 0 */
    if (param_1 == 1) IE |= 0x40;   /* chan 1 */
}


/* =====================================================================
 * @0x1efc - read one UART data register (returns the byte in R7).
 * SIGCHANGE @0x1efc: stub `u8(void)` -> `u8(u8)`; reads R7 (chan), returns byte.
 * ===================================================================== */
u8 uart_read_data(u8 chan)
{
    if (chan == 0) return SBUF;      /* UART0 data   */
    if (chan == 1) return CCAP1L;    /* UART1 data   */
    return 0;
}


/* =====================================================================
 * @0x1f0b - clear bit0 (RI) of the selected UART status register.
 * SIGCHANGE @0x1f0b: stub `u8(void)` -> `void(u8)`; reads R7 (chan).
 * ===================================================================== */
void uart_clr_ri(u8 chan)
{
    if (chan == 0) SCON   &= 0xFE;
    if (chan == 1) CCAP0L &= 0xFE;
}


/* =====================================================================
 * @0x1f20 - clear bit1 (TI) of the selected UART status register.
 * SIGCHANGE @0x1f20: stub `u8(void)` -> `void(u8)`; reads R7 (chan).
 * ===================================================================== */
void uart_clr_ti(u8 chan)
{
    if (chan == 0) SCON   &= 0xFD;
    if (chan == 1) CCAP0L &= 0xFD;
}


/* =====================================================================
 * @0x1f4e - test bit0 (RI / RX-ready) of the selected UART status register.
 * SIGCHANGE @0x1f4e: stub `u8(void)` -> `u8(u8)`; reads R7 (chan), returns 0/1.
 * ===================================================================== */
u8 uart_test_ri(u8 chan)
{
    if (chan == 0) return (u8)(SCON   & 1);
    if (chan == 1) return (u8)(CCAP0L & 1);
    return 0;
}


/* =====================================================================
 * @0x1f6d - test bit1 (TI / TX-done) of the selected UART status register.
 * SIGCHANGE @0x1f6d: stub `u8(void)` -> `u8(u8)`; reads R7 (chan), returns 0/1.
 * ===================================================================== */
u8 uart_test_ti(u8 chan)
{
    if (chan == 0) return (u8)((SCON   >> 1) & 1);
    if (chan == 1) return (u8)((CCAP0L >> 1) & 1);
    return 0;
}


/* =====================================================================
 * @0x1f8c - reset the UART RX ring / frame-FSM bookkeeping.
 * XDATA 0x0000 and 0x0001 are RX-ring cells with no frozen symbol (reported).
 * ===================================================================== */
u8 rx_reset(void)
{
    x_rxring_rd = 0xFF;      /* 0x0134 */
    x_rxring_wr = 0;         /* 0x0133 */
    XB(0x0000)  = 0;         /* RX ring cell (unnamed) */
    g_rxcopy_wr      = 0;
    g_rxcopy_rd      = 0;
    X_RX_STATE  = 0;         /* 0x00cd */
    XB(0x0001)  = 0;         /* RX ring cell (unnamed) */
    X_RX_TMO    = 0;         /* 0x01ee */
    return 0;
}


/* =====================================================================
 * @0x20e9 - TX-queue retry tick: for each of 20 slots, if its retry counter
 * is non-zero decrement it, and when it reaches zero re-dispatch the slot
 * type through FUN_2145.
 * (The Ghidra params/DAT_INTMEM_22..24 are the FUN_2145 return registers, not
 *  inputs; this function takes no arguments - matches the void stub.)
 * ===================================================================== */
u8 txq_retry_tick(void)
{
    u8 i = 0;
    do {
        if (TXQ(i, TXQ_RETRY) != 0) {
            u8 r = (u8)(TXQ(i, TXQ_RETRY) - 1);
            TXQ(i, TXQ_RETRY) = r;
            if (r == 0) {
                txq_scan_pop(TXQ(i, TXQ_TYPE));
            }
        }
        i++;
    } while (i < 0x14);
    return 0;
}


/* =====================================================================
 * @0x2145 - scan the TX message queue for the first slot whose type matches
 * param_1; on hit, latch its args (x_txq_scan/g_txq_scan_arg1/g_txq_scan_arg2), clear the slot,
 * and unlink it from the head-chain.  Walks at most 20 links.
 * (Original returns a far pointer to 0x135 in R1:R2:R3; return value unused
 *  by callers -> returns 0.)  head=x_txq_head, count=x_txq_count.
 * ===================================================================== */
u8 txq_scan_pop(u8 param_1)
{
    u8 cur, prev = 0, n = 0;

    x_txq_scan = 0;
    if (x_txq_head == x_txq_count) return 0;    /* queue empty */

    cur = x_txq_head;
    for (;;) {
        if ((u8)(param_1 ^ TXQ(cur, TXQ_TYPE)) == 0) {
            u8 nxt;
            x_txq_scan = TXQ(cur, TXQ_A3);
            g_txq_scan_arg1     = TXQ(cur, TXQ_A1);
            g_txq_scan_arg2     = TXQ(cur, TXQ_A2);
            TXQ(cur, TXQ_TYPE)  = 0;
            TXQ(cur, TXQ_A3)    = 0;
            TXQ(cur, TXQ_A1)    = 0;
            TXQ(cur, TXQ_A2)    = 0;
            TXQ(cur, TXQ_RETRY) = 0;
            nxt = TXQ(cur, TXQ_LINK);
            if (x_txq_head == cur) x_txq_head = nxt;   /* pop head            */
            else TXQ(prev, TXQ_LINK) = nxt;            /* unlink mid-chain    */
            return 0;
        }
        /* no match: advance to next link */
        prev = cur;
        cur = TXQ(cur, TXQ_LINK);
        if (cur == x_txq_count) return 0;      /* reached tail sentinel */
        if (++n == 0x14) return 0;             /* safety bound          */
    }
}


/* =====================================================================
 * @0x22cd - reset/clear the whole TX message queue (head=count=0, wipe slots).
 * ===================================================================== */
u8 txq_reset(void)
{
    u8 i;
    x_txq_head  = 0;
    x_txq_count = 0;
    for (i = 0; i != 0x14; i++) {
        TXQ(i, TXQ_TYPE)  = 0;
        TXQ(i, TXQ_A3)    = 0;
        TXQ(i, TXQ_A1)    = 0;
        TXQ(i, TXQ_A2)    = 0;
        TXQ(i, TXQ_LINK)  = 0;
        TXQ(i, TXQ_RETRY) = 0;
    }
    return 0;
}


/* =====================================================================
 * @0x25c6 - copy `param_1` (a length byte) plus that many payload bytes into
 * the RX copy ring at XDATA (g_rxcopy_wr + 3), advancing g_rxcopy_wr (wrap at 200).
 * Stops when the write index catches the consumer index g_rxcopy_rd; on that
 * collision it rewinds g_rxcopy_wr to IRAM 0x53 and returns it, otherwise returns
 * the leftover count.
 * SIGCHANGE @0x25c6: stub `u8(void)` -> `u8(u8,u8)`; param_1=R5 (length),
 * param_2 the frame base seed (forwarded to FUN_01c4 which is modelled by
 * offset only, so param_2 is unused in this abstraction).
 * XDATA (g_rxcopy_wr+3) target has no frozen symbol (RX ring, reported).
 * ===================================================================== */
u8 uart_tx_stage(u8 param_1, u8 param_2)
{
    u8 n = 0;
    (void)param_2;

    for (;;) {
        if ((u8)(param_1 + 1) <= n)
            return (u8)(n - (u8)(param_1 + 1));

        if (n == 0)
            XB((u16)(g_rxcopy_wr + 3)) = param_1;
        else
            XB((u16)(g_rxcopy_wr + 3)) = frame_get_cmd((u8)(n - 1));

        g_rxcopy_wr = (u8)(g_rxcopy_wr + 1);
        if (g_rxcopy_wr > 199) g_rxcopy_wr = 0;
        if (g_rxcopy_wr == g_rxcopy_rd) break;
        n++;
    }
    g_rxcopy_wr = IRAM(0x53);
    return IRAM(0x53);
}


/* uart_tx_wake_burst @0x1f35: send four 0x00 bytes on UART0, each followed by a
 * short (0x0002-count) watchdog-guarded delay.  IDATA 0x21 is the loop counter.
 * Called from rail step 0x01 (arm rail-up). */
u8 uart_tx_wake_burst(void)
{
    IRAM(0x21) = 0;                     /* MOV 0x21,#0                         */
    do {
        uart_tx_byte(0, 0);             /* R5=0 (val), R7=0 (chan) -> SBUF=0   */
        util_delay16_wdt4(0, 2);        /* R6=0 (hi), R7=2 (lo) -> delay 2     */
        IRAM(0x21)++;                   /* INC 0x21                            */
    } while (IRAM(0x21) < 4);           /* SUBB #4; JC loop                    */
    return 0;
}


/* ==========================================================================
 * @0x2624 - decrement the XDATA 0x0002 down-counter (floor 0).
 * NOTE: XDATA 0x0002 is not a cm68_xram.h symbol (RX-ring low area) -> inline.
 * ======================================================================== */
u8 uart_txwin_dec(void)
{
    if (XD(0x0002) != 0)
        XD(0x0002)--;
    return 0;
}


/* ==========================================================================
 * @0x27d5 - if the link is up (X_FLAGS bit3), enqueue a 0x37 notify frame.
 * ======================================================================== */
u8 txq_notify_link(void)
{
    if (X_FLAGS & FLAG_LINK)
        txq_enqueue(0, 1, 0x37, 3);
    return 0;
}

