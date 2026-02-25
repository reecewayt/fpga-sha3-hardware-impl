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
        VERILATOR_ARGS --timing
    )
    
    # Register with CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # Set test properties (optional: add timeout, working directory, etc.)
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 30
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# WB_DIR points to the Wishbone RTL
set(WB_DIR ${CMAKE_SOURCE_DIR}/wb)

# =============================================================================
# Add your tests below - one line per test!
# =============================================================================

add_verilator_test(tb_rconst_lut rconst_lut ${RTL_DIR}/rconst_lut.sv)
add_verilator_test(tb_round round ${RTL_DIR}/sha3_pkg.sv ${RTL_DIR}/round.sv)
add_verilator_test(tb_sha3_wb sha3_wb ${RTL_DIR}/wb/sha3-wb.sv)

# To add more tests, just add lines like:
# add_verilator_test(tb_keccak_round keccak_round ${RTL_DIR}/keccak_round.sv)
# add_verilator_test(tb_theta theta ${RTL_DIR}/theta.sv ${RTL_DIR}/helper.sv)
