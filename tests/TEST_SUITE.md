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
         │  20 Test Vectors   │              │  18 Test Vectors   │
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

#### Test Vectors (20 total)
5 test scenarios across 4 SHA3 variants (224, 256, 384, 512):

| Test Scenario | Variants | Count | Test Names |
|---------------|----------|-------|-----------|
| Empty message | 224/256/384/512 | 4 | `empty_SHA3_224`, `empty_SHA3_256`, `empty_SHA3_384`, `empty_SHA3_512` |
| Single byte (0x42) | 224/256/384/512 | 4 | `single_byte_SHA3_224`, `single_byte_SHA3_256`, `single_byte_SHA3_384`, `single_byte_SHA3_512` |
| Three bytes ("abc") | 224/256/384/512 | 4 | `abc_SHA3_224`, `abc_SHA3_256`, `abc_SHA3_384`, `abc_SHA3_512` |
| Four bytes | 224/256/384/512 | 4 | `four_bytes_SHA3_224`, `four_bytes_SHA3_256`, `four_bytes_SHA3_384`, `four_bytes_SHA3_512` |
| Eight bytes | 224/256/384/512 | 4 | `eight_bytes_SHA3_224`, `eight_bytes_SHA3_256`, `eight_bytes_SHA3_384`, `eight_bytes_SHA3_512` |

#### Test Validation
- Verifies correct bit-level padding output with 0x06 prefix and 0x80 suffix
- Checks `buffer_full` signal timing and transitions
- Validates rate-block alignment per variant (18, 17, 13, 9 words)
- Confirms `done` signal asserted at correct time
- Validates output word sequencing and parity

---

### 3. **tb_f_permutation.cpp** - Keccak-f[1600] Permutation
**Module Tested:** `f_permutation.sv`  
**Function:** Core Keccak-f[1600] state permutation (24 rounds)  
**Total Tests:** 18 test vectors

#### Test Organization
Tests are organized into 5 categories covering baseline behavior, variant specifics, message variations, multiblock chaining, and edge bit patterns.

#### SECTION 1: BASELINE (1 test)
Fundamental correctness reference:

| Test Name | Description |
|-----------|-------------|
| `baseline_all_zeros` | Keccak-f[1600] applied to all-zero state (reference correctness) |

#### SECTION 2: SHA3 VARIANT BOUNDARIES (4 tests)
Validates correct rate-block sizing and closing sentinel placement for each SHA3 variant. Tests empty message (padding only) to isolate variant-specific rate behavior:

| Test Name | Description | Rate | Sentinel Index |
|-----------|-------------|------|-----------------|
| `variant_sha3_224_empty` | SHA3-224 empty message | 18 words (144 bytes) | Index 35 |
| `variant_sha3_256_empty` | SHA3-256 empty message | 17 words (136 bytes) | Index 33 |
| `variant_sha3_384_empty` | SHA3-384 empty message | 13 words (104 bytes) | Index 25 |
| `variant_sha3_512_empty` | SHA3-512 empty message | 9 words (72 bytes) | Index 17 |

**Test Purpose:** Ensure f_permutation correctly processes rate blocks of different sizes with sentinels at variant-specific boundaries. Critical for full-system integration.

#### SECTION 3: MESSAGE LENGTH VARIATIONS (5 tests)
Tests different message sizes to validate padder integration and various data/padding configurations:

| Test Name | Description | Variants |
|-----------|-------------|----------|
| `message_sha224_single_byte` | Single byte 0x42 (SHA3-224) | SHA3-224 |
| `message_sha256_single_byte` | Single byte 0x42 (SHA3-256) | SHA3-256 |
| `message_sha256_abc` | Three-byte "abc" (SHA3-256) | SHA3-256 |
| `message_sha512_abc` | Three-byte "abc" (SHA3-512) | SHA3-512 |
| `message_sha256_8bytes` | Eight bytes [0x00..0x07] (SHA3-256) | SHA3-256 |

**Test Purpose:** Validate permutation correctness across different message lengths and variants, ensuring proper XOR of padded data into zero state.

#### SECTION 4: MULTIBLOCK CHAINS (4 tests)
Tests state persistence across multiple blocks without reset—critical for messages exceeding one rate-block:

| Test Name | Description | Chained From |
|-----------|-------------|--------------|
| `multiblock_sha256_block1` | SHA3-256 intermediate block 1 (17 × 0xDEADBEEF) | — |
| `multiblock_sha256_block2` | SHA3-256 final block XOR into block1 state | block1 (index 10) |
| `multiblock_sha512_block1` | SHA3-512 intermediate block 1 (9 × 0xAAAAAAAA) | — |
| `multiblock_sha512_block2` | SHA3-512 final block XOR into block1 state | block1 (index 12) |

**Test Purpose:** Verify that the f_permutation correctly XORs successive rate-blocks into the state register without requiring explicit reset between blocks. Tests validate state persistence across the pipeline.

#### SECTION 5: EDGE PATTERNS (4 tests)
Tests specific bit patterns and corner cases for comprehensive coverage:

| Test Name | Description | Pattern |
|-----------|-------------|---------|
| `pattern_alternating_bits` | Alternating 0xAAAAAAAA and 0x55555555 across lanes | Bit-level variation |
| `pattern_sequential` | Sequential values (0x00..0xF repeated as 0xNN...NNN) | Enumerated lanes |
| `pattern_all_ones` | All ones (0xFFFFFFFF) across all rate lanes | Saturation |
| `pattern_sparse` | Sparse data (0x000000FF in first 6 words, zeros elsewhere) | Sparse coverage |

**Test Purpose:** Validate permutation correctness on diverse bit patterns to ensure no subtle bit-manipulation errors and all lanes are processed correctly.

#### Test Configuration
- **Variant boundary tests:** Isolate rate-block sizing behavior per SHA3 type
- **Message length tests:** Cross-variant validation at different input sizes
- **Chain tests:** Verify state register persistence (prev_result_idx links)
- **Pattern tests:** Bit-level correctness across diverse data

#### Validation
- All 24 rounds execute with correct round constants (RC[0..23])
- Output state matches reference Keccak-f[1600] implementation
- Lane-wise state transformation verified at each variant rate
- Chain test dependency tracking (block2 skipped if block1 fails)
- Bit-level correctness on diverse input patterns

---

### 4. **tb_round.cpp** - Single Keccak Round Compression
**Module Tested:** `round.sv`  
**Function:** Implements one round of Keccak-f[1600] permutation  
**Total Tests:** 6 test vectors

#### Test Vectors (6 total)

| Test Name | Round | Input Pattern | Purpose |
|-----------|-------|---------------|---------|
| `all_zeros_round_0` | 0 | All-zero state | Baseline: RC[0] = 0x0000000000000001 applied to zeros |
| `simple_pattern_round_0` | 0 | Sequential lanes (0x0..0x18) | Verify round operates on diverse lane values |
| `alternating_pattern_round_5` | 5 | 0x5555.../0xAAAA... alternating bits | Test round on bit-level pattern variation |
| `all_ones_round_10` | 10 | Lane value 0xFFFFFFFFFFFFFFFF | Mid-round all-ones saturation test |
| `pseudo_random_round_23` | 23 | Pseudo-random lane values | Final round complex state verification |
| `sparse_pattern_round_12` | 12 | Sparse lane values (mostly zeros) | Mid-round sparse state handling |

#### Test Structure
- Each test applies a specific round constant dependent on round index
- Validates θ, ρ, π, χ, ι operations within single round
- Covers round indices distributed from 0 (early) to 23 (final)

#### Validation
- Lane-wise state transformation correctness per round
- Bit-level integrity maintained through operations
- Round constant (RC) application verified
- Output matches reference Keccak round computation

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

| File | Purpose | Variants | Tests | Test Categories |
|------|---------|----------|-------|-----------------|
| `sha3_nist_vectors.h` | Full-system integration tests | SHA3-224/256/384/512 | 40 | 10 message scenarios × 4 variants |
| `padder_test_vectors.h` | Padder unit tests | SHA3-224/256/384/512 | 20 | 5 message lengths × 4 variants |
| `f_permutation_test_vectors.h` | F-permutation unit tests | All states | 7 | baseline, SHA3 variants, chain tests |
| `round_test_vectors.h` | Round compression tests | Various patterns | 6 | 6 distinct state pattern tests |

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

| Testbench | Module | Total Tests | Test Count Breakdown | Status |
|-----------|--------|-------------|----------------------|--------|
| tb_keccak | Full pipeline | 48 | 40 NIST vectors + 8 edge protocol | ✅ All Pass |
| tb_padder | Padder | 20 | 5 scenarios × 4 variants | ✅ All Pass |
| tb_f_permutation | F-permutation | 7 | 3 baseline + 2 SHA3 + 2 chain | ✅ All Pass |
| tb_round | Round compression | 6 | 6 distinct pattern tests | ✅ All Pass |
| tb_round_abc | ABC matrices | Variable | θ, ρ, π, χ, ι isolation | ✅ All Pass |
| tb_rconst_lut | Round constants | 25 | 24 indices + boundary | ✅ All Pass |
| tb_sha3_wb_fifo8 | WB + 8-word FIFO | ~40 | NIST vectors + protocol | ✅ All Pass |
| tb_sha3_wb_fifo16 | WB + 16-word FIFO | ~40 | NIST vectors + protocol | ✅ All Pass |
| tb_sha3_wb_fifo32 | WB + 32-word FIFO | ~40 | NIST vectors + protocol | ✅ All Pass |
| tb_sha3_wb_fifo64 | WB + 64-word FIFO | ~40 | NIST vectors + protocol | ✅ All Pass |
| **TOTAL** | | **~280+** | | ✅ |

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
