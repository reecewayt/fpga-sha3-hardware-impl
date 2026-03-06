#!/usr/bin/env python3
"""
Generate test vectors for the padder module (64-bit word format).

This script generates comprehensive test cases for the SHA-3 padder using
64-bit little-endian words to match the f_permutation module format.

IMPORTANT: Data Flow and Byte Ordering
========================================
The padder operates AFTER keccak.sv's byte-swap operation:

User Input → keccak.sv byte swap → Padder → f_permutation

Example: 'abc' message
  - User provides:    0x61 0x62 0x63 (big-endian bytes)
  - After byte swap:  bytes at LE positions [7:0]='a', [15:8]='b', [23:16]='c'
  - Padder adds 0x06: 0x0000000006636261 (little-endian lane)

Output format is a C++ header file with 64-bit test vectors in little-endian format.
"""

import sys
from pathlib import Path

# Import the tested and verified padding function
from pad10star1 import pad10star1

# Test messages with varying characteristics
TEST_CASES = [
    {
        'name': 'empty',
        'message': b'',
        'description': 'Empty message (padding only)'
    },
    {
        'name': 'single_byte',
        'message': b'\x42',
        'description': 'Single byte 0x42'
    },
    {
        'name': 'abc',
        'message': b'abc',
        'description': 'Classic abc test vector'
    },
    {
        'name': 'four_bytes',
        'message': b'\xAA\xBB\xCC\xDD',
        'description': 'Exactly 4 bytes'
    },
    {
        'name': 'eight_bytes',
        'message': b'HELLO123',
        'description': 'Eight bytes (one full 64-bit word)'
    },
    {
        'name': 'medium',
        'message': b'The quick brown fox jumps over the lazy dog',
        'description': 'Medium-length message (43 bytes)'
    },
    {
        'name': 'boundary_sha3_512',
        'message': b'\x00' * 71,  # 71 bytes = one byte short of SHA3-512 rate (72 bytes)
        'description': 'One byte short of SHA3-512 rate boundary'
    },
    {
        'name': 'exact_sha3_512',
        'message': b'\xFF' * 72,  # Exactly SHA3-512 rate
        'description': 'Exactly SHA3-512 rate (72 bytes)'
    },
    {
        'name': 'large_multiblock',
        # 290 bytes = multiblock for all variants
        'message': b'\xA5\x5A' * 145,
        'description': 'Large multiblock message (290 bytes)'
    },
]

# SHA-3 variant parameters (rates in 64-bit words)
VARIANTS = {
    'SHA3_224': {'rate_bits': 1152, 'rate_bytes': 144, 'rate_words_64': 18, 'enum': 'SHA3_224'},
    'SHA3_256': {'rate_bits': 1088, 'rate_bytes': 136, 'rate_words_64': 17, 'enum': 'SHA3_256'},
    'SHA3_384': {'rate_bits': 832,  'rate_bytes': 104, 'rate_words_64': 13, 'enum': 'SHA3_384'},
    'SHA3_512': {'rate_bits': 576,  'rate_bytes': 72,  'rate_words_64': 9,  'enum': 'SHA3_512'},
}


def keccak_byte_swap_64(value: int) -> int:
    """
    Simulates keccak.sv byte swap operation on a 64-bit word.
    Converts big-endian input to little-endian format.
    
    in_switch = {in[7:0], in[15:8], in[23:16], in[31:24],
                 in[39:32], in[47:40], in[55:48], in[63:56]}
    
    Example: 0x0102030405060708 → 0x0807060504030201
    """
    bytes_list = [(value >> (i * 8)) & 0xFF for i in range(8)]
    result = 0
    for i, byte_val in enumerate(reversed(bytes_list)):
        result |= (byte_val << (i * 8))
    return result


def message_to_words_64_le(message: bytes) -> list:
    """
    Convert message bytes to 64-bit words in little-endian format.
    
    This simulates the data AFTER keccak.sv's byte swap operation.
    Message bytes are placed in little-endian byte positions within each 64-bit word.
    
    Args:
        message: Raw message bytes
    
    Returns:
        List of 64-bit words in little-endian format
    """
    words = []
    for i in range(0, len(message), 8):
        chunk = message[i:i+8]
        # Pack as little-endian: first byte at [7:0], second at [15:8], etc.
        word = 0
        for j, byte in enumerate(chunk):
            word |= (byte << (j * 8))
        words.append(word)
    return words


def simulate_padder_output_le(message: bytes, rate_bits: int) -> list:
    """
    Simulate the padder's output in 64-bit little-endian format.
    
    This accounts for:
    1. keccak.sv byte swap (input arrives in LE format)
    2. Padder adds 0x06 suffix and 0x80 closing sentinel
    3. Output is in little-endian lane format
    
    Args:
        message: Raw message bytes
        rate_bits: SHA-3 variant rate in bits
    
    Returns:
        List of blocks, where each block is a list of 64-bit LE words
    """
    # Use pad10star1 with domain_suffix=0x06 (SHA-3 hardware convention)
    # NOT 0x60 which is the Python library convention
    padded = pad10star1(message, rate_bits, domain_suffix=0x06)
    
    # Convert padded bytes to 64-bit little-endian words
    words = message_to_words_64_le(padded)
    
    return words


def generate_test_vector(test_case, variant_name, variant_info):
    """Generate a single test vector for 64-bit word format."""
    message = test_case['message']
    rate_bits = variant_info['rate_bits']
    rate_bytes = variant_info['rate_bytes']
    rate_words_64 = variant_info['rate_words_64']
    
    # Generate padded output in 64-bit little-endian format
    padded_words_64 = simulate_padder_output_le(message, rate_bits)
    
    # Determine input characteristics
    message_len = len(message)
    num_full_words_64 = message_len // 8
    remaining_bytes = message_len % 8
    
    # Convert message to input words (64-bit LE)
    input_words_64 = message_to_words_64_le(message)
    
    # Split padded output into rate-sized blocks
    num_blocks = (len(padded_words_64) + rate_words_64 - 1) // rate_words_64
    
    # All blocks except the last are "intermediate" 
    intermediate_blocks = []
    for i in range(num_blocks - 1):
        block = padded_words_64[i * rate_words_64 : (i + 1) * rate_words_64]
        # Pad block to rate_words_64 if necessary
        while len(block) < rate_words_64:
            block.append(0)
        intermediate_blocks.append(block)
    
    # Final block (padded to rate_words_64)
    final_block = padded_words_64[(num_blocks - 1) * rate_words_64 :]
    while len(final_block) < rate_words_64:
        final_block.append(0)
    
    return {
        'test_name': test_case['name'],
        'variant': variant_name,
        'variant_enum': variant_info['enum'],
        'description': test_case['description'],
        'message': message,
        'message_len': message_len,
        'num_full_words_64': num_full_words_64,
        'remaining_bytes': remaining_bytes,
        'input_words_64': input_words_64,
        'expected_output': final_block,
        'intermediate_blocks': intermediate_blocks,
        'rate_words_64': rate_words_64,
    }


def format_word64_hex(word: int) -> str:
    """Format a 64-bit word as hex with human-readable byte breakdown."""
    return f"0x{word:016X}ULL"


def format_word64_with_comment(word: int) -> str:
    """Format a 64-bit word with human-readable comment showing LE byte layout."""
    hex_str = f"0x{word:016X}ULL"
    
    # Show printable ASCII bytes or hex
    comment_parts = []
    for byte_pos in range(8):
        byte_val = (word >> (byte_pos * 8)) & 0xFF
        if byte_val == 0:
            continue
        if 0x20 <= byte_val <= 0x7E:
            comment_parts.append(f"[{byte_pos}]='{chr(byte_val)}'")
        else:
            comment_parts.append(f"[{byte_pos}]=0x{byte_val:02X}")
    
    if comment_parts:
        return f"{hex_str}  // LE: {', '.join(comment_parts)}"
    else:
        return hex_str


def generate_header_file(output_path):
    """Generate C++ header file with all test vectors (64-bit format)."""
    
    with open(output_path, 'w') as f:
        f.write("""// Auto-generated test vectors for padder module (64-bit word format)
// Generated by generate_padder_vectors_64bit.py
//
// IMPORTANT: Data Format
// =======================
// These test vectors use LITTLE-ENDIAN 64-bit word format per Keccak specification.
// This matches the format used by f_permutation module.
//
// Data Flow: User Input → keccak.sv byte swap → Padder → f_permutation
//
// Example: SHA3-256('abc') - after keccak.sv byte swap, padder receives:
//   input word[0] = 0x0000000000636261  (LE: [7:0]='a', [15:8]='b', [23:16]='c')
//   
//   Padder adds 0x06 suffix:
//   output word[0] = 0x0000000006636261  (LE: [7:0]='a', [15:8]='b', [23:16]='c', [31:24]=0x06)
//
// This little-endian format is Keccak's standard and matches how the
// hardware naturally operates. Reading hex values "backwards" is normal!

#ifndef PADDER_TEST_VECTORS_H
#define PADDER_TEST_VECTORS_H

#include <vector>
#include <string>
#include <cstdint>

// SHA-3 variant enum (matches RTL)
enum class SHA3Variant {
    SHA3_224 = 0,
    SHA3_256 = 1,
    SHA3_384 = 2,
    SHA3_512 = 3
};

struct PadderTestVector {
    std::string name;
    std::string description;
    SHA3Variant variant;
    std::vector<uint64_t> input_words_64;  // Input 64-bit words (little-endian)
    uint32_t num_full_words_64;            // Number of complete 64-bit words
    uint32_t remaining_bytes;              // Remaining bytes (0-7) in last word
    std::vector<uint64_t> expected_output; // Expected padded output (64-bit LE words)
    uint32_t rate_words_64;                // Rate in 64-bit words
    std::vector<std::vector<uint64_t>> intermediate_blocks; // Intermediate full blocks
};

""")
        
        # Generate test vectors for each combination
        all_vectors = []
        for test_case in TEST_CASES:
            for variant_name, variant_info in VARIANTS.items():
                vec = generate_test_vector(test_case, variant_name, variant_info)
                all_vectors.append(vec)
        
        # Write test vector array
        f.write("static const std::vector<PadderTestVector> PADDER_TEST_VECTORS = {\n")
        
        for i, vec in enumerate(all_vectors):
            f.write("    {\n")
            f.write(f"        \"{vec['test_name']}_{vec['variant']}\",\n")
            f.write(f"        \"{vec['description']} ({vec['variant']})\",\n")
            f.write(f"        SHA3Variant::{vec['variant_enum']},\n")
            
            # Input words (64-bit) - needs double braces for std::vector in aggregate init
            f.write("        {\n")
            if vec['input_words_64']:
                f.write("            {\n")
                for j, word in enumerate(vec['input_words_64']):
                    comma = "," if j < len(vec['input_words_64']) - 1 else ""
                    f.write(f"                {format_word64_hex(word)}{comma}\n")
                f.write("            }\n")
            else:
                # Empty input_words_64 - use single braces only
                f.write("            \n")
            f.write("        },\n")
            
            f.write(f"        {vec['num_full_words_64']},\n")
            f.write(f"        {vec['remaining_bytes']},\n")
            
            # Expected output (64-bit) - needs double braces for std::vector in aggregate init
            f.write("        {\n")
            f.write("            {\n")
            for j, word in enumerate(vec['expected_output']):
                f.write(f"                {format_word64_hex(word)}")
                if j < len(vec['expected_output']) - 1:
                    f.write(",")
                f.write("\n")
            f.write("            }\n")
            f.write("        },\n")
            
            f.write(f"        {vec['rate_words_64']}")

            # Emit intermediate_blocks (always - even if empty, to avoid struct field misalignment)
            f.write(",\n        // intermediate_blocks\n")
            f.write("        {\n")
            if vec['intermediate_blocks']:
                f.write("            {\n")
                for bi, block in enumerate(vec['intermediate_blocks']):
                    f.write("                {\n")
                    for j, word in enumerate(block):
                        f.write(f"                    {format_word64_hex(word)}")
                        if j < len(block) - 1:
                            f.write(",")
                        f.write("\n")
                    f.write("                }")
                    if bi < len(vec['intermediate_blocks']) - 1:
                        f.write(",")
                    f.write("\n")
                f.write("            }\n")
            else:
                # Empty intermediate_blocks - use empty braces only
                f.write("            \n")
            f.write("        }\n")

            f.write("    }")

            if i < len(all_vectors) - 1:
                f.write(",\n")
            else:
                f.write("\n")
        
        f.write("};\n\n")
        f.write("#endif // PADDER_TEST_VECTORS_H\n")
    
    print(f"Generated {len(all_vectors)} test vectors (64-bit format)")
    for vec in all_vectors:
        num_inter = len(vec['intermediate_blocks'])
        chain_str = f" ({num_inter} intermediate blocks)" if num_inter > 0 else ""
        print(f"  - {vec['test_name']}_{vec['variant']}: {vec['message_len']} bytes{chain_str}")


if __name__ == '__main__':
    script_dir = Path(__file__).parent
    output_dir = script_dir.parent.parent / 'tests'
    output_file = output_dir / 'padder_test_vectors.h'
    
    generate_header_file(output_file)
    print(f"\nTest vectors written to: {output_file}")
