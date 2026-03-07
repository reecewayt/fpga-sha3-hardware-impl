// Testbench for the keccak top-level module.
// Drives the full keccak pipeline (padder + f_permutation) and compares the
// hash digest output against official NIST SHA-3 test vectors.

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include "Vkeccak.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "sha3_nist_vectors.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Return the number of 32-bit digest words for a given variant.
static uint32_t digest_words(SHA3Variant v) {
    switch (v) {
        case SHA3Variant::SHA3_224: return 7;
        case SHA3Variant::SHA3_256: return 8;
        case SHA3Variant::SHA3_384: return 12;
        case SHA3Variant::SHA3_512: return 16;
        default:                    return 16;
    }
}

static std::string variant_name(SHA3Variant v) {
    switch (v) {
        case SHA3Variant::SHA3_224: return "SHA3-224";
        case SHA3Variant::SHA3_256: return "SHA3-256";
        case SHA3Variant::SHA3_384: return "SHA3-384";
        case SHA3Variant::SHA3_512: return "SHA3-512";
        default:                    return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Testbench class
// ---------------------------------------------------------------------------

class KeccakTestbench {
public:
    Vkeccak*      dut;
    VerilatedVcdC* trace;
    uint64_t       sim_time;

    KeccakTestbench() : sim_time(0), trace(nullptr) {
        dut           = new Vkeccak;
        dut->clk      = 0;
        dut->reset    = 1;
        dut->in       = 0ULL;
        dut->in_ready = 0;
        dut->is_last  = 0;
        dut->byte_num = 0;
        dut->variant  = 0;
    }

    ~KeccakTestbench() {
        if (trace) { trace->close(); delete trace; }
        dut->final();
        delete dut;
    }

    void open_trace(const std::string& filename) {
        if (!trace) {
            trace = new VerilatedVcdC;
            dut->trace(trace, 99);
            trace->open(filename.c_str());
            std::cout << "Opened VCD trace: " << filename << "\n";
        }
    }

    void close_trace() {
        if (trace) { trace->close(); delete trace; trace = nullptr; }
    }

    void tick() {
        dut->clk = 1; dut->eval();
        if (trace) trace->dump(sim_time);
        sim_time++;
        dut->clk = 0; dut->eval();
        if (trace) trace->dump(sim_time);
        sim_time++;
    }

    void reset_dut() {
        dut->reset    = 1;
        dut->in_ready = 0;
        dut->is_last  = 0;
        for (int i = 0; i < 5; i++) tick();
        dut->reset = 0;
        tick();
    }

    // -----------------------------------------------------------------------
    // Feed all input words to the keccak module using the same protocol as
    // the padder testbench.
    // -----------------------------------------------------------------------
    void feed_input(const SHA3NISTVector& tv) {
        if (tv.input_words.empty()) {
            // Empty message: assert in_ready + is_last with byte_num=0.
            std::cout << "  Empty message - asserting is_last\n";
            dut->in       = 0;
            dut->in_ready = 1;
            dut->is_last  = 1;
            dut->byte_num = 0;
            tick();
            dut->in_ready = 0;
            dut->is_last  = 0;
            tick();
            return;
        }

        std::cout << "  Feeding " << tv.input_words.size() << " word(s)...\n";

        for (size_t i = 0; i < tv.input_words.size(); i++) {
            bool is_last_word = (i == tv.input_words.size() - 1);

            // Wait if the padder's internal buffer is full before pushing more data.
            int stall = 0;
            while (dut->buffer_full && stall < 200) { tick(); stall++; }
            if (stall >= 200) {
                std::cerr << "[ERROR] buffer_full stuck high\n";
                return;
            }

            dut->in       = tv.input_words[i];
            dut->in_ready = 1;

            if (is_last_word && tv.remaining_bytes != 0) {
                // Partial last word: byte_num = number of valid bytes (1-7).
                dut->is_last  = 1;
                dut->byte_num = tv.remaining_bytes;
                std::cout << "    word[" << i << "] = 0x"
                          << std::hex << std::setw(16) << std::setfill('0')
                          << tv.input_words[i]
                          << " (LAST, " << std::dec << tv.remaining_bytes << " bytes)\n";
            } else if (is_last_word && tv.remaining_bytes == 0) {
                // Full last 64-bit word: send WITHOUT is_last; follow up with
                // a separate is_last transaction (byte_num = 0) afterwards.
                dut->is_last  = 0;
                dut->byte_num = 7;
                std::cout << "    word[" << i << "] = 0x"
                          << std::hex << std::setw(16) << std::setfill('0')
                          << tv.input_words[i]
                          << " (full word, is_last follows)\n";
            } else {
                dut->is_last  = 0;
                dut->byte_num = 7;
                std::cout << "    word[" << i << "] = 0x"
                          << std::hex << std::setw(16) << std::setfill('0')
                          << tv.input_words[i] << std::dec << "\n";
            }

            tick();
            dut->in_ready = 0;
            dut->is_last  = 0;
            tick();

            // For a full last word, send the terminating is_last transaction.
            // Regression note: for exact-rate endings (e.g. SHA3-512 2 full
            // blocks + padding-only block), asserting trailing is_last while
            // buffer_full is still high can miss the pulse and drop the extra
            // padding block request.
            if (is_last_word && tv.remaining_bytes == 0) {
                // If the final full data word filled a rate block, wait until
                // padder can accept the trailing is_last pulse that requests
                // the extra padding-only block (0x06 ... 0x80).
                int pad_stall = 0;
                while (dut->buffer_full && pad_stall < 400) { tick(); pad_stall++; }
                if (pad_stall >= 400) {
                    std::cerr << "[ERROR] buffer_full stuck high before trailing is_last\n";
                    return;
                }

                std::cout << "    (trailing is_last, byte_num=0)\n";
                dut->in       = 0;
                dut->in_ready = 1;
                dut->is_last  = 1;
                dut->byte_num = 0;
                tick();
                dut->in_ready = 0;
                dut->is_last  = 0;
                tick();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Compare the DUT digest output against expected words.
    // The keccak module outputs 512 bits on out[511:0].
    // In Verilator: dut->out[15] = bits [511:480], dut->out[0] = bits [31:0]
    // The hash digest is placed at the MSB end for all variants:
    //   SHA3-224: out[511:224] contains digest (8 words at indices 15..8)
    //   SHA3-256: out[511:256] contains digest (8 words at indices 15..8)
    //   SHA3-384: out[511:0] contains digest (16 words, all indices)
    //   SHA3-512: out[511:0] contains digest (16 words, all indices)
    // Correct indexing: digest_word[i] = dut->out[15 - i]
    // -----------------------------------------------------------------------
    bool verify_digest(const SHA3NISTVector& tv) {
        uint32_t dw   = digest_words(tv.variant);
        bool     pass = true;

        std::cout << "\n  Expected digest (" << variant_name(tv.variant) << "):\n    ";
        for (uint32_t i = 0; i < dw; i++)
            std::cout << std::hex << std::setw(8) << std::setfill('0')
                      << tv.expected_digest[i] << " ";
        std::cout << "\n  Got:\n    ";

        for (uint32_t i = 0; i < dw; i++) {
            uint32_t actual = dut->out[15 - i];
            std::cout << std::hex << std::setw(8) << std::setfill('0') << actual << " ";
        }
        std::cout << "\n";

        // Compare using correct MSB-first indexing
        for (uint32_t i = 0; i < dw; i++) {
            uint32_t actual   = dut->out[15 - i];
            uint32_t expected = tv.expected_digest[i];
            if (actual != expected) {
                std::cout << "  [MISMATCH] word[" << i << "]: expected 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << expected
                          << " got 0x" << std::setw(8) << std::setfill('0') << actual
                          << std::dec << "\n";
                pass = false;
            }
        }
        return pass;
    }

    // -----------------------------------------------------------------------
    // Run a single NIST test vector end-to-end.
    // -----------------------------------------------------------------------
    bool run_test(const SHA3NISTVector& tv) {
        std::cout << "\n----------------------------------------\n";
        std::cout << "[" << tv.name << "]\n";
        std::cout << tv.description << "\n";
        std::cout << "----------------------------------------\n";

        reset_dut();

        dut->variant = static_cast<uint8_t>(tv.variant);
        tick();

        feed_input(tv);

        // Wait for out_ready (the complete hash is available).
        const int MAX_WAIT = 2000;
        int cycles = 0;
        while (!dut->out_ready && cycles < MAX_WAIT) {
            tick();
            cycles++;
        }

        if (cycles >= MAX_WAIT) {
            std::cout << "[FAIL] " << tv.name << " - timeout waiting for out_ready\n";
            return false;
        }

        std::cout << "  out_ready after " << cycles << " cycles\n";

        bool pass = verify_digest(tv);

        if (pass)
            std::cout << "[PASS] " << tv.name << "\n";
        else
            std::cout << "[FAIL] " << tv.name << "\n";

        return pass;
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    bool        enable_trace = false;
    std::string trace_file   = "keccak_trace.vcd";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--trace") {
            enable_trace = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                trace_file = argv[++i];
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--trace [file.vcd]]\n";
            return 0;
        }
    }

    std::cout << "========================================\n";
    std::cout << "Keccak (SHA-3) Top-Level Test Suite\n";
    std::cout << "========================================\n";
    std::cout << "Test vectors: " << SHA3_NIST_VECTORS.size() << "\n";

    KeccakTestbench tb;
    if (enable_trace) tb.open_trace(trace_file);

    int passed = 0, failed = 0;
    int test_num = 0;
    for (const auto& tv : SHA3_NIST_VECTORS) {
        test_num++;
        if (tb.run_test(tv)) passed++;
        else                 failed++;
    }

    if (enable_trace) {
        tb.close_trace();
        std::cout << "\nVCD trace written to: " << trace_file << "\n";
    }

    std::cout << "\n========================================\n";
    std::cout << std::dec;  // Ensure decimal output
    std::cout << "Passed: " << passed << " / " << SHA3_NIST_VECTORS.size() << "\n";
    std::cout << "Failed: " << failed << " / " << SHA3_NIST_VECTORS.size() << "\n";

    if (failed == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!\n";
        return 1;
    }
}
