#include <iostream>
#include <vector>
#include <iomanip>
#include "Vrconst_lut.h"    
#include "verilated.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vrconst_lut* top = new Vrconst_lut;

    // The gold standard values from the Keccak team
    uint64_t official_rc[24] = {
        0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
        0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
        0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
        0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
        0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
        0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008
    };

    bool pass = true;

    std::cout << "Starting SHA-3 Round Constant LUT Verification..." << std::endl;
    std::cout << "----------------------------------------------------" << std::endl;

    for (int i = 0; i < 24; i++) {
        // 1. Set the input
        top->rnd_idx = i;

        // 2. Evaluate the combinational logic
        top->eval();

        // 3. Check the output
        uint64_t hardware_out = top->rc_out;
        
        if (hardware_out == official_rc[i]) {
            std::cout << "[PASS] Round " << std::setw(2) << i 
                      << ": 0x" << std::hex << std::setfill('0') << std::setw(16) << hardware_out 
                      << std::dec << std::endl;
        } else {
            std::cout << "[FAIL] Round " << std::setw(2) << i 
                      << ": Expected 0x" << std::hex << official_rc[i] 
                      << " but got 0x" << hardware_out << std::dec << std::endl;
            pass = false;
        }
    }

    std::cout << "----------------------------------------------------" << std::endl;
    if (pass) {
        std::cout << "RESULT: ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "RESULT: TEST FAILED!" << std::endl;
    }

    delete top;
    return pass ? 0 : 1;
}