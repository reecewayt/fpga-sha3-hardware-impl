#!/usr/bin/env python3
"""
Test SHA3_Instrumented implementation against official test vectors and Python's hashlib.
Test vectors from: https://www.di-mgt.com.au/sha_testvectors.html
Copyright (C) 2012-2022 DI Management Services Pty Limited
"""

import unittest
import hashlib
from sha3 import SHA3_Instrumented


class TestSHA3_256(unittest.TestCase):
    """Test SHA3-256 implementation against known test vectors."""
    
    def test_abc(self):
        """Test 'abc' - the bit string (0x)616263 of length 24 bits"""
        message = b"abc"
        expected = "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"
        
        sha3 = SHA3_Instrumented(output_bits=256)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_256()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_empty_string(self):
        """Test empty string - a bit string of length 0"""
        message = b""
        expected = "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"
        
        sha3 = SHA3_Instrumented(output_bits=256)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_256()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_448_bits(self):
        """Test message of length 448 bits"""
        message = b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        expected = "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376"
        
        sha3 = SHA3_Instrumented(output_bits=256)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_256()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_896_bits(self):
        """Test message of length 896 bits"""
        message = b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
        expected = "916f6061fe879741ca6469b43971dfdb28b1a32dc36cb3254e812be27aad1d18"
        
        sha3 = SHA3_Instrumented(output_bits=256)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_256()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_one_million_a(self):
        """Test one million (1,000,000) repetitions of 'a'"""
        message = b"a" * 1000000
        expected = "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1"
        
        sha3 = SHA3_Instrumented(output_bits=256)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_256()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())


class TestSHA3_224(unittest.TestCase):
    """Test SHA3-224 implementation."""
    
    def test_abc(self):
        """Test 'abc' with SHA3-224"""
        message = b"abc"
        expected = "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf"
        
        sha3 = SHA3_Instrumented(output_bits=224)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_224()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_empty_string(self):
        """Test empty string with SHA3-224"""
        message = b""
        expected = "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7"
        
        sha3 = SHA3_Instrumented(output_bits=224)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_224()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())


class TestSHA3_384(unittest.TestCase):
    """Test SHA3-384 implementation."""
    
    def test_abc(self):
        """Test 'abc' with SHA3-384"""
        message = b"abc"
        expected = "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25"
        
        sha3 = SHA3_Instrumented(output_bits=384)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_384()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_empty_string(self):
        """Test empty string with SHA3-384"""
        message = b""
        expected = "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004"
        
        sha3 = SHA3_Instrumented(output_bits=384)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_384()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())


class TestSHA3_512(unittest.TestCase):
    """Test SHA3-512 implementation."""
    
    def test_abc(self):
        """Test 'abc' with SHA3-512"""
        message = b"abc"
        expected = "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"
        
        sha3 = SHA3_Instrumented(output_bits=512)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_512()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())
    
    def test_empty_string(self):
        """Test empty string with SHA3-512"""
        message = b""
        expected = "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26"
        
        sha3 = SHA3_Instrumented(output_bits=512)
        result = sha3.hash(message)
        
        self.assertEqual(result.hex(), expected)
        
        # Also verify against hashlib
        sha3_hashlib = hashlib.sha3_512()
        sha3_hashlib.update(message)
        self.assertEqual(result, sha3_hashlib.digest())


if __name__ == "__main__":
    unittest.main()
