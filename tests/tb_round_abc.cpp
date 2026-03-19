/*
 * Testbench: round_abc
 * ====================
 * End-to-end test of the round module with NIST SHA-3-256 test vectors.
 *
 * Drives the combinational `round` module for all 24 Keccak-f[1600] rounds
 * for two NIST SHA-3-256 test vectors, comparing the resulting digest.
 *
 * Test vectors:
 *   1. SHA-3-256("abc")
 *      expected: 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532
 *
 *   2. SHA-3-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
 *      expected: 41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376
 *
 * Input construction:
 *   SHA-3-256 rate = 1088 bits = 136 bytes.
 *   The padded message block is XOR'd into the zero state.
 *   Padding: append 0x06 immediately after message, then zeros, then 0x80
 *   at the last byte of the rate block (byte 135).
 *
 * Note: This testbench was developed with the assistance of GitHub Copilot.
 *
 * Digest extraction (SHA-3-256 squeeze):
 *   First 256 bits = lanes A[0][0]..A[3][0] serialised little-endian.
 *
 * VCD output:
 *   round_abc_trace.vcd  -  test 1 at sim-time  0..23
 *                           test 2 at sim-time 50..73
 */

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <array>
#include <sstream>
#include "Vround.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// ---------------------------------------------------------------------------
// Hardware constants
// ---------------------------------------------------------------------------
constexpr int STATE_WIDTH  = 1600;
constexpr int LANE_WIDTH   = 64;
constexpr int GRID_SIZE    = 5;
constexpr int NUM_ROUNDS   = 24;

// ---------------------------------------------------------------------------
// Keccak-f[1600] round constants (24 rounds)
// Source: NIST FIPS 202 / Keccak reference
// ---------------------------------------------------------------------------
static const uint64_t ROUND_CONSTS[NUM_ROUNDS] = {
    0x0000000000000001ULL,
    0x0000000000008082ULL,
    0x800000000000808AULL,
    0x8000000080008000ULL,
    0x000000000000808BULL,
    0x0000000080000001ULL,
    0x8000000080008081ULL,
    0x8000000000008009ULL,
    0x000000000000008AULL,
    0x0000000000000088ULL,
    0x0000000080008009ULL,
    0x000000008000000AULL,
    0x000000008000808BULL,
    0x800000000000008BULL,
    0x8000000000008089ULL,
    0x8000000000008003ULL,
    0x8000000000008002ULL,
    0x8000000000000080ULL,
    0x000000000000800AULL,
    0x800000008000000AULL,
    0x8000000080008081ULL,
    0x8000000000008080ULL,
    0x0000000080000001ULL,
    0x8000000080008008ULL
};

// ---------------------------------------------------------------------------
// State packing / unpacking
//
// Hardware bit ordering (from sha3_pkg.sv):
//   high_pos(x,y) = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE*y + x)
//   low_pos(x,y)  = high_pos(x,y) - (LANE_WIDTH - 1)
//
// lanes[y*5+x] is the 64-bit lane A[x][y].
// ---------------------------------------------------------------------------

void lanes_to_state(const uint64_t lanes[25], uint32_t* words)
{
    memset(words, 0, 50 * sizeof(uint32_t));

    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            uint64_t lane = lanes[y * GRID_SIZE + x];
            int high = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x);
            int low  = high - (LANE_WIDTH - 1);

            for (int b = 0; b < LANE_WIDTH; b++) {
                int pos      = low + b;
                int word_idx = pos / 32;
                int bit_idx  = pos % 32;
                if ((lane >> b) & 1ULL)
                    words[word_idx] |= (1U << bit_idx);
            }
        }
    }
}

void state_to_lanes(const uint32_t* words, uint64_t lanes[25])
{
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            uint64_t lane = 0;
            int high = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x);
            int low  = high - (LANE_WIDTH - 1);

            for (int b = 0; b < LANE_WIDTH; b++) {
                int pos      = low + b;
                int word_idx = pos / 32;
                int bit_idx  = pos % 32;
                if ((words[word_idx] >> bit_idx) & 1U)
                    lane |= (1ULL << b);
            }
            lanes[y * GRID_SIZE + x] = lane;
        }
    }
}

// ---------------------------------------------------------------------------
// Build the initial absorbed state from a raw message.
//
// SHA-3-256 rate = 136 bytes.  The message is padded as:
//   msg[0..len-1]  |  0x06  |  0x00...  |  0x80  (byte 135)
// then XOR'd into the zero state lane-by-lane.
//
// Lane sequential index = byte_offset / 8  (matches NIST A[x][y] ordering)
// Bit offset within lane = (byte_offset % 8) * 8  (little-endian)
// ---------------------------------------------------------------------------
void build_initial_state(const uint8_t* msg, size_t len, uint64_t lanes[25])
{
    constexpr int RATE_BYTES = 136;  // SHA-3-256 rate

    uint8_t block[RATE_BYTES] = {};
    memcpy(block, msg, len);
    block[len]           = 0x06;   // SHA-3 domain separator / pad start
    block[RATE_BYTES-1] |= 0x80;   // multirate padding end

    memset(lanes, 0, 25 * sizeof(uint64_t));
    for (int i = 0; i < RATE_BYTES; i++) {
        int lane_idx = i / 8;
        int bit_off  = (i % 8) * 8;
        lanes[lane_idx] ^= (uint64_t)block[i] << bit_off;
    }
}

// ---------------------------------------------------------------------------
// Digest extraction
//
// SHA-3-256 squeezes the first 256 bits = lanes A[0][0]..A[3][0].
// Each 64-bit lane is serialised little-endian (byte 0 = bits 7:0, etc.).
// ---------------------------------------------------------------------------
std::string extract_digest_hex(const uint64_t lanes[25])
{
    uint8_t digest[32];
    for (int lx = 0; lx < 4; lx++) {
        uint64_t lane = lanes[0 * GRID_SIZE + lx];  // A[lx][0], y=0
        for (int byte_i = 0; byte_i < 8; byte_i++)
            digest[lx * 8 + byte_i] = (lane >> (byte_i * 8)) & 0xFF;
    }

    std::ostringstream oss;
    for (int i = 0; i < 32; i++)
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];
    return oss.str();
}

// ---------------------------------------------------------------------------
// Run a single SHA-3-256 test: 24 rounds through the DUT.
// Returns true on pass.  Dumps each round to *tfp at time (time_offset + rnd).
// ---------------------------------------------------------------------------
bool run_test(Vround* dut, VerilatedVcdC* tfp, uint64_t time_offset,
              const char* name, const uint8_t* msg, size_t len,
              const std::string& expected)
{
    uint64_t state_lanes[25];
    build_initial_state(msg, len, state_lanes);

    std::cout << "--------------------------------------------" << std::endl;
    std::cout << "Test: " << name << std::endl;
    std::cout << "--------------------------------------------" << std::endl;

    for (int rnd = 0; rnd < NUM_ROUNDS; rnd++) {
        uint32_t in_words[50];
        lanes_to_state(state_lanes, in_words);

        for (int i = 0; i < 50; i++)
            dut->in[i] = in_words[i];
        dut->round_const = ROUND_CONSTS[rnd];

        dut->eval();
        tfp->dump(time_offset + rnd);

        uint32_t out_words[50];
        for (int i = 0; i < 50; i++)
            out_words[i] = dut->out[i];

        state_to_lanes(out_words, state_lanes);

        std::cout << "  Round " << std::dec << std::setw(2) << rnd
                  << "  A[0][0] = 0x"
                  << std::hex << std::setfill('0') << std::setw(16)
                  << state_lanes[0] << std::dec << std::endl;
    }

    std::string got = extract_digest_hex(state_lanes);
    bool pass = (got == expected);

    std::cout << std::endl;
    std::cout << "  Expected : " << expected << std::endl;
    std::cout << "  Got      : " << got      << std::endl;
    std::cout << "  Result   : " << (pass ? "PASS ✓" : "FAIL ✗") << std::endl;
    std::cout << std::endl;

    return pass;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vround* dut = new Vround;

    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("round_abc_trace.vcd");

    std::cout << "============================================" << std::endl;
    std::cout << "SHA-3-256 round module - NIST test vectors" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    bool all_pass = true;

    // -----------------------------------------------------------------------
    // Vector 1: SHA-3-256("abc")
    //   Padded block (136 bytes):  61 62 63 06 00...00 80
    //   Non-zero lanes:
    //     A[0][0] [idx  0] = 0x0000000006636261
    //     A[1][3] [idx 16] = 0x8000000000000000
    //   VCD time-steps: 0..23
    // -----------------------------------------------------------------------
    {
        const uint8_t msg[] = "abc";
        all_pass &= run_test(dut, tfp, 0,
            "SHA-3-256(\"abc\")",
            msg, 3,
            "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
    }

    // -----------------------------------------------------------------------
    // Vector 2: SHA-3-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    //   Message length = 56 bytes, fits in one 136-byte block.
    //   Padded block: msg[0..55] | 0x06 | 0x00...0x00 | 0x80 (byte 135)
    //   Non-zero lanes:
    //     A[0][0] [idx  0] = 0x6564636264636261  (bytes  0- 7)
    //     A[1][0] [idx  1] = 0x6766656466656463  (bytes  8-15)
    //     A[2][0] [idx  2] = 0x6968676866676665  (bytes 16-23)
    //     A[3][0] [idx  3] = 0x6b6a69686a696867  (bytes 24-31)
    //     A[4][0] [idx  4] = 0x6d6c6b6a6c6b6a69  (bytes 32-39)
    //     A[0][1] [idx  5] = 0x6f6e6d6c6e6d6c6b  (bytes 40-47)
    //     A[1][1] [idx  6] = 0x716f706e706f6e6d  (bytes 48-55)
    //     A[2][1] [idx  7] = 0x0000000000000006  (byte  56 = 0x06)
    //     A[1][3] [idx 16] = 0x8000000000000000  (byte 135 = 0x80)
    //   VCD time-steps: 50..73
    // -----------------------------------------------------------------------
    {
        const uint8_t msg[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        all_pass &= run_test(dut, tfp, 50,
            "SHA-3-256(\"abcdbcdecdef...nopq\")",
            msg, 56,
            "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376");
    }

    tfp->close();

    std::cout << "============================================" << std::endl;
    std::cout << "Overall  : " << (all_pass ? "ALL PASS ✓" : "SOME FAILED ✗") << std::endl;
    std::cout << "Trace    : round_abc_trace.vcd" << std::endl;
    std::cout << "           t=0..23  -> vector 1 (abc)" << std::endl;
    std::cout << "           t=50..73 -> vector 2 (abcdbcde...nopq)" << std::endl;
    std::cout << "============================================" << std::endl;

    delete tfp;
    delete dut;
    return all_pass ? 0 : 1;
}
