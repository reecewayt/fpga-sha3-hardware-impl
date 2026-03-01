# SHA-3 Out-of-Context Synthesis

Quick resource utilization checks for SHA-3 modules without full SoC integration. This is for incremental development and tracking how each module grows in complexity, both to ensure modules are synthesizing correctly and to monitor resource usage as we build up the design.

**Prerequisites**: 
- Vivado 2024.2 installed and added to PATH.
- Basic familiarity with FPGA synthesis and Vivado's Tcl interface.
- Developing in a Linux environment (scripts are bash-based).

## Quick Start

```bash
# Synthesize all modules
vivado -mode batch -source synth_ooc.tcl

# Or use the wrapper script (after making it executable)
chmod +x run_synth.sh
./run_synth.sh

# Synthesize a single module
./run_synth.sh round
```

## Adding New Modules

### Simple Module (Non-Parameterized)

Edit [synth_ooc.tcl](synth_ooc.tcl):

1. **Add module to list** (~line 20):
```tcl
set MODULES [list \
    "rconst_lut" \
    "round" \
    "your_new_module" \
]
```

2. **Add dependencies if needed** (~line 30):
```tcl
array set MODULE_DEPS {
    your_new_module  {rtl/dependency1.sv rtl/dependency2.sv}
}
```

3. **Add top module name mapping** (~line 45):
```tcl
array set MODULE_TOP_NAME {
    your_new_module  "your_new_module"
}
```

4. **Add empty generics entry** (~line 60):
```tcl
array set MODULE_GENERICS {
    your_new_module  {}
}
```

5. **Run synthesis**:
```bash
./run_synth.sh your_new_module
```

**Note**: Steps 3-4 are technically optional (the script defaults to the module name and no generics), but adding them explicitly maintains consistency and makes the configuration clearer.

### Parameterized Module with Multiple Variants

For modules with parameters (like `sha3_wb` with different FIFO depths), you can create multiple synthesis variants:

1. **Add all variants to module list** (~line 20):
```tcl
set MODULES [list \
    "your_module-variant1" \
    "your_module-variant2" \
    "your_module-variant3" \
]
```

2. **Add dependencies** (~line 30):
```tcl
array set MODULE_DEPS {
    your_module-variant1  {}
    your_module-variant2  {}
    your_module-variant3  {}
}
```

3. **Map variants to actual RTL top module** (~line 45):
```tcl
array set MODULE_TOP_NAME {
    your_module-variant1  "your_module"
    your_module-variant2  "your_module"
    your_module-variant3  "your_module"
}
```

4. **Specify generic parameters for each variant** (~line 60):
```tcl
array set MODULE_GENERICS {
    your_module-variant1  {-generic PARAM1=8}
    your_module-variant2  {-generic PARAM1=16}
    your_module-variant3  {-generic PARAM1=32 -generic PARAM2=64}
}
```

**Example**: The `sha3_wb` module demonstrates this pattern:
```tcl
# Five variants of sha3_wb with different FIFO depths
set MODULES [list \
    "sha3-wb-fifo8" \
    "sha3-wb-fifo16" \
    "sha3-wb-fifo32" \
    "sha3-wb-fifo64" \
]

# All map to same RTL module
array set MODULE_TOP_NAME {
    sha3-wb-fifo8   "sha3_wb"
    sha3-wb-fifo16  "sha3_wb"
    sha3-wb-fifo32  "sha3_wb"
    sha3-wb-fifo64  "sha3_wb"
}

# Each with different FIFO_DEPTH parameter
array set MODULE_GENERICS {
    sha3-wb-fifo8   {-generic FIFO_DEPTH=8}
    sha3-wb-fifo16  {-generic FIFO_DEPTH=16}
    sha3-wb-fifo32  {-generic FIFO_DEPTH=32}
    sha3-wb-fifo64  {-generic FIFO_DEPTH=64}
}
```

This allows comparing resource usage across different parameter values in a single synthesis run.

## Output Reports

All reports are saved to `synth_reports/`:

- **`<module>_utilization.rpt`** - LUT, FF, BRAM, DSP usage
- **`<module>_utilization_hier.rpt`** - Hierarchical breakdown
- **`<module>_timing.rpt`** - Timing analysis
- **`<module>_design_analysis.rpt`** - Design quality metrics
- **`<module>_ooc.dcp`** - Checkpoint for reuse


## Notes
- Synthesis runs in **out-of-context** mode (no I/O constraints)
- Good for relative comparisons and resource tracking
- Add modules incrementally to monitor growth
- TODO: Could add timing constraints for more realistic estimates in the future
