# Helper function to create a Verilator testbench executable
# Usage: add_verilator_test(<test_name> <top_module> <rtl_files...>)
#
# Example:
#   add_verilator_test(tb_my_module my_module ${RTL_DIR}/my_module.sv ${RTL_DIR}/helper.sv)
#
function(add_verilator_test TEST_NAME TOP_MODULE)
    set(RTL_SOURCES ${ARGN})
    
    # Create executable from testbench C++ file
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    
    # Verilate the SystemVerilog design
    verilate(${TEST_NAME} 
        SOURCES ${RTL_SOURCES}
        TOP_MODULE ${TOP_MODULE}
        PREFIX V${TOP_MODULE}
        VERILATOR_ARGS --timing --trace
    )
    
    # Register with CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # Set test properties (optional: add timeout, working directory, etc.)
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 30
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# =============================================================================
# Parameterised FIFO-depth tests for sha3_wb
#
# Each variant compiles the RTL with a different FIFO_DEPTH using Verilator's
# -G<param>=<value> override, and passes the same value to the C++ testbench
# via -DTEST_FIFO_DEPTH so that T15/T16 (FIFO-full and stall tests) scale
# automatically.  All existing functional tests also run against each variant,
# confirming the design works across the supported FIFO depth range.
# =============================================================================
function(add_sha3_wb_fifo_test TEST_NAME FIFO_DEPTH)
    add_executable(${TEST_NAME} tb_sha3_wb.cpp)
    target_compile_definitions(${TEST_NAME} PRIVATE TEST_FIFO_DEPTH=${FIFO_DEPTH})
    verilate(${TEST_NAME}
        SOURCES ${RTL_DIR}/wb/sha3-wb.sv
        TOP_MODULE sha3_wb
        VERILATOR_ARGS --timing -GFIFO_DEPTH=${FIFO_DEPTH}
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# =============================================================================
# Add your tests below - one line per test!
# =============================================================================

add_verilator_test(tb_rconst_lut rconst_lut ${RTL_DIR}/rconst_lut.sv)
add_verilator_test(tb_round round ${RTL_DIR}/sha3_pkg.sv ${RTL_DIR}/round.sv)
add_verilator_test(tb_round_abc round ${RTL_DIR}/sha3_pkg.sv ${RTL_DIR}/round.sv)
add_sha3_wb_fifo_test(tb_sha3_wb_fifo8  8)
add_sha3_wb_fifo_test(tb_sha3_wb_fifo64 64)
add_verilator_test(tb_padder padder ${RTL_DIR}/sha3_pkg.sv ${RTL_DIR}/padder.sv)
add_verilator_test(tb_f_permutation f_permutation
    ${RTL_DIR}/sha3_pkg.sv
    ${RTL_DIR}/rconst_lut.sv
    ${RTL_DIR}/round.sv
    ${RTL_DIR}/f_permutation.sv
)
add_verilator_test(tb_keccak keccak
    ${RTL_DIR}/sha3_pkg.sv
    ${RTL_DIR}/rconst_lut.sv
    ${RTL_DIR}/round.sv
    ${RTL_DIR}/f_permutation.sv
    ${RTL_DIR}/padder.sv
    ${RTL_DIR}/keccak.sv
)

# To add more tests, just add lines like:
# add_verilator_test(tb_keccak_round keccak_round ${RTL_DIR}/keccak_round.sv)
# add_verilator_test(tb_theta theta ${RTL_DIR}/theta.sv ${RTL_DIR}/helper.sv)
