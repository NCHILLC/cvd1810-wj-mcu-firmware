/* cm68_sfr.h - SFR map for the cm68 head-unit MCU (Cmsemicon enhanced 8051).
 *
 * Addresses recovered from cm68_image.bin by cross-checking the Ghidra
 * decompile against the raw opcodes (MOV/ANL/ORL direct,#imm).  Where the
 * decompile only gave a symbolic name, the byte address printed after the
 * name was confirmed from the machine code (the confirming instruction is
 * noted in a comment).  A few purely-standard 8051 SFRs are taken from the
 * MCS-51 core map and are marked "std".
 *
 * Build target: Keil C51 (re-derived from SDCC src).
 */
#ifndef CM68_SFR_H
#define CM68_SFR_H

/* ---- Standard MCS-51 core SFRs (std) ---------------------------------- */
sfr P0 = 0x80;
sfr SP = 0x81;         /* startup sets SP=0x5D (FUN_0461)            */
sfr DPL = 0x82;
sfr DPH = 0x83;
sfr PCON = 0x87;       /* confirmed: ORL PCON,#0x80 @0x1ed8          */
sfr TCON = 0x88;
sfr TMOD = 0x89;
sfr TL0 = 0x8A;
sfr TL1 = 0x8B;
sfr TH0 = 0x8C;
sfr TH1 = 0x8D;
sfr P1 = 0x90;
sfr SCON = 0x98;
sfr SBUF = 0x99;       /* UART0 data; confirmed MOV 0x99,R5 @0x1d78  */
sfr P2 = 0xA0;
sfr IE = 0xA8;         /* confirmed: ORL IE @0x5c7                    */
sfr P3 = 0xB0;
sfr IP = 0xB8;
sfr PSW = 0xD0;
sfr ACC = 0xE0;
sfr B = 0xF0;

/* ---- Cmsemicon-specific SFRs (address confirmed from opcodes) --------- */
sfr HKEY = 0x96;       /* flash/WDT unlock key: write 0xAA then 0x55 (FUN_05e4) */
sfr HADDR = 0x97;      /* watchdog/ISP trigger: ORL 0x97,#1 after key (FUN_05e4) */
sfr SFR_9A = 0x9A;     /* aux peripheral ctrl: ORL 0x9A,#1 (FUN_07d0), ANL #0xEF/ORL #0x20 (FUN_2342) */
sfr HIE = 0xA1;        /* confirmed: ANL 0xA1,#0xBF (FUN_1795), ORL 0xA1,#0x20 (FUN_0773) */
sfr FIE = 0xA2;        /* confirmed: ORL 0xA2,#0x20 (FUN_07d0)        */
sfr FIE1 = 0xA3;       /* confirmed: ORL 0xA3,#0x40 (FUN_2342), ANL 0xA3,#0xFD (FUN_179e) */
sfr P4 = 0xB4;         /* confirmed: ANL A,0xB4 (FUN_0ff6)            */
sfr P5 = 0xB5;         /* confirmed: ANL A,0xB5 (FUN_0ff6)            */
sfr P6 = 0xB6;         /* confirmed: ANL A,0xB6 (FUN_0ff6). NOTE: this
                                 is the port byte 0xB6, NOT bit P3.6. The SoC
                                 rail bit lives inside one of these ports.   */
sfr IPH0 = 0xB7;       /* interrupt priority high (std position)      */
sfr SFR_BA = 0xBA;     /* ANL 0xBA,#0xDF / ORL 0xBA,#0x20 (FUN_05b0)  */
sfr T2CON = 0xC8;      /* ANL 0xC8,#0xFC (FUN_007d)                   */
sfr SFR_C9 = 0xC9;     /* MOV 0xC9,#0x7F (FUN_0065)                   */
sfr SFR_CE = 0xCE;     /* ANL 0xCE,#... backlight/PWM mux (FUN_15fa)  */
sfr HPCON = 0xCF;      /* confirmed: ORL 0xCF,#0x80 (FUN_004d), ANL 0xCF,#0x7F (FUN_0075) */

/* PCA block (std MCS-51 PCA positions; used here for timing + 2nd serial) */
sfr CCON = 0xD8;
sfr CMOD = 0xD9;
sfr CCAPM0 = 0xDA;     /* PCA capture-mode 0: MOV 0xDA (FUN_12d3)      */
sfr CCAPM1 = 0xDB;     /* PCA capture-mode 1: MOV 0xDB (FUN_12d3)      */
sfr CCAPM2 = 0xDC;
sfr CCAPM3 = 0xDD;
sfr CCAPM4 = 0xDE;     /* confirmed: ANL 0xDE,#0x8F (FUN_0577)        */
sfr PCON1 = 0xDF;      /* confirmed: ANL 0xDF,#0xBF (FUN_0577), ORL 0xDF,#2 power-down */
sfr CCAP0L = 0xEA;     /* PCA capture0 low / 2nd-UART SCON/status: RI=bit0 TI=bit1 (FUN_1828/1ee2/1f0b/1f4e) */
sfr CCAP1L = 0xEB;     /* 2nd UART / PCA1 data: MOV 0xEB,R5 (FUN_1d7a) */

/* ---- Shared "new" SFRs promoted from the port batches (byte-exact from the
 * raw opcodes in re/cm68_disasm.txt).  Kept here once so no port TU declares a
 * duplicate; __sfr declarations create no linker symbols.                     */
sfr SFR_8E = 0x8E;     /* timer-clock/aux + PCA-ish ctrl: ANL/ORL 0x8E (FUN_12f6/1ff3) */
sfr SFR_91 = 0x91;     /* serial-mode aux ctrl: ANL/ORL 0x91 (FUN_1828) */
sfr EIE = 0xAA;        /* secondary int-enable: ORL 0xAA,#1/#2 (FUN_12b6) */
sfr RCAP2L = 0xCA;     /* Timer2 reload low  (FUN_1381)                */
sfr RCAP2H = 0xCB;     /* Timer2 reload high (FUN_1381)                */
sfr TL2 = 0xCC;        /* Timer2 low  (FUN_1381)                       */
sfr TH2 = 0xCD;        /* Timer2 high (FUN_1381)                       */
sfr SOFL = 0xD2;       /* 2nd-serial mode reg: ANL/ORL 0xD2 (FUN_1239/12f6) */
sfr RXSTAT = 0xE2;     /* 2nd-UART status: MOV 0xE2 (FUN_12d3)         */
sfr RXDAT = 0xE3;      /* 2nd-UART data:   MOV 0xE3 (FUN_12d3)         */
/* T2MOD is SFR 0xC9 == the existing SFR_C9 symbol (FUN_17e4/17fd/1819).       */

/* Extended interrupt/peripheral-enable bank 0xAC..0xAF, indexed 0..3 by a
 * selector.  Confirmed as DIRECT-BYTE registers (ANL/ORL direct,A) by the
 * paired bit set/clear helpers FUN_0821 (ORL) and FUN_0860 (ANL ~mask).  These
 * byte SFRs are distinct hardware from the IE bits at the same nibble.         */
sfr SFR_AC = 0xAC;
sfr SFR_AD = 0xAD;     /* byte SFR (distinct from the IE.5 bit at 0xAD) */
sfr SFR_AE = 0xAE;
sfr SFR_AF = 0xAF;     /* byte SFR (distinct from the EA/IE.7 bit at 0xAF) */
sfr SFR_B9 = 0xB9;     /* aux ctrl: ANL 0xB9,#0xFD / ORL 0xB9,#2 (FUN_083f) */
sfr SFR_BC = 0xBC;     /* poff/PWM window: MOV 0xBC,R5 (FUN_122c)        */
sfr SFR_BD = 0xBD;     /* poff/PWM ctrl: ANL/ORL 0xBD (FUN_0035/002e/11f6/122c/243a-region) */

/* ---- Bit SFRs used by the port ---------------------------------------- */
/* IE */
sbit EA = 0xAF;
sbit SFR_IE_AD = 0xAD; /* ET2-ish; CLR/ SETB in FUN_006d             */
sbit ET0 = 0xA9;       /* IE.1 timer0 enable (FUN_12b6)              */
sbit ET1 = 0xAB;       /* IE.3 timer1 enable (FUN_12b6)              */
/* P0 (0x80) */
sbit P0_0 = 0x80;
sbit P0_5 = 0x85;
/* P1 (0x90) */
sbit P1_3 = 0x93;
sbit P1_5 = 0x95;
sbit P1_6 = 0x96;
sbit P1_7 = 0x97;
/* P2 (0xA0) */
sbit P2_5 = 0xA5;
/* P3 (0xB0) - standard MCS-51 alternate-function bit names.               */
sbit TXD = 0xB1;       /* P3.1 */
sbit INT0 = 0xB2;      /* P3.2 */
sbit P3_6 = 0xB6;      /* P3.6 == std "WR". THE SoC POWER-RAIL BIT.
                                 CLR P3.6 cuts the head-unit rail; the sleep
                                 fix must keep this HIGH on ACC-off.        */
sbit RD = 0xB7;        /* P3.7 */
/* NOTE: byte SFR 0xB6 (P6) and bit 0xB6 (P3.6) are distinct hardware:
 *   - "P6"  (byte, MOV/ANL direct 0xB6) is an extra CMS port, read by FUN_0ff6.
 *   - "P3_6"(bit, SETB/CLR 0xB6) is bit 6 of P3 at 0x0B0 - the rail control. */

#endif /* CM68_SFR_H */
