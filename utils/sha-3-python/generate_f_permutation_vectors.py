#!/usr/bin/env python3
"""
Generate Test Vectors for f_permutation Module
===============================================
Generates hardware-centric test vectors for the Keccak-f[1600] permutation.

Bit / lane ordering used by the hardware
-----------------------------------------
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

Padder output word-order note
-------------------------------
The padder fills its shift-register with the FIRST input word at the MSB:
  out <= {out[MAX_RATE-33:0], in_switch}   (new word enters at LSB each cycle)
After filling N words the layout is:
  padder_out[MAX_RATE-1 : MAX_RATE-32*N] = {word_1, word_2, ..., word_N}

So for SHA3-256 (rate=34 words):
  padder_out for empty message = {0x06000000, 0,...0, 0x00000080, 0, 0}
  ─> in_words[0]=0x06000000, in_words[33]=0x00000080, in_words[34..35]=0
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
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i : i + per_line]
        lines.append(indent + ", ".join(f"0x{w:08X}U" for w in chunk))
    return ",\n".join(lines)


def make_in_words(*patches) -> list:
    """Build a 36-word zero-initialised in_words, then apply (index, value) patches."""
    words = [0] * MAX_RATE_WORDS
    for idx, val in patches:
        words[idx] = val
    return words


# ── Header generator ──────────────────────────────────────────────────────────

def generate_header(vectors: list, output_path: str):
    with open(output_path, "w") as f:
        f.write("// Auto-generated test vectors for f_permutation module\n")
        f.write("// DO NOT EDIT - Generated by generate_f_permutation_vectors.py\n\n")
        f.write("#ifndef F_PERMUTATION_TEST_VECTORS_H\n")
        f.write("#define F_PERMUTATION_TEST_VECTORS_H\n\n")
        f.write("#include <cstdint>\n\n")

        f.write("// Bit layout\n")
        f.write("//   in_words[36]  : MSB first; in_words[0]  = in[1151:1120]\n")
        f.write("//   expected_out[50]: MSB first; expected_out[0] = state[1599:1568] = A[0][0] upper 32\n")
        f.write("//   prev_result_idx : index of the test whose expected_out feeds this\n")
        f.write("//                    test's initial state; -1 = start from reset (zeros)\n\n")

        f.write("struct FPermTestVector {\n")
        f.write("    const char* name;\n")
        f.write("    const char* description;\n")
        f.write("    uint32_t    in_words[36];\n")
        f.write("    uint32_t    expected_out[50];\n")
        f.write("    int         prev_result_idx;\n")
        f.write("};\n\n")

        f.write(f"constexpr int NUM_F_PERM_TEST_VECTORS = {len(vectors)};\n\n")
        f.write("const FPermTestVector F_PERM_TEST_VECTORS[NUM_F_PERM_TEST_VECTORS] = {\n\n")

        for i, v in enumerate(vectors):
            f.write(f'    // [{i}] {v["name"]}\n')
            f.write("    {\n")
            f.write(f'        "{v["name"]}",\n')
            f.write(f'        "{v["description"]}",\n')
            f.write("        // in_words[0..35], MSB first\n")
            f.write("        {\n")
            f.write(fmt_words(v["in_words"]))
            f.write("\n        },\n")
            f.write("        // expected_out[0..49], MSB first\n")
            f.write("        {\n")
            f.write(fmt_words(v["expected_out"]))
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
    # Padder out for empty msg, 34 active words (rate=1088 bits):
    #   {0x06000000, 0x00..., 0x00000080}  +  2 zero words  (pad to 36)
    in_sha256_empty = make_in_words((0, 0x06000000), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_empty",
        "description":     "SHA3-256 empty message: padded block XOR into zero state",
        "in_words":        in_sha256_empty,
        "expected_out":    run_permutation(in_sha256_empty),
        "prev_result_idx": -1,
    })

    # ── [2] SHA3-512 empty message (single block) ─────────────────────────────
    # Padder out for empty msg, 18 active words (rate=576 bits):
    #   {0x06000000, 0x00..., 0x00000080}  +  18 zero words
    in_sha512_empty = make_in_words((0, 0x06000000), (17, 0x00000080))
    vectors.append({
        "name":            "sha3_512_empty",
        "description":     "SHA3-512 empty message: padded block XOR into zero state",
        "in_words":        in_sha512_empty,
        "expected_out":    run_permutation(in_sha512_empty),
        "prev_result_idx": -1,
    })

    # ── [3] SHA3-256 single byte 0x42 ────────────────────────────────────────
    # Padder out for 0x42 with byte_num=1:
    #   {0x42060000, 0x00..., 0x00000080}  (34 active words + 2 zeros)
    in_sha256_0x42 = make_in_words((0, 0x42060000), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_byte_0x42",
        "description":     "SHA3-256 single byte 0x42: padded block XOR into zero state",
        "in_words":        in_sha256_0x42,
        "expected_out":    run_permutation(in_sha256_0x42),
        "prev_result_idx": -1,
    })

    # ── [4] SHA3-256 'abc' (3 bytes) ─────────────────────────────────────────
    # Padder out: {0x61626306, 0,..., 0x00000080}  (34 active words + 2 zeros)
    in_sha256_abc = make_in_words((0, 0x61626306), (33, 0x00000080))
    vectors.append({
        "name":            "sha3_256_abc",
        "description":     "SHA3-256 'abc' (3 bytes): padded block XOR into zero state",
        "in_words":        in_sha256_abc,
        "expected_out":    run_permutation(in_sha256_abc),
        "prev_result_idx": -1,
    })

    # ── [5] SHA3-512 multiblock — first block (18 × 0xDEADBEEF) ─────────────
    # Corresponds to padder intermediate block for multiblock_SHA3_512 test.
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
    # Padder final block: {0xDEADBEEF, 0xDEADBEEF, 0x06000000, 0,..., 0x00000080}
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
