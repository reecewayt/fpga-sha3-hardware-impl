// Official NIST SHA-3 test vectors for the full keccak module
// Sources: NIST FIPS 202 / SHA-3 Known-Answer Tests

#ifndef SHA3_NIST_VECTORS_H
#define SHA3_NIST_VECTORS_H

#include <vector>
#include <string>
#include <cstdint>

// Re-use the SHA3Variant enum from padder_test_vectors.h if already included,
// otherwise define it here.
#ifndef PADDER_TEST_VECTORS_H
enum class SHA3Variant {
    SHA3_224 = 0,
    SHA3_256 = 1,
    SHA3_384 = 2,
    SHA3_512 = 3
};
#endif

struct SHA3NISTVector {
    std::string  name;
    std::string  description;
    SHA3Variant  variant;
    // Input message encoded as big-endian 64-bit words.  The most-significant
    // byte of each word is the chronologically first byte.  E.g. "abc"
    // (0x61 0x62 0x63) is stored as {0x6162630000000000ULL} with
    // num_full_words=0, remaining_bytes=3.
    std::vector<uint64_t> input_words;
    uint32_t num_full_words;   // number of complete (8-byte) words
    uint32_t remaining_bytes;  // 0-7 valid bytes in the last word
    // Expected digest as big-endian 32-bit words.
    // SHA3-224 → 7 words, SHA3-256 → 8 words,
    // SHA3-384 → 12 words, SHA3-512 → 16 words.
    std::vector<uint32_t> expected_digest;
};

static const std::vector<SHA3NISTVector> SHA3_NIST_VECTORS = {
    // -----------------------------------------------------------------------
    // SHA3-256
    // -----------------------------------------------------------------------
    {
        "sha3_256_empty",
        "SHA3-256 of empty string (NIST FIPS 202 Appendix A)",
        SHA3Variant::SHA3_256,
        /*input_words=*/ {},
        /*num_full_words=*/ 0,
        /*remaining_bytes=*/ 0,
        // a7ffc6f8 bf1ed766 51c14756 a061d662 f580ff4d e43b49fa 82d80a4b 80f8434a
        {0xa7ffc6f8, 0xbf1ed766, 0x51c14756, 0xa061d662,
         0xf580ff4d, 0xe43b49fa, 0x82d80a4b, 0x80f8434a}
    },
    {
        "sha3_256_abc",
        "SHA3-256 of \"abc\" (NIST FIPS 202 Appendix A)",
        SHA3Variant::SHA3_256,
        /*input_words=*/ {0x6162630000000000ULL},   // 'a','b','c' in MSB, rest unused
        /*num_full_words=*/ 0,
        /*remaining_bytes=*/ 3,
        // 3a985da7 4fe225b2 045c172d 6bd390bd 855f086e 3e9d525b 46bfe245 11431532
        {0x3a985da7, 0x4fe225b2, 0x045c172d, 0x6bd390bd,
         0x855f086e, 0x3e9d525b, 0x46bfe245, 0x11431532}
    },
};

#endif // SHA3_NIST_VECTORS_H
