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
            Verilated::traceEverOn(true);
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
        if (test.input_words.size() == 0) {
            std::cout << "Empty message - sending is_last without data (in_ready=0)..." << std::endl;
            // For empty messages, assert is_last WITHOUT in_ready.
            // The padder treats is_last & !in_ready as "end of message, 0 bytes".
            dut->in = 0;
            dut->in_ready = 0;
            dut->is_last = 1;
            dut->byte_num = 0;
            tick();

            dut->in_ready = 0;
            dut->is_last = 0;
            tick();
        } else {
            std::cout << "Feeding " << test.input_words.size() << " input words..." << std::endl;
            
            for (size_t i = 0; i < test.input_words.size(); i++) {
                bool is_last_word = (i == test.input_words.size() - 1);
                
                dut->in = test.input_words[i];
                dut->in_ready = 1;
                dut->is_last = is_last_word ? 1 : 0;
                
                // Determine byte_num for last word
                if (is_last_word) {
                    // byte_num encoding: 0=1 byte, 1=2, 2=3, 3=4
                    if (test.remaining_bytes == 0) {
                        dut->byte_num = 3;  // Full word
                    } else {
                        dut->byte_num = test.remaining_bytes - 1;
                    }
                    
                    int valid_bytes = (test.remaining_bytes == 0) ? 4 : test.remaining_bytes;
                    std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(8) 
                             << std::setfill('0') << test.input_words[i] 
                             << " (LAST, " << std::dec << valid_bytes << " bytes)" << std::endl;
                } else {
                    dut->byte_num = 3;  // Full word
                    std::cout << "  Word " << i << ": 0x" << std::hex << std::setw(8) 
                             << std::setfill('0') << test.input_words[i] 
                             << " (4 bytes)" << std::dec << std::endl;
                }
                
                tick();
                
                // Deassert in_ready after word accepted
                dut->in_ready = 0;
                dut->is_last = 0;
                tick();
            }
        }
        
        // Wait for out_ready signal (padder enters DONE state)
        int max_cycles = 500;
        int cycles = 0;
        std::cout << "Waiting for out_ready..." << std::endl;
        
        while (!dut->out_ready && cycles < max_cycles) {
            tick();
            cycles++;
        }
        
        if (cycles >= max_cycles) {
            std::cout << "[FAIL] Timeout waiting for out_ready" << std::endl;
            return false;
        }
        
        std::cout << "Padding complete after " << cycles << " cycles" << std::endl;
        std::cout << "out_ready=" << (int)dut->out_ready 
                  << ", buffer_full=" << (int)dut->buffer_full << std::endl;
        
        // Verify output
        std::cout << "\nVerifying output..." << std::endl;
        bool pass = true;
        
        // The new padder packs output as: out[i*32 +: 32] = buffer[i]
        // So word 0 is in out[31:0], word 1 in out[63:32], etc.
        // Verilator stores wide signals as arrays of uint32_t chunks
        for (uint32_t i = 0; i < test.rate_words; i++) {
            // Each word is 32 bits, extract from Verilator's array
            // out is stored as WData array where each element is 32 bits
            uint32_t actual_word = dut->out[i];
            uint32_t expected_word = test.expected_output[i];
            
            if (actual_word != expected_word) {
                std::cout << "[FAIL] Word " << std::setw(2) << i << ": Expected 0x" 
                         << std::hex << std::setw(8) << std::setfill('0') << expected_word
                         << " but got 0x" << std::setw(8) << std::setfill('0') << actual_word
                         << std::dec << std::endl;
                pass = false;
            } else {
                // Print first 5 words, last 2 words, and any padding transitions
                if (i < 5 || i >= test.rate_words - 2) {
                    std::cout << "[PASS] Word " << std::setw(2) << i << ": 0x" 
                             << std::hex << std::setw(8) << std::setfill('0') << actual_word
                             << std::dec << std::endl;
                } else if (i == 5) {
                    std::cout << "  ... (middle words omitted) ..." << std::endl;
                }
            }
        }
        
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
