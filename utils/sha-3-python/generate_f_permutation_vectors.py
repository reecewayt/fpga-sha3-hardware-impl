#!/usr/bin/env python3
"""
Generate Test Vectors for f_permutation Module
===============================================
Generates hardware-centric test vectors for the Keccak-f[1600] permutation.

IMPORTANT: These test vectors account for the keccak.sv byte-swap operation
that converts big-endian input to little-endian format before the padder.

Data Flow: User Input → keccak.sv byte swap → Padder → f_permutation
  Input:  0x6162630000000000 (big-endian 'abc')
  Swap:   0x0000000000636261 (little-endian)
  Padder: 0x0000000006636261 (with 0x06 suffix)

Hardware Bit Layout
-------------------
f_permutation XOR step:
  round_in = {in ^ out[STATE_WIDTH-1 : STATE_WIDTH-MAX_RATE],
               out[STATE_WIDTH-MAX_RATE-1 : 0]}

Where MAX_RATE = 1152 (SHA3-224 rate, the widest).

The 18 lanes of `in[1151:0]` map to Python lanes in this order:
  in[1151:1088]  <->  A[0][0]   (in[1151] = A[0][0] bit 63 = MSB)
  in[1087:1024]  <->  A[1][0]
  in[1023: 960]  <->  A[2][0]
  in[ 959: 896]  <->  A[3][0]
  in[ 895: 832]  <->  A[4][0]
  in[ 831: 768]  <->  A[0][1]
  in[ 767: 704]  <->  A[1][1]
  in[ 703: 640]  <->  A[2][1]
  in[ 639: 576]  <->  A[3][1]
  in[ 575: 512]  <->  A[4][1]
  in[ 511: 448]  <->  A[0][2]
  in[ 447: 384]  <->  A[1][2]
  in[ 383: 320]  <->  A[2][2]
  in[ 319: 256]  <->  A[3][2]
  in[ 255: 192]  <->  A[4][2]
  in[ 191: 128]  <->  A[0][3]
  in[ 127:  64]  <->  A[1][3]
  in[  63:   0]  <->  A[2][3]
  (remaining capacity lanes A[3][3]..A[4][4] are not touched by `in`)

Hardware `in` is exposed as a 36-word uint32_t array in Verilator:
  dut->in[35] = in[1151:1120]  (MSB)
  dut->in[ 0] = in[  31:   0]  (LSB)
so  in_words[k]  (MSB-first list)  ->  dut->in[35-k]

Hardware `out` state words (50 x uint32_t, MSB-first):
  out_words[0]  = state[1599:1568]  = A[0][0] upper 32 bits
  out_words[1]  = state[1567:1536]  = A[0][0] lower 32 bits
  out_words[2k] = A[x][y] upper 32 bits  (x,y per GRID scan: y outer, x inner)
"""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from sha3 import SHA3_Instrumented

# ── Constants ─────────────────────────────────────────────────────────────────
STATE_WIDTH    = 1600
LANE_WIDTH     = 64
GRID_SIZE      = 5
MAX_RATE       = 1152          # RATE_SHA3_224 — widest supported rate
MAX_RATE_WORDS = MAX_RATE // 32   # 36 words
STATE_WORDS    = STATE_WIDTH // 32  # 50 words

# Lane order as they appear in in[MAX_RATE-1:0], MSB lane first
RATE_LANE_ORDER = [(x, y) for y in range(GRID_SIZE) for x in range(GRID_SIZE)][: MAX_RATE // LANE_WIDTH]


# ── Hardware Transformation Helpers ───────────────────────────────────────────

def keccak_byte_swap_64(value: int) -> int:
    """
    Simulates keccak.sv byte swap operation:
      in_switch = {in[7:0], in[15:8], in[23:16], in[31:24],
                   in[39:32], in[47:40], in[55:48], in[63:56]}
    
    Converts big-endian input to little-endian format.
    Example: 0x6162630000000000 → 0x0000000000636261
    """
    bytes_list = [(value >> (i * 8)) & 0xFF for i in range(8)]
    result = 0
    for i, byte_val in enumerate(reversed(bytes_list)):
        result |= (byte_val << (i * 8))
    return result


def simulate_padder_last_word(message_bytes: list, byte_num: int) -> int:
    """
    Simulates padder.sv padding logic for is_last=1.
    
    Input: message_bytes in LITTLE-ENDIAN format (after keccak byte swap)
    byte_num: number of valid bytes (0-7)
    
    Returns 64-bit padded word in little-endian format.
    
    Padder logic (from padder.sv):
      3'b000: in_switch = 64'h0000_0000_0000_0006;
      3'b001: in_switch = {48'h0, 8'h06, in[7:0]};
      3'b010: in_switch = {40'h0, 8'h06, in[15:0]};
      3'b011: in_switch = {32'h0, 8'h06, in[23:0]};
      ...
    """
    # Build the data portion from message bytes
    data = 0
    for i in range(min(byte_num, len(message_bytes))):
        data |= (message_bytes[i] << (i * 8))
    
    # Add 0x06 suffix at the appropriate position
    if byte_num == 0:
        result = 0x06
    else:
        result = data | (0x06 << (byte_num * 8))
    
    return result


def message_to_padder_output_le(message_bytes: list) -> int:
    """
    Convert a message to its padded 64-bit word in LITTLE-ENDIAN format,
    accounting for keccak.sv byte swap and padder logic.
    
    Args:
        message_bytes: Message bytes in natural order [0x61, 0x62, 0x63] for 'abc'
    
    Returns:
        64-bit padded word in little-endian format
    """
    num_bytes = len(message_bytes)
    
    # The padder operates on data that has already been byte-swapped by keccak.sv
    # So the message bytes are already in little-endian positions after the swap
    return simulate_padder_last_word(message_bytes, num_bytes)


def le64_to_words32_msb_first(le_value: int) -> tuple:
    """
    Split a 64-bit little-endian value into two 32-bit words, MSB-first.
    
    Example: 0x0000000006636261 → (0x00000000, 0x06636261)
    
    Returns: (upper_32bits, lower_32bits)
    """
    upper = (le_value >> 32) & 0xFFFF_FFFF
    lower = le_value & 0xFFFF_FFFF
    return (upper, lower)


# ── Conversion helpers ────────────────────────────────────────────────────────

def hw_in_words_to_py_lanes(in_words: list) -> list:
    """
    Convert 36 hardware `in` words (MSB at index 0) to a 5x5 Python lane array.
    in_words[2k] | in_words[2k+1]  ->  lane k  (A[x][y] per RATE_LANE_ORDER)
    Capacity lanes (index ≥ 18) are left zero.
    """
    lanes = [[0] * GRID_SIZE for _ in range(GRID_SIZE)]
    for k, (x, y) in enumerate(RATE_LANE_ORDER):
        hi = in_words[2 * k]
        lo = in_words[2 * k + 1]
        lanes[x][y] = (hi << 32) | lo
    return lanes


def py_lanes_to_hw_state_words(lanes: list) -> list:
    """
    Convert 5x5 Python lanes to 50 hardware state words (MSB at index 0).
    words[2*(5y+x)]   = A[x][y] upper 32 bits  (state bits [1599-64*(5y+x)-1 : ...])
    words[2*(5y+x)+1] = A[x][y] lower 32 bits
    """
    words = []
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            lane = lanes[x][y]
            words.append((lane >> 32) & 0xFFFF_FFFF)
            words.append(lane         & 0xFFFF_FFFF)
    return words   # 50 words


def hw_state_words_to_py_lanes(state_words: list) -> list:
    """Reverse of py_lanes_to_hw_state_words."""
    lanes = [[0] * GRID_SIZE for _ in range(GRID_SIZE)]
    for y in range(GRID_SIZE):
        for x in range(GRID_SIZE):
            idx = 2 * (GRID_SIZE * y + x)
            lanes[x][y] = (state_words[idx] << 32) | state_words[idx + 1]
    return lanes


# ── Core simulation ───────────────────────────────────────────────────────────

def run_permutation(in_words: list, initial_state_words: list = None) -> list:
    """
    Simulate one f_permutation call:
      1. Load initial state (zeros if None).
      2. XOR in_words (18 lanes) into the rate portion of the state.
      3. Run 24 Keccak rounds.
    Returns 50 hardware state words for the output.
    """
    sha3 = SHA3_Instrumented()
    if initial_state_words:
        sha3.lanes = hw_state_words_to_py_lanes(initial_state_words)
    else:
        sha3.lanes = [[0] * GRID_SIZE for _ in range(GRID_SIZE)]

    # XOR `in` lanes into the current state
    for k, (x, y) in enumerate(RATE_LANE_ORDER):
        hi = in_words[2 * k]
        lo = in_words[2 * k + 1]
        sha3.lanes[x][y] ^= (hi << 32) | lo

    # 24 Keccak rounds
    sha3.snapshots = []
    for ri in range(24):
        sha3.keccak_round(ri)

    return py_lanes_to_hw_state_words(sha3.lanes)


# ── Formatting helper ─────────────────────────────────────────────────────────

def fmt_words(words: list, indent: str = "            ", per_line: int = 8) -> str:
    """Format 32-bit words (legacy format)."""
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i : i + per_line]
        lines.append(indent + ", ".join(f"0x{w:08X}U" for w in chunk))
    return ",\n".join(lines)


def fmt_words_64(words: list, indent: str = "            ", per_line: int = 4) -> str:
    """Format 64-bit words (new 64-bit format)."""
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i : i + per_line]
        lines.append(indent + ", ".join(f"0x{w:016X}ULL" for w in chunk))
    return ",\n".join(lines)


def make_in_words(*patches) -> list:
    """Build a 36-word zero-initialised in_words (32-bit), then apply (index, value) patches."""
    words = [0] * MAX_RATE_WORDS
    for idx, val in patches:
        words[idx] = val
    return words


def make_in_words_64(*patches) -> list:
    """Build an 18-word zero-initialised in_words (64-bit), then apply (index, value) patches."""
    words = [0] * (MAX_RATE_WORDS // 2)  # 18 words of 64-bit
    for idx, val in patches:
        words[idx] = val
    return words


def words32_to_words64_msb(words32: list) -> list:
    """Convert 32-bit words (MSB-first) to 64-bit words (MSB-first).
    
    Input: [hi0, lo0, hi1, lo1, ...]
    Output: [lane0, lane1, ...]
    where lane = (hi << 32) | lo
    """
    words64 = []
    for i in range(0, len(words32), 2):
        hi = words32[i]
        lo = words32[i + 1]
        words64.append((hi << 32) | lo)
    return words64


def words32_state_to_words64_msb(words32: list) -> list:
    """Convert 50 32-bit state words (MSB-first) to 25 64-bit words (MSB-first).
    
    Input: state_words[50] in groups of (hi, lo)
    Output: state_words[25] as 64-bit lanes
    """
    words64 = []
    for i in range(0, len(words32), 2):
        hi = words32[i]
        lo = words32[i + 1]
        words64.append((hi << 32) | lo)
    return words64


# ── Header generator ──────────────────────────────────────────────────────────

def generate_header(vectors: list, output_path: str):
    """Generate C++ header with 64-bit test vectors."""
    with open(output_path, "w") as f:
        f.write("// Auto-generated test vectors for f_permutation module (64-bit format)\n")
        f.write("// DO NOT EDIT - Generated by generate_f_permutation_vectors.py\n")
        f.write("//\n")
        f.write("// IMPORTANT: Data Format\n")
        f.write("// =======================\n")
        f.write("// These test vectors use 64-bit LITTLE-ENDIAN lane format per Keccak specification.\n")
        f.write("// One 64-bit word = one complete Keccak lane (5×5 state grid).\n")
        f.write("//\n")
        f.write("// Example: SHA3-256('abc') produces the first 64-bit lane as:\n")
        f.write("//   in_words[0] = 0x0000000006636261ULL  (after byte swap + padding)\n")
        f.write("//\n")
        f.write("//   Byte breakdown (little-endian lane format):\n")
        f.write("//     bits [7:0]   = 0x61 ('a') ← First message byte at LSB\n")
        f.write("//     bits [15:8]  = 0x62 ('b') ← Second message byte\n")
        f.write("//     bits [23:16] = 0x63 ('c') ← Third message byte\n")
        f.write("//     bits [31:24] = 0x06        ← SHA3 domain suffix\n")
        f.write("//     bits [63:32] = 0x00000000  ← Padding zeros\n")
        f.write("//\n")
        f.write("// This little-endian format is Keccak's standard and matches how the\n")
        f.write("// hardware naturally operates. Reading hex values \"backwards\" is normal!\n")
        f.write("//\n")
        f.write("// Array Layout:\n")
        f.write("//   in_words[18]     : MSB-first; in_words[0] = in[1151:1088] = A[0][0]\n")
        f.write("//   expected_out[25] : MSB-first; expected_out[0] = state[1599:1536]\n")
        f.write("//   prev_result_idx  : Chain from previous test (-1 = start from zeros)\n")
        f.write("//\n\n")
        f.write("#ifndef F_PERMUTATION_TEST_VECTORS_H\n")
        f.write("#define F_PERMUTATION_TEST_VECTORS_H\n\n")
        f.write("#include <cstdint>\n\n")

        f.write("struct FPermTestVector {\n")
        f.write("    const char* name;\n")
        f.write("    const char* description;\n")
        f.write("    uint64_t    in_words[18];   // 18 x 64-bit = 1152 bits (MAX_RATE)\n")
        f.write("    uint64_t    expected_out[25]; // 25 x 64-bit = 1600 bits (STATE_WIDTH)\n")
        f.write("    int         prev_result_idx;\n")
        f.write("};\n\n")

        f.write(f"constexpr int NUM_F_PERM_TEST_VECTORS = {len(vectors)};\n\n")
        f.write("const FPermTestVector F_PERM_TEST_VECTORS[NUM_F_PERM_TEST_VECTORS] = {\n\n")

        for i, v in enumerate(vectors):
            f.write(f'    // [{i}] {v["name"]}\n')
            
            # Add human-readable explanation if present
            if "human_readable" in v:
                f.write(f'    // {v["human_readable"]}\n')
            
            f.write("    {\n")
            f.write(f'        "{v["name"]}",\n')
            f.write(f'        "{v["description"]}",\n')
            f.write("        // in_words[0..17], MSB first (64-bit lane format)\n")
            f.write("        {\n")
            # Convert 32-bit words to 64-bit for output
            in_words_64 = words32_to_words64_msb(v["in_words"])
            f.write(fmt_words_64(in_words_64))
            f.write("\n        },\n")
            f.write("        // expected_out[0..24], MSB first\n")
            f.write("        {\n")
            # Convert 32-bit state words to 64-bit for output
            out_words_64 = words32_state_to_words64_msb(v["expected_out"])
            f.write(fmt_words_64(out_words_64))
            f.write("\n        },\n")
            f.write(f'        {v["prev_result_idx"]}  // prev_result_idx\n')
            f.write("    }" + ("," if i < len(vectors) - 1 else "") + "\n\n")

        f.write("};\n\n")
        f.write("#endif // F_PERMUTATION_TEST_VECTORS_H\n")


# ── Test vector definitions ───────────────────────────────────────────────────

def main():
    vectors = []

    # ── [0] All-zero in, zero initial state ───────────────────────────────────
    # Keccak-f[1600] applied to the all-zero state.
    in_zeros = [0] * MAX_RATE_WORDS
    vectors.append({
        "name":            "all_zeros",
        "description":     "Zero in XOR zero state = Keccak-f[1600] on all-zero state",
        "in_words":        in_zeros,
        "expected_out":    run_permutation(in_zeros),
        "prev_result_idx": -1,
    })

    # ── [1] SHA3-256 empty message (single block) ─────────────────────────────
    # Empty message: byte_num=0, is_last=1
    # After keccak byte swap: no data bytes
    # Padder produces: 0x0000000000000006 (0x06 suffix at bit [7:0])
    # For SHA3-256, rate=1088 bits (136 bytes, 17 words of 64-bit)
    # Closing sentinel 0x80 goes at end of rate portion
    padded_empty = message_to_padder_output_le([])  # 0x0000000000000006
    w0_hi, w0_lo = le64_to_words32_msb_first(padded_empty)
    
    in_sha256_empty = make_in_words((0, w0_hi), (1, w0_lo), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_empty",
        "description":     "SHA3-256 empty message: padded block XOR into zero state",
        "human_readable":  "Lane 1 = 0x0000000000000006 (LE: [7:0]=0x06 suffix)",
        "in_words":        in_sha256_empty,
        "expected_out":    run_permutation(in_sha256_empty),
        "prev_result_idx": -1,
    })

    # ── [2] SHA3-512 empty message (single block) ─────────────────────────────
    # For SHA3-512, rate=576 bits (72 bytes, 9 words of 64-bit)
    # Same padded word as SHA3-256 empty, but closing sentinel at different position
    w0_hi_512, w0_lo_512 = le64_to_words32_msb_first(padded_empty)
    
    in_sha512_empty = make_in_words((0, w0_hi_512), (1, w0_lo_512), (17, 0x00000080))
    vectors.append({
        "name":            "sha3_512_empty",
        "description":     "SHA3-512 empty message: padded block XOR into zero state",
        "human_readable":  "Lane 1 = 0x0000000000000006 (LE: [7:0]=0x06 suffix)",
        "in_words":        in_sha512_empty,
        "expected_out":    run_permutation(in_sha512_empty),
        "prev_result_idx": -1,
    })

    # ── [3] SHA3-256 single byte 0x42 ────────────────────────────────────────
    # Message: single byte 0x42
    # After keccak byte swap: 0x42 ends up at bit [7:0] (little-endian)
    # Padder with byte_num=1: {48'h0, 8'h06, 0x42} = 0x0000000000000642
    padded_0x42 = message_to_padder_output_le([0x42])  # 0x0000000000000642
    w1_hi, w1_lo = le64_to_words32_msb_first(padded_0x42)
    
    in_sha256_0x42 = make_in_words((0, w1_hi), (1, w1_lo), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_byte_0x42",
        "description":     "SHA3-256 single byte 0x42: padded block XOR into zero state",
        "human_readable":  "Lane 1 = 0x0000000000000642 (LE: [7:0]=0x42, [15:8]=0x06)",
        "in_words":        in_sha256_0x42,
        "expected_out":    run_permutation(in_sha256_0x42),
        "prev_result_idx": -1,
    })

    # ── [4] SHA3-256 'abc' (3 bytes) ─────────────────────────────────────────
    # Message: 'abc' = [0x61, 0x62, 0x63]
    # After keccak byte swap: bytes are in little-endian positions
    # Padder with byte_num=3: {32'h0, 8'h06, 0x636261} = 0x0000000006636261
    #   Bit layout: [31:24]=0x06, [23:16]=0x63, [15:8]=0x62, [7:0]=0x61
    padded_abc = message_to_padder_output_le([0x61, 0x62, 0x63])  # 0x0000000006636261
    w2_hi, w2_lo = le64_to_words32_msb_first(padded_abc)
    
    in_sha256_abc = make_in_words((0, w2_hi), (1, w2_lo), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_abc",
        "human_readable":  "Lane 1 = 0x0000000006636261 (LE: [7:0]='a', [15:8]='b', [23:16]='c', [31:24]=0x06)",
        "description":     "SHA3-256 'abc' (3 bytes): padded block XOR into zero state",
        "in_words":        in_sha256_abc,
        "expected_out":    run_permutation(in_sha256_abc),
        "prev_result_idx": -1,
    })

    # ── [5] SHA3-512 multiblock — first block (18 × 0xDEADBEEF) ─────────────
    # Intermediate block: no padding yet, just raw data
    # Each 32-bit word 0xDEADBEEF needs to go through the pipeline
    # HOWEVER: For intermediate blocks, the padder doesn't modify the data
    # The 0xDEADBEEF values represent 32-bit chunks already in the system
    # For multiblock, intermediate blocks are NOT byte-swapped per-word
    # They represent actual lane data in little-endian format
    in_block1 = make_in_words(*[(i, 0xDEADBEEF) for i in range(18)])
    expected_block1 = run_permutation(in_block1)
    vectors.append({
        "name":            "sha3_512_multiblock_block1",
        "description":     "SHA3-512 multiblock: 18 x 0xDEADBEEF XOR into zero state",
        "in_words":        in_block1,
        "expected_out":    expected_block1,
        "prev_result_idx": -1,
    })

    # ── [6] SHA3-512 multiblock — second block (chained from [5]) ────────────
    # Final block with padding: 2 words of 0xDEADBEEF, then padding
    # The 0x06 suffix and closing sentinel 0x80 are added
    # For the final block, we have 8 bytes of data (2 × 32-bit words)
    # This would be byte_num=0 (full 64-bit word) followed by padding
    in_block2 = make_in_words(
        (0,  0xDEADBEEF), (1,  0xDEADBEEF),
        (2,  0x06000000), (17, 0x00000080),
    )
    vectors.append({
        "name":            "sha3_512_multiblock_block2",
        "description":     "SHA3-512 multiblock: second (padded) block chained from block1",
        "in_words":        in_block2,
        "expected_out":    run_permutation(in_block2, initial_state_words=expected_block1),
        "prev_result_idx": 5,   # index of sha3_512_multiblock_block1
    })

    # ── Write header ──────────────────────────────────────────────────────────
    here = os.path.dirname(os.path.abspath(__file__))
    output = os.path.normpath(os.path.join(here, "../../tests/f_permutation_test_vectors.h"))
    generate_header(vectors, output)

    print(f"Generated {len(vectors)} test vectors -> {output}")
    for i, v in enumerate(vectors):
        dep = v["prev_result_idx"]
        chain = f" (chained from [{dep}])" if dep >= 0 else ""
        print(f"  [{i}] {v['name']}{chain}")


if __name__ == "__main__":
    main()
