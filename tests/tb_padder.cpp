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
    
    bool verify_block(const std::vector<uint32_t>& expected, uint32_t rate_words,
                      const std::string& label) {
        bool pass = true;
        for (uint32_t i = 0; i < rate_words; i++) {
            uint32_t actual   = dut->out[rate_words - 1 - i];
            uint32_t expected_word = expected[i];
            if (actual != expected_word) {
                std::cout << "[FAIL] " << label << " Word " << std::setw(2) << i
                          << ": Expected 0x" << std::hex << std::setw(8) << std::setfill('0')
                          << expected_word << " but got 0x" << std::setw(8) << std::setfill('0')
                          << actual << std::dec << std::endl;
                pass = false;
            } else if (i < 5 || i >= rate_words - 2) {
                std::cout << "[PASS] " << label << " Word " << std::setw(2) << i << ": 0x"
                          << std::hex << std::setw(8) << std::setfill('0') << actual
                          << std::dec << std::endl;
            } else if (i == 5) {
                std::cout << "  ... (middle words omitted) ..." << std::endl;
            }
        }
        return pass;
    }

    bool run_test(const PadderTestVector& test) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test: " << test.name << std::endl;
        std::cout << "Description: " << test.description << std::endl;
        std::cout << "========================================" << std::endl;
        
        reset_dut();
        
        // Set variant
        dut->variant = static_cast<uint8_t>(test.variant);
        tick();
        
        // Feed input words
        // Track how many intermediate blocks have been verified across all phases.
        size_t inter_idx = 0;

        if (test.input_words.size() == 0) {
            std::cout << "Empty message - sending is_last without data" << std::endl;
            // For empty messages, assert is_last WITHOUT in_ready.
            // The padder treats is_last & !in_ready as "end of message, 0 bytes".
            dut->in = 0;
            dut->in_ready = 1;
            dut->is_last = 1;
            dut->byte_num = 0;
            tick();

            dut->in_ready = 0;
            dut->is_last = 0;
            tick();
        } else {
            std::cout << "Feeding " << test.input_words.size() << " input words..." << std::endl;

            for (size_t i = 0; i < test.input_words.size(); i++) {
                // If the padder emitted a mid-message block while we were feeding,
                // verify it and send f_ack before offering the next word.
                if (dut->out_ready && inter_idx < test.intermediate_blocks.size()) {
                    std::cout << "\n--- Intermediate block " << inter_idx << " ready ---" << std::endl;
                    bool blk_pass = verify_block(
                        test.intermediate_blocks[inter_idx], test.rate_words,
                        "IntermediateBlock[" + std::to_string(inter_idx) + "]");
                    if (!blk_pass) {
                        std::cout << "[FAIL] Intermediate block " << inter_idx
                                  << " mismatch\n" << std::endl;
                    }
                    inter_idx++;
                    dut->f_ack = 1;
                    tick();
                    dut->f_ack = 0;
                    tick();
                }

                bool is_last_word = (i == test.input_words.size() - 1);

                dut->in = test.input_words[i];
                dut->in_ready = 1;
                dut->is_last = is_last_word ? 1 : 0;

                // Determine byte_num for last word
                if (is_last_word && test.remaining_bytes != 0) {
                    // Partial last word: byte_num = number of valid bytes (1, 2, or 3)
                    dut->byte_num = test.remaining_bytes;
                    std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(8)
                             << std::setfill('0') << test.input_words[i]
                             << " (LAST, " << std::dec << test.remaining_bytes << " bytes)" << std::endl;
                } else {
                    // Full 4-byte word (including full last word - is_last sent separately after)
                    dut->byte_num = 3;
                    if (is_last_word) {
                        std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(8)
                                 << std::setfill('0') << test.input_words[i]
                                 << " (full word, is_last sent after)" << std::dec << std::endl;
                    } else {
                        std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(8)
                                 << std::setfill('0') << test.input_words[i]
                                 << " (4 bytes)" << std::dec << std::endl;
                    }
                }

                // For a full last word, send it without is_last - protocol requires a
                // separate is_last=1 transaction with byte_num=0 (0 valid bytes → 0x06000000)
                if (is_last_word && test.remaining_bytes == 0) {
                    dut->is_last = 0;
                }

                tick();

                // Deassert in_ready after word accepted
                dut->in_ready = 0;
                dut->is_last = 0;
                tick();

                // For full last word: send the trailing is_last with 0 valid bytes.
                // IMPORTANT: the padder may have just filled its rate buffer on this
                // word and asserted out_ready.  It cannot accept the trailing is_last
                // while out_ready is high (it's waiting for f_ack).  Drain first.
                if (is_last_word && test.remaining_bytes == 0) {
                    // Drain any intermediate block that became ready on the last word.
                    while (dut->out_ready && inter_idx < test.intermediate_blocks.size()) {
                        std::cout << "\n--- Intermediate block " << inter_idx
                                  << " ready (pre-is_last drain) ---" << std::endl;
                        bool blk_pass = verify_block(
                            test.intermediate_blocks[inter_idx], test.rate_words,
                            "IntermediateBlock[" + std::to_string(inter_idx) + "]");
                        if (!blk_pass) {
                            std::cout << "[FAIL] Intermediate block " << inter_idx
                                      << " mismatch" << std::endl;
                        }
                        inter_idx++;
                        dut->f_ack = 1;
                        tick();
                        dut->f_ack = 0;
                        tick();
                    }
                    std::cout << "  (sending is_last with byte_num=0 for full-word end)" << std::endl;
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
        
        // Drain any intermediate blocks that became ready after the feeding loop.
        // This handles the case where the padder emits a full block on or after
        // the last input word, which the per-word check above would have missed.
        int max_cycles = 500;
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
                test.intermediate_blocks[inter_idx], test.rate_words,
                "IntermediateBlock[" + std::to_string(inter_idx) + "]");
            if (!blk_pass) {
                std::cout << "[FAIL] Intermediate block " << inter_idx
                          << " mismatch" << std::endl;
            }
            inter_idx++;
            dut->f_ack = 1;
            tick();
            dut->f_ack = 0;
            tick();
        }

        // Wait for the final padded block.
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
        bool pass = verify_block(test.expected_output, test.rate_words, "FinalBlock");
        
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
    std::string trace_file = "padder_trace.vcd";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--trace") {
            enable_trace = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                trace_file = argv[i + 1];
                i++;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--trace [filename.vcd]]" << std::endl;
            std::cout << "  --trace [file]  Enable VCD waveform output (default: padder_trace.vcd)" << std::endl;
            return 0;
        }
    }
    
    Verilated::traceEverOn(true);

    std::cout << "========================================" << std::endl;
    std::cout << "SHA-3 Padder Module Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total test vectors: " << PADDER_TEST_VECTORS.size() << std::endl;
    
    PadderTestbench tb;
    
    if (enable_trace) {
        tb.open_trace(trace_file);
    }
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : PADDER_TEST_VECTORS) {
        bool result = tb.run_test(test);
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
    std::cout << "Passed: " << passed << " / " << PADDER_TEST_VECTORS.size() << std::endl;
    std::cout << "Failed: " << failed << " / " << PADDER_TEST_VECTORS.size() << std::endl;
    
    if (failed == 0) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
