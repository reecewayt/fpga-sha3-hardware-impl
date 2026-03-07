# SHA3 Hardware Implementation - Test Suite Documentation

## Overview

This document describes the comprehensive test suite for the FPGA SHA3 hardware implementation. The test suite consists of multiple Verilator-based testbenches that validate each module in the pipeline from low-level components (round compression, permutation) to the full integrated system.

---

## Test Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Full System (tb_keccak)                  │
│                    40 NIST Vectors + 8 Edge Tests               │
└────────────┬──────────────────────────────────────────┬──────────┘
             │                                          │
         ┌───▼────────────────┐              ┌──────────▼─────────┐
         │   tb_padder        │              │  tb_f_permutation  │
         │  20 Test Vectors   │              │  20 Test Vectors   │
         └────────────────────┘              └────────────────────┘
             │                                          │
         ┌───▼────────────────┐              ┌──────────▼─────────┐
         │    tb_round        │              │   tb_round_abc     │
         │   60 Test Vectors  │              │    ABC Matrices    │
         └────────────────────┘              └────────────────────┘
             │
         ┌───▼────────────────┐
         │  tb_rconst_lut     │
         │   25 Test Cases    │
         └────────────────────┘
```

---

## Testbench Details

### 1. **tb_keccak.cpp** - Full Pipeline Integration Test
**Module Tested:** `keccak.sv` (top-level)  
**Pipeline:** Input → Padder → F-Permutation → Output  
**Total Tests:** 48 (40 nominal + 8 edge protocol)

#### Standard Test Vectors (40 tests)
10 test scenarios across 4 SHA3 variants (224, 256, 384, 512):

| Test Type | Count | Description |
|-----------|-------|-------------|
| `empty` | 4 | Empty string (padding only) |
| `abc` | 4 | "abc" - Basic NIST reference vector |
| `single_byte` | 4 | Single byte (0x42) - Minimal message |
| `password123` | 4 | Multi-byte message without rate alignment |
| `long_145A` | 4 | 145 bytes (splits into 2 blocks for SHA3-224) |
| `long_296pattern` | 4 | 296 bytes repeating pattern (multi-block) |
| `heavy_10blk` | 4 | 1000 bytes (10 full blocks) - Heavy stress test |
| `exact_1blk_no_overflow` | 4 | Data + padding fit exactly in 1 block |
| `exact_2blk_no_overflow` | 4 | Data + padding fit exactly in 2 blocks |
| `exact_2blk_overflow_to_3blk` | 4 | 2 full blocks of data force 3rd block for padding |

**Exact-Boundary Test Details:**
- **No overflow tests:** Verify padding stays within expected block boundaries (message = N×rate - 2 bytes)
- **Overflow test:** Verify exact-rate input (message = 2×rate bytes) correctly requires a 3rd block for padding
- These test critical edge cases where data and padding must align precisely at block boundaries

#### Edge Protocol Robustness Tests (8 tests)
Run on the overflow vectors (full-word boundaries only):

| Test | Count | Protocol Violation | Purpose |
|------|-------|-------------------|---------|
| `edge_premature_is_last` | 4 | Assert `is_last` while `buffer_full` still high | Verify padder handles late signal arrival |
| `edge_spurious_nonlast` | 4 | Send fake non-last pulse while buffer full | Verify rejection of spurious inputs during full condition |

**Why overflow vectors only?** Edge protocol tests require full-word-boundary messages (remaining_bytes = 0). The no-overflow vectors have partial bytes and are already covered by nominal tests.

---

### 2. **tb_padder.cpp** - Message Padding Component
**Module Tested:** `padder.sv`  
**Function:** Implements SHA3 padding (0x06 prefix + domain separator + 0x80 suffix) and rate-based buffering  
**Total Tests:** 20 test vectors

#### Test Coverage
- Empty message (all 4 variants)
- Single-byte messages
- Messages at various positions within rate blocks
- Messages spanning multiple blocks
- Boundary conditions (message length = rate, 2×rate, etc.)

#### Test Validation
- Verifies correct bit-level padding output
- Checks `buffer_full` signal timing
- Validates rate-block alignment per variant
- Confirms `done` signal asserted at correct time

---

### 3. **tb_f_permutation.cpp** - Keccak-f[1600] Permutation
**Module Tested:** `f_permutation.sv`  
**Function:** Core Keccak-f[1600] state permutation (24 rounds)  
**Total Tests:** 20+ test vectors

#### Test Configuration
- **Chain tests:** Multiple permutations without reset between blocks
  - Verifies hardware state register persists correctly
  - New input XORs into existing state (no reset assumption)
- **Standalone tests:** Single permutation with reset
- **Coverage:** Various input states and patterns

#### Validation
- All 24 rounds execute with correct round constants
- Output state matches reference Keccak implementation
- Chain test dependency tracking (skips if prerequisite fails)

---

### 4. **tb_round.cpp** - Single Keccak Round Compression
**Module Tested:** `round.sv`  
**Function:** Implements one round of Keccak-f[1600] permutation  
**Total Tests:** 60 test vectors

#### Test Structure
- Covers all 24 round constants
- Tests round compression on various state patterns
- Verifies XOR of round index and round constant

#### Validation
- Lane-wise state transformation correctness
- Bit-level integrity through round operations
- Round constant application verified

---

### 5. **tb_round_abc.cpp** - Round Step Matrices
**Module Tested:** Round A, B, C transformation matrices  
**Function:** Verify θ, ρ, π, χ, ι operations in isolation  
**Total Tests:** ABC matrices for test vectors

#### Coverage
- θ (Theta) - column parity
- ρ (Rho) - lane rotation
- π (Pi) - lane permutation
- χ (Chi) - non-linear mixing
- ι (Iota) - round constant addition

---

### 6. **tb_rconst_lut.cpp** - Round Constants Lookup Table
**Module Tested:** `rconst_lut.sv`  
**Function:** Provides 64-bit round constants for all 24 rounds  
**Total Tests:** 25 test cases

#### Test Cases
- All 24 valid round indices (0-23)
- Out-of-range handling
- Timing validation

#### Validation
- Correct constant for each round index
- Combinational logic output stability
- No metastability issues

---

### 7. **tb_sha3_wb.cpp** - Wishbone Peripheral Interface
**Module Tested:** `sha3_wb.sv` (with parametric FIFO depth)  
**Function:** Wishbone bus interface for SHA3 core  
**Test Variants:** Multiple FIFO depths (8, 16, 32, 64)

#### Test Categories
- **Functional tests:** Basic SHA3 operation through Wishbone
- **Protocol tests:** Wishbone bus compliance
- **FIFO tests:** Full FIFO handling, stall conditions
- **Variant coverage:** Each FIFO depth tested with all standard vectors

#### Parameterization
Each FIFO depth (8, 16, 32, 64 words) creates a separate test executable:
```
tb_sha3_wb_fifo8   - 8-word FIFO
tb_sha3_wb_fifo16  - 16-word FIFO
tb_sha3_wb_fifo32  - 32-word FIFO
tb_sha3_wb_fifo64  - 64-word FIFO
```

---

## Test Vector Organization

### Vector Files

| File | Purpose | Variants | Count |
|------|---------|----------|-------|
| `sha3_nist_vectors.h` | Full-system integration tests | SHA3-224/256/384/512 | 40 |
| `padder_test_vectors.h` | Padder unit tests | SHA3-224/256/384/512 | 20 |
| `f_permutation_test_vectors.h` | F-permutation unit tests | All states | 20+ |
| `round_test_vectors.h` | Round compression tests | All rounds | 60 |

### Vector Naming Convention

**NIST Vectors Format:** `sha3_{variant}_{category}`
- `sha3_224_abc` - SHA3-224 of "abc"
- `sha3_512_exact_2blk_overflow_to_3blk` - Overflow scenario

**Test Vector Structure:**
```cpp
struct SHA3NISTVector {
    std::string name;
    std::string description;
    SHA3Variant variant;
    std::vector<uint64_t> input_words;
    uint32_t num_full_words;
    uint32_t remaining_bytes;
    std::vector<uint32_t> expected_digest;
};
```

---

## Data Format Notes

### Big-Endian 64-bit Words (NIST Vectors)
SHA3 NIST vectors use **big-endian** format:
- Most-significant byte is the first chronological byte
- Example: "abc" → `0x6162630000000000ULL` (3 bytes, LSBs are 0)

### Little-Endian 64-bit Words (Padder/F-Permutation)
Internal modules use **little-endian** format per Keccak specification:
- Least-significant byte is the first chronological byte
- Example: "abc" → `0x0000000000636261ULL` (bytes laid out right-to-left)
- `keccak.sv` byte-swap layer converts between formats

---

## Running Tests

### Run All Tests
```bash
cd build
ctest
```

### Run Specific Testbench
```bash
./tests/tb_keccak          # Full pipeline
./tests/tb_padder          # Padder only
./tests/tb_f_permutation   # F-permutation only
./tests/tb_round           # Round compression
./tests/tb_rconst_lut      # Round constants
```

### Run with VCD Trace
```bash
./tests/tb_keccak --trace keccak.vcd
gtkwave keccak.vcd &
```

### Sanitized Output (Pass/Fail Summary)
```bash
./tests/tb_keccak 2>&1 | grep -E "^\[PASS\]|^\[FAIL\]"
```

---

## Test Execution Summary

| Testbench | Module | Tests | Status |
|-----------|--------|-------|--------|
| tb_keccak | Full pipeline | 48 | ✅ All Pass |
| tb_padder | Padder | 20 | ✅ All Pass |
| tb_f_permutation | F-permutation | 20+ | ✅ All Pass |
| tb_round | Round compression | 60 | ✅ All Pass |
| tb_round_abc | ABC matrices | Variable | ✅ All Pass |
| tb_rconst_lut | Round constants | 25 | ✅ All Pass |
| tb_sha3_wb_fifo8 | WB + 8-word FIFO | Multiple | ✅ All Pass |
| tb_sha3_wb_fifo16 | WB + 16-word FIFO | Multiple | ✅ All Pass |
| tb_sha3_wb_fifo32 | WB + 32-word FIFO | Multiple | ✅ All Pass |
| tb_sha3_wb_fifo64 | WB + 64-word FIFO | Multiple | ✅ All Pass |

---

## Key Testing Strategies

### 1. Boundary Alignment Testing
- **No-overflow cases:** Data + padding fit in expected blocks
- **Overflow cases:** Full-block input forces padding into next block
- Tests all SHA3 variants with different rate block sizes

### 2. Edge Case Protocol Testing
- **Premature signal arrival:** `is_last` asserted while `buffer_full` high
- **Spurious input rejection:** Non-last pulse during full condition
- Ensures robustness for external Wishbone peripheral use

### 3. Multi-Block Validation
- Messages spanning 2, 5, 10+ blocks
- Verifies state persistence between blocks
- Validates correct digest even with complex message structure

### 4. NIST Compliance
- All NIST SHA3 test vectors included
- Matches official FIPS 202 reference implementations
- Covers empty, short, and long messages

### 5. Unit Module Isolation
- Each pipeline stage testable independently
- Allows debugging of specific components
- Validates data format conversions

---

## Test Vector Generation

Test vectors are generated by Python scripts:

```bash
python3 utils/generate_sha3_nist_vectors.py
  # Creates sha3_nist_vectors.h with all exact-boundary test cases

python3 utils/generate_padder_vectors_64bit.py
  # Creates padder_test_vectors.h with padding validation vectors

python3 utils/generate_f_permutation_vectors.py
  # Creates f_permutation_test_vectors.h with permutation test cases
```

---

## Regression Testing

The test suite is designed to catch:
- ✅ Bit-width mismatches
- ✅ Endianness errors
- ✅ Off-by-one errors in block boundaries
- ✅ Padding bit errors
- ✅ Round constant corruption
- ✅ State register persistence issues
- ✅ Protocol timing violations
- ✅ FIFO overflow/underflow conditions

---

## Future Enhancements

Potential areas for expanded testing:
- Formal verification of permutation correctness
- Fuzz testing with random input lengths
- Power/timing analysis correlation tests
- Cross-variant consistency validation
- Parameterized rate-block testing for custom variants
