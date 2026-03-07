/*
    tb_sha3_wb_top.cpp - WB integration testbench for sha3_wb_top

    Drives Wishbone transactions into sha3_wb_top (sha3_wb + keccak) and
    verifies digest words against official NIST vectors.
*/

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "Vsha3_wb_top.h"
#include "verilated.h"
#include "sha3_nist_vectors.h"

static Vsha3_wb_top* dut = nullptr;
static uint64_t sim_time = 0;

static constexpr uint32_t CTRL_START = (1u << 0);
static constexpr uint32_t CTRL_ABORT = (1u << 2);
static constexpr uint32_t CTRL_MODE_MASK = (3u << 3);

static constexpr uint32_t ST_IDLE = (1u << 0);
static constexpr uint32_t ST_DONE = (1u << 2);
static constexpr uint32_t ST_ERR_MASK = (1u << 8) | (1u << 9) | (1u << 10);

static inline uint32_t digest_words(SHA3Variant v) {
    switch (v) {
        case SHA3Variant::SHA3_224: return 7;
        case SHA3Variant::SHA3_256: return 8;
        case SHA3Variant::SHA3_384: return 12;
        case SHA3Variant::SHA3_512: return 16;
        default: return 16;
    }
}

static inline uint32_t mode_bits(SHA3Variant v) {
    return (static_cast<uint32_t>(v) & 0x3u) << 3;
}

static void eval_step() {
    dut->eval();
    sim_time++;
}

static void tick() {
    dut->wb_clk_i = 0;
    eval_step();
    dut->wb_clk_i = 1;
    eval_step();
}

static void wb_idle() {
    dut->wb_cyc_i = 0;
    dut->wb_stb_i = 0;
    dut->wb_we_i = 0;
    dut->wb_adr_i = 0;
    dut->wb_dat_i = 0;
    dut->wb_sel_i = 0xF;
}

static bool wb_write(uint32_t addr, uint32_t data, int max_cycles = 20000) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i = 1;
    dut->wb_adr_i = addr;
    dut->wb_dat_i = data;
    dut->wb_sel_i = 0xF;

    for (int i = 0; i < max_cycles; i++) {
        tick();
        if (dut->wb_ack_o) {
            wb_idle();
            tick();
            return true;
        }
    }

    wb_idle();
    tick();
    return false;
}

static bool wb_read(uint32_t addr, uint32_t& data, int max_cycles = 20000) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i = 0;
    dut->wb_adr_i = addr;
    dut->wb_sel_i = 0xF;

    for (int i = 0; i < max_cycles; i++) {
        tick();
        if (dut->wb_ack_o) {
            data = dut->wb_dat_o;
            wb_idle();
            tick();
            return true;
        }
    }

    wb_idle();
    tick();
    return false;
}

static void reset_dut() {
    wb_idle();
    dut->wb_rst_i = 1;
    for (int i = 0; i < 6; i++) tick();
    dut->wb_rst_i = 0;
    tick();
}

static std::vector<uint8_t> vector_to_bytes(const SHA3NISTVector& tv) {
    std::vector<uint8_t> out;
    out.reserve(tv.num_full_words * 8 + tv.remaining_bytes);

    for (uint32_t i = 0; i < tv.num_full_words; i++) {
        uint64_t w = tv.input_words[i];
        for (int b = 0; b < 8; b++) {
            out.push_back(static_cast<uint8_t>((w >> (56 - 8 * b)) & 0xFFu));
        }
    }

    if (tv.remaining_bytes > 0) {
        uint64_t w = tv.input_words[tv.num_full_words];
        for (uint32_t b = 0; b < tv.remaining_bytes; b++) {
            out.push_back(static_cast<uint8_t>((w >> (56 - 8 * b)) & 0xFFu));
        }
    }

    return out;
}

static uint32_t pack_u32_be(const uint8_t* p, size_t n) {
    uint32_t w = 0;
    if (n > 0) w |= static_cast<uint32_t>(p[0]) << 24;
    if (n > 1) w |= static_cast<uint32_t>(p[1]) << 16;
    if (n > 2) w |= static_cast<uint32_t>(p[2]) << 8;
    if (n > 3) w |= static_cast<uint32_t>(p[3]) << 0;
    return w;
}

static bool write_message_bytes(const std::vector<uint8_t>& msg) {
    for (size_t i = 0; i < msg.size(); i += 4) {
        size_t rem = msg.size() - i;
        size_t n = rem >= 4 ? 4 : rem;
        if (!wb_write(0x08, pack_u32_be(&msg[i], n))) {
            return false;
        }
    }
    return true;
}

static bool wait_done(uint32_t& status_out, int max_cycles = 200000) {
    for (int i = 0; i < max_cycles; i++) {
        uint32_t st = 0;
        if (!wb_read(0x04, st)) {
            return false;
        }
        if (st & ST_DONE) {
            status_out = st;
            return true;
        }
    }
    return false;
}

static const SHA3NISTVector* find_vector(const std::string& name) {
    for (const auto& v : SHA3_NIST_VECTORS) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

static bool run_vector(const SHA3NISTVector& tv) {
    std::cout << "\n[TEST] " << tv.name << "\n";

    // Convert vector payload into chronological bytes, then stream as 32-bit writes.
    std::vector<uint8_t> msg = vector_to_bytes(tv);
    uint64_t msg_len = static_cast<uint64_t>(msg.size());

    // ABORT/reset peripheral state while preserving intended mode bits.
    uint32_t mode = mode_bits(tv.variant);
    if (!wb_write(0x00, mode | CTRL_ABORT)) return false;

    // Ensure IDLE before programming.
    uint32_t st = 0;
    if (!wb_read(0x04, st)) return false;
    if ((st & ST_IDLE) == 0) {
        std::cerr << "  Not IDLE after abort, status=0x" << std::hex << st << std::dec << "\n";
        return false;
    }

    // Program mode + message length.
    if (!wb_write(0x00, mode)) return false;
    if (!wb_write(0x18, static_cast<uint32_t>(msg_len & 0xFFFFFFFFULL))) return false;
    if (!wb_write(0x1C, static_cast<uint32_t>(msg_len >> 32))) return false;

    // Preload input FIFO.
    if (!write_message_bytes(msg)) return false;

    // Start hashing.
    if (!wb_write(0x00, mode | CTRL_START)) return false;

    // Wait for digest ready.
    uint32_t done_status = 0;
    if (!wait_done(done_status)) {
        std::cerr << "  Timeout waiting DONE\n";
        return false;
    }

    if (done_status & ST_ERR_MASK) {
        std::cerr << "  Error bits set, status=0x" << std::hex << done_status << std::dec << "\n";
        return false;
    }

    // Read and compare digest words.
    uint32_t dw = digest_words(tv.variant);
    bool pass = true;
    for (uint32_t i = 0; i < dw; i++) {
        uint32_t got = 0;
        if (!wb_read(0x10, got)) return false;
        uint32_t exp = tv.expected_digest[i];
        if (got != exp) {
            std::cerr << "  Mismatch word[" << i << "]: exp=0x"
                      << std::hex << std::setw(8) << std::setfill('0') << exp
                      << " got=0x" << std::setw(8) << got << std::dec << "\n";
            pass = false;
        }
    }

    if (pass) {
        std::cout << "  [PASS]\n";
    } else {
        std::cout << "  [FAIL]\n";
    }

    return pass;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    dut = new Vsha3_wb_top;
    reset_dut();

    const char* names[] = {
        "sha3_224_abc",
        "sha3_256_abc",
        "sha3_384_abc",
        "sha3_512_abc"
    };

    int passed = 0;
    int total = 0;
    for (const char* n : names) {
        const SHA3NISTVector* tv = find_vector(n);
        if (!tv) {
            std::cerr << "[FAIL] Missing vector: " << n << "\n";
            total++;
            continue;
        }

        total++;
        if (run_vector(*tv)) passed++;
    }

    std::cout << "\nSummary: " << passed << "/" << total << " passed\n";

    dut->final();
    delete dut;

    return (passed == total) ? 0 : 1;
}
