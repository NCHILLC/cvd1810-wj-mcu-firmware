/* initimg.c - startup DATA-INIT image and its RLE unpacker.
 *
 * Faithful reproduction of the original reset-time decompressor FUN_0461
 * (@CODE 0x0461; expand loop @0x04b9..0x04f9) that unpacks the packed image
 * DAT_CODE_04fb into DATA/XDATA before the main init (FUN_0795) runs.
 *
 * The SDCC port previously left this as reference-only (`#if 0` in startup.c
 * plus an uncompiled table header), so TWO things were missing from the build:
 *   1. every boot-time DATA/XDATA preset the original applies, and
 *   2. the embedded version string, which only physically exists inside
 *      DAT_CODE_04fb and therefore never made it into the CODE image at all.
 *
 * DAT_CODE_04fb is compiled here as a REAL `__code __at(0x04fb)` object (pulled
 * in from cm68_code_tables.h).  Keeping it at 0x04fb is REQUIRED, not cosmetic:
 * the version-reply path reads the NUL-terminated version string through a fixed
 * CODE far-pointer {sel 0xFF : 0x05 : 0x4E} = CODE 0x054E (see port02.c
 * frame_build_04_1a, IRAM[0x54..0x56] = {0xFF,0x05,0x4E}).  The decompressor
 * STOPS at the 0x00 control byte at table offset 82 (= CODE 0x054D); the bytes
 * that follow it are NOT decompressed - they are trailing CODE data.  The 'C'
 * (0x43) of "CVD1810-WJ_26.08.12_399" sits at table offset 83, i.e. exactly
 * 0x04FB + 83 = CODE 0x054E, only when the table is placed at its original
 * address.  So compiling the table at __at(0x04fb) is what puts the string
 * where the reply reads it.
 *
 * =====================================================================
 * DECODED PRESET LIST  (exact writes performed by DAT_CODE_04fb, in order;
 * cross-checked byte-for-byte against FUN_0461's loop logic)
 * ---------------------------------------------------------------------
 *   ctrl 0x42  XDATA[0x0274..0x0275] = 00 00     (g_t2cap_hi, g_t2cap_lo)
 *   ctrl 0x04  DATA [0x08..0x0B]     = 02 DC 6C 00
 *   ctrl 0x41  XDATA[0x0273]         = 00        (X_TICKS)
 *   ctrl 0x41  XDATA[0x026D]         = 00        (x_work_ph0)
 *   ctrl 0x41  XDATA[0x026E]         = 00        (x_work_ph1)
 *   ctrl 0x41  XDATA[0x026F]         = 00        (x_work_ph2)
 *   ctrl 0x41  XDATA[0x0276]         = 00        (X_SLEEP_ARMED)
 *   ctrl 0x41  XDATA[0x024D]         = 02        (g_volt_state)   <- nonzero
 *   ctrl 0x01  DATA [0x0C]           = 0A
 *   ctrl 0x41  XDATA[0x01EC]         = 00        (X_SLEEP_DEB)
 *   ctrl 0x41  XDATA[0x021B]         = 00        (g_gaugeX_cnt)
 *   ctrl 0x41  XDATA[0x0214]         = 00        (g_gaugeX_last)
 *   ctrl 0x41  XDATA[0x021C]         = 00        (g_gaugeY_cnt)
 *   ctrl 0x41  XDATA[0x0215]         = 00        (g_gaugeY_last)
 *   ctrl 0x41  XDATA[0x0203]         = 00        (g_gaugeC_cnt)
 *   ctrl 0x41  XDATA[0x0200]         = 00        (g_gaugeC_last)
 *   ctrl 0x41  XDATA[0x0204]         = 00        (g_gaugeD_cnt)
 *   ctrl 0x41  XDATA[0x0201]         = 00        (g_gaugeD_last)
 *   ctrl 0x41  XDATA[0x01F8]         = 00        (g_bandA_debounce)
 *   ctrl 0x41  XDATA[0x01FD]         = 00        (g_bandB_debounce)
 *   ctrl 0x00  -> TERMINATOR @ table offset 82 / CODE 0x054D
 *   (trailing, NOT decompressed) "CVD1810-WJ_26.08.12_399\0" @ CODE 0x054E
 *
 * NOTES on fidelity
 * -----------------
 *  - XDATA presets: fully faithful and effective.  Every address maps to a
 *    pinned `__at` global in cm68_xram.h, so the physical byte written here is
 *    the exact variable the port's logic later reads.  The original also
 *    blanket-zeroes XDATA 0x000..0x3FF first (FUN_0461 @0x0467), which SDCC
 *    crt0 / the __at BSS init already covers; the 18 zero writes above are thus
 *    redundant-but-faithful, and g_volt_state = 0x02 is the one live preset.
 *  - DATA (internal-RAM) presets at 0x08..0x0C are reproduced at the hardware
 *    level exactly as FUN_0461 writes them (0x08=02 0x09=DC 0x0A=6C 0x0B=00
 *    0x0C=0A).  These sit in register-bank-1 space; under the original link map
 *    they are persistent state read elsewhere.  The port models absolute IRAM
 *    the same way (see the I8()/IRAM() macros), so this is consistent; just be
 *    aware the consuming routine must read the same absolute addresses.
 *  - Control-byte forms NOT present in the 25.03.05 image (extended run-count
 *    prefix bit 0x20; 8-bit-XDATA form 0x80; bit-set/clear form 0xC0) are
 *    reproduced structurally below for completeness but are never exercised.
 *    Nothing in DAT_CODE_04fb failed to decode.
 * ===================================================================== */
#include "cm68.h"
#include <intrins.h>
#include "cm68_code_tables.h"   /* __code __at(0x04fb) const u8 DAT_CODE_04fb[124] */

/* Faithful C reproduction of FUN_0461's expand loop (@0x04b9..0x04f9).
 *
 * Control byte `b`:
 *   b == 0x00                 : end of image (0x04bd JZ).
 *   b & 0x20                  : extended - low 5 bits are the outer run count,
 *                               a second length byte becomes the inner count
 *                               (+1 unless that byte is 0).           (0x04c6)
 *   dispatch on (b & 0xC0), with CY = bit7 carried from `ADD A,A`:
 *     0x00  -> 0x047e, CY=0 : copy `inner` bytes to DATA @R0  (8-bit dest)
 *     0x80  -> 0x047e, CY=1 : copy `inner` bytes to XDATA @R0 (8-bit dest)
 *     0x40  -> 0x04d8       : block-copy outer*inner bytes to XDATA @DPTR
 *                             (16-bit dest read once, then runs consecutively)
 *     0xC0  -> 0x0490       : bit set/clear form (unused here)
 */
void initimg_apply(void)
{
    const unsigned char code *p = &DAT_CODE_04fb[0];
    unsigned char b;

    while ((b = *p++) != 0) {                 /* 0x04bc MOVC ctrl; 0x04bd JZ  */
        unsigned char inner = (unsigned char)(b & 0x3f);   /* R7 inner count  */
        unsigned char outer = 1;                            /* R6 outer count */

        if (b & 0x20) {                       /* 0x04c3 JNB ACC.5 (unused)    */
            outer = (unsigned char)(b & 0x1f);
            inner = *p++;                     /* second length byte -> R7     */
            if (inner != 0)
                outer++;
        }

        switch (b & 0xc0) {
        case 0x00:                            /* 0x047e, CY=0: DATA @R0       */
        case 0x80: {                          /* 0x047e, CY=1: XDATA @R0      */
            unsigned char dst = *p++;         /* 8-bit destination address    */
            unsigned char i;
            for (i = 0; i < inner; i++) {
                unsigned char v = *p++;
                if (b & 0x80)                 /* MOVX @R0,A (low-page XDATA)   */
                    *(volatile unsigned char xdata *)(unsigned int)dst = v;
                else                          /* MOV  @R0,A (internal RAM)     */
                    *(volatile unsigned char idata *)dst = v;
                dst++;
            }
            break;
        }
        case 0x40: {                          /* 0x04d8: XDATA block copy      */
            unsigned int dst = ((unsigned int)p[0] << 8) | p[1];  /* dhi:dlo   */
            unsigned char j;
            p += 2;                           /* address read once (0x04d8..df) */
            for (j = 0; j < outer; j++) {     /* DJNZ R6 re-enters after addr  */
                unsigned char i;
                for (i = 0; i < inner; i++)   /* DJNZ R7 inner                 */
                    *(volatile unsigned char xdata *)dst++ = *p++;
            }
            break;
        }
        default:                              /* 0xC0: bit form - never used   */
            break;
        }
    }
}
