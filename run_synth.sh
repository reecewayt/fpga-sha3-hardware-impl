#!/bin/bash
# Wrapper script to run out-of-context synthesis
# Usage: ./run_synth.sh [module_name]
#        ./run_synth.sh           # Synthesize all modules
#        ./run_synth.sh round     # Synthesize only 'round' module

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting SHA-3 Out-of-Context Synthesis${NC}"

# Check if Vivado is available
if ! command -v vivado &> /dev/null; then
    echo -e "${RED}ERROR: Vivado not found in PATH${NC}"
    echo "Please source Vivado settings script first:"
    echo "  source /path/to/Vivado/2023.x/settings64.sh"
    exit 1
fi

# Run synthesis
if [ $# -eq 0 ]; then
    echo -e "${YELLOW}Synthesizing all modules...${NC}"
    vivado -mode batch -source synth_ooc.tcl -notrace
else
    echo -e "${YELLOW}Synthesizing module: $1${NC}"
    vivado -mode batch -source synth_ooc.tcl -notrace -tclargs "$1"
fi

# Check if successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Synthesis completed successfully!${NC}"
    echo ""
    echo "Reports available in synth_reports/"
    echo ""
    echo "View utilization:"
    echo "  cat synth_reports/*_utilization.rpt"
else
    echo -e "${RED}✗ Synthesis failed!${NC}"
    echo "Check vivado.log for details"
    exit 1
fi
