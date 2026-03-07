# fpga-sha3-hardware-impl

RTL implementation of SHA-3 standard for hardware acceleration; this project targets the RVfpga platform and synthesized for Real Digital Boolean Board.

> Although the implementation is designed for RVfpga, it can be adapted to other FPGA platforms with minimal modifications. The top-level module is `sha3_wb_top.sv`, which acts as the front-end Wishbone interface to the SHA-3 core. The design is modular and reusable, making it straightforward to integrate into other bus architectures. This was an educational project and is not optimized for performance or security.

---

## Getting Started

### Dependencies

- [Verilator](https://verilator.org) — RTL simulation and testbench compilation
- CMake ≥ 3.10
- CTest (bundled with CMake)
- Vivado 2024.x — for synthesis (optional)

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Run Tests

```bash
cd build
ctest
```

Tests cover each module individually and the full SHA-3 core against NIST vectors. To re-run only failing tests:

```bash
ctest --rerun-failed --output-on-failure
```

### Out-of-Context Synthesis (Vivado)

```bash
vivado -mode batch -source synth_ooc.tcl
# Synthesize a single module:
vivado -mode batch -source synth_ooc.tcl -tclargs keccak
```

Reports are written to `synth_reports/`.

---

## Architecture

All RTL source is under `rtl/`. The design is a fully synchronous, iterative Keccak-f[1600] implementation supporting all four SHA-3 variants (SHA3-224/256/384/512), selectable at runtime by user application.

### Module Hierarchy

```
sha3_wb_top        — Wishbone bus wrapper (top-level integration point)
└── sha3_wb        — Wishbone peripheral: control/status registers, input/output FIFOs
    └── keccak     — SHA-3 core: streaming 64-bit input, 512-bit digest output
        ├── padder         — pad10*1 padding, rate-block assembly, variant-aware
        └── f_permutation  — 24-cycle iterative Keccak-f[1600] state machine
            ├── round      — combinational single-round datapath (θ ρ π χ ι)
            └── rconst_lut — round constant ROM (LUT-based, 24 × 64-bit)
```

### Module Descriptions

**`sha3_pkg.sv`** — Package defining shared constants, the `sha3_variant_t` enum, rate/capacity values per variant, and the `PROGRAMMABLE` compile-time flag.

**`sha3_wb_top.sv`** — Thin top-level wrapper. Exposes only the Wishbone bus externally and wires the internal control/data signals between `sha3_wb` and `keccak`.

**`sha3-wb.sv`** — Wishbone peripheral. Implements a 32-bit register map with an input FIFO, output FIFO, control/status registers, and message-length tracking. Pairs consecutive 32-bit writes into 64-bit words for the keccak interface. Key registers:

| Offset | Register | Access |
|---|---|---|
| `0x00` | Control (`START`, `ABORT`, `MODE`) | R/W |
| `0x04` | Status (`IDLE`, `BUSY`, `DONE`, FIFO flags, error flags) | R |
| `0x08` | `IN_FIFO_DATA` | W |
| `0x10` | `OUT_FIFO_DATA` | R |
| `0x18/0x1C` | `MSG_LEN_LO/HI` | R/W |

**`keccak.sv`** — Core top. Accepts a 64-bit streaming input with `in_ready`/`is_last`/`byte_num` handshake and drives `padder` → `f_permutation`. Outputs the digest on `out[511:0]` with `out_ready`.

**`padder.sv`** — Implements pad10\*1 padding per NIST FIPS 202. Shifts incoming 64-bit words into a rate-width shift register, inserts the padding byte, and presents complete rate-blocks to `f_permutation` one at a time via `out_ready`/`f_ack`.

**`f_permutation.sv`** — State machine that iterates `round` 24 times per Keccak-f[1600] call. Holds the 1600-bit state in a register. On the accept cycle it XORs the padded input block into the state before proceeding with round 0. Issues `done` when all 24 rounds complete.

**`round.sv`** — Fully combinational single-round datapath implementing the five Keccak step mappings: θ, ρ, π, χ, ι. Takes a 1600-bit state and a 64-bit round constant; produces the next 1600-bit state in one clock cycle of setup time.

**`rconst_lut.sv`** — LUT-based ROM returning the 64-bit Keccak round constant for a given round index (0–23).

### Synthesis Targets (XC7S50 Spartan-7)

| Module | LUTs | FFs | BRAM |
|---|---|---|---|
| `keccak` (full core) | 3,970 (12.2%) | 2,782 (4.3%) | 0 |
| `sha3_wb` (WB interface, FIFO=64) | 532 (1.6%) | 736 (1.1%) | 1× RAMB18 |
| `round` (combinational) | 2,752 | 0 | 0 |
| `padder` | 73 | 1,173 | 0 |

The worst-case combinational path through `keccak` is **5.78 ns** (7 logic levels), giving a synthesis-estimated ceiling of ~173 MHz before place-and-route.
