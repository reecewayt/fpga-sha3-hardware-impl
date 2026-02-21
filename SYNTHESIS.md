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

Edit [synth_ooc.tcl](synth_ooc.tcl):

1. **Add module to list** (line ~20):
```tcl
set MODULES [list \
    "rconst_lut" \
    "round" \
    "your_new_module" \
]
```

2. **Add dependencies if needed** (line ~26):
```tcl
array set MODULE_DEPS {
    your_new_module  {rtl/dependency1.sv rtl/dependency2.sv}
}
```

3. **Run synthesis**:
```bash
./run_synth.sh your_new_module
```

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
