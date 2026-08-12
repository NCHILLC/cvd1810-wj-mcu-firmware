/* frame.c - TX/RX frame-buffer accessors and frame builders.  Moved verbatim
 * (pure code motion, no logic changes) from leaf.c/port02.c/port03.c/port04.c
 * during the Phase-C file reorganization: every frame_* function.
 */
#include "cm68.h"
#include "cm68_stubs.h"

#define IRAM(a)   (*(volatile unsigned char idata *)(a))
#define CELL32(base)  ( ((u32)IRAM(base)       << 24) \
                      | ((u32)IRAM((base) + 1) << 16) \
                      | ((u32)IRAM((base) + 2) <<  8) \
                      |  (u32)IRAM((base) + 3) )
#define I8(a)  (*(volatile unsigned char idata *)(a))

/* @0x1b04 - build the "0x37 sub-3" message: type=0x37, sub=3, byte7=X_0272,
 * terminator 3. Faithful message stream. */
u8 frame_build_37sub3(u8 base_lo, u8 base_hi, u8 base_sel)
{
    (void)base_lo; (void)base_hi; (void)base_sel;   /* frame ptr -> frozen X_RXBUF_BASE base */
    frame_set(0x37, 5);
    frame_set(3, 6);
    /* frame[7] = low byte (R7) of the scaled vmin, 24a2(R6=X_0271, R7=X_0272) */
    frame_set(util_scale_adc_down(g_vmin_hi, g_vmin_lo), 7);
    frame_set(3, 3);
    return 0;
}


/* @0x1b3a - build the "0x37 sub-2" message; byte7 carries a sleep/PCA sample
 * merged into the high nibble.
 * NOTE: FUN_1bc8 is now widened to the full 12-bit sample V (R6:R7); the
 * original merges it as byte7 = (R6<<4)|(R7>>4) = (u8)(V>>4). */
u8 frame_build_37sub2(u8 base_lo, u8 base_hi, u8 base_sel)
{
    (void)base_lo; (void)base_hi; (void)base_sel;
    frame_set(0x37, 5);
    frame_set(2, 6);
    frame_set((u8)(pwr_sleep_sample(7) >> 4), 7);  /* faithful (R6<<4)|(R7>>4) */
    frame_set(3, 3);
    return 0;
}


/* frame_build_04_1a @0x1b73: build the "0x04/0x1a" message.
 * frame[5]=0x04 (type), frame[6]=0x01 (sub), frame[3]=0x1A (terminator).
 * Dest region is frame+7 (24-bit ptr {a2:a1:a0}+7):
 *   - FUN_1fae zero-fills 30 bytes there (memset, fill=R5=0, count=IRAM[0x55]).
 *   - FUN_2642 copies the NUL-terminated code string at {0xFF:0x05:0x4E}=0x054E
 *     to that dest (src ptr in IRAM[0x54..0x56]).
 * Params: a0=R1 base low, a1=R2 base high, a2=R3 near/far selector.
 * hi7 = base high after the +7 low-byte carry (disasm 1b8d-1b8f). */
u8 frame_build_04_1a(u8 base_lo, u8 base_hi, u8 base_sel)
{
    u8 hi7 = (u8)(base_hi + ((base_lo > 0xF8) ? 1u : 0u));   /* carry out of base_lo+7 */

    frame_set(4, 5);          /* frame[5] = 0x04 */
    frame_set(1, 6);          /* frame[6] = 0x01 */

    /* memset(frame+7, 0, 30): IRAM[0x55]=count, ptr {base_sel:hi7:base_lo+7}, fill=0. */
    IRAM(0x55) = 0x1E;
    frame_memset((u8)(base_lo + 7), hi7, base_sel, 0);

    /* strcpy(frame+7, code@0x054E): src ptr = {sel 0xFF, high 0x05, low 0x4E}. */
    IRAM(0x54) = 0xFF;
    IRAM(0x55) = 0x05;
    IRAM(0x56) = 0x4E;
    frame_strcpy((u8)(base_lo + 7), hi7, base_sel);

    frame_set(0x1A, 3);       /* frame[3] = 0x1A */
    return 0;
}


/* frame_build_send @0x1de6: assemble and send a framed message.
 * Frame layout (offsets into the base pointer {a2:a1:a0}):
 *   [0]=0x55  [1]=0xA0  [2]=seq(a3)  [3]=length(L)  [4]=~L
 *   [len+4]=XOR checksum  ->  FUN_25c6 transmits (L+5) bytes.
 * The XOR checksum (IRAM 0x52) covers L+2 bytes starting at offset 2, i.e.
 * frame offsets 2..L+3 = {seq, L, ~L, payload...}.
 * Returns 1 on send, 0 when L > 0x23 (over-long abort).
 *
 * Frozen-leaf mapping (see file-top note): the real base pointer is idata@0x23
 * with selector R3=0, but leaf.c models the whole frame as near XDATA at
 * X_RXBUF_BASE.  To keep the frame coherent every accessor uses that near
 * model:
 *   - FUN_01f1 uses mode=1 (near XDATA path, base=X_RXBUF_BASE) so the
 *     frame[3]+=1 increment lands in the same buffer FUN_01c4(3) reads back.
 *   - The original walks a 24-bit cursor (IRAM 0x4f:0x50:0x51 seeded at base+2,
 *     advanced +1 per byte by FUN_042b) and reads it via FUN_01ab; the frozen
 *     FUN_01ab reads by offset, so the walk is expressed directly as the
 *     offsets 2..L+3 -> the checksum VALUE is byte-exact within this model.
 *   - FUN_25c6's return (R7) is discarded by the original (1ea5: MOV R7,#1);
 *     this returns the literal 1.
 * Params: a0=R1 base low, a1=R2 base high, a2=R3 selector, a3=R5 sequence. */
u8 frame_build_send(u8 base_lo, u8 base_hi, u8 base_sel, u8 seq)
{
    u8 len, i, xorsum;
    (void)base_lo; (void)base_hi; (void)base_sel;     /* frame ptr -> frozen X_RXBUF_BASE base */

    xorsum = 0;                        /* IRAM 0x52 running XOR                  */
    frame_put(0x55, 0);            /* frame[0] = 0x55 (MCU->SoC sync)        */
    frame_set(0xA0, 1);            /* frame[1] = 0xA0                        */
    frame_set(seq, 2);             /* frame[2] = sequence byte               */
    math_addbyte_mode(1, X_RXBUF_BASE, 3, 0, 1);  /* frame[3] += 1 (near XDATA path) */

    len = frame_get_cmd(3);            /* length field (post-increment)          */
    if (len > 0x23)                    /* over-long -> abort, return 0           */
        return 0;
    frame_set((u8)~len, 4);        /* frame[4] = ~length                     */

    /* XOR of frame offsets 2..len+3 (len+2 bytes); cursor seeds at base+2. */
    for (i = 0; i < (u8)(len + 2); i++)
        xorsum ^= frame_get_off((u8)(2 + i));

    frame_set(xorsum, (u8)(len + 4));  /* checksum trailer at offset len+4   */

    uart_tx_stage((u8)(len + 5), 0);   /* transmit len+5 bytes (base unused)     */
    return 1;                          /* original loads R7=#1, ignoring 25c6    */
}


/* =====================================================================
 * frame_memset @0x1fae: fill IRAM[0x55] bytes with param_4 through the
 * 24-bit frame pointer.
 * SIGCHANGE @0x1fae: stub `u8(void)` -> `void(u8,u8,u8,u8)`; args R1,R2,R3,R5.
 *
 * Frame pointer in IRAM 0x51..0x53:
 *   [0x51] = space (R3): 1=XDATA(hi:lo), 0=IDATA(lo), 0xFE=paged-XDATA(lo),
 *            else CODE (write is a no-op in FUN_0224)
 *   [0x52] = high (R2 / DPH)
 *   [0x53] = low  (R1 / DPL)
 * Each pass: FUN_042b post-increments the 16-bit (hi:lo) and returns the
 * pre-increment pointer in R1:R2:R3, at which FUN_0224 stores param_4.  The
 * leaf FUN_0224(val,off) model is fixed-base (X_RXBUF_BASE) and cannot carry
 * an arbitrary advancing pointer, so the store is inlined here per the space
 * selector to stay byte-faithful; the FUN_042b leaf model (0x53 += 1, 0x52 +=
 * carry) is faithful and kept.  REPORT: inlined FUN_0224 store.
 * ===================================================================== */
void frame_memset(u8 ptr_lo, u8 ptr_hi, u8 space_sel, u8 fill_val)
{
    IRAM(0x51) = space_sel;
    IRAM(0x52) = ptr_hi;
    IRAM(0x53) = ptr_lo;
    if (((ptr_lo | ptr_hi | space_sel) != 0) && (IRAM(0x55) != 0)) {
        for (; IRAM(0x55) != 0; IRAM(0x55)--) {
            u8 sp     = IRAM(0x51);         /* space selector (R3) */
            u8 old_lo = IRAM(0x53);         /* pre-increment low  (R1) */
            u8 old_hi = IRAM(0x52);         /* pre-increment high (R2) */
            math_add16_idata(0, 1, (u8 idata *)0x51);   /* [0x52:0x53] += 1 */
            /* FUN_0224: store fill_val at the pre-increment pointer */
            if (sp == 1)         *(u8 xdata *)(((u16)old_hi << 8) | old_lo) = fill_val;
            else if (sp == 0)    IRAM(old_lo) = fill_val;
            else if (sp == 0xFE) *(u8 xdata *)(u16)old_lo = fill_val;  /* current XDATA page */
            /* else: CODE space -> FUN_0224 performs no store */
        }
    }
}


/* =====================================================================
 * frame_pack_be32 @0x2376: pack 4 frame bytes into a 32-bit big-endian word at
 * XDATA 0x269.  Reads offsets 0..3 of the frame (base 0x28:0x27:0x26 =
 * param_1:param_2:param_3) via FUN_01c4, stages each byte zero-extended into
 * IRAM cells 0x2a/0x2e/0x32/0x36, then assembles
 *   word = (b0<<24) | (b1<<16) | (b2<<8) | b3
 * and stores it big-endian to XDATA 0x269..0x26c (FUN_041f, tail call).
 * SIGCHANGE @0x2376: stub `u8(void)` -> `void(u8,u8,u8)`; args R1,R2,R3.
 * PARTIAL @0x2376: FUN_0185 is an opaque block-copy dispatcher (returns
 * param_3 in the port model) whose dest region 0x2a..0x39 is fully overwritten
 * by the loop below, so its data effect here is nil; its R1/R2/R4 copy regs
 * (0x67,5,0) have no parameter in the 5-arg prototype.  The frozen FUN_01c4
 * near-model reads X_RXBUF_BASE+off (frame base 0x28:0x27:0x26 dropped).
 * ===================================================================== */
void frame_pack_be32(u8 frame_base_hi, u8 frame_base_mid, u8 frame_base_lo)
{
    u8 b0, b1, b2, b3;

    IRAM(0x26) = frame_base_lo;   /* R3 */
    IRAM(0x27) = frame_base_mid;  /* R2 */
    IRAM(0x28) = frame_base_hi;   /* R1 */

    /* NOTE: the original also ran FUN_0185(R0=0x2a, R3=0xff, R5=0, R6=0,
     * R7=0x10) here. With dst_mode=0xff, util_block_copy's col selector
     * ((u8)(0xff + 2) == 1) hits the "no writable destination thunk" case
     * and returns before touching memory or any global (see util_block_copy,
     * port00.c) -- the call is a provable no-op, so it is dropped.
     *
     * The original also staged each fetched byte into a scratch IRAM cell
     * (0x2a/0x2e/0x32/0x36) and re-read it back through CELL32() purely to
     * zero-extend it to 32 bits before shifting; those cells are not read
     * by any other function, so the round-trip is equivalent to just using
     * the fetched byte directly. */
    b0 = frame_get_cmd(0);
    b1 = frame_get_cmd(1);
    b2 = frame_get_cmd(2);
    b3 = frame_get_cmd(3);

    /* word = (b0<<24)|(b1<<16)|(b2<<8)|b3, so word>>24==b0, >>16==b1, >>8==b2,
     * and word itself (truncated to u8) == b3 - store the bytes directly and
     * skip materializing the 32-bit word. */
    math_store4_xdata((u8 xdata *)0x269, b0, b1, b2, b3);
}


/* ==========================================================================
 * @0x2642 - copy a NUL-terminated byte string from the frame source pointer
 * (INTMEM 0x54:0x55:0x56, pre-set by the caller) to the destination pointer
 * passed in, appending a terminating 0.  Near-model: pointer low byte = frame
 * offset (see leaf.c 01ab/0224).
 * SIGCHANGE @0x2642: stubs.h `u8 frame_strcpy(void)` -> void(u8,u8,u8);
 *   the single call site passes a 3-byte destination pointer.
 * ======================================================================== */
void frame_strcpy(u8 dst_memtype, u8 dst_hi, u8 dst_lo)
{
    I8(0x51) = dst_lo;           /* dest ptr low  */
    I8(0x52) = dst_hi;           /* dest ptr high */
    I8(0x53) = dst_memtype;      /* dest ptr memtype */
    I8(0x57) = I8(0x54);         /* src ptr (copied from persistent 54:55:56) */
    I8(0x58) = I8(0x55);
    I8(0x59) = I8(0x56);

    for (;;) {
        /* Read src byte from the full 24-bit pointer {sel:high:low} = {0x57:0x58:0x59}.
         * sel 0xFF = CODE (MOVC) - this is how the version string @0x054E is read;
         * else near XDATA (MOVX).  The near-only model missed the code-space case. */
        u16 saddr = (u16)(((u16)I8(0x58) << 8) | I8(0x59));
        u8 b = (I8(0x57) == 0xFF) ? *((u8 code *)saddr)
                                  : *((volatile u8 xdata *)saddr);
        if (b == 0)
            break;
        frame_put(b, I8(0x51));          /* write to dest (low offset)  */
        if (++I8(0x51) == 0) I8(0x52)++;     /* 16-bit inc dest             */
        if (++I8(0x59) == 0) I8(0x58)++;     /* 16-bit inc src              */
    }
    frame_put(0, I8(0x51));              /* NUL terminator              */
}


/* ==========================================================================
 * frame_build_counter @0x2687: split a 32-bit XDATA accumulator into a frame.
 *   Frame base pointer arrives in R1:R2:R3 and is saved to 0x4d:0x4c:0x4b.
 *   The number is a big-endian 32-bit accumulator at XDATA 0x269..0x26c
 *   (0x269 = MSB), pre-loaded by the caller.  R4:R7 (Ghidra "param_4..param_7")
 *   are overwritten by the 0x0407 load at entry and carry no input.
 *   Sequence: header (offset 5 = 5, offset 6 = 1); acc += 0x14 in place; then
 *   4 iterations, each emitting acc's LSB to frame offset (cnt-1) and shifting
 *   acc right 8 bits, which lays the 4 accumulator bytes big-endian into frame
 *   offsets 0..3 (offset 0 = MSB); finally offset 3 = 6.  This is a raw 4-byte
 *   split, not BCD digit extraction.
 *   Helpers (faithful u32 model in port00.c): 0x0407 loads BE32 from XDATA,
 *   0x041f stores 4 bytes big-endian, 0x03e1 logical-shifts-right by `count`.
 * ======================================================================== */
u8 frame_build_counter(u8 base_memtype, u8 base_hi, u8 base_lo)
{
    u16 s;

    I8(0x4b) = base_lo;          /* R3: frame ptr low     */
    I8(0x4c) = base_hi;          /* R2: frame ptr high    */
    I8(0x4d) = base_memtype;     /* R1: frame ptr memtype */

    frame_set(5, 5);
    frame_set(1, 6);

    /* Big-endian 32-bit counter at XDATA 0x269 (0x269=MSB..0x26c=LSB): add 0x14
     * byte-wise (carry chain), then lay its 4 bytes big-endian into frame
     * offsets 0..3 and clear the counter. On this 8-bit core this avoids the
     * u32 load/shift/store runtime helpers the faithful model pulled in. */
    s = (u16)XB(0x26c) + 0x14;
    XB(0x26c) = (u8)s;
    if (s > 0xFF)
        if (++XB(0x26b) == 0)
            if (++XB(0x26a) == 0)
                ++XB(0x269);

    frame_set(XB(0x269), 0);     /* MSB @ offset 0 */
    frame_set(XB(0x26a), 1);
    frame_set(XB(0x26b), 2);
    frame_set(XB(0x26c), 3);     /* LSB @ offset 3 (overwritten by the 6 below) */
    XB(0x269) = 0; XB(0x26a) = 0; XB(0x26b) = 0; XB(0x26c) = 0;

    frame_set(6, 3);
    return 0;
}


/* ==========================================================================
 * @0x26f9 - render a small fixed frame (marker + one field cleared).
 * ======================================================================== */
u8 frame_build_small(u8 base_memtype, u8 base_hi, u8 base_lo)
{
    (void)base_hi;
    I8(0x4b) = base_lo;
    I8(0x4c) = base_hi;
    I8(0x4d) = base_memtype;

    frame_set(3, 5);
    frame_set(1, 6);
    frame_put(0, (u8)(base_memtype + 7));
    frame_set(3, 3);
    return 0;
}


/* ==========================================================================
 * @0x2728 - build the command frame for a UI/state message.  Gated on
 * X_FLAGS bit2 (peer present).  The two operands arrive in R4/R5 (the 4th/5th
 * call arguments); a0..a2 (=0x23,0,0 at the call site) are the frame id.
 * ======================================================================== */
u8 frame_build_ui(u8 base_lo, u8 base_hi, u8 base_sel, u8 kind, u8 val)
{
    (void)base_lo; (void)base_hi; (void)base_sel;
    I8(0x4b) = kind;
    I8(0x4c) = val;

    if (!(X_FLAGS & FLAG_PRESENT))
        return 0;

    frame_set(6, 5);
    frame_set(1, 6);

    if (kind == 2 || kind == 4 || kind == 5 || kind == 1) {
        /* LAB @0x2761 */
        frame_set(kind, 7);
        frame_set(0, 8);
        frame_set(val, 9);
        frame_set(1, 10);
        if (val == 0xff) {
            frame_set(0, 9);
            frame_set(0, 10);
        }
    } else if (kind == 7) {
        /* LAB @0x278e */
        frame_set(kind, 7);
        frame_set(0, 8);
        frame_set(val, 9);
        frame_set(1, 10);
    } else if (kind == 0x0a) {
        /* LAB @0x27aa */
        frame_set(kind, 7);
        frame_set(g_decode_out_hi, 8);
        frame_set(g_decode_out_lo, 9);
        frame_set(val, 10);
    }

    frame_set(6, 3);
    return 0;
}


/* ==========================================================================
 * @0x281d - build+optionally transmit a status frame.
 * ======================================================================== */
u8 frame_build_status(void)
{
    /* @0x281d call site sets R0=0x21 R1=0x66 R2=5 R3=0xff R4=0 R5=0 R6=0 R7=1.
     * With dst_mode=0xff, util_block_copy's col selector ((u8)(0xff+2)==1)
     * hits the "no writable destination thunk" case and returns before
     * touching memory or any global (see util_block_copy, port00.c) -- this
     * call is a provable no-op, so it is dropped. */
    if (g_frame02_seen != 1 && (g_alert_lo_a != 0 || g_alert_lo_b != 0))
        uart_tx_byte(I8(0x21), 0);
    return 0;
}


/* ---- Frame-buffer accessors ------------------------------------------ *
 * The original helpers take a base pointer in registers plus a byte offset
 * and a near/far mode selector.  On the 0x50 command path the base is the
 * received-payload buffer at XDATA 0x00BA, so we model the common (near)
 * case: byte at X_RXBUF_BASE + off.  See PORT.md "frame accessors".
 */
/* @0x1ab / @0x1c4 - read one payload byte. */
u8 frame_get_off(u8 off) { return XB(X_RXBUF_BASE + off); }


u8 frame_get_cmd(u8 off) { return XB(X_RXBUF_BASE + off); }


/* @0x26e - read payload byte at off+1. */
u8 frame_get_next(u8 off) { return XB(X_RXBUF_BASE + off + 1); }


/* @0x224 / @0x236 - write one payload byte. */
void frame_put(u8 val, u8 off) { XB(X_RXBUF_BASE + off) = val; }


void frame_set(u8 val, u8 off) { XB(X_RXBUF_BASE + off) = val; }


/* @0x1a59 - build+send one periodic status frame. Faithful skeleton. */
void frame_build_periodic(u8 param)
{
    s8 c;
    txq_scan_pop(3);
    c = (s8)frame_get_off(0);
    if ((c != 0) && ((X_FLAGS >> 3) & 1)) {
        u8 v = frame_get_next(1);
        frame_route_status(param, v, c);
    }
}


/* @0x1a77 - route a status sub-frame by type. Faithful. */
void frame_route_status(u8 arg_a, u8 arg_b, u8 kind)
{
    frame_put(arg_a, 0x21);   /* stash operands (modelled) */
    frame_put(arg_b, 0x22);
    if (kind == 3) {
        frame_build_small(0x23, 0, 0);
    } else if (kind == 5) {
        frame_build_counter(0x23, 0, 0);
    } else if (kind == 6) {
        frame_build_ui(0x23, 0, 0, arg_a, arg_b);
    } else if (kind == '7') {    /* 0x37 */
        if (arg_b == 1 && arg_a == 0) {
            u8 b = (u8)~XB(0x0271);
            if (b == 0) b = (u8)~XB(0x0272);
            if (b == 0) return;
            frame_build_37sub3(0x23, 0, 0);
            XB(0x0271) = 0xFF; XB(0x0272) = 0xFF;
        } else if (arg_b == 2 && arg_a == 0) {
            frame_build_37sub2(0x23, 0, 0);
        }
    } else if (kind == 4) {
        frame_build_04_1a(0x23, 0, 0);
    } else {
        return;
    }
    XB(0x01EF)++;
    frame_build_send(0x23, 0, 0, XB(0x01EF));
}

