# SHA-3 Wishbone Peripheral (`sha3_wb`)

A memory-mapped Wishbone B4 peripheral that wraps a Keccak/SHA-3 IP core.
Supports SHA3-224, SHA3-256, SHA3-384, and SHA3-512 via a software-selectable
mode bit-field. Padding is **length-driven**: firmware programs the expected
message byte count before starting; the core handles all domain-suffix and
`pad10*1` padding automatically.

---

## Register Map

All offsets are relative to the peripheral base address (`SHA3_BASE`).
Registers are 32-bit wide; the Wishbone decoder uses `Address[5:2]` to select
the register, so offset increments are 4 bytes.

| Offset | Name            | Access | Description                                     |
|--------|-----------------|--------|-------------------------------------------------|
| `0x00` | `CONTROL`       | R/W    | Start, abort/reset, mode select                 |
| `0x04` | `STATUS`        | R      | FSM state, FIFO flags, error latches            |
| `0x08` | `IN_FIFO_DATA`  | W      | Push one 32-bit word into the input FIFO        |
| `0x0C` | `IN_FIFO_LEVEL` | R      | Words currently in the input FIFO (0–64)        |
| `0x10` | `OUT_FIFO_DATA` | R      | Pop one 32-bit digest word from the output FIFO |
| `0x14` | `OUT_FIFO_LVL`  | R      | Words currently in the output FIFO (0–16)       |
| `0x18` | `MSG_LEN_LO`    | R/W    | Low 32 bits of message length in bytes          |
| `0x1C` | `MSG_LEN_HI`    | R/W    | High 32 bits of message length in bytes         |

---

## Register Bit-Field Definitions

### `CONTROL` — `0x00` (R/W, reset `0x0000_0000`)

| Bits  | Name     | Access | Description                                                                                     |
|-------|----------|--------|-------------------------------------------------------------------------------------------------|
| `0`   | `START`  | W      | Write `1` to transition IDLE → ABSORB. Auto-clears after one cycle.                            |
| `1`   | —        | —      | Reserved.                                                                                       |
| `2`   | `ABORT`  | W      | Write `1` to reset FSM, flush both FIFOs, and clear Keccak state. Auto-clears.                 |
| `4:3` | `MODE`   | R/W    | Hash variant: `00` = SHA3-224, `01` = SHA3-256, `10` = SHA3-384, `11` = SHA3-512.              |
| `31:5`| —        | —      | Reserved, write zero.                                                                           |

> **Note:** Writing `MODE` while `BUSY=1` is silently ignored and sets `ERR_ILL` in `STATUS`.

---

### `STATUS` — `0x04` (R, reset `0x0000_0011`)

| Bits    | Name       | Description                                                                    |
|---------|------------|--------------------------------------------------------------------------------|
| `0`     | `IDLE`     | FSM is in `S_IDLE` — no message in progress.                                  |
| `1`     | `BUSY`     | FSM is in `S_ABSORB` or `S_WAIT_HASH` — absorption or permutation in progress.|
| `2`     | `DONE`     | FSM is in `S_DONE` — digest is loaded in the output FIFO and ready to read.   |
| `3`     | —          | Reserved.                                                                      |
| `4`     | `IN_EMPTY` | Input FIFO is empty.                                                           |
| `5`     | `IN_FULL`  | Input FIFO is full (64 words). Further writes stall the Wishbone bus.          |
| `6`     | `OUT_EMPTY`| Output FIFO is empty.                                                          |
| `7`     | `OUT_FULL` | Output FIFO is full (16 words).                                                |
| `8`     | `ERR_ILL`  | Illegal register write attempted while `BUSY=1` (e.g., mode change).          |
| `9`     | `ERR_UF`   | Output FIFO read attempted while empty (`ERR_FIFO_UNDERFLOW`).                |
| `10`    | `ERR_OF`   | Input FIFO write attempted while full (`ERR_FIFO_OVERFLOW`).                  |
| `31:11` | —          | Reserved.                                                                      |

---

### `IN_FIFO_DATA` — `0x08` (W)

Each 32-bit write pushes one word into the input FIFO. The peripheral counts
ingested bytes internally and compares against `{MSG_LEN_HI, MSG_LEN_LO}` to
determine when to trigger padding and finalization.

- If the FIFO is **full**, the Wishbone bus **stalls** (ACK is withheld) until
  space is available — no data is lost.
- A write while full that *would* be rejected instead asserts `ERR_OF`.

---

### `IN_FIFO_LEVEL` — `0x0C` (R)

Number of 32-bit words currently queued in the input FIFO. Range `0`–`64`
(default depth; parameterizable at synthesis time).

---

### `OUT_FIFO_DATA` — `0x10` (R)

Each 32-bit read pops one digest word from the output FIFO (oldest word first,
little-endian byte order). Reading when `OUT_EMPTY=1` sets `ERR_UF`.

Digest word counts by mode:

| MODE  | Algorithm | Digest bits | Words to read |
|-------|-----------|-------------|---------------|
| `00`  | SHA3-224  | 224         | 7             |
| `01`  | SHA3-256  | 256         | 8             |
| `10`  | SHA3-384  | 384         | 12            |
| `11`  | SHA3-512  | 512         | 16            |

---

### `OUT_FIFO_LVL` — `0x14` (R)

Number of digest words currently available in the output FIFO. Range `0`–`16`.
When this value equals the digest word count for the selected mode and
`BUSY=0`, the `DONE` flag in `STATUS` is set.

---

### `MSG_LEN_LO` / `MSG_LEN_HI` — `0x18` / `0x1C` (R/W)

64-bit message length in **bytes**. Must be set before asserting `START`.
The core uses this value to:

1. Determine when absorption is complete.
2. Compute the number of valid bytes in the final (potentially partial) word.
3. Trigger automatic SHA-3 padding (`0x06` domain suffix + `pad10*1`).

Write `MSG_LEN_LO` first, then `MSG_LEN_HI`. Both registers are cleared on
`ABORT` or system reset.

---

## C Header — Memory-Mapped Register Access

The base address below is a placeholder; update it to match your SoC address
map before use.

```c
/* sha3_wb.h — Memory-mapped register definitions for the SHA-3 Wishbone peripheral */
#ifndef SHA3_WB_H
#define SHA3_WB_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Base address — update to match your SoC address map
 * ------------------------------------------------------------------------- */
#define SHA3_BASE           0x80001600UL

/* -------------------------------------------------------------------------
 * Register access macros
 * Each macro expands to a volatile 32-bit lvalue that can be read or written
 * directly, e.g.:  SHA3_CONTROL = SHA3_CTRL_START;
 * ------------------------------------------------------------------------- */
#define SHA3_CONTROL        (*(volatile uint32_t *)(SHA3_BASE + 0x00))
#define SHA3_STATUS         (*(volatile uint32_t *)(SHA3_BASE + 0x04))
#define SHA3_IN_FIFO_DATA   (*(volatile uint32_t *)(SHA3_BASE + 0x08))
#define SHA3_IN_FIFO_LEVEL  (*(volatile uint32_t *)(SHA3_BASE + 0x0C))
#define SHA3_OUT_FIFO_DATA  (*(volatile uint32_t *)(SHA3_BASE + 0x10))
#define SHA3_OUT_FIFO_LVL   (*(volatile uint32_t *)(SHA3_BASE + 0x14))
#define SHA3_MSG_LEN_LO     (*(volatile uint32_t *)(SHA3_BASE + 0x18))
#define SHA3_MSG_LEN_HI     (*(volatile uint32_t *)(SHA3_BASE + 0x1C))

/* -------------------------------------------------------------------------
 * CONTROL register bit-fields  (0x00)
 * ------------------------------------------------------------------------- */
#define SHA3_CTRL_START         (1u << 0)   /* W  — begin absorption; auto-clears     */
#define SHA3_CTRL_ABORT         (1u << 2)   /* W  — reset core + FIFOs; auto-clears   */
#define SHA3_CTRL_MODE_MASK     (3u << 3)   /* RW — hash variant select               */
#define SHA3_CTRL_MODE_SHA3_224 (0u << 3)   /* 00 — SHA3-224  (7 output words)        */
#define SHA3_CTRL_MODE_SHA3_256 (1u << 3)   /* 01 — SHA3-256  (8 output words)        */
#define SHA3_CTRL_MODE_SHA3_384 (2u << 3)   /* 10 — SHA3-384 (12 output words)        */
#define SHA3_CTRL_MODE_SHA3_512 (3u << 3)   /* 11 — SHA3-512 (16 output words)        */

/* -------------------------------------------------------------------------
 * STATUS register bit-fields  (0x04)
 * ------------------------------------------------------------------------- */
#define SHA3_ST_IDLE            (1u << 0)   /* FSM idle, no message in progress       */
#define SHA3_ST_BUSY            (1u << 1)   /* Absorbing or waiting on permutation    */
#define SHA3_ST_DONE            (1u << 2)   /* Digest ready in output FIFO            */
#define SHA3_ST_IN_EMPTY        (1u << 4)   /* Input FIFO empty                       */
#define SHA3_ST_IN_FULL         (1u << 5)   /* Input FIFO full (64 words)             */
#define SHA3_ST_OUT_EMPTY       (1u << 6)   /* Output FIFO empty                      */
#define SHA3_ST_OUT_FULL        (1u << 7)   /* Output FIFO full (16 words)            */
#define SHA3_ST_ERR_ILL         (1u << 8)   /* Illegal write while BUSY               */
#define SHA3_ST_ERR_UF          (1u << 9)   /* Output FIFO underflow                  */
#define SHA3_ST_ERR_OF          (1u << 10)  /* Input FIFO overflow                    */
#define SHA3_ST_ERR_MASK        (SHA3_ST_ERR_ILL | SHA3_ST_ERR_UF | SHA3_ST_ERR_OF)

#endif /* SHA3_WB_H */
```

---

## Driver API

```c
/* sha3_wb_driver.h — Polling driver for the SHA-3 Wishbone peripheral */
#ifndef SHA3_WB_DRIVER_H
#define SHA3_WB_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "sha3_wb.h"

/* SHA-3 algorithm variants */
typedef enum {
    SHA3_MODE_224 = 0,   /* SHA3-224:  7 output words */
    SHA3_MODE_256 = 1,   /* SHA3-256:  8 output words */
    SHA3_MODE_384 = 2,   /* SHA3-384: 12 output words */
    SHA3_MODE_512 = 3    /* SHA3-512: 16 output words */
} sha3_mode_t;

/* Number of 32-bit digest words produced by each mode */
static inline int sha3_digest_words(sha3_mode_t mode) {
    switch (mode) {
        case SHA3_MODE_224: return 7;
        case SHA3_MODE_256: return 8;
        case SHA3_MODE_384: return 12;
        default:            return 16;   /* SHA3_MODE_512 */
    }
}

/* -------------------------------------------------------------------------
 * sha3_reset()
 *
 * Assert ABORT to flush both FIFOs and return the core to S_IDLE.
 * Preserves the current MODE bits so an abort mid-stream does not
 * trigger ERR_ILL (mode change while busy).
 * Blocks until STATUS.IDLE is set.
 * ------------------------------------------------------------------------- */
static inline void sha3_reset(void) {
    uint32_t mode_bits = SHA3_CONTROL & SHA3_CTRL_MODE_MASK;
    SHA3_CONTROL = mode_bits | SHA3_CTRL_ABORT;
    while (!(SHA3_STATUS & SHA3_ST_IDLE));
}

/* -------------------------------------------------------------------------
 * sha3_set_mode()
 *
 * Select the hash variant.  Must be called while IDLE.
 * Calling while BUSY is silently ignored by hardware and sets ERR_ILL.
 * ------------------------------------------------------------------------- */
static inline void sha3_set_mode(sha3_mode_t mode) {
    uint32_t ctrl = SHA3_CONTROL & ~SHA3_CTRL_MODE_MASK;
    SHA3_CONTROL  = ctrl | ((uint32_t)mode << 3);
}

/* -------------------------------------------------------------------------
 * sha3_set_msglen()
 *
 * Program the 64-bit message length in bytes.
 * Must be set before calling sha3_start().
 * ------------------------------------------------------------------------- */
static inline void sha3_set_msglen(uint64_t len_bytes) {
    SHA3_MSG_LEN_LO = (uint32_t)(len_bytes & 0xFFFFFFFFUL);
    SHA3_MSG_LEN_HI = (uint32_t)(len_bytes >> 32);
}

/* -------------------------------------------------------------------------
 * sha3_start()
 *
 * Transition the core from S_IDLE to S_ABSORB.
 * Data already in the input FIFO begins draining immediately.
 * ------------------------------------------------------------------------- */
static inline void sha3_start(void) {
    uint32_t ctrl = SHA3_CONTROL & SHA3_CTRL_MODE_MASK;   /* keep mode bits */
    SHA3_CONTROL  = ctrl | SHA3_CTRL_START;
}

/* -------------------------------------------------------------------------
 * sha3_write_word()
 *
 * Push a single 32-bit word into the input FIFO.
 * Busy-waits if the FIFO is full (backpressure).
 * ------------------------------------------------------------------------- */
static inline void sha3_write_word(uint32_t word) {
    while (SHA3_STATUS & SHA3_ST_IN_FULL);
    SHA3_IN_FIFO_DATA = word;
}

/* -------------------------------------------------------------------------
 * sha3_write_words()
 *
 * Stream an array of 32-bit words into the input FIFO, stalling on backpressure.
 * `n` is the number of words (not bytes).
 * ------------------------------------------------------------------------- */
static inline void sha3_write_words(const uint32_t *words, size_t n) {
    for (size_t i = 0; i < n; i++)
        sha3_write_word(words[i]);
}

/* -------------------------------------------------------------------------
 * sha3_pack_u32_le()
 *
 * Pack up to 4 bytes into one 32-bit word using little-endian byte order:
 *   word[7:0]   = b0
 *   word[15:8]  = b1
 *   word[23:16] = b2
 *   word[31:24] = b3
 *
 * This lets firmware iterate through a byte/string buffer naturally
 * (index 0..N-1) and stream it to IN_FIFO_DATA in 32-bit chunks.
 * ------------------------------------------------------------------------- */
static inline uint32_t sha3_pack_u32_le(const uint8_t *p, size_t n) {
    uint32_t w = 0;
    if (n > 0) w |= (uint32_t)p[0] << 0;
    if (n > 1) w |= (uint32_t)p[1] << 8;
    if (n > 2) w |= (uint32_t)p[2] << 16;
    if (n > 3) w |= (uint32_t)p[3] << 24;
    return w;
}

/* -------------------------------------------------------------------------
 * sha3_write_bytes()
 *
 * Stream a byte buffer directly to the input FIFO by packing every 4 bytes
 * into one 32-bit word (little-endian).  The final partial word is
 * zero-padded in unused byte lanes.
 *
 * IMPORTANT: call sha3_set_msglen(len) with the exact byte count before START.
 * ------------------------------------------------------------------------- */
static inline void sha3_write_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        size_t rem = len - i;
        size_t n = (rem >= 4) ? 4 : rem;
        sha3_write_word(sha3_pack_u32_le(&data[i], n));
    }
}

/* -------------------------------------------------------------------------
 * sha3_wait_done()
 *
 * Block until STATUS.DONE is set (digest ready in output FIFO).
 * Returns the final STATUS register value for optional error inspection.
 * ------------------------------------------------------------------------- */
static inline uint32_t sha3_wait_done(void) {
    uint32_t status;
    do { status = SHA3_STATUS; } while (!(status & SHA3_ST_DONE));
    return status;
}

/* -------------------------------------------------------------------------
 * sha3_read_word()
 *
 * Pop one 32-bit digest word from the output FIFO.
 * Caller must ensure the FIFO is not empty; reading empty sets ERR_UF.
 * ------------------------------------------------------------------------- */
static inline uint32_t sha3_read_word(void) {
    return SHA3_OUT_FIFO_DATA;
}

/* -------------------------------------------------------------------------
 * sha3_read_digest()
 *
 * Read exactly `n` digest words into `dst[]`.
 * Use sha3_digest_words(mode) to obtain the correct word count.
 * ------------------------------------------------------------------------- */
static inline void sha3_read_digest(uint32_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = sha3_read_word();
}

/* -------------------------------------------------------------------------
 * sha3_get_errors()
 *
 * Return the error bits from STATUS (ERR_ILL | ERR_UF | ERR_OF).
 * Returns 0 if no errors are latched.
 * ------------------------------------------------------------------------- */
static inline uint32_t sha3_get_errors(void) {
    return SHA3_STATUS & SHA3_ST_ERR_MASK;
}

#endif /* SHA3_WB_DRIVER_H */
```

---

## Usage Guide

### Typical Single-Message Flow

```
RESET → SET MODE → SET MSG_LEN → WRITE DATA → START → WAIT DONE → READ DIGEST
```

> **Important:** `MSG_LEN` and `MODE` **must** be programmed before `START`.
> Absorption begins the moment `START` is written — data already in the input
> FIFO drains immediately. The total bytes written via `IN_FIFO_DATA` **must
> exactly match** `MSG_LEN`; mismatch will cause an incorrect digest or a
> stalled FSM.

### Byte-Stream Helper and Ordering (`"ABC"` example)

Yes, you can iterate through a normal C string/byte array and write each 32-bit
chunk to `IN_FIFO_DATA`.

- Firmware writes 32-bit words in order (`word0`, `word1`, ...).
- Hardware pairs them into 64-bit beats internally before keccak.
- `MSG_LEN` defines how many bytes are valid in the last beat.

Example for `"ABC"` (`0x41 0x42 0x43`):

```c
const uint8_t msg[] = { 'A', 'B', 'C' };

sha3_reset();
sha3_set_mode(SHA3_MODE_256);
sha3_set_msglen(sizeof(msg));
sha3_write_bytes(msg, sizeof(msg));   /* writes one word: 0x00434241 */
sha3_start();
sha3_wait_done();
```

In other words: iterate bytes normally, pack into 32-bit little-endian words,
write to `0x08`, and let the WB module do the 64-bit chunking automatically.

---

### Complete Example — SHA3-256 of a 16-byte Message

```c
#include "sha3_wb_driver.h"

/* 16-byte message: "Hello, SHA-3!\n\0\0" packed into 32-bit words (little-endian) */
static const uint32_t msg[4] = {
    0x6C6C6548,   /* "Hell" */
    0x532C6F6F,   /* "o, S" */
    0x332D4148,   /* "HA-3" */
    0x00000A21    /* "!\n"  + 2 padding bytes (not part of message) */
};
static const uint64_t MSG_BYTES = 14;   /* "Hello, SHA-3!\n" = 14 bytes */

int main(void) {
    uint32_t digest[8];   /* SHA3-256 = 8 words */

    /* 1. Reset the peripheral and clear any previous state */
    sha3_reset();

    /* 2. Select SHA3-256 */
    sha3_set_mode(SHA3_MODE_256);

    /* 3. Program the exact message length in bytes */
    sha3_set_msglen(MSG_BYTES);

    /*
     * 4. Pre-load the input FIFO before asserting START (optional but
     *    maximises throughput by removing any underflow latency).
     *    The last word contains only 2 valid bytes (MSG_BYTES % 4 == 2);
     *    hardware reads MSG_LEN[1:0] = 2 to extract byte_num automatically.
     */
    sha3_write_words(msg, 4);

    /* 5. Assert START — absorption begins immediately */
    sha3_start();

    /* 6. Wait for the core to complete padding and permutation */
    uint32_t status = sha3_wait_done();
    if (status & SHA3_ST_ERR_MASK) {
        /* handle error */
        return -1;
    }

    /* 7. Read the 8-word (256-bit) digest */
    sha3_read_digest(digest, sha3_digest_words(SHA3_MODE_256));

    /* digest[0] holds the lowest-address 32 bits of the hash */
    return 0;
}
```

---

### Zero-Length Message

The core supports a zero-byte message (`MSG_LEN = 0`). A `final_pulse` is
issued immediately on `START` without popping any FIFO words.

```c
sha3_reset();
sha3_set_mode(SHA3_MODE_256);
sha3_set_msglen(0);
sha3_start();
sha3_wait_done();
sha3_read_digest(digest, 8);
```

---

### Sequential Messages

To hash a new message after `DONE` is set, either:

- Call `sha3_reset()` to fully clear state and FIFOs, then run the sequence
  again, **or**
- Write a new `MSG_LEN`, pre-load data, and call `sha3_start()` directly —
  the transition from `S_DONE` back to `S_IDLE` happens automatically when
  `START` is written, provided the output FIFO has been fully drained first.

Only **one message can be processed at a time**; concurrent hashing is not
supported.

---

### Error Handling

All error bits are sticky; they remain set until an `ABORT` or system reset.

| Error flag     | Cause                                          | Recovery         |
|----------------|------------------------------------------------|------------------|
| `ERR_ILL`      | Register write (e.g., MODE change) while BUSY  | `sha3_reset()`   |
| `ERR_UF`       | `OUT_FIFO_DATA` read while output FIFO empty   | `sha3_reset()`   |
| `ERR_OF`       | `IN_FIFO_DATA` write while input FIFO full     | `sha3_reset()`   |

```c
/* Check for any latched error after waiting for DONE */
if (sha3_get_errors()) {
    sha3_reset();   /* clears all error latches */
}
```

---

## FSM State Summary

| State        | Encoding | Description                                              |
|--------------|----------|----------------------------------------------------------|
| `S_IDLE`     | `2'b00`  | Waiting for `START`. Both FIFOs accessible.             |
| `S_ABSORB`   | `2'b01`  | Draining input FIFO → Keccak core word by word.         |
| `S_WAIT_HASH`| `2'b10`  | All data absorbed; waiting for final permutation output. |
| `S_DONE`     | `2'b11`  | Digest loaded into output FIFO; `DONE` flag set.        |

---

## Hardware Parameters

| Parameter    | Default | Description                           |
|--------------|---------|---------------------------------------|
| `dw`         | 32      | Wishbone data bus width (fixed)       |
| `aw`         | 8       | Wishbone address bus width            |
| `FIFO_DEPTH` | 64      | Input FIFO depth in 32-bit words      |
| `BUFF_SIZE`  | 512     | Hash output register width in bits    |
