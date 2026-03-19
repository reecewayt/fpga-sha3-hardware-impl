/*
 * Testbench: round
 * ================
 * Verifies a single round of the Keccak-f[1600] permutation, which applies
 * the five transformation steps: theta (θ), rho (ρ), pi (π), chi (χ), and
 * iota (ι) with a round constant.
 *
 * This is a combinational testbench that validates the correct application
 * of each round transformation against known test vectors.
 *
 * Note: This testbench was developed with the assistance of GitHub Copilot.
 */

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>
#include "Vround.h"
#include "verilated.h"
#include "round_test_vectors.h"

// Constants matching the hardware
constexpr int STATE_WIDTH = 1600;
constexpr int LANE_WIDTH = 64;
constexpr int GRID_SIZE = 5;

/**
 * Convert lane array (5x5 array of 64-bit values) to flat 1600-bit state.
 * 
 * This matches the bit ordering used in the hardware:
 * A[x,y,z] = S[w(5y+x)+z] where w=64 (lane width)
 * 
 * In SystemVerilog (high_pos/low_pos):
 *   high_pos(x,y) = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x)
 *   low_pos(x,y) = high_pos(x,y) - (LANE_WIDTH - 1)
 * 
 * Lane (x,y) is stored in bits [high_pos:low_pos] of the state vector.
 */
void lanes_to_state(const uint64_t lanes[25], uint32_t* state_words) {
    // Initialize state to zero
    for (int i = 0; i < STATE_WIDTH / 32; i++) {
        state_words[i] = 0;
    }
    
    // Pack lanes into state
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            int lane_index = y * GRID_SIZE + x;  // Linear index in lanes array
            uint64_t lane_value = lanes[lane_index];
            
            // Calculate bit positions in state (matching hardware)
            int high_pos = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x);
            int low_pos = high_pos - (LANE_WIDTH - 1);
            
            // Place lane bits into state
            for (int bit = 0; bit < LANE_WIDTH; bit++) {
                int state_bit_pos = low_pos + bit;
                int word_idx = state_bit_pos / 32;
                int bit_in_word = state_bit_pos % 32;
                
                if ((lane_value >> bit) & 1) {
                    state_words[word_idx] |= (1U << bit_in_word);
                }
            }
        }
    }
}

/**
 * Convert flat 1600-bit state to lane array.
 */
void state_to_lanes(const uint32_t* state_words, uint64_t lanes[25]) {
    // Extract lanes from state
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            int lane_index = y * GRID_SIZE + x;
            uint64_t lane_value = 0;
            
            // Calculate bit positions in state
            int high_pos = STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x);
            int low_pos = high_pos - (LANE_WIDTH - 1);
            
            // Extract lane bits from state
            for (int bit = 0; bit < LANE_WIDTH; bit++) {
                int state_bit_pos = low_pos + bit;
                int word_idx = state_bit_pos / 32;
                int bit_in_word = state_bit_pos % 32;
                
                if ((state_words[word_idx] >> bit_in_word) & 1) {
                    lane_value |= (1ULL << bit);
                }
            }
            
            lanes[lane_index] = lane_value;
        }
    }
}

/**
 * Apply state to Verilator model inputs.
 */
void apply_state_to_model(Vround* dut, const uint32_t* state_words, uint64_t round_const) {
    // Set input state (1600 bits = 50 x 32-bit words)
    for (int i = 0; i < 50; i++) {
        dut->in[i] = state_words[i];
    }
    
    // Set round constant (64-bit value)
    dut->round_const = round_const;
}

/**
 * Read output state from Verilator model.
 */
void read_state_from_model(Vround* dut, uint32_t* state_words) {
    for (int i = 0; i < 50; i++) {
        state_words[i] = dut->out[i];
    }
}

/**
 * Compare two states and report differences.
 */
bool compare_states(const uint64_t expected[25], const uint64_t actual[25], const std::string& test_name) {
    bool match = true;
    
    for (int i = 0; i < 25; i++) {
        if (expected[i] != actual[i]) {
            if (match) {  // First mismatch
                std::cout << "[FAIL] " << test_name << std::endl;
                std::cout << "       Mismatches found:" << std::endl;
                match = false;
            }
            
            int x = i % GRID_SIZE;
            int y = i / GRID_SIZE;
            std::cout << "       Lane[" << x << "][" << y << "]: "
                      << "Expected 0x" << std::hex << std::setfill('0') << std::setw(16) << expected[i]
                      << " but got 0x" << std::setw(16) << actual[i]
                      << std::dec << std::endl;
        }
    }
    
    return match;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vround* dut = new Vround;
    
    bool all_passed = true;
    int num_passed = 0;
    
    std::cout << "========================================" << std::endl;
    std::cout << "SHA-3 Round Module Validation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Running " << NUM_TEST_VECTORS << " test vectors..." << std::endl;
    std::cout << std::endl;
    
    for (int test_idx = 0; test_idx < NUM_TEST_VECTORS; test_idx++) {
        const RoundTestVector& tv = test_vectors[test_idx];
        
        // Convert input lanes to state format
        uint32_t input_state[50];
        lanes_to_state(tv.input_state, input_state);
        
        // Apply inputs to DUT
        apply_state_to_model(dut, input_state, tv.round_constant);
        
        // Evaluate combinational logic
        dut->eval();
        
        // Read output state
        uint32_t output_state[50];
        read_state_from_model(dut, output_state);
        
        // Convert output state to lanes for comparison
        uint64_t output_lanes[25];
        state_to_lanes(output_state, output_lanes);
        
        // Compare with expected output
        bool passed = compare_states(tv.output_state, output_lanes, tv.name);
        
        if (passed) {
            std::cout << "[PASS] " << tv.name 
                      << " (round " << tv.round_index << ")" << std::endl;
            num_passed++;
        } else {
            all_passed = false;
        }
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << num_passed << "/" << NUM_TEST_VECTORS << " tests passed" << std::endl;
    
    if (all_passed) {
        std::cout << "STATUS: ALL TESTS PASSED! ✓" << std::endl;
    } else {
        std::cout << "STATUS: SOME TESTS FAILED! ✗" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    delete dut;
    return all_passed ? 0 : 1;
}
