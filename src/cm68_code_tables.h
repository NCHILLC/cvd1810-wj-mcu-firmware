/* cm68_code_tables.h - CODE-space constant / jump tables for the cm68 MCU.
 *
 * All bytes below are the ACTUAL image bytes extracted from re/cm68_image.bin
 * (base 0x0000).  This file is part of the frozen memory contract: porters wire
 * FUN_043b action handlers from JT_043b[] and treat DAT_CODE_04fb as the startup
 * data-init image.  `code` = MOVC/CODE space (not writable at runtime).
 *
 * KEIL PLACEMENT NOTE
 * -------------------
 * SDCC used `__code __at(0xNNNN)` to pin these tables.  Keil C51 rejects an
 * absolute specifier on an *initialized* code object (error C274), so the two
 * arrays are declared as ordinary `code` constants and located by the BL51
 * linker instead.  DAT_CODE_04fb is DECLARED FIRST here so it leads this TU's
 * const segment `?CO?INITIMG`; the linker directive
 *     CODE (?CO?INITIMG (0x04FB))
 * (see cm68.uvproj Lx51 MiscControls / the build recipe) then places
 * DAT_CODE_04fb at CODE 0x04FB, which puts the embedded version string
 * "CVD1810-..." at table offset 83 = CODE 0x054E exactly where the reply path
 * MOVC-reads it via the fixed far-pointer {0xFF:0x05:0x4E}.  JT_043b follows in
 * the same segment; nothing reads it by absolute address in the C port (the
 * handlers are reconstructed as functions in leaf.c), so its exact placement is
 * non-critical.
 */
#ifndef CM68_CODE_TABLES_H
#define CM68_CODE_TABLES_H

/* =====================================================================
 * DAT_CODE_04fb  -  STARTUP DATA-INIT IMAGE   (declared first => segment lead)
 * ---------------------------------------------------------------------
 * Read by the crt0 copy loop (FUN_0461 -> loop @0x04b6: MOV DPTR,#0x4fb).
 * Record-based init of DATA/IDATA/XDATA; a control byte of 0x00 ends it.
 * The decompressor TERMINATES at the 0x00 control byte at table offset 82
 * (= CODE 0x054D).  Everything after it is trailing CODE data, NOT unpacked:
 * the firmware version string "CVD1810-WJ_26.08.12_399\0" begins at table
 * offset 83 = CODE 0x054E (its 'C'=0x43), which is exactly where the reply
 * path MOVC-reads it via the fixed far-pointer {0xFF:0x05:0x4E} (port02.c).
 * See src/initimg.c for the full decoded preset list + faithful unpacker.
 * Version string now set to CVD1810-WJ_26.08.12_399 (our build, derived from the 25.03.05 image).  124 bytes, CODE 0x04fb: */
const unsigned char code DAT_CODE_04fb[124] = {
    0x42, 0x02, 0x74, 0x00, 0x00, 0x04, 0x08, 0x02, 0xdc, 0x6c, 0x00, 0x41,
    0x02, 0x73, 0x00, 0x41, 0x02, 0x6d, 0x00, 0x41, 0x02, 0x6e, 0x00, 0x41,
    0x02, 0x6f, 0x00, 0x41, 0x02, 0x76, 0x00, 0x41, 0x02, 0x4d, 0x02, 0x01,
    0x0c, 0x0a, 0x41, 0x01, 0xec, 0x00, 0x41, 0x02, 0x1b, 0x00, 0x41, 0x02,
    0x14, 0x00, 0x41, 0x02, 0x1c, 0x00, 0x41, 0x02, 0x15, 0x00, 0x41, 0x02,
    0x03, 0x00, 0x41, 0x02, 0x00, 0x00, 0x41, 0x02, 0x04, 0x00, 0x41, 0x02,
    0x01, 0x00, 0x41, 0x01, 0xf8, 0x00, 0x41, 0x01, 0xfd, 0x00, 0x00, 0x43,
    0x56, 0x44, 0x31, 0x38, 0x31, 0x30, 0x2d, 0x57, 0x4a, 0x5f, 0x32, 0x36,
    0x2e, 0x30, 0x38, 0x2e, 0x31, 0x32, 0x5f, 0x33, 0x39, 0x39, 0x00, 0x41,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

/* =====================================================================
 * FUN_043b ACTION JUMP TABLE  (original inline at CODE 0x08a0)
 * ---------------------------------------------------------------------
 * Sole caller: FUN_0899 @0x089d  ->  A = X_ARB_ACTION (XDATA 0x0254);
 *              LCALL 0x043b.  FUN_043b does POP DPH/DPL to recover the
 *              table base, linear-searches 3-byte entries {addr_hi, addr_lo,
 *              match_code} for code==A, then JMP @A+DPTR.  A {0,0} terminator
 *              is followed by a 2-byte DEFAULT handler address (0x0a4f).
 * Raw table bytes (58): */
const unsigned char code JT_043b[58] = {
    0x08, 0xda, 0x00, 0x09, 0x32, 0x01, 0x09, 0x57, 0x02, 0x09, 0x75, 0x03,
    0x09, 0x22, 0x04, 0x09, 0x43, 0x05, 0x09, 0x03, 0x06, 0x09, 0x15, 0x07,
    0x09, 0x8c, 0x08, 0x09, 0x9b, 0x0d, 0x09, 0xaa, 0x10, 0x09, 0xba, 0x11,
    0x09, 0xcd, 0x12, 0x09, 0xf2, 0x13, 0x09, 0xff, 0x14, 0x0a, 0x17, 0x15,
    0x0a, 0x23, 0x16, 0x0a, 0x3b, 0x17, 0x00, 0x00, 0x0a, 0x4f,
};

/* Decoded {code -> handler} for wiring in C (mirror of JT_043b, for porters
 * who reconstruct the handlers as functions).  Handler addr is CODE space.
 * ('code' is a Keil reserved word, so the field is named act_code.) */
struct jt043b_ent { unsigned char act_code; unsigned int handler; };
static const struct jt043b_ent code JT_043b_DECODED[] = {
    { 0x00, 0x08da },
    { 0x01, 0x0932 },
    { 0x02, 0x0957 },
    { 0x03, 0x0975 },
    { 0x04, 0x0922 },
    { 0x05, 0x0943 },
    { 0x06, 0x0903 },
    { 0x07, 0x0915 },
    { 0x08, 0x098c },
    { 0x0d, 0x099b },
    { 0x10, 0x09aa },
    { 0x11, 0x09ba },
    { 0x12, 0x09cd },
    { 0x13, 0x09f2 },
    { 0x14, 0x09ff },
    { 0x15, 0x0a17 },
    { 0x16, 0x0a23 },
    { 0x17, 0x0a3b },
    /* default */ { 0xff, 0x0a4f },
};

#endif /* CM68_CODE_TABLES_H */
