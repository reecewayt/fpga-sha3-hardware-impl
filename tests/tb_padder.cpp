/*
 * Testbench: padder
 * =================
 * Verifies the padder module (64-bit word format) that implements pad10*1
 * padding as specified in FIPS 202 for SHA-3.
 *
 * Data Flow: keccak.sv byte swap → Padder → f_permutation
 *
 * The test vectors simulate data AFTER keccak.sv's byte swap operation,
 * so all data is in little-endian format. The padder accepts 64-bit words
 * and produces padded message blocks ready for the f_permutation module.
 *
 * Note: This testbench was developed with the assistance of GitHub Copilot.
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include "Vpadder.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "padder_test_vectors.h"

class PadderTestbench {
public:
    Vpadder* dut;
    VerilatedVcdC* trace;
    uint64_t sim_time;
    
    PadderTestbench() : sim_time(0), trace(nullptr) {
        dut = new Vpadder;
        dut->clk = 0;
        dut->reset = 1;
        dut->in = 0;
        dut->in_ready = 0;
        dut->is_last = 0;
        dut->byte_num = 0;
        dut->variant = 0;
        dut->f_ack = 0;
    }
    
    ~PadderTestbench() {
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
    
    void tick() {
        dut->clk = 1;
        dut->eval();
        if (trace) trace->dump(sim_time);
        sim_time++;
        
        dut->clk = 0;
        dut->eval();
        if (trace) trace->dump(sim_time);
        sim_time++;
    }
    
    void reset_dut() {
        dut->reset = 1;
        dut->in_ready = 0;
        dut->is_last = 0;
        dut->f_ack = 0;
        for (int i = 0; i < 5; i++) {
            tick();
        }
        dut->reset = 0;
        tick();
    }
    
    // Read 64-bit word from padder output (MSB first, verilator provides as array of 32-bit)
    uint64_t read_output_word_64(uint32_t word_idx, uint32_t rate_words_64) {
        // Padder shifts data left, so output is MSB-first within the rate boundary.
        // For variant rate R (in 64-bit words), after R shifts:
        //   bits [R*64-1:0] contain the padded data
        //   bits [1151:R*64] are zero
        //
        // Verilator maps dut->out[k] = bits [(k+1)*32-1 : k*32]
        // So word_idx 0 (MSB) = bits [R*64-1 : (R-1)*64]
        //                     = dut->out[2*(rate_words_64-1)-1 : 2*(rate_words_64-1)]
        
        uint32_t verilator_idx_lo = (rate_words_64 - 1 - word_idx) * 2;
        uint32_t verilator_idx_hi = verilator_idx_lo + 1;
        
        uint64_t hi = dut->out[verilator_idx_hi];
        uint64_t lo = dut->out[verilator_idx_lo];
        
        return (hi << 32) | lo;
    }
    
    bool verify_block(const std::vector<uint64_t>& expected, uint32_t rate_words_64,
                      const std::string& label, bool verbose = false) {
        bool pass = true;
        int mismatch_count = 0;
        
        for (uint32_t i = 0; i < rate_words_64; i++) {
            uint64_t actual = read_output_word_64(i, rate_words_64);
            uint64_t expected_word = expected[i];
            
            if (actual != expected_word) {
                pass = false;
                mismatch_count++;
                if (verbose || mismatch_count <= 3) {
                    std::cout << "[FAIL] " << label << " Word " << std::setw(2) << i
                              << ": Expected 0x" << std::hex << std::setw(16) << std::setfill('0')
                              << expected_word << " but got 0x" << std::setw(16) << std::setfill('0')
                              << actual << std::dec << std::endl;
                }
            } else if (verbose) {
                if (i < 3 || i >= rate_words_64 - 2) {
                    std::cout << "[PASS] " << label << " Word " << std::setw(2) << i << ": 0x"
                              << std::hex << std::setw(16) << std::setfill('0') << actual
                              << std::dec << std::endl;
                } else if (i == 3) {
                    std::cout << "  ... (middle words omitted) ..." << std::endl;
                }
            }
        }
        
        if (!pass && mismatch_count > 3) {
            std::cout << "  ... and " << (mismatch_count - 3) << " more mismatches" << std::endl;
        }
        
        return pass;
    }

    bool run_test(const PadderTestVector& test, bool verbose = false) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test: " << test.name << std::endl;
        std::cout << "Description: " << test.description << std::endl;
        std::cout << "Message: " << test.input_words_64.size() << " words (64-bit LE), "
                  << (int)test.remaining_bytes << " remaining bytes" << std::endl;
        std::cout << "========================================" << std::endl;
        
        reset_dut();
        
        // Set variant
        dut->variant = static_cast<uint8_t>(test.variant);
        tick();
        
        // Track intermediate blocks
        size_t inter_idx = 0;
        const int max_cycles = 500;

        if (test.input_words_64.size() == 0) {
            std::cout << "Empty message - sending is_last with byte_num=0" << std::endl;
            dut->in = 0;
            dut->in_ready = 1;
            dut->is_last = 1;
            dut->byte_num = 0;
            tick();

            dut->in_ready = 0;
            dut->is_last = 0;
            tick();
        } else {
            std::cout << "Feeding " << test.input_words_64.size() << " input words (64-bit LE)..." << std::endl;

            for (size_t i = 0; i < test.input_words_64.size(); i++) {
                // Check for intermediate blocks before feeding next word
                if (dut->out_ready && inter_idx < test.intermediate_blocks.size()) {
                    std::cout << "\n--- Intermediate block " << inter_idx << " ready ---" << std::endl;
                    bool blk_pass = verify_block(
                        test.intermediate_blocks[inter_idx], test.rate_words_64,
                        "IntermediateBlock[" + std::to_string(inter_idx) + "]", verbose);
                    if (!blk_pass) {
                        std::cout << "[FAIL] Intermediate block " << inter_idx << " mismatch\n" << std::endl;
                        return false;
                    }
                    inter_idx++;
                    dut->f_ack = 1;
                    tick();
                    dut->f_ack = 0;
                    tick();
                }

                bool is_last_word = (i == test.input_words_64.size() - 1);

                dut->in = test.input_words_64[i];
                dut->in_ready = 1;
                dut->is_last = is_last_word ? 1 : 0;

                // Determine byte_num for last word (0-7 for 64-bit words)
                if (is_last_word && test.remaining_bytes != 0) {
                    // Partial last word: byte_num = number of valid bytes (1-7)
                    dut->byte_num = test.remaining_bytes;
                    if (verbose) {
                        std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(16)
                                 << std::setfill('0') << test.input_words_64[i]
                                 << " (LAST, " << std::dec << test.remaining_bytes << " bytes)" << std::endl;
                    }
                } else {
                    // Full 8-byte word
                    dut->byte_num = 7;  // All 8 bytes valid (byte_num=7 means bytes [7:0] valid)
                    if (is_last_word) {
                        if (verbose) {
                            std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(16)
                                     << std::setfill('0') << test.input_words_64[i]
                                     << " (full word, is_last with byte_num=0 sent after)" << std::dec << std::endl;
                        }
                    } else {
                        if (verbose) {
                            std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(16)
                                     << std::setfill('0') << test.input_words_64[i]
                                     << " (8 bytes)" << std::dec << std::endl;
                        }
                    }
                }

                // For a full last word, send it without is_last
                // Then send separate is_last=1 with byte_num=0
                if (is_last_word && test.remaining_bytes == 0) {
                    dut->is_last = 0;
                }

                tick();

                // Deassert in_ready after word accepted
                dut->in_ready = 0;
                dut->is_last = 0;
                tick();

                // For full last word: drain any intermediate block, then send trailing is_last
                if (is_last_word && test.remaining_bytes == 0) {
                    // Drain intermediate blocks
                    while (dut->out_ready && inter_idx < test.intermediate_blocks.size()) {
                        std::cout << "\n--- Intermediate block " << inter_idx
                                  << " ready (pre-is_last drain) ---" << std::endl;
                        bool blk_pass = verify_block(
                            test.intermediate_blocks[inter_idx], test.rate_words_64,
                            "IntermediateBlock[" + std::to_string(inter_idx) + "]", verbose);
                        if (!blk_pass) {
                            std::cout << "[FAIL] Intermediate block " << inter_idx << " mismatch" << std::endl;
                            return false;
                        }
                        inter_idx++;
                        dut->f_ack = 1;
                        tick();
                        dut->f_ack = 0;
                        tick();
                    }
                    
                    if (verbose) {
                        std::cout << "  (sending is_last with byte_num=0 for full-word end)" << std::endl;
                    }
                    dut->in = 0;
                    dut->in_ready = 1;
                    dut->is_last = 1;
                    dut->byte_num = 0;
                    tick();
                    dut->in_ready = 0;
                    dut->is_last = 0;
                    tick();
                }
            }
        }
        
        // Drain remaining intermediate blocks
        while (inter_idx < test.intermediate_blocks.size()) {
            int drain_cycles = 0;
            std::cout << "Waiting for intermediate block " << inter_idx << "..." << std::endl;
            while (!dut->out_ready && drain_cycles < max_cycles) {
                tick();
                drain_cycles++;
            }
            if (drain_cycles >= max_cycles) {
                std::cout << "[FAIL] Timeout waiting for intermediate block " << inter_idx << std::endl;
                return false;
            }
            std::cout << "\n--- Intermediate block " << inter_idx << " ready ---" << std::endl;
            bool blk_pass = verify_block(
                test.intermediate_blocks[inter_idx], test.rate_words_64,
                "IntermediateBlock[" + std::to_string(inter_idx) + "]", verbose);
            if (!blk_pass) {
                std::cout << "[FAIL] Intermediate block " << inter_idx << " mismatch" << std::endl;
                return false;
            }
            inter_idx++;
            dut->f_ack = 1;
            tick();
            dut->f_ack = 0;
            tick();
        }

        // Wait for the final padded block
        int cycles = 0;
        std::cout << "Waiting for final out_ready..." << std::endl;
        
        while (!dut->out_ready && cycles < max_cycles) {
            tick();
            cycles++;
        }
        
        if (cycles >= max_cycles) {
            std::cout << "[FAIL] Timeout waiting for out_ready" << std::endl;
            return false;
        }
        
        std::cout << "Padding complete after " << cycles << " additional cycles" << std::endl;
        std::cout << "out_ready=" << (int)dut->out_ready 
                  << ", buffer_full=" << (int)dut->buffer_full << std::endl;
        
        // Verify final padded block
        std::cout << "\nVerifying final block..." << std::endl;
        bool pass = verify_block(test.expected_output, test.rate_words_64, "FinalBlock", verbose);
        
        if (pass) {
            std::cout << "\n[PASS] " << test.name << std::endl;
        } else {
            std::cout << "\n[FAIL] " << test.name << std::endl;
        }
        
        // Acknowledge completion to reset padder
        dut->f_ack = 1;
        tick();
        dut->f_ack = 0;
        tick();
        
        return pass;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    bool enable_trace = false;
    bool verbose = false;
    std::string trace_file = "padder_trace.vcd";
    std::string test_filter = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--trace") {
            enable_trace = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                trace_file = argv[i + 1];
                i++;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--filter") {
            if (i + 1 < argc) {
                test_filter = argv[i + 1];
                i++;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --trace [file]   Enable VCD waveform output (default: padder_trace.vcd)" << std::endl;
            std::cout << "  -v, --verbose    Show detailed verification output" << std::endl;
            std::cout << "  --filter <name>  Run only tests matching substring" << std::endl;
            std::cout << "  -h, --help       Show this help message" << std::endl;
            return 0;
        }
    }
    
    Verilated::traceEverOn(true);

    std::cout << "========================================" << std::endl;
    std::cout << "SHA-3 Padder Module Test Suite (64-bit)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total test vectors: " << PADDER_TEST_VECTORS.size() << std::endl;
    if (!test_filter.empty()) {
        std::cout << "Filter: " << test_filter << std::endl;
    }
    if (verbose) {
        std::cout << "Verbose mode: ON" << std::endl;
    }
    std::cout << std::endl;
    
    PadderTestbench tb;
    
    if (enable_trace) {
        tb.open_trace(trace_file);
    }
    
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    
    for (const auto& test : PADDER_TEST_VECTORS) {
        // Apply filter if specified
        if (!test_filter.empty() && test.name.find(test_filter) == std::string::npos) {
            skipped++;
            continue;
        }
        
        bool result = tb.run_test(test, verbose);
        if (result) {
            passed++;
        } else {
            failed++;
        }
    }
    
    if (enable_trace) {
        tb.close_trace();
        std::cout << "\nVCD trace written to: " << trace_file << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    int total_run = passed + failed;
    std::cout << "Passed:  " << passed << " / " << total_run << std::endl;
    std::cout << "Failed:  " << failed << " / " << total_run << std::endl;
    if (skipped > 0) {
        std::cout << "Skipped: " << skipped << " (filtered out)" << std::endl;
    }
    
    if (failed == 0 && total_run > 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else if (total_run == 0) {
        std::cout << "\n⚠ NO TESTS RUN!" << std::endl;
        return 1;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
