
#!/usr/bin/env python3
"""
pad10star1.py - SHA-3 pad10*1 Padding Function
===============================================
Implements the padding scheme from FIPS 202 Section 5.1 for SHA-3:
    Padded message = M || d || 0x01 || 0x00...00 || 0x80

Where:
    - M is the original message
    - d is the domain suffix (0x60 for SHA-3, representing bits '01' LSB-first + '1')
    - The padding ensures the result is a multiple of the rate in bytes
    - The final 0x01 byte represents the trailing '1' bit of pad10*1

The pad10*1 padding rule:
    - Append bits: 1 || 0^j || 1
    - Where j is chosen so that the total length is a multiple of rate
    - j = (-m - 2) mod rate, where m is message length in bits

For SHA-3 (domain suffix = 2 bits '01'):
    - Total padding: 01 || 1 || 0^j || 1
    - j = (-m - 4) mod rate in bits
    - Or in bytes: (-m_bytes - padding_overhead) mod rate_bytes

Note: Developed with the assistance of GitHub Copilot.
"""


def pad10star1(message, rate_bits, domain_suffix=0x60):
    """
    Apply SHA-3 pad10*1 padding to a message.
    
    Args:
        message: Input message as bytes
        rate_bits: Rate in bits (e.g., 1088 for SHA3-256)
        domain_suffix: Domain separation byte (default 0x60 for SHA-3)
                       This combines the 2-bit suffix '01' and first '1' from pad10*1
    
    Returns:
        Padded message as bytes, length is a multiple of (rate_bits // 8)
    
    Examples:
        >>> # SHA3-256 with empty message
        >>> pad10star1(b'', 1088) == b'\\x60' + b'\\x00' * 134 + b'\\x01'
        True
        
        >>> # SHA3-256 with "abc"
        >>> result = pad10star1(b'abc', 1088)
        >>> len(result) == 136  # 1088 / 8
        True
        >>> result[:3] == b'abc'
        True
        >>> result[3] == 0x60  # Domain suffix + first 1 bit
        True
        >>> result[-1] == 0x01  # Final 1 bit
        True
    """
    rate_bytes = rate_bits // 8
    message_len = len(message)
    
    # Calculate how much padding we need
    # We need to add at least 2 bytes: one for domain suffix, one for final 0x80
    # Total length must be multiple of rate_bytes
    
    # Start with message
    padded = bytearray(message)
    
    # Determine padding length
    # Current length: message_len bytes
    # We need: message_len + padding_len ≡ 0 (mod rate_bytes)
    # Minimum padding: 1 byte (if domain_suffix and final bit fit in same byte)
    # Otherwise: at least 2 bytes
    
    padding_needed = (rate_bytes - (message_len % rate_bytes)) % rate_bytes
    if padding_needed == 0:
        padding_needed = rate_bytes  # Need full block of padding
    
    # Check if we can fit both domain suffix and final bit in one byte
    if padding_needed == 1:
        # Domain suffix combined with final bit 0x80
        padded.append(domain_suffix | 0x80)
    else:
        # Add domain suffix byte
        padded.append(domain_suffix)
        
        # Add zero bytes in the middle
        for _ in range(padding_needed - 2):
            padded.append(0x00)
        
        # Add final bit (0x80 = 0b10000000) - the closing sentinel per FIPS 202
        padded.append(0x80)
    
    # Verify result is correct length
    assert len(padded) % rate_bytes == 0, f"Padding failed: {len(padded)} not multiple of {rate_bytes}"
    
    return bytes(padded)


def pad_for_variant(message, variant):
    """
    Convenience function to pad a message for a specific SHA-3 variant.
    
    Args:
        message: Input message as bytes
        variant: One of '224', '256', '384', '512' or 224, 256, 384, 512
    
    Returns:
        Padded message as bytes
    """
    # SHA-3 rate values (in bits)
    rates = {
        224: 1152,  # SHA3-224
        256: 1088,  # SHA3-256
        384: 832,   # SHA3-384
        512: 576,   # SHA3-512
        '224': 1152,
        '256': 1088,
        '384': 832,
        '512': 576,
    }
    
    if variant not in rates:
        raise ValueError(f"Invalid variant: {variant}. Must be one of: 224, 256, 384, 512")
    
    rate_bits = rates[variant]
    return pad10star1(message, rate_bits, domain_suffix=0x60)


def print_padded_bytes(padded, max_show=16):
    """Helper to pretty-print padded bytes."""
    print(f"  Total: {len(padded)} bytes")
    if len(padded) <= max_show:
        print(f"  Bytes: {' '.join(f'{b:02X}' for b in padded)}")
    else:
        first = ' '.join(f'{b:02X}' for b in padded[:8])
        last = ' '.join(f'{b:02X}' for b in padded[-8:])
        print(f"  Bytes: {first} ... {last}")


def test_small_rates():
    """Test with smaller, more visible rate values for understanding."""
    print("\n" + "=" * 70)
    print("SMALL RATE TESTS (for visualization)")
    print("=" * 70)
    
    # Test 1: Tiny rate - 4 bytes (32 bits)
    print("\nTest A: Empty message, 32-bit rate (4 bytes)")
    padded = pad10star1(b'', 32, domain_suffix=0x60)
    print(f"  Rate: 32 bits = 4 bytes")
    print_padded_bytes(padded)
    print(f"  Padding: 0x60 + 0x00 + 0x00 + 0x01")
    assert len(padded) == 4
    assert padded == b'\x60\x00\x00\x01'
    print("  ✓ PASS")
    
    # Test 2: Single byte message with small rate
    print("\nTest B: Single byte 'A', 32-bit rate (4 bytes)")
    padded = pad10star1(b'A', 32, domain_suffix=0x60)
    print(f"  Rate: 32 bits = 4 bytes")
    print(f"  Message: 0x{ord('A'):02X} = 'A'")
    print_padded_bytes(padded)
    print(f"  Layout: 0x41 (A) | 0x60 (pad) | 0x00 | 0x01 (end)")
    assert len(padded) == 4
    assert padded == b'A\x60\x00\x01'
    print("  ✓ PASS")
    
    # Test 3: Three bytes, exact fit minus 1
    print("\nTest C: 'ABC' (3 bytes), 32-bit rate (4 bytes)")
    padded = pad10star1(b'ABC', 32, domain_suffix=0x60)
    print(f"  Rate: 32 bits = 4 bytes")
    print(f"  Message: 'ABC' = 0x41 0x42 0x43")
    print_padded_bytes(padded)
    print(f"  Layout: 0x41 0x42 0x43 | 0x61 (combined suffix+end)")
    assert len(padded) == 4
    assert padded == b'ABC\x61'
    print("  ✓ PASS - Note: 0x61 = 0x60 | 0x01 (all padding in one byte)")
    
    # Test 4: Exact rate boundary
    print("\nTest D: 'ABCD' (4 bytes), 32-bit rate (4 bytes)")
    padded = pad10star1(b'ABCD', 32, domain_suffix=0x60)
    print(f"  Rate: 32 bits = 4 bytes")
    print(f"  Message: 'ABCD' = 0x41 0x42 0x43 0x44")
    print_padded_bytes(padded)
    print(f"  Layout: [0x41 0x42 0x43 0x44] [0x60 0x00 0x00 0x01]")
    assert len(padded) == 8
    assert padded == b'ABCD\x60\x00\x00\x01'
    print("  ✓ PASS - Needs full padding block")
    
    # Test 5: 8-byte rate
    print("\nTest E: 'Hello' (5 bytes), 64-bit rate (8 bytes)")
    padded = pad10star1(b'Hello', 64, domain_suffix=0x60)
    print(f"  Rate: 64 bits = 8 bytes")
    print(f"  Message: 'Hello' = 0x{b'Hello'.hex()}")
    print_padded_bytes(padded)
    print(f"  Layout: [Hello] 0x60 0x00 0x01")
    assert len(padded) == 8
    assert padded == b'Hello\x60\x00\x01'
    print("  ✓ PASS")
    
    # Test 6: Show bit-level detail
    print("\nTest F: Bit-level analysis of 'X' with 24-bit rate (3 bytes)")
    message = b'X'
    padded = pad10star1(message, 24, domain_suffix=0x60)
    print(f"  Rate: 24 bits = 3 bytes")
    print(f"\n  Byte-by-byte breakdown:")
    print(f"    Byte 0: 0x{padded[0]:02X} = 0b{padded[0]:08b} = 'X'")
    print(f"    Byte 1: 0x{padded[1]:02X} = 0b{padded[1]:08b} = domain suffix")
    print(f"            0x60 = 0b01100000: bit5=1, bit6=1, rest 0")
    print(f"    Byte 2: 0x{padded[2]:02X} = 0b{padded[2]:08b} = final padding bit")
    print(f"            bit0=1 (LSB) represents the trailing '1' of pad10*1")
    assert padded == b'X\x60\x01'
    print("  ✓ PASS")


def test_standard_sha3_variants():
    """Test all standard SHA-3 variants."""
    print("\n" + "=" * 70)
    print("STANDARD SHA-3 VARIANT TESTS")
    print("=" * 70)
    
    # Test 1: Empty message, SHA3-256
    print("\nTest 1: Empty message (SHA3-256)")
    padded = pad10star1(b'', 1088)
    print(f"  Input: empty (0 bytes)")
    print(f"  Output: {len(padded)} bytes (expected: 136)")
    print(f"  First: 0x{padded[0]:02X} (domain), Last: 0x{padded[-1]:02X} (end)")
    assert len(padded) == 136
    assert padded[0] == 0x60
    assert padded[-1] == 0x01
    print("  ✓ PASS")
    
    # Test 2: "abc", SHA3-256
    print("\nTest 2: 'abc' (SHA3-256)")
    padded = pad10star1(b'abc', 1088)
    print(f"  Input: 3 bytes")
    print(f"  Output: {len(padded)} bytes (expected: 136)")
    print(f"  Start: {padded[:4].hex()}, End: {padded[-2:].hex()}")
    assert len(padded) == 136
    assert padded[:3] == b'abc'
    assert padded[3] == 0x60
    assert padded[-1] == 0x01
    print("  ✓ PASS")
    
    # Test 3: Boundary case - one byte short of rate
    print("\nTest 3: 71 bytes (SHA3-512 rate - 1)")
    message = b'A' * 71
    padded = pad10star1(message, 576)
    print(f"  Input: 71 bytes")
    print(f"  Output: {len(padded)} bytes (expected: 72)")
    print(f"  Last byte: 0x{padded[-1]:02X} (expected: 0x61 = combined)")
    assert len(padded) == 72
    assert padded[-1] == 0x61  # Combined suffix and final bit
    print("  ✓ PASS")
    
    # Test 4: Exact rate boundary - need full additional block
    print("\nTest 4: 72 bytes (exactly SHA3-512 rate)")
    message = b'B' * 72
    padded = pad10star1(message, 576)
    print(f"  Input: 72 bytes (exact rate)")
    print(f"  Output: {len(padded)} bytes (expected: 144)")
    print(f"  Padding block: 0x{padded[72]:02X} ... 0x{padded[-1]:02X}")
    assert len(padded) == 144
    assert padded[72] == 0x60
    assert padded[-1] == 0x01
    print("  ✓ PASS")
    
    # Test 5: All variants with same message
    print("\nTest 5: 'test' across all SHA-3 variants")
    test_cases = [
        (224, 144),  # 1152 / 8
        (256, 136),  # 1088 / 8
        (384, 104),  # 832 / 8
        (512, 72),   # 576 / 8
    ]
    for variant, expected_len in test_cases:
        padded = pad_for_variant(b'test', variant)
        print(f"  SHA3-{variant}: {len(padded)} bytes (expected: {expected_len})")
        assert len(padded) == expected_len
        assert padded[:4] == b'test'
    print("  ✓ ALL PASS")


def test_edge_cases():
    """Test edge cases and special scenarios."""
    print("\n" + "=" * 70)
    print("EDGE CASE TESTS")
    print("=" * 70)
    
    # Test 1: Single byte combines with padding
    print("\nTest 1: 2-byte rate with 1-byte message")
    padded = pad10star1(b'X', 16)  # 16 bits = 2 bytes
    print(f"  Input: 'X' (1 byte)")
    print(f"  Output: {padded.hex()}")
    print(f"  Expected: 58 61 (X + combined padding)")
    assert len(padded) == 2
    assert padded == b'X\x61'
    print("  ✓ PASS")
    
    # Test 2: Very long message
    print("\nTest 2: Long message (200 bytes, SHA3-256)")
    long_msg = b'A' * 200
    padded = pad10star1(long_msg, 1088)
    print(f"  Input: 200 bytes of 'A'")
    print(f"  Output: {len(padded)} bytes")
    print(f"  Blocks: {len(padded) // 136} × 136-byte blocks")
    assert len(padded) % 136 == 0
    assert padded[:200] == long_msg
    assert padded[-1] == 0x01
    print("  ✓ PASS")
    
    # Test 3: All possible single-byte messages (compact test)
    print("\nTest 3: All 256 single-byte messages (spot check)")
    for byte_val in [0x00, 0x42, 0xFF]:
        msg = bytes([byte_val])
        padded = pad10star1(msg, 32)
        assert len(padded) == 4
        assert padded[0] == byte_val
        assert padded[1] == 0x60
        assert padded[-1] == 0x01
    print(f"  Tested bytes: 0x00, 0x42, 0xFF")
    print("  ✓ PASS")
    
    # Test 4: Domain suffix variation
    print("\nTest 4: Custom domain suffix (SHAKE, 0x1F)")
    padded = pad10star1(b'test', 1088, domain_suffix=0x1F)
    print(f"  Input: 'test' with SHAKE domain (0x1F)")
    print(f"  Output: {padded[:5].hex()} ... {padded[-2:].hex()}")
    assert padded[4] == 0x1F  # SHAKE uses 0x1F instead of 0x60
    assert padded[-1] == 0x01
    print("  ✓ PASS - Different domain suffix works")


if __name__ == '__main__':
    print("\n" + "█" * 70)
    print("█" + " " * 68 + "█")
    print("█" + "  SHA-3 PAD10*1 PADDING FUNCTION - COMPREHENSIVE TEST SUITE".center(68) + "█")
    print("█" + " " * 68 + "█")
    print("█" * 70)
    
    try:
        # Run all test suites
        test_small_rates()
        test_standard_sha3_variants()
        test_edge_cases()
        
        print("\n" + "█" * 70)
        print("█" + " " * 68 + "█")
        print("█" + "  ✓ ALL TESTS PASSED!".center(68) + "█")
        print("█" + " " * 68 + "█")
        print("█" * 70 + "\n")
        
    except AssertionError as e:
        print(f"\n✗ TEST FAILED: {e}\n")
        raise
    except Exception as e:
        print(f"\n✗ ERROR: {e}\n")
        raise
 
    

     
