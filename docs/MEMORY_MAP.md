# cm68 MCU - Authoritative Memory Map (Phase A contract)

Cmsemicon enhanced-8051 head-unit MCU, the stock firmware image (25.03.05 build, string `CVD1810-WJ_25.03.05_399` embedded at CODE 0x0549). Header-stripped APROM image, base 0x0000. This document is the frozen contract; all porters build against `src/cm68_xram.h`, `src/cm68_sfr.h`, `src/cm68_code_tables.h`.

- **XDATA symbols declared:** 144 (every distinct `DAT_EXTMEM_*`).
- **CODE tables:** `JT_043b` (FUN_043b action jump table, 18 handlers + default) and `DAT_CODE_04fb` (startup data-init image).
- **IDATA/register-bank:** direct addresses 0x21-0x39 and 0x4b-0x5d (below).

## 1. XDATA map (external RAM, MOVX @DPTR)

Grouped by region. `fns` = functions (by @0xADDR) that read/write the byte. Names in **bold** were fixed by the first porter and are preserved verbatim.


### RX FSM tail / TX queue / ring (0x00cb-0x0141)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0x00cb | x_00cb | 1 | rx-copy scratch pointer/index low | 0x1cc7 0x1f8c 0x25c6 |
| 0x00cc | x_00cc | 1 | rx-copy scratch pointer/index high | 0x1cc7 0x1f8c 0x25c6 |
| 0x00cd | **X_RX_STATE** | 1 | UART RX frame FSM state 0..5 | 0x18eb 0x1f8c 0x284a |
| 0x0100 | x_0100 | 1 | rx ring / copy scratch | 0x1cc7 0x1d85 |
| 0x0133 | x_rxring_wr | 1 | RX ring producer (write) index | 0x18eb 0x1d85 0x1f8c |
| 0x0134 | x_rxring_rd | 1 | RX ring consumer (read) index | 0x1cc7 0x1d85 0x1f8c |
| 0x0135 | x_txq_scan | 1 | TX-queue scan result (FUN_2145 output) | 0x2145 |
| 0x0136 | x_0136 | 1 | TX-queue scan scratch | 0x2145 |
| 0x0137 | x_0137 | 1 | TX-queue scan scratch | 0x2145 |
| 0x013a | x_txq_head | 1 | TX message queue head index | 0x0a9b 0x0aa5 0x0aa9 0x0aad 0x2145 0x22cd |
| 0x013b | x_txq_count | 1 | TX message queue count/tail (0=empty) | 0x0a9b 0x0aa5 0x0aa9 0x0aad 0x2145 0x22cd |
| 0x013c | x_txq_active | 1 | TX queue current-entry active flag | 0x0a9b 0x0aa5 0x0aa9 0x0aad |
| 0x013d | x_txq_retry | 1 | TX queue retry counter (seeded 10) | 0x0a9b 0x0aa5 0x0aa9 0x0aad |
| 0x013e | x_txq_e | 1 | TX queue staging: entry/type byte | 0x0a9b 0x0aa5 0x0aa9 0x0aad |
| 0x013f | x_txq_arg3 | 1 | TX queue staging: payload byte3 | 0x0a9b 0x0aa5 0x0aa9 0x0aad |
| 0x0140 | x_txq_arg1 | 1 | TX queue staging: payload byte1 | 0x0a9b 0x0aa5 0x0aa9 0x0aad |
| 0x0141 | x_txq_arg2 | 1 | TX queue staging: payload byte2 | 0x0a9b 0x0aa5 0x0aa9 0x0aad |

### RX frame FSM + timers/scratch (0x01b7-0x01ff)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0x01b7 | **X_RX_XOR** | 1 | RX running XOR checksum | 0x18eb |
| 0x01b8 | **X_RX_LEN** | 1 | RX payload length / write index | 0x18eb |
| 0x01b9 | **X_RX_B9** | 1 | RX length byte (ones-complement checked) | 0x18eb |
| 0x01ba | **X_RX_HDR** | 1 | RX header byte | 0x18eb |
| 0x01bb | x_rx_bb | 1 | RX FSM scratch | 0x18eb |
| 0x01bc | x_rx_bc | 1 | RX FSM scratch | 0x18eb |
| 0x01ec | **X_SLEEP_DEB** | 1 | sleep-emit debounce (fires >1; FUN_27e8) | 0x27e8 |
| 0x01ed | x_notify_pending | 1 | 0x50/0x06 notify-pending flag (set 1) | 0x1613 0x1c9f |
| 0x01ee | **X_RX_TMO** | 1 | RX inter-byte timeout (dec FUN_284a) | 0x18eb 0x1f8c 0x284a |
| 0x01ef | x_01ef | 1 | tx/ack scratch | 0x1a77 0x1ea8 |
| 0x01f0 | x_tx_f0 | 1 | TX pump index/state | 0x18ba 0x1eca |
| 0x01f1 | x_tx_f1 | 1 | TX pump index/state | 0x18ba 0x1eca 0x284a |
| 0x01f2 | x_01f2 | 1 | FUN_0b59/281d state | 0x0b59 0x281d |
| 0x01f3 | x_01f3 | 1 | FUN_0b59/281d state | 0x0b59 0x281d |
| 0x01f4 | x_01f4 | 1 | FUN_0e39/18eb/281d shared flag | 0x0e39 0x18eb 0x281d |
| 0x01f5 | x_01f5 | 1 | FUN_2b93 block | 0x2b93 |
| 0x01f6 | x_01f6 | 1 | FUN_2b93 block | 0x2b93 |
| 0x01f7 | x_01f7 | 1 | FUN_2b93 block | 0x2b93 |
| 0x01f8 | x_01f8 | 1 | FUN_2b93 block | 0x2b93 |
| 0x01f9 | x_01f9 | 1 | FUN_2b93 block | 0x2b93 |
| 0x01fa | x_01fa | 1 | FUN_2d51 block | 0x2d51 |
| 0x01fb | x_01fb | 1 | FUN_2d51 block | 0x2d51 |
| 0x01fc | x_01fc | 1 | FUN_2d51 block | 0x2d51 |
| 0x01fd | x_01fd | 1 | FUN_2d51 block | 0x2d51 |
| 0x01fe | x_01fe | 1 | FUN_2d51 block | 0x2d51 |
| 0x01ff | x_01ff | 1 | FUN_14a1 scratch | 0x14a1 |

### Sub-system state blocks (0x0200-0x0221)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0x0200 | x_0200 | 1 | FUN_2a29 state | 0x2a29 |
| 0x0201 | x_0201 | 1 | FUN_2ab3 state | 0x2ab3 |
| 0x0202 | x_0202 | 1 | FUN_2b61/2b93 state | 0x2b61 0x2b93 |
| 0x0203 | x_0203 | 1 | FUN_2a29 state | 0x2a29 |
| 0x0204 | x_0204 | 1 | FUN_2ab3 state | 0x2ab3 |
| 0x0205 | x_0205 | 1 | shared config/state (0b59,1c9f,2516,28f1,298d) | 0x0b59 0x1c9f 0x2516 0x28f1 0x298d |
| 0x0206 | x_0206 | 1 | FUN_0b59 state | 0x0b59 |
| 0x0207 | x_0207 | 1 | shared (1c39,1c9f,2863,28f1) | 0x1c39 0x1c9f 0x2863 0x28f1 |
| 0x0208 | x_0208 | 1 | shared (1c39,1c9f,2863,298d) | 0x1c39 0x1c9f 0x2863 0x298d |
| 0x0209 | x_0209 | 1 | FUN_0b59/1c39 state | 0x0b59 0x1c39 |
| 0x020a | x_020a | 1 | FUN_0b59/1c39 state | 0x0b59 0x1c39 |
| 0x020b | x_020b | 1 | 0x50/0x06 stored param (FUN_1613) | 0x0b59 0x1613 |
| 0x020c | x_020c | 1 | FUN_2b61/2d51 state | 0x2b61 0x2d51 |
| 0x020d | x_020d | 1 | FUN_14a1/2ee7/2f0e state | 0x14a1 0x2ee7 0x2f0e |
| 0x020e | x_020e | 1 | FUN_0b59 state | 0x0b59 |
| 0x020f | x_020f | 1 | FUN_0b59 state | 0x0b59 |
| 0x0210 | x_0210 | 1 | FUN_14a1/2ee7 state | 0x14a1 0x2ee7 |
| 0x0211 | x_0211 | 1 | FUN_14a1/15c9/2ee7 state | 0x14a1 0x15c9 0x2ee7 |
| 0x0212 | x_0212 | 1 | FUN_14a1/15c9/2ee7/2f0e state | 0x14a1 0x15c9 0x2ee7 0x2f0e |
| 0x0213 | x_0213 | 1 | FUN_14a1/15c9/2ee7/2f0e state | 0x14a1 0x15c9 0x2ee7 0x2f0e |
| 0x0214 | x_0214 | 1 | FUN_28f1 state | 0x28f1 |
| 0x0215 | x_0215 | 1 | FUN_298d state | 0x298d |
| 0x0216 | x_0216 | 1 | FUN_1c39/2863/28f1 state | 0x1c39 0x2863 0x28f1 |
| 0x0217 | x_0217 | 1 | FUN_1c39/2863/298d state | 0x1c39 0x2863 0x298d |
| 0x0218 | x_0218 | 1 | FUN_1413/14a1/2ee7/2f0e state | 0x1413 0x14a1 0x2ee7 0x2f0e 0xthunk_1413 |
| 0x0219 | x_0219 | 1 | FUN_15c9/2728 state | 0x15c9 0x2728 |
| 0x021a | x_021a | 1 | FUN_15c9/2728 state | 0x15c9 0x2728 |
| 0x021b | x_021b | 1 | FUN_28f1 state | 0x28f1 |
| 0x021c | x_021c | 1 | FUN_298d state | 0x298d |
| 0x021d | x_021d | 1 | FUN_14a1/2ee7/2f0e state | 0x14a1 0x2ee7 0x2f0e |
| 0x021e | x_021e | 1 | FUN_2b61/2b93 state | 0x2b61 0x2b93 |
| 0x021f | x_021f | 1 | FUN_2b61/2d51 state | 0x2b61 0x2d51 |
| 0x0220 | x_0220 | 1 | FUN_1c39/2863 state | 0x1c39 0x2863 |
| 0x0221 | x_0221 | 1 | FUN_1c39/2863 state | 0x1c39 0x2863 |

### Config / ACC-arbiter / power state (0x024a-0x0268)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0x024a | x_024a | 1 | FUN_0624/07a5/2f2c state | 0x0624 0x07a5 0x2f2c |
| 0x024b | x_024b | 1 | FUN_0624/07a5 state | 0x0624 0x07a5 |
| 0x024c | x_024c | 1 | FUN_0cc9 state | 0x0cc9 |
| 0x024d | x_024d | 1 | FUN_0cc9 state | 0x0cc9 |
| 0x024e | x_024e | 1 | FUN_0cc9/247f/24ce (LE word lo with 0x024f) | 0x0cc9 0x247f 0x24ce |
| 0x024f | x_024f | 1 | FUN_0cc9/247f/24ce (LE word hi) | 0x0cc9 0x247f 0x24ce |
| 0x0250 | x_0250 | 1 | FUN_24ce init state | 0x24ce |
| 0x0251 | x_0251 | 1 | FUN_07b9/18ba TX-ready flag | 0x07b9 0x18ba |
| 0x0252 | x_0252 | 1 | FUN_24ce/25a2/25b2 state | 0x24ce 0x25a2 0x25b2 |
| 0x0253 | **X_ARB_STATE** | 1 | ACC-arbiter sub-state 0/2/3 | 0x2239 0x24ce |
| 0x0254 | **X_ARB_ACTION** | 1 | ACC-arbiter action code -> FUN_043b jumptable | 0x009d 0x0899 0x2239 0x24ce 0x25a2 |
| 0x0255 | **X_POFF_TMO** | 1 | poweroff-timeout seed (LE u16 lo with 0x0256) | 0x1613 0x24ce |
| 0x0256 | **X_POFF_CNT** | 1 | poweroff-timeout counter (LE u16 hi) | 0x1613 0x24ce |
| 0x0257 | x_0257 | 1 | FUN_1613/2b61/2b93/2d51 (LE word lo with 0x0258) | 0x1613 0x2b61 0x2b93 0x2d51 |
| 0x0258 | x_0258 | 1 | FUN_1613/2b61/2b93/2d51 (LE word hi) | 0x1613 0x2b61 0x2b93 0x2d51 |
| 0x0259 | x_0259 | 1 | FUN_0a5d/0a61/0fc3/24ce (LE word lo with 0x025a) | 0x0a5d 0x0a61 0x0fc3 0x24ce |
| 0x025a | x_025a | 1 | FUN_0a5d/0a61/0fc3/24ce (LE word hi) | 0x0a5d 0x0a61 0x0fc3 0x24ce |
| 0x025b | x_025b | 1 | FUN_1613/2b61 state | 0x1613 0x2b61 |
| 0x025c | x_025c | 1 | FUN_1613/2b61 state | 0x1613 0x2b61 |
| 0x025d | x_025d | 1 | FUN_0a6a/0a6c/0a70/0fc3 state | 0x0a6a 0x0a6c 0x0a70 0x0fc3 |
| 0x025e | **X_ACC_VALID** | 1 | ACC mode validity (normally 0) | 0x0624 0x0702 0x070e 0x07d0 0x14a1 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 0x2ee7 |
| 0x025f | **X_ACC_MODE** | 1 | ACC/power mode 0..3 (3=ACC OFF) | 0x0624 0x0702 0x070e 0x07d0 0x14a1 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 0x2ee7 |
| 0x0260 | x_0260 | 1 | FUN_0cc9/247f/24ce (LE word lo with 0x0261) | 0x0cc9 0x247f 0x24ce |
| 0x0261 | x_0261 | 1 | FUN_0cc9/247f/24ce (LE word hi) | 0x0cc9 0x247f 0x24ce |
| 0x0262 | **X_ACC_PREV_V** | 1 | previous X_ACC_VALID (edge detect) | 0x1613 0x2b61 |
| 0x0263 | **X_ACC_PREV_M** | 1 | previous X_ACC_MODE (edge detect) | 0x1613 0x2b61 |
| 0x0264 | x_0264 | 1 | FUN_1613/2b61 state | 0x1613 0x2b61 |
| 0x0265 | x_0265 | 1 | FUN_1613/2b61 state | 0x1613 0x2b61 |
| 0x0266 | **X_FLAGS** | 1 | bit0 rail-en, bit2 present, bit3 link, bit4 KEEP-ALIVE | 0x0624 0x07a5 0x07d0 0x0a6a 0x0a6c 0x0a70 0x0b59 0x0cc9 0x0e39 0x0fbb 0x0fc3 0x1613 0x1a59 0x1cc7 0x2239 0x24ce 0x2524 0x2728 0x27d5 0x27e8 0x28f1 0x298d 0x2a29 0x2ab3 |
| 0x0267 | x_0267 | 1 | FUN_24ce init state | 0x24ce |
| 0x0268 | x_0268 | 1 | FUN_24ce init state | 0x24ce |

### Work scheduler + edge latches (0x026d-0x0276)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0x026d | x_work_ph0 | 1 | FUN_0f24 phase-0 sub-tick counter (mod 5) | 0x0f24 |
| 0x026e | x_work_ph1 | 1 | FUN_0f24 phase-1 sub-tick counter (mod 10) | 0x0f24 |
| 0x026f | x_work_ph2 | 1 | FUN_0f24 phase-2 sub-tick counter (mod 10) | 0x0f24 |
| 0x0270 | **X_WORK** | 1 | work-request bitfield (set by FUN_208c tick) | 0x0f24 0x208c |
| 0x0271 | x_0271 | 1 | FUN_0cc9/1a77/1b04/241c (LE word lo with 0x0272) | 0x0cc9 0x1a77 0x1b04 0x241c |
| 0x0272 | x_0272 | 1 | FUN_0cc9/1a77/1b04/241c (LE word hi) | 0x0cc9 0x1a77 0x1b04 0x241c |
| 0x0273 | **X_TICKS** | 1 | Timer0 sub-tick counter 0..0x26 | 0x208c |
| 0x0274 | x_0274 | 1 | FUN_1413 state | 0x1413 0xthunk_1413 |
| 0x0275 | x_0275 | 1 | FUN_1413 state | 0x1413 0xthunk_1413 |
| 0x0276 | **X_SLEEP_ARMED** | 1 | 1 => power-down arm request (FUN_241c) | 0x1613 0x1bc8 0x241c 0x27e8 0x2b61 |

### CMS extended-peripheral MOVX window (0xF000+)

| Addr | Name | Size | Purpose | Referencing fns |
|------|------|------|---------|-----------------|
| 0xf000 | xp_f000 | 1 | CMS extended-peripheral reg (FUN_07d0) | 0x07d0 |
| 0xf002 | xp_f002 | 1 | ext-periph reg (FUN_0587 power-down seq) | 0x0587 |
| 0xf003 | xp_f003 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf004 | xp_f004 | 1 | ext-periph reg (FUN_2342) | 0x2342 |
| 0xf005 | xp_f005 | 1 | ext-periph reg (FUN_2342) | 0x2342 |
| 0xf013 | xp_f013 | 1 | ext-periph reg (FUN_2342) | 0x2342 |
| 0xf014 | xp_f014 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf015 | xp_f015 | 1 | ext-periph reg (FUN_0773/2342) | 0x0773 0x2342 |
| 0xf016 | xp_f016 | 1 | ext-periph reg (FUN_1795/2b4f) | 0x1795 0x2b4f |
| 0xf017 | xp_f017 | 1 | ext-periph reg (FUN_17a7/2b3d) | 0x17a7 0x2b3d |
| 0xf021 | xp_f021 | 1 | ext-periph reg (FUN_187d) | 0x187d |
| 0xf022 | xp_f022 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf023 | xp_f023 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf024 | xp_f024 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf025 | xp_f025 | 1 | ext-periph reg (FUN_07d0/133a/241c) | 0x07d0 0x133a 0x241c 0xthunk_133a |
| 0xf026 | xp_f026 | 1 | ext-periph reg (FUN_0587) | 0x0587 |
| 0xf02a | xp_f02a | 1 | ext-periph reg (FUN_133a) | 0x133a 0xthunk_133a |
| 0xf031 | xp_f031 | 1 | ext-periph reg (FUN_179e/2b58) | 0x179e 0x2b58 |
| 0xf032 | xp_f032 | 1 | ext-periph reg (FUN_17db/2b46) | 0x17db 0x2b46 |
| 0xf035 | xp_f035 | 1 | ext-periph reg (FUN_187d) | 0x187d |
| 0xf036 | xp_f036 | 1 | ext-periph reg (FUN_2342) | 0x2342 |
| 0xf0c8 | xp_f0c8 | 1 | ext-periph reg (FUN_133a) | 0x133a 0xthunk_133a |
| 0xf5c0 | xp_f5c0 | 1 | ext-periph reg (FUN_1eae/1ebf) | 0x1eae 0x1ebf |
| 0xf5c1 | xp_f5c1 | 1 | ext-periph reg (FUN_1eb6, LE word lo w/ 0xf5c2) | 0x1eb6 |
| 0xf5c2 | xp_f5c2 | 1 | ext-periph reg (FUN_1eb6, LE word hi) | 0x1eb6 |
| 0xf692 | xp_f692 | 1 | ext-periph reg (FUN_0819) | 0x0819 |

**Notes on multi-byte fields (declared as individual bytes, no overlap):** 0x0255:0x0256 (poweroff-timeout, LE u16, via FUN_0258 16-bit add), plus suspected LE word pairs 0x024e:24f, 0x0257:258, 0x0259:25a, 0x0260:261, 0x0271:272, 0xf5c1:5c2.

**RX ring (no single __at symbol):** a 50-slot inbound ring lives at XDATA 0x0000.. with base ~0x0032, addressed via computed pointers in FUN_18eb/1d85/1f8c; only its producer/consumer indices (0x0133/0x0134) and 0x0100 scratch surface as literals.

## 2. IDATA / register-bank (direct addressing)

Internal RAM direct addresses. 0x00-0x1f = register banks 0-3 (8051 std). 0x20-0x2f = bit-addressable byte area (heavily used as bit flags + working vars). 0x30-0x5d = general IDATA / stack region (startup sets SP=0x5D). Counts are decompile references; these are compiler-allocated locals/globals, not a fixed contract, but the ranges are reserved.

| Range | Use | Notable (ref count) |
|-------|-----|---------------------|
| 0x21-0x39 | bit-flag + working bytes (0x20-0x2f bit-addressable) | 0x21(55), 0x27(48), 0x23(37), 0x25(35), 0x26(28), 0x22(26) |
| 0x4b-0x5d | scratch / stacked locals | 0x4b(25), 0x4c(21), 0x4d(16), 0x4e(23), 0x4f(8), 0x50(8), 0x51(7), 0x52(10), 0x53(9), 0x54(2), 0x55(7), 0x56(2), 0x57(2), 0x58(4), 0x59(5), 0x5a(5), 0x5b(5), 0x5c(1), 0x5d(1) |
| SP | 0x81 = 0x5D at reset (FUN_0461) | stack grows from 0x5E up |

## 3. CODE-space tables

### 3.1 FUN_043b action jump table  (CODE 0x08a0, len 58)

`FUN_043b` (0x043b) is called via `LCALL 0x043b`; **sole caller FUN_0899 @0x089d** loads `A = X_ARB_ACTION` (XDATA 0x0254) then `LCALL 0x043b`. The routine does `POP DPH/DPL` to recover the return address (0x08a0) as the table base, then linear-searches 3-byte entries `{addr_hi, addr_lo, match_code}` for `code == A` and `JMP @A+DPTR` to `addr_hi:addr_lo`. A `{0,0}` terminator is followed by a 2-byte DEFAULT handler address.

This is a **rail power-sequencer state machine**: each handler advances `X_ARB_ACTION` (0x0254) to the next action code and toggles P3.6 (the SoC rail).

| Action code | Handler | P3.6 effect | Next X_ARB_ACTION | Notes |
|-------------|---------|-------------|-------------------|-------|
| 0x00 | 0x08da | **CLR P3.6 -> rail CUT** | 0x0f | also X_ARB_STATE:=0, LCALL 0x257c |
| 0x01 | 0x0932 | - | 0x05 | ACALL 0x0a70/0x0a5d, LCALL 0x1f35 |
| 0x02 | 0x0957 | - | 0x03/0x06 | branch; ACALL 0x0a5d |
| 0x03 | 0x0975 | - | 0x02 | ACALL 0x0a50 |
| 0x04 | 0x0922 | - | - | reads X_FLAGS; ACALL 0x0aad (emit) |
| 0x05 | 0x0943 | **SETB P3.6 -> rail UP** | 0x02 | ACALL 0x0a50 |
| 0x06 | 0x0903 | **CLR P3.6 -> rail CUT** | 0x07 | ACALL 0x0a50 |
| 0x07 | 0x0915 | - | 0x01 | ACALL 0x0a50 |
| 0x08 | 0x098c | - | 0x0d | LCALL 0x0095; ACALL 0x0a61 |
| 0x0d | 0x099b | - | 0x0f | ACALL 0x0a50; clears bit (CLR 0x85) |
| 0x10 | 0x09aa | - | 0x11 | ACALL 0x0a70/0x0a61 |
| 0x11 | 0x09ba | - | 0x12 | ACALL 0x0a50/0x0a61 |
| 0x12 | 0x09cd | - | 0x14/0x13 | branch; ACALL 0x0fbb/0x0a5d |
| 0x13 | 0x09f2 | **CLR P3.6 -> rail CUT** | 0x15 | ACALL 0x0a50 |
| 0x14 | 0x09ff | - | 0x12 | ACALL 0x0a50 |
| 0x15 | 0x0a17 | - | 0x16 | - |
| 0x16 | 0x0a23 | **CLR P3.6 -> rail CUT** | 0x17 | ACALL 0x0a50/0x0fbb/0x0a5d; reads X_FLAGS |
| 0x17 | 0x0a3b | - | - | ACALL 0x0a50/0x0882; LCALL 0x10f0; ACALL 0x0e04 |
| default | 0x0a4f | - | - | taken when no code matches (e.g. action 0x0f, which has NO table entry) |

**Rail-critical decode (the P3.6 / power-off answer):** action codes **0x00, 0x06, 0x13, 0x16 CUT the rail (CLR P3.6)**; **0x05 raises it (SETB P3.6)**. The keep-alive guard must gate entry into 0x00/0x06/0x13/0x16 (or force 0x05). Raw P3.6 sites in the image: CLR@0x08dc,0x0909,0x09f6,0x0a32 (these handlers) + SETB@0x094f (0x05); plus three CLR P3.6 OUTSIDE the table at 0x0df2, 0x1151, 0x2373(FUN_2342).

> Caveat: handler bodies were decoded from raw image bytes (the linear disasm is misaligned in 0x08a0-0x0a4f because it decoded the inline table as code). Entry P3.6 ops and the `X_ARB_ACTION:=code` writes are byte-exact; the 'Next' column for branchy handlers (0x02,0x12) lists both arms.

Byte dump (CODE 0x08a0):
```
08 da 00 09 32 01 09 57 02 09 75 03
09 22 04 09 43 05 09 03 06 09 15 07
09 8c 08 09 9b 0d 09 aa 10 09 ba 11
09 cd 12 09 f2 13 09 ff 14 0a 17 15
0a 23 16 0a 3b 17 00 00 0a 4f
```

### 3.2 DAT_CODE_04fb - startup data-init image  (CODE 0x04fb, len 124)

Consumed by the crt0 copy loop: `FUN_0461` -> `@0x04b6 MOV DPTR,#0x4fb` then a record loop (`@0x04b9`: read control byte, `JZ` end). Record form observed `{flag, addr_hi, addr_lo, value}` where flag 0x41/0x42 selects DATA vs XDATA store; it zero-inits many of the XDATA state vars in section 1 (0x0273, 0x026d-0x0276, 0x01ec, 0x0203, 0x0214, 0x021b, 0x021c, 0x0215, 0x01f8, 0x01fd, ...). The firmware version ASCII `CVD1810-WJ_25.03.05_399` is embedded at 0x0549. The table ends at 0x0576; FUN_0577 code begins at 0x0577. (PORT.md notes SDCC's own crt0 substitutes for this loop; the bytes are preserved here for reference/verification.)

Byte dump (CODE 0x04fb):
```
42 02 74 00 00 04 08 02 dc 6c 00 41
02 73 00 41 02 6d 00 41 02 6e 00 41
02 6f 00 41 02 76 00 41 02 4d 02 01
0c 0a 41 01 ec 00 41 02 1b 00 41 02
14 00 41 02 1c 00 41 02 15 00 41 02
03 00 41 02 00 00 41 02 04 00 41 02
01 00 41 01 f8 00 41 01 fd 00 00 43
56 44 31 38 31 30 2d 57 4a 5f 32 35
2e 30 33 2e 30 35 5f 33 39 39 00 41
00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00
```

## 4. Unresolved / purpose-unclear addresses

Declared by address (function xref known, semantic role not pinned down):

0x00cb, 0x00cc, 0x01ef, 0x01f2, 0x01f3, 0x01f4, 0x01f5, 0x01f6, 0x01f7, 0x01f8, 0x01f9, 0x01fa, 0x01fb, 0x01fc, 0x01fd, 0x01fe, 0x01ff, 0x0200, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0206, 0x0207, 0x0208, 0x0209, 0x020a, 0x020c, 0x020d, 0x020e, 0x020f, 0x0210, 0x0211, 0x0212, 0x0213, 0x0214, 0x0215, 0x0216, 0x0217, 0x0218, 0x0219, 0x021a, 0x021b, 0x021c, 0x021d, 0x021e, 0x021f, 0x0220, 0x0221, 0x024a, 0x024b, 0x024c, 0x024d, 0x024e, 0x024f, 0x0250, 0x0252, 0x0257, 0x0258, 0x0259, 0x025a, 0x025b, 0x025c, 0x025d, 0x0260, 0x0261, 0x0264, 0x0265, 0x0267, 0x0268, 0x026d, 0x026e, 0x026f, 0x0271, 0x0272, 0x0274, 0x0275.

The entire **0xF000+ block (28 regs)** is the CMS extended-peripheral MOVX window (clock/power/GPIO-mux/analog); individual register semantics are not documented by the vendor and were left as `xp_fXXX` by address, each tagged with its accessing functions. 0xf002/f003/f014/f022/f023/f024/f026 cluster in the FUN_0587 power-down sequence; 0xf025 is touched by the sleep path (FUN_07d0/241c).

