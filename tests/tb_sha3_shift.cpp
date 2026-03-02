// ============================================================================
// tb_sha3_shift.cpp — Verilator testbench for sha3_shift
//
// For each SHA-3 variant a test vector is built that fills exactly the
// variant's rate bits (from bit 0 upward) with all 1s.  After driving
// in_ready the bench waits for out_ready to pulse, then verifies:
//
//   • The low `additional_shifts * 64` bits are all 0   (trailing zeros)
//   • The remaining rate_bits above them are all 1      (shifted payload)
//   • The bits above that (if any) are also all 0       (never written)
//
// Variant   rate   additional_shifts   trailing zero bits
// -------   ----   -----------------   ------------------
// SHA3_224  1152         0                      0
// SHA3_256  1088         1                     64
// SHA3_384   832         5                    320
// SHA3_512   576         9                    576
// ============================================================================

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "Vsha3_shift.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// ─── constants ───────────────────────────────────────────────────────────────
static constexpr int MAX_RATE_BITS  = 1152;
static constexpr int WORDS          = MAX_RATE_BITS / 32; // 36

// Verilated enum values must match sha3_pkg sha3_variant_t encoding
static constexpr uint8_t V_SHA3_224 = 0;
static constexpr uint8_t V_SHA3_256 = 1;
static constexpr uint8_t V_SHA3_384 = 2;
static constexpr uint8_t V_SHA3_512 = 3;

// ─── helpers ─────────────────────────────────────────────────────────────────
struct VariantInfo {
    const char* name;
    uint8_t     code;
    int         rate_bits;         // active bits for this variant
    int         additional_shifts; // 64-bit left-shifts performed by DUT
};

static const VariantInfo VARIANTS[] = {
    { "SHA3_224", V_SHA3_224, 1152, 0 },
    { "SHA3_256", V_SHA3_256, 1088, 1 },
    { "SHA3_384", V_SHA3_384,  832, 5 },
    { "SHA3_512", V_SHA3_512,  576, 9 },
};

// ─── testbench class ─────────────────────────────────────────────────────────
class Sha3ShiftTb {
public:
    Vsha3_shift*  dut;
    VerilatedVcdC* trace;
    uint64_t       sim_time;

    Sha3ShiftTb() : sim_time(0), trace(nullptr) {
        dut          = new Vsha3_shift;
        dut->clk     = 0;
        dut->reset   = 1;
        dut->in_ready = 0;
        dut->variant  = 0;
        std::memset(dut->in, 0, sizeof(dut->in));
    }

    ~Sha3ShiftTb() {
        if (trace) { trace->close(); delete trace; }
        dut->final();
        delete dut;
    }

    void open_trace(const std::string& filename) {
        if (!trace) {
            trace = new VerilatedVcdC;
            dut->trace(trace, 99);
            trace->open(filename.c_str());
        }
    }

    void tick() {
        dut->clk = 1; dut->eval();
        if (trace) trace->dump(sim_time++);
        dut->clk = 0; dut->eval();
        if (trace) trace->dump(sim_time++);
    }

    void reset_dut() {
        dut->reset    = 1;
        dut->in_ready = 0;
        for (int i = 0; i < 5; ++i) tick();
        dut->reset = 0;
        tick();
    }

    // Build a 36-word input vector: lower rate_bits all 1, rest 0
    void build_input(int rate_bits) {
        std::memset(dut->in, 0, sizeof(dut->in));
        int full_words = rate_bits / 32;
        int remainder  = rate_bits % 32;
        for (int w = 0; w < full_words; ++w)
            dut->in[w] = 0xFFFFFFFFu;
        if (remainder)
            dut->in[full_words] = (1u << remainder) - 1u;
    }

    // Print all WORDS 32-bit words, MSW first
    void print_words(const char* label, const uint32_t* arr, int count) {
        std::cout << "  " << label << ":\n";
        for (int w = count - 1; w >= 0; --w) {
            std::cout << "    [" << std::setw(2) << std::dec << w << "] 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << arr[w]
                      << "\n";
        }
        std::cout << std::dec;
    }

    // Verify output; returns true on PASS
    bool verify_output(const VariantInfo& v) {
        int trailing_zero_bits  = v.additional_shifts * 64;
        int trailing_zero_words = trailing_zero_bits / 32; // always integer since shifts are 64-bit
        int ones_words          = v.rate_bits / 32;        // rate always 64-bit aligned

        bool pass = true;

        // ── trailing zero words (indices 0 .. trailing_zero_words-1) ──
        for (int w = 0; w < trailing_zero_words; ++w) {
            if (dut->out[w] != 0u) {
                std::cout << "  [FAIL] trailing zero word[" << w << "] = 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << dut->out[w]
                          << " (expected 0x00000000)\n" << std::dec;
                pass = false;
            }
        }

        // ── ones words (indices trailing_zero_words .. trailing_zero_words+ones_words-1) ──
        for (int w = trailing_zero_words; w < trailing_zero_words + ones_words; ++w) {
            if (dut->out[w] != 0xFFFFFFFFu) {
                std::cout << "  [FAIL] ones word[" << w << "] = 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << dut->out[w]
                          << " (expected 0xFFFFFFFF)\n" << std::dec;
                pass = false;
            }
        }

        // ── any upper words above the ones region should also be 0 ──
        for (int w = trailing_zero_words + ones_words; w < WORDS; ++w) {
            if (dut->out[w] != 0u) {
                std::cout << "  [FAIL] upper zero word[" << w << "] = 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << dut->out[w]
                          << " (expected 0x00000000)\n" << std::dec;
                pass = false;
            }
        }

        if (pass) {
            std::cout << "  [PASS] " << ones_words << " ones words at [" 
                      << trailing_zero_words << ".." 
                      << trailing_zero_words + ones_words - 1 << "], "
                      << trailing_zero_words << " trailing zero words at [0.."
                      << (trailing_zero_words > 0 ? trailing_zero_words - 1 : 0) << "]\n";
        }
        return pass;
    }

    // Run a single variant test; returns true on PASS
    bool run_test(const VariantInfo& v) {
        std::cout << "\n========================================\n"
                  << "Test: " << v.name << "\n"
                  << "  rate_bits=" << v.rate_bits
                  << "  additional_shifts=" << v.additional_shifts << "\n"
                  << "========================================\n";

        reset_dut();

        // Configure variant and build all-F input
        dut->variant = v.code;
        build_input(v.rate_bits);
        dut->eval();

        std::cout << "--- Input (word[35]=MSB, word[0]=LSB) ---\n";
        print_words("in", dut->in, WORDS);

        // Assert in_ready for exactly one cycle to trigger the shift sequence
        dut->in_ready = 1;
        tick();
        dut->in_ready = 0;

        // Wait for out_ready pulse (max 32 cycles should be plenty)
        bool got_out_ready = false;
        for (int cyc = 0; cyc < 32; ++cyc) {
            tick();
            if (dut->out_ready) {
                std::cout << "  out_ready pulsed after " << (cyc + 1) << " cycle(s)\n";
                std::cout << "--- Output (word[35]=MSB, word[0]=LSB) ---\n";
                print_words("out", dut->out, WORDS);
                got_out_ready = true;
                break;
            }
        }

        if (!got_out_ready) {
            std::cout << "  [FAIL] out_ready never pulsed within timeout\n";
            return false;
        }

        return verify_output(v);
    }
};

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    Sha3ShiftTb tb;
    tb.open_trace("sha3_shift.vcd");

    int passed = 0;
    int total  = static_cast<int>(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

    for (int i = 0; i < total; ++i) {
        if (tb.run_test(VARIANTS[i]))
            ++passed;
    }

    std::cout << "\n========================================\n"
              << "Results: " << passed << " / " << total << " tests passed\n"
              << "========================================\n";

    return (passed == total) ? 0 : 1;
}
