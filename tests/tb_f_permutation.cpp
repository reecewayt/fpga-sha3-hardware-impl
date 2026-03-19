/*
 * Testbench: f_permutation
 * ========================
 * Verifies the Keccak-f[1600] permutation module against test vectors
 * generated from the SHA-3 specification.
 *
 * Timing (all relative to the cycle where in_ready is high):
 *   Accept cycle   : rc[0] applied, XOR input latched, rnd_idx → 1
 *   Cycles 1–22    : rc[1]..rc[22] applied
 *   Cycle 23       : rc[23] applied, done = 1
 *   Cycle 24       : out_ready = 1, result stable in dut->out
 *
 * Chain tests (prev_result_idx >= 0):
 *   The hardware state register is left as-is after a completed permutation;
 *   the next in_ready XORs the new block into it.  No reset between blocks.
 *   A chain test is skipped if its dependency failed (state would be wrong).
 *
 * Port bit layout (Verilator WData arrays, little-endian word order):
 *   dut->in[35]  = in[1151:1120]  (MSB)    dut->in[0]  = in[31:0]   (LSB)
 *   dut->out[49] = state[1599:1568](MSB)   dut->out[0] = state[31:0](LSB)
 *
 * Our arrays are MSB-first, so:
 *   in_words[k]       --> dut->in[35 - k]
 *   expected_out[k]   --> dut->out[49 - k]
 *
 * Note: This testbench was developed with the assistance of GitHub Copilot.
 *
 */

#include <iostream>
#include <iomanip>
#include <cstring>
#include <string>
#include "Vf_permutation.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "f_permutation_test_vectors.h"

constexpr int IN_WORDS    = 36;   // MAX_RATE / 32 (for Verilator interface)
constexpr int STATE_WORDS = 50;   // STATE_WIDTH / 32 (for Verilator interface)

// ── Testbench class ───────────────────────────────────────────────────────────

class FPermTB {
public:
    Vf_permutation* dut;
    VerilatedVcdC*  trace    = nullptr;
    uint64_t        sim_time = 0;

    FPermTB() {
        dut           = new Vf_permutation;
        dut->clk      = 0;
        dut->reset    = 1;
        dut->in_ready = 0;
        for (int i = 0; i < IN_WORDS; i++)
            dut->in[i] = 0;
        dut->eval();
    }

    ~FPermTB() {
        if (trace) {
            trace->close();
            delete trace;
        }
        dut->final();
        delete dut;
    }

    void open_trace(const std::string& filename) {
        if (!trace) {
            trace = new VerilatedVcdC;
            dut->trace(trace, 99);
            trace->open(filename.c_str());
            std::cout << "Opened VCD trace: " << filename << std::endl;
        }
    }

    void close_trace() {
        if (trace) {
            trace->close();
            delete trace;
            trace = nullptr;
        }
    }

    // ── Clock ───────────────────────────────────────────────────────────────
    void tick() {
        dut->clk = 1;  dut->eval();
        if (trace) trace->dump(sim_time);
        ++sim_time;
        dut->clk = 0;  dut->eval();
        if (trace) trace->dump(sim_time);
        ++sim_time;
    }

    // ── Reset ───────────────────────────────────────────────────────────────
    void do_reset() {
        dut->reset    = 1;
        dut->in_ready = 0;
        for (int c = 0; c < 5; c++) tick();
        dut->reset = 0;
        tick();
    }

    // ── Apply in_words (64-bit, MSB-first) to dut->in (32-bit interface) ────
    // Input: in_words[18] where each is a 64-bit lane
    // Output: dut->in[36] where each is a 32-bit word
    void set_in(const uint64_t* in_words_64) {
        // Convert 18 x 64-bit to 36 x 32-bit, then apply to Verilator interface
        for (int k = 0; k < 18; k++) {
            uint32_t hi = (in_words_64[k] >> 32) & 0xFFFFFFFFU;
            uint32_t lo = in_words_64[k] & 0xFFFFFFFFU;
            // Map to Verilator: in_words[k] (MSB-first) -> dut->in[35-k*2] and [34-k*2]
            dut->in[IN_WORDS - 1 - (k * 2)] = hi;      // hi at index 35-(2k)
            dut->in[IN_WORDS - 2 - (k * 2)] = lo;      // lo at index 34-(2k)
        }
    }
    
    // ── Legacy 32-bit interface (kept for backward compatibility if needed) ──
    void set_in_32(const uint32_t* in_words) {
        for (int k = 0; k < IN_WORDS; k++)
            dut->in[IN_WORDS - 1 - k] = in_words[k];
    }

    // ── Print input summary ───────────────────────────────────────────────────
    void print_input_summary(const FPermTestVector& tv) {
        std::cout << "     Input (first 3 lanes, LE format):" << std::endl;
        for (int lane = 0; lane < 3 && lane < 18; lane++) {
            uint64_t lane_val = tv.in_words[lane];
            if (lane_val == 0 && lane > 0) continue;  // Skip zero lanes after first
            
            int x = lane % 5;
            int y = lane / 5;
            std::cout << "       A[" << x << "][" << y << "] = 0x"
                      << std::hex << std::setw(16) << std::setfill('0') << lane_val;
            
            // Show human-readable interpretation for non-zero lanes
            if (lane_val != 0) {
                std::cout << "  (";
                bool first = true;
                for (int b = 0; b < 8; b++) {
                    uint8_t byte = (lane_val >> (b * 8)) & 0xFF;
                    if (byte != 0) {
                        if (!first) std::cout << " ";
                        if (byte >= 0x20 && byte <= 0x7E) {
                            std::cout << "[" << b << "]='" << char(byte) << "'";
                        } else {
                            std::cout << "[" << b << "]=0x" << std::hex << std::setw(2) << int(byte);
                        }
                        first = false;
                    }
                }
                std::cout << ")";
            }
            std::cout << std::dec << std::endl;
        }
    }

    // ── Print expected output digest ──────────────────────────────────────────
    void print_expected_digest(const FPermTestVector& tv, int digest_bits = 256) {
        int digest_words = digest_bits / 64;  // Now working with 64-bit words
        std::cout << "     Expected digest (" << digest_bits << " bits):" << std::endl;
        std::cout << "       0x";
        for (int k = 0; k < digest_words; k++) {
            std::cout << std::hex << std::setw(16) << std::setfill('0') << tv.expected_out[k];
        }
        std::cout << std::dec << std::endl;
    }

    // ── Print actual output digest ────────────────────────────────────────────
    void print_actual_digest(int digest_bits = 256) {
        int digest_words = digest_bits / 64;  // Now working with 64-bit words
        std::cout << "     Actual digest   (" << digest_bits << " bits):" << std::endl;
        std::cout << "       0x";
        for (int k = 0; k < digest_words; k++) {
            // Combine 32-bit words from hardware into 64-bit
            uint32_t hi = dut->out[STATE_WORDS - 1 - (k * 2)];
            uint32_t lo = dut->out[STATE_WORDS - 2 - (k * 2)];
            std::cout << std::hex << std::setw(16) << std::setfill('0') 
                      << ((uint64_t(hi) << 32) | lo);
        }
        std::cout << std::dec << std::endl;
    }

    // ── Run one permutation and return pass/fail ─────────────────────────────
    // Called after reset OR after a previous permutation completed.
    bool run(const FPermTestVector& tv, bool verbose = false) {
        // Show what we're testing
        print_input_summary(tv);
        
        // Determine digest size from test name
        int digest_bits = 256;  // default
        std::string name_str(tv.name);
        if (name_str.find("512") != std::string::npos) {
            digest_bits = 512;
        } else if (name_str.find("256") != std::string::npos) {
            digest_bits = 256;
        }
        
        // Feed the block for exactly one cycle
        set_in(tv.in_words);
        dut->in_ready = 1;
        tick();
        dut->in_ready = 0;

        // Wait for out_ready (max 30 cycles; exactly 24 expected)
        int cycles = 0;
        while (!dut->out_ready && cycles < 30) {
            tick();
            ++cycles;
        }

        if (!dut->out_ready) {
            std::cout << "[FAIL] " << tv.name
                      << " — timeout waiting for out_ready" << std::endl;
            return false;
        }

        // Compare output (convert 64-bit expected to 32-bit for comparison)
        bool pass = true;
        int mismatch_count = 0;
        for (int k = 0; k < 25; k++) {  // 25 x 64-bit = 50 x 32-bit
            // Split expected 64-bit word into hi/lo 32-bit
            uint32_t exp_hi = (tv.expected_out[k] >> 32) & 0xFFFFFFFFU;
            uint32_t exp_lo = tv.expected_out[k] & 0xFFFFFFFFU;
            
            // Get actual 32-bit words from hardware (MSB-first indexing)
            uint32_t got_hi = dut->out[STATE_WORDS - 1 - (k * 2)];
            uint32_t got_lo = dut->out[STATE_WORDS - 2 - (k * 2)];
            
            if (got_hi != exp_hi || got_lo != exp_lo) {
                pass = false;
                mismatch_count++;
            }
        }

        if (!pass) {
            std::cout << "[FAIL] " << tv.name << " — " << mismatch_count << " lane mismatches" << std::endl;
            print_expected_digest(tv, digest_bits);
            print_actual_digest(digest_bits);
            
            if (verbose) {
                std::cout << "     First mismatches:" << std::endl;
                int shown = 0;
                for (int k = 0; k < 25 && shown < 5; k++) {
                    uint32_t exp_hi = (tv.expected_out[k] >> 32) & 0xFFFFFFFFU;
                    uint32_t exp_lo = tv.expected_out[k] & 0xFFFFFFFFU;
                    uint32_t got_hi = dut->out[STATE_WORDS - 1 - (k * 2)];
                    uint32_t got_lo = dut->out[STATE_WORDS - 2 - (k * 2)];
                    
                    if (got_hi != exp_hi || got_lo != exp_lo) {
                        int x = (k % 5);
                        int y = (k / 5);
                        std::cout << "       A[" << x << "][" << y << "]"
                                  << ": exp=0x" << std::hex << std::setw(16) << std::setfill('0') 
                                  << ((uint64_t(exp_hi) << 32) | exp_lo)
                                  << " got=0x" << std::setw(16)
                                  << ((uint64_t(got_hi) << 32) | got_lo)
                                  << std::dec << std::setfill(' ') << std::endl;
                        shown++;
                    }
                }
            }
        } else {
            std::cout << "[PASS] " << tv.name
                      << "  (computed in " << cycles << " cycles)"
                      << std::endl;
            print_expected_digest(tv, digest_bits);
            print_actual_digest(digest_bits);
        }

        return pass;
    }
};


// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    bool        enable_trace = false;
    bool        verbose      = false;
    std::string trace_file   = "f_permutation_trace.vcd";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--trace") {
            enable_trace = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                trace_file = argv[i + 1];
                ++i;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --trace [file]  Enable VCD waveform output (default: f_permutation_trace.vcd)" << std::endl;
            std::cout << "  -v, --verbose   Show detailed test information including actual outputs" << std::endl;
            std::cout << "  -h, --help      Show this help message" << std::endl;
            return 0;
        }
    }

    Verilated::traceEverOn(true);

    FPermTB tb;

    if (enable_trace)
        tb.open_trace(trace_file);

    std::cout << "========================================" << std::endl;
    std::cout << "f_permutation Module Test Suite"         << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Test vectors: " << NUM_F_PERM_TEST_VECTORS << std::endl;
    if (verbose)
        std::cout << "Verbose mode: ON" << std::endl;
    std::cout << std::endl;

    int passed = 0, failed = 0, skipped = 0;

    // Track which tests passed so we know whether a chain test is safe to run
    bool ok[NUM_F_PERM_TEST_VECTORS] = {};

    for (int i = 0; i < NUM_F_PERM_TEST_VECTORS; i++) {
        const FPermTestVector& tv = F_PERM_TEST_VECTORS[i];

        std::cout << "\n[" << i << "] " << tv.name << std::endl;
        std::cout << "     " << tv.description << std::endl;

        if (tv.prev_result_idx == -1) {
            // Independent test — start from a clean reset
            tb.do_reset();
        } else {
            // Chain test — hardware state must carry over from the dependency
            int dep = tv.prev_result_idx;
            if (!ok[dep]) {
                std::cout << "[SKIP] Dependency [" << dep
                          << "] failed; chain test skipped." << std::endl;
                ++skipped;
                continue;
            }
            std::cout << "     Chained from test [" << dep << "]" << std::endl;
            // No reset; hardware holds the output of the previous permutation
        }

        bool result = tb.run(tv, verbose);
        ok[i] = result;
        if (result) ++passed;
        else        ++failed;
    }

    int total = NUM_F_PERM_TEST_VECTORS - skipped;
    std::cout << "\n========================================" << std::endl;
    std::cout << "Passed:  " << passed  << " / " << total << std::endl;
    std::cout << "Failed:  " << failed                     << std::endl;
    if (skipped)
        std::cout << "Skipped: " << skipped << " (chain dependency failed)" << std::endl;

    if (enable_trace) {
        tb.close_trace();
        std::cout << "\nVCD trace written to: " << trace_file << std::endl;
    }

    if (failed == 0 && skipped == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
