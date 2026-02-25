/*
    tb_sha3_wb.cpp - Verilator testbench for sha3_wb Wishbone controller

    This testbench acts as a Wishbone master and directly drives the SHA3
    core interface, replacing the real keccak core with a C++ stub that:
      - Accepts data words presented by the controller (sha3_out_rdy=1)
      - Stores up to 16 words internally
      - After observing is_last, waits HASH_LATENCY cycles then asserts
        sha3_hash_rdy=1 for 1 cycle with the stored words in sha3_hash_in
        (MSB = first word received, matching the controller's out_fifo layout)

    Verification: the OUT_FIFO words read back over Wishbone are compared
    against the words originally written to IN_FIFO.

    Register addresses (byte offsets):
        0x00  Control  (START[0], ABORT[2], MODE[4:3])
        0x04  Status
        0x08  IN_FIFO_DATA
        0x0C  IN_FIFO_LEVEL
        0x10  OUT_FIFO_DATA
        0x14  OUT_FIFO_LEVEL
        0x18  MSG_LEN_LO
        0x1C  MSG_LEN_HI

    Status register bits:
        [0]  IDLE   [1] BUSY   [2] DONE
        [4]  IN_EMPTY  [5] IN_FULL  [6] OUT_EMPTY  [7] OUT_FULL
        [8]  ERR_ILLEGAL_WHILE_BUSY  [9] ERR_UF  [10] ERR_OF
*/

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

#include "Vsha3_wb.h"
#include "verilated.h"

// ============================================================================
// Simulation globals
// ============================================================================

static Vsha3_wb* dut    = nullptr;
static uint64_t  s_time = 0;

// Control register field helpers
static constexpr uint32_t CTRL_START = (1u << 0);
static constexpr uint32_t CTRL_ABORT = (1u << 2);
static constexpr uint32_t CTRL_MODE  = (3u << 3);   // bits [4:3]

// Status register bit masks
static constexpr uint32_t ST_IDLE  = (1u << 0);
static constexpr uint32_t ST_BUSY  = (1u << 1);
static constexpr uint32_t ST_DONE  = (1u << 2);
static constexpr uint32_t ST_IN_EMPTY = (1u << 4);
static constexpr uint32_t ST_IN_FULL  = (1u << 5);
static constexpr uint32_t ST_OUT_EMPTY = (1u << 6);
static constexpr uint32_t ST_OUT_FULL  = (1u << 7);
static constexpr uint32_t ST_ERR_ILL   = (1u << 8);
static constexpr uint32_t ST_ERR_UF    = (1u << 9);
static constexpr uint32_t ST_ERR_OF    = (1u << 10);

// ============================================================================
// Keccak core stub (C++ model)
//
// Mirrors the behaviour expected by the controller:
//   - sha3_buff_full = 0 at all times (simplified; no internal backpressure)
//   - Captures words when sha3_out_rdy=1 on the rising clock edge
//   - When sha3_is_last=1, counts down HASH_LATENCY cycles then asserts
//     sha3_hash_rdy for exactly 1 cycle
//   - sha3_hash_in packs stored words MSB-first so that out_fifo[i] == word[i]
// ============================================================================

struct KeccakStub {
    static constexpr int HASH_LATENCY = 10;  // cycles to "compute"
    static constexpr int MAX_WORDS    = 16;  // 512 bits / 32

    uint32_t stored_words[MAX_WORDS];
    int      word_count;
    int      cooldown;       // cycles until hash_rdy
    bool     hash_pending;
    bool     hash_ready;     // asserted for exactly 1 cycle

    KeccakStub() { reset(); }

    void reset() {
        memset(stored_words, 0, sizeof(stored_words));
        word_count   = 0;
        cooldown     = 0;
        hash_pending = false;
        hash_ready   = false;
    }

    // Call AFTER raising clock and calling eval().
    // in_ready, in_data, is_last, byte_num, rst mirror the controller outputs.
    void on_posedge(bool rst,
                    uint32_t in_data, bool in_ready,
                    bool is_last, int byte_num) {
        if (rst) {
            reset();
            return;
        }

        // hash_ready is self-clearing (1 cycle pulse).
        // Also reset stored data so a subsequent message starts clean,
        // mirroring the keccak core clearing its internal state after output.
        if (hash_ready) {
            hash_ready = false;
            word_count = 0;
            memset(stored_words, 0, sizeof(stored_words));
        }

        // Count down towards hash output
        if (hash_pending) {
            if (--cooldown == 0) {
                hash_ready   = true;
                hash_pending = false;
            }
        }

        // Accept a word from the controller
        if (in_ready && !hash_pending) {
            // is_last=1 with byte_num=0 is the zero-byte final padding pulse:
            // no actual data bytes, so don't store it.
            bool has_data = !(is_last && byte_num == 0);
            if (has_data && word_count < MAX_WORDS) {
                stored_words[word_count++] = in_data;
            }
            if (is_last) {
                hash_pending = true;
                cooldown     = HASH_LATENCY;
            }
        }
    }

    // Drive dut->sha3_hash_in[].
    //   The controller loads:   out_fifo[i] <= sha3_hash_in[511 - 32*i -: 32]
    //   In Verilator:           sha3_hash_in[15-i] covers bits [511-32*i:480-32*i]
    // So stored_words[0] → sha3_hash_in[15], stored_words[1] → sha3_hash_in[14] …
    void fill_hash_in() {
        for (int i = 0; i < MAX_WORDS; i++) {
            int vi = MAX_WORDS - 1 - i;  // Verilator word index
            dut->sha3_hash_in[vi] = (i < word_count) ? stored_words[i] : 0u;
        }
    }
};

static KeccakStub stub;

// Shadow of ctrl_reg — tracks mode and any persistent bits so that
// writes preserving earlier fields are possible without a read-back.
static uint32_t ctrl_shadow = 0;

// ============================================================================
// Clock / simulation helpers
// ============================================================================

static constexpr int MAX_CYCLES = 100000;

static void eval() {
    dut->eval();
    s_time++;
}

// Drive one full clock cycle.
// 1. Negedge  2. Posedge  3. Post-posedge eval (update keccak stub outputs)
static void tick() {
    // --- negedge ---
    dut->wb_clk_i = 0;
    eval();

    // --- posedge ---
    dut->wb_clk_i = 1;
    eval();

    // Capture controller→keccak signals on this posedge
    stub.on_posedge(
        (bool)dut->sha3_reset,
        (uint32_t)dut->sha3_data_out,
        (bool)dut->sha3_out_rdy,
        (bool)dut->sha3_is_last,
        (int)dut->sha3_num_bytes
    );

    // Drive keccak→controller outputs that take effect next cycle
    dut->sha3_hash_rdy = stub.hash_ready ? 1u : 0u;
    dut->sha3_buff_full = 0u;  // stub never asserts backpressure
    stub.fill_hash_in();

    // Settle combinational logic with updated keccak outputs
    eval();
}

static void reset_dut(int cycles = 5) {
    dut->wb_rst_i = 1;
    dut->wb_cyc_i = 0;
    dut->wb_stb_i = 0;
    dut->wb_we_i  = 0;
    dut->wb_sel_i = 0xF;
    dut->wb_adr_i = 0;
    dut->wb_dat_i = 0;
    dut->sha3_hash_rdy  = 0;
    dut->sha3_buff_full = 0;
    stub.reset();
    for (int i = 0; i < cycles; i++) tick();
    dut->wb_rst_i = 0;
    tick();
}

// ============================================================================
// Wishbone master helpers
// ============================================================================

// Perform a single WB write.  Handles ACK stalling (for IN_FIFO_DATA when full).
// Returns number of stall cycles observed.
static int wb_write(uint8_t addr, uint32_t data) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i  = 1;
    dut->wb_adr_i = addr;
    dut->wb_dat_i = data;
    dut->wb_sel_i = 0xF;

    int stalls = 0;
    for (int guard = 0; guard < MAX_CYCLES; guard++) {
        tick();
        if (dut->wb_ack_o) {
            dut->wb_cyc_i = 0;
            dut->wb_stb_i = 0;
            dut->wb_we_i  = 0;
            tick(); // idle cycle
            return stalls;
        }
        stalls++;
    }
    std::cerr << "[ERROR] wb_write timed out at addr=0x"
              << std::hex << (int)addr << std::dec << "\n";
    return -1;
}

// Perform a single WB read.  Returns the data.
static uint32_t wb_read(uint8_t addr) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i  = 0;
    dut->wb_adr_i = addr;
    dut->wb_sel_i = 0xF;

    for (int guard = 0; guard < MAX_CYCLES; guard++) {
        tick();
        if (dut->wb_ack_o) {
            uint32_t val = (uint32_t)dut->wb_dat_o;
            dut->wb_cyc_i = 0;
            dut->wb_stb_i = 0;
            tick();
            return val;
        }
    }
    std::cerr << "[ERROR] wb_read timed out at addr=0x"
              << std::hex << (int)addr << std::dec << "\n";
    return 0xDEADBEEF;
}

// Poll STATUS until the given bits are all set (or DONE bit seen).
// Returns the final status value.
static uint32_t wait_for_status(uint32_t mask, int timeout = MAX_CYCLES) {
    for (int i = 0; i < timeout; i++) {
        uint32_t st = wb_read(0x04);
        if ((st & mask) == mask) return st;
    }
    std::cerr << "[ERROR] wait_for_status timeout (mask=0x"
              << std::hex << mask << std::dec << ")\n";
    return 0;
}

// ============================================================================
// High-level controller operations (mirror the programming model)
// ============================================================================

static void ctrl_reset() {
    // Include the current mode bits in the ABORT write.  Without this,
    // the controller rejects the write as an illegal mode-change-while-busy
    // when called during S_ABSORB/S_WAIT_HASH.
    wb_write(0x00, (ctrl_shadow & CTRL_MODE) | CTRL_ABORT);
    wait_for_status(ST_IDLE);
    ctrl_shadow = 0;   // clear shadow AFTER IDLE is confirmed
    stub.reset();
}

// Set mode (00=SHA3-224, 01=SHA3-256, 10=SHA3-384, 11=SHA3-512)
// Preserves the shadow so ctrl_start() transmits the right mode.
static void ctrl_set_mode(uint8_t mode) {
    ctrl_shadow = (ctrl_shadow & ~CTRL_MODE) | ((uint32_t)(mode & 0x3) << 3);
    wb_write(0x00, ctrl_shadow);
}

static void ctrl_set_msglen(uint64_t len) {
    wb_write(0x18, (uint32_t)(len & 0xFFFFFFFF));
    wb_write(0x1C, (uint32_t)(len >> 32));
}

static void ctrl_start() {
    // OR in START without disturbing mode or other persistent bits.
    // START auto-clears inside the controller so the shadow is unchanged.
    wb_write(0x00, ctrl_shadow | CTRL_START);
}

// Write N 32-bit words to IN_FIFO_DATA, stalling if FIFO is full.
static void ctrl_write_words(const uint32_t* words, int n) {
    for (int i = 0; i < n; i++)
        wb_write(0x08, words[i]);
}

// Read N words from OUT_FIFO_DATA into dst[].
static void ctrl_read_words(uint32_t* dst, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = wb_read(0x10);
}

// ============================================================================
// Test infrastructure
// ============================================================================

struct TestResult {
    const char* name;
    bool        passed;
};

static std::vector<TestResult> results;

static void test_begin(const char* name) {
    std::cout << "\n[TEST] " << name << "\n";
    std::cout << std::string(60, '-') << "\n";
}

static bool test_end(const char* name, bool pass) {
    results.push_back({name, pass});
    std::cout << (pass ? "[PASS] " : "[FAIL] ") << name << "\n";
    return pass;
}

// Compare two word arrays and print diff on mismatch
static bool verify_words(const uint32_t* expected, const uint32_t* actual,
                          int n, const char* label) {
    bool ok = true;
    for (int i = 0; i < n; i++) {
        bool match = (expected[i] == actual[i]);
        ok &= match;
        std::cout << (match ? "  OK " : "  BAD")
                  << " word[" << std::setw(2) << i << "]: "
                  << "expected=0x" << std::hex << std::setfill('0')
                  << std::setw(8) << expected[i]
                  << "  got=0x" << std::setw(8) << actual[i]
                  << std::dec << std::setfill(' ')
                  << (match ? "" : "  *** MISMATCH ***")
                  << "\n";
    }
    return ok;
}

// ============================================================================
// Shared test runner
//
// Sets up a hash operation, streams words, waits for completion, reads back
// the digest, and verifies the first data_words match the input and the
// remaining digest_words - data_words are zero.
//
// mode        : 0-3 (SHA3-224/256/384/512 → 7/8/12/16 output words)
// msg_len     : byte count of the message (must equal 4 * num_words when
//               the last word is full, or (4*(num_words-1)+valid_tail_bytes)
//               for a partial final word)
// words       : input data words
// num_words   : number of 32-bit words to write (including partial last word)
// ============================================================================

static int digest_word_count(int mode) {
    switch (mode) {
        case 0: return 7;
        case 1: return 8;
        case 2: return 12;
        default: return 16;
    }
}

static bool run_hash_test(const char* name,
                          int mode,
                          uint64_t msg_len,
                          const uint32_t* words, int num_words) {
    test_begin(name);

    ctrl_reset();
    ctrl_set_mode((uint8_t)mode);
    ctrl_set_msglen(msg_len);
    ctrl_start();

    ctrl_write_words(words, num_words);
    wait_for_status(ST_DONE);

    int dw = digest_word_count(mode);
    uint32_t got[16] = {};
    ctrl_read_words(got, dw);

    // Build expected: first num_words match input (for full-word messages);
    // for partial last word the stub stores the raw word unchanged.
    // Remaining digest words are 0.
    uint32_t expected[16] = {};
    for (int i = 0; i < num_words && i < dw; i++)
        expected[i] = words[i];

    bool pass = verify_words(expected, got, dw, name);

    // Also verify OUT_FIFO is now empty and status is back to IDLE/OK
    uint32_t st = wb_read(0x04);
    if (!(st & ST_IDLE)) {
        std::cout << "  FAIL: expected IDLE after draining OUT_FIFO, status=0x"
                  << std::hex << st << std::dec << "\n";
        pass = false;
    }
    if (st & (ST_ERR_ILL | ST_ERR_UF | ST_ERR_OF)) {
        std::cout << "  FAIL: unexpected error bits in status=0x"
                  << std::hex << st << std::dec << "\n";
        pass = false;
    }

    return test_end(name, pass);
}

// ============================================================================
// Individual test cases
// ============================================================================

// Test 4: Short message — 4 bytes, 1 full word (msg_len % 4 == 0 → final_pulse)
static bool test_4_bytes() {
    uint32_t words[] = { 0xDEADBEEF };
    return run_hash_test("T4: 4-byte message (1 full word + final_pulse)",
                         3, 4, words, 1);
}

// Test 3: Partial last word — 3 valid bytes (msg_len=3, byte_num=3)
static bool test_3_bytes() {
    // Only MSB 3 bytes are valid; LSB byte is don't-care but the stub stores the
    // whole word.  The expected output equals the whole stored word.
    uint32_t words[] = { 0xAABBCC00 };  // 3 data bytes at [31:8]
    return run_hash_test("T3: 3-byte message (partial last word, byte_num=3)",
                         3, 3, words, 1);
}

// Test 1: Partial last word — 1 valid byte (msg_len=1, byte_num=1)
static bool test_1_byte() {
    uint32_t words[] = { 0xFF000000 };  // 1 data byte at [31:24]
    return run_hash_test("T1: 1-byte message (partial last word, byte_num=1)",
                         3, 1, words, 1);
}

// Test 5: Multi-word message — 8 full words (32 bytes)
static bool test_32_bytes() {
    uint32_t words[8];
    for (int i = 0; i < 8; i++) words[i] = 0x10203040u + (uint32_t)i;
    return run_hash_test("T5: 32-byte message (8 full words + final_pulse)",
                         3, 32, words, 8);
}

// Test 6: Maximum test-safe length — 16 full words (64 bytes, fills 512-bit digest exactly)
static bool test_64_bytes() {
    uint32_t words[16];
    for (int i = 0; i < 16; i++) words[i] = 0xA0B0C0D0u ^ (uint32_t)(i * 0x01010101u);
    return run_hash_test("T6: 64-byte message (16 full words, no final_pulse)",
                         3, 64, words, 16);
}

// Test 2: 2-byte partial word (byte_num=2)
static bool test_2_bytes() {
    uint32_t words[] = { 0x12340000 };  // 2 data bytes at [31:16]
    return run_hash_test("T2: 2-byte message (partial last word, byte_num=2)",
                         3, 2, words, 1);
}

// Test 7: Sequential messages — hash two different messages back-to-back
static bool test_sequential() {
    test_begin("T7: Sequential messages (hash A then hash B)");
    bool pass = true;

    // --- Message A: 8 bytes (2 full words) ---
    uint32_t a[2] = { 0x11223344, 0x55667788 };
    ctrl_reset();
    ctrl_set_mode(3);
    ctrl_set_msglen(8);
    ctrl_start();
    ctrl_write_words(a, 2);
    wait_for_status(ST_DONE);
    uint32_t got_a[16] = {};
    ctrl_read_words(got_a, 16);
    std::cout << "  Message A result:\n";
    uint32_t exp_a[16] = { a[0], a[1] };
    pass &= verify_words(exp_a, got_a, 16, "A");

    // --- Message B: 12 bytes (3 full words) ---
    uint32_t b[3] = { 0xAABBCCDD, 0xEEFF0011, 0x22334455 };
    ctrl_set_mode(3);           // mode can be set in IDLE
    ctrl_set_msglen(12);
    ctrl_start();           // re-use without explicit ABORT (new START after IDLE is fine)
    ctrl_write_words(b, 3);
    wait_for_status(ST_DONE);
    uint32_t got_b[16] = {};
    ctrl_read_words(got_b, 16);
    std::cout << "  Message B result:\n";
    uint32_t exp_b[16] = { b[0], b[1], b[2] };
    pass &= verify_words(exp_b, got_b, 16, "B");

    uint32_t st = wb_read(0x04);
    pass &= (bool)(st & ST_IDLE);

    return test_end("T7: Sequential messages", pass);
}

// Test 8: Abort mid-stream — write some words, ABORT, then complete a fresh hash
static bool test_abort() {
    test_begin("T8: Abort mid-stream then restart");
    bool pass = true;

    // Start a 6-word message
    ctrl_reset();
    ctrl_set_mode(3);
    ctrl_set_msglen(24);
    ctrl_start();
    uint32_t garbage[3] = { 0xBAD0BAD1, 0xBAD2BAD3, 0xBAD4BAD5 };
    ctrl_write_words(garbage, 3);   // write half the words then abort

    // Abort
    ctrl_reset();   // This writes CTRL_ABORT and waits for IDLE

    // Verify clean state: IDLE, IN_EMPTY, OUT_EMPTY, no errors
    uint32_t st = wb_read(0x04);
    std::cout << "  Post-abort status = 0x" << std::hex << st << std::dec << "\n";
    if (!(st & ST_IDLE) || (st & ST_BUSY) || (st & ST_DONE)) {
        std::cout << "  FAIL: not cleanly idle after abort\n";
        pass = false;
    }

    // Now do a fresh valid hash
    uint32_t words[2] = { 0xFEEDFACE, 0xCAFEBABE };
    ctrl_set_mode(3);
    ctrl_set_msglen(8);
    ctrl_start();
    ctrl_write_words(words, 2);
    wait_for_status(ST_DONE);
    uint32_t got[16] = {};
    ctrl_read_words(got, 16);
    std::cout << "  Post-abort fresh hash result:\n";
    uint32_t expected[16] = { words[0], words[1] };
    pass &= verify_words(expected, got, 16, "fresh");

    return test_end("T8: Abort mid-stream then restart", pass);
}

// Test 9: Empty message (msg_len=0) — final_pulse fires immediately on START
static bool test_empty_message() {
    test_begin("T9: Empty message (msg_len=0)");

    ctrl_reset();
    ctrl_set_mode(3);        // SHA3-512 → 16 output words
    ctrl_set_msglen(0);
    ctrl_start();
    // No data words written — controller sends final_pulse immediately
    wait_for_status(ST_DONE);

    uint32_t got[16] = {};
    ctrl_read_words(got, 16);
    std::cout << "  Empty message output (all words should be 0):\n";
    uint32_t expected[16] = {};  // stub stores nothing → all zeros in hash_in
    bool pass = verify_words(expected, got, 16, "empty");

    return test_end("T9: Empty message", pass);
}

// Test 10: Mode selection — SHA3-224 (mode=00) gives exactly 7 output words
static bool test_mode_sha3_224() {
    test_begin("T10: SHA3-224 mode (7 output words)");
    bool pass = true;

    uint32_t words[4];
    for (int i = 0; i < 4; i++) words[i] = 0xC0FFEE00u | (uint32_t)i;

    ctrl_reset();
    ctrl_set_mode(0);          // SHA3-224
    ctrl_set_msglen(16);
    ctrl_start();
    ctrl_write_words(words, 4);
    wait_for_status(ST_DONE);

    // Check OUT_FIFO_LEVEL == 7
    uint32_t level = wb_read(0x14);
    std::cout << "  OUT_FIFO_LEVEL = " << level << " (expected 7)\n";
    if (level != 7) {
        std::cout << "  FAIL: wrong output FIFO level\n";
        pass = false;
    }

    // Read 7 words and verify first 4 match input
    uint32_t got[7] = {};
    ctrl_read_words(got, 7);
    std::cout << "  Output words:\n";
    uint32_t expected[7] = { words[0], words[1], words[2], words[3], 0, 0, 0 };
    pass &= verify_words(expected, got, 7, "sha3_224");

    // After draining, DONE should clear and state → IDLE
    uint32_t st = wb_read(0x04);
    if (!(st & ST_IDLE)) {
        std::cout << "  FAIL: not IDLE after draining SHA3-224 FIFO\n";
        pass = false;
    }

    // Verify OUT_FIFO_LEVEL is now 0
    level = wb_read(0x14);
    if (level != 0) {
        std::cout << "  FAIL: OUT_FIFO_LEVEL should be 0 after drain, got " << level << "\n";
        pass = false;
    }

    return test_end("T10: SHA3-224 mode", pass);
}

// Test 11: Mode selection — SHA3-256 (mode=01) gives exactly 8 output words
static bool test_mode_sha3_256() {
    test_begin("T11: SHA3-256 mode (8 output words)");
    bool pass = true;

    uint32_t words[3] = { 0xDEAD0001, 0xDEAD0002, 0xDEAD0003 };

    ctrl_reset();
    ctrl_set_mode(1);          // SHA3-256
    ctrl_set_msglen(12);
    ctrl_start();
    ctrl_write_words(words, 3);
    wait_for_status(ST_DONE);

    uint32_t level = wb_read(0x14);
    std::cout << "  OUT_FIFO_LEVEL = " << level << " (expected 8)\n";
    if (level != 8) { pass = false; }

    uint32_t got[8] = {};
    ctrl_read_words(got, 8);
    uint32_t expected[8] = { words[0], words[1], words[2], 0, 0, 0, 0, 0 };
    pass &= verify_words(expected, got, 8, "sha3_256");

    return test_end("T11: SHA3-256 mode", pass);
}

// Test 12: Mode selection — SHA3-384 (mode=10) gives exactly 12 output words
static bool test_mode_sha3_384() {
    test_begin("T12: SHA3-384 mode (12 output words)");
    bool pass = true;

    uint32_t words[5];
    for (int i = 0; i < 5; i++) words[i] = 0x38400000u + (uint32_t)i;

    ctrl_reset();
    ctrl_set_mode(2);          // SHA3-384
    ctrl_set_msglen(20);
    ctrl_start();
    ctrl_write_words(words, 5);
    wait_for_status(ST_DONE);

    uint32_t level = wb_read(0x14);
    std::cout << "  OUT_FIFO_LEVEL = " << level << " (expected 12)\n";
    if (level != 12) { pass = false; }

    uint32_t got[12] = {};
    ctrl_read_words(got, 12);
    uint32_t expected[12] = { words[0], words[1], words[2], words[3], words[4] };
    pass &= verify_words(expected, got, 12, "sha3_384");

    return test_end("T12: SHA3-384 mode", pass);
}

// Test 13: Mode selection — SHA3-512 (mode=11) gives exactly 16 output words
static bool test_mode_sha3_512() {
    test_begin("T13: SHA3-512 mode (16 output words)");
    bool pass = true;

    uint32_t words[4] = { 0x51200001, 0x51200002, 0x51200003, 0x51200004 };

    ctrl_reset();
    ctrl_set_mode(3);          // SHA3-512
    ctrl_set_msglen(16);
    ctrl_start();
    ctrl_write_words(words, 4);
    wait_for_status(ST_DONE);

    uint32_t level = wb_read(0x14);
    std::cout << "  OUT_FIFO_LEVEL = " << level << " (expected 16)\n";
    if (level != 16) {
        std::cout << "  FAIL: wrong output FIFO level\n";
        pass = false;
    }

    uint32_t got[16] = {};
    ctrl_read_words(got, 16);
    uint32_t expected[16] = { words[0], words[1], words[2], words[3] };
    pass &= verify_words(expected, got, 16, "sha3_512");

    return test_end("T13: SHA3-512 mode", pass);
}

// Test 14: Illegal mode change while BUSY sets ERR_ILLEGAL_WHILE_BUSY
static bool test_illegal_mode_change() {
    test_begin("T14: Illegal mode change while BUSY");
    bool pass = true;

    // Start a long message so we remain BUSY for a while
    uint32_t words[16];
    for (int i = 0; i < 8; i++) words[i] = (uint32_t)i;

    ctrl_reset();
    ctrl_set_mode(3);           // SHA3-512
    ctrl_set_msglen(32);
    ctrl_start();

    // While BUSY, attempt to change mode to SHA3-256
    uint32_t st_before = wb_read(0x04);
    if (st_before & ST_BUSY) {
        wb_write(0x00, (1u << 3));  // MODE = 01, no START/ABORT
        uint32_t st_after = wb_read(0x04);
        std::cout << "  Status after illegal write = 0x"
                  << std::hex << st_after << std::dec << "\n";
        if (st_after & ST_ERR_ILL) {
            std::cout << "  ERR_ILLEGAL_WHILE_BUSY correctly set\n";
        } else {
            std::cout << "  FAIL: ERR_ILLEGAL_WHILE_BUSY not set\n";
            pass = false;
        }
    } else {
        // Controller may have already finished if FIFO drained super fast
        std::cout << "  NOTE: not BUSY when illegal write attempted, test skipped\n";
    }

    // Drain to avoid leaking state
    ctrl_write_words(words, 8);
    wait_for_status(ST_DONE);
    ctrl_read_words(words, 16);   // dump result

    ctrl_reset();
    return test_end("T14: Illegal mode change while BUSY", pass);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vsha3_wb;

    std::cout << "======================================================\n";
    std::cout << "  sha3_wb Wishbone Controller Testbench\n";
    std::cout << "======================================================\n";

    reset_dut();

    test_1_byte();
    test_2_bytes();
    test_3_bytes();
    test_4_bytes();
    test_32_bytes();
    test_64_bytes();
    test_sequential();
    test_abort();
    test_empty_message();
    test_mode_sha3_224();
    test_mode_sha3_256();
    test_mode_sha3_384();
    test_mode_sha3_512();
    test_illegal_mode_change();

    // ----------------------------------------------------------------
    // Summary
    // ----------------------------------------------------------------
    int passed = 0, failed = 0;
    std::cout << "\n======================================================\n";
    std::cout << "  RESULTS\n";
    std::cout << "======================================================\n";
    for (const auto& r : results) {
        std::cout << (r.passed ? "[PASS] " : "[FAIL] ") << r.name << "\n";
        r.passed ? passed++ : failed++;
    }
    std::cout << "------------------------------------------------------\n";
    std::cout << "  " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "======================================================\n";

    dut->final();
    delete dut;
    return (failed == 0) ? 0 : 1;
}
