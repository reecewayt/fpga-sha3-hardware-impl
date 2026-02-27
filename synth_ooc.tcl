#!/usr/bin/tclsh
# Out-of-Context Synthesis Script for SHA-3 Modules
# Target: Xilinx Spartan-7 XC7S50-CSGA324
#
# Usage: vivado -mode batch -source synth_ooc.tcl
#        vivado -mode batch -source synth_ooc.tcl -tclargs <module_name>

# =============================================================================
# Configuration
# =============================================================================

# Target FPGA part
set FPGA_PART "xc7s50csga324-1"

# Project paths
set RTL_DIR "rtl"
set REPORTS_DIR "synth_reports"

# List of modules to synthesize (add new modules here as design grows)
set MODULES [list \
    "rconst_lut" \
    "round" \
]

# Module dependencies - add files that each module depends on
# Format: module_name -> list of additional files
array set MODULE_DEPS {
    rconst_lut  {}
    round       {}
}

# Common files needed by all modules (package files, etc.)
set COMMON_FILES [list \
    "$RTL_DIR/sha3_pkg.sv" \
]

# =============================================================================
# Helper Functions
# =============================================================================

proc create_reports_dir {dir} {
    if {![file exists $dir]} {
        file mkdir $dir
        puts "Created reports directory: $dir"
    }
}

proc synth_module {module_name part common_files deps reports_dir} {
    puts "\n========================================="
    puts "Synthesizing module: $module_name"
    puts "=========================================\n"
    
    # Read common files (package)
    foreach f $common_files {
        if {[file exists $f]} {
            puts "Reading: $f"
            read_verilog -sv $f
        } else {
            puts "WARNING: File not found: $f"
        }
    }
    
    # Read module file
    set module_file "rtl/${module_name}.sv"
    if {[file exists $module_file]} {
        puts "Reading: $module_file"
        read_verilog -sv $module_file
    } else {
        puts "ERROR: Module file not found: $module_file"
        return 1
    }
    
    # Read module-specific dependencies
    foreach f $deps {
        if {[file exists $f]} {
            puts "Reading: $f"
            read_verilog -sv $f
        } else {
            puts "WARNING: Dependency not found: $f"
        }
    }
    
    # Synthesize out-of-context
    puts "\nRunning synthesis for $module_name..."
    if {[catch {synth_design -top $module_name -part $part -mode out_of_context} err]} {
        puts "ERROR: Synthesis failed for $module_name"
        puts $err
        return 1
    }
    
    # Generate reports
    set report_prefix "$reports_dir/${module_name}"
    
    puts "Generating utilization report..."
    report_utilization -file "${report_prefix}_utilization.rpt"
    report_utilization -hierarchical -file "${report_prefix}_utilization_hier.rpt"
    
    puts "Generating timing summary..."
    report_timing_summary -file "${report_prefix}_timing.rpt"
    
    puts "Generating design analysis..."
    report_design_analysis -file "${report_prefix}_design_analysis.rpt"
    
    # Write checkpoint for potential reuse
    puts "Writing checkpoint..."
    write_checkpoint -force "${report_prefix}_ooc.dcp"
    
    # Print quick summary to console
    puts "\n--- Quick Summary for $module_name ---"
    report_utilization -quiet
    
    # Clean up for next module
    close_design
    
    puts "\nCompleted: $module_name"
    puts "Reports saved to: $reports_dir/"
    
    return 0
}

# =============================================================================
# Main Script
# =============================================================================

puts "========================================="
puts "SHA-3 Out-of-Context Synthesis"
puts "Target Device: $FPGA_PART"
puts "========================================="

# Create reports directory
create_reports_dir $REPORTS_DIR

# Check if specific module requested via command line
set modules_to_synth $MODULES
if {$argc > 0} {
    set requested_module [lindex $argv 0]
    if {[lsearch -exact $MODULES $requested_module] >= 0} {
        set modules_to_synth [list $requested_module]
        puts "\nSynthesizing single module: $requested_module"
    } else {
        puts "\nERROR: Unknown module '$requested_module'"
        puts "Available modules: $MODULES"
        exit 1
    }
}

# Synthesize each module
set success_count 0
set fail_count 0
set failed_modules [list]

foreach module $modules_to_synth {
    # Get module dependencies
    if {[info exists MODULE_DEPS($module)]} {
        set deps $MODULE_DEPS($module)
    } else {
        set deps {}
    }
    
    # Synthesize
    set result [synth_module $module $FPGA_PART $COMMON_FILES $deps $REPORTS_DIR]
    
    if {$result == 0} {
        incr success_count
    } else {
        incr fail_count
        lappend failed_modules $module
    }
}

# Final summary
puts "\n========================================="
puts "Synthesis Complete"
puts "========================================="
puts "Successful: $success_count"
puts "Failed:     $fail_count"

if {$fail_count > 0} {
    puts "\nFailed modules: $failed_modules"
    exit 1
}

puts "\nAll modules synthesized successfully!"
puts "Check $REPORTS_DIR/ for detailed reports"

exit 0
