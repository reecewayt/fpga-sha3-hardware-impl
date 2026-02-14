# Build Instructions

## Prerequisites

- CMake 3.10 or higher
- Verilator (latest version recommended)
- C++ compiler with C++11 support

## Building and Running Tests

### First-time setup:

```bash
# Create build directory
mkdir build
cd build

# Configure the project
cmake ..

# Build all tests
make

# Run all tests
ctest

# Or run with verbose output
ctest --verbose
```

### Running individual tests:

```bash
# From the build directory
./tests/tb_rconst_lut

# Or using ctest
ctest -R tb_rconst_lut --verbose
```

### Incremental builds:

After making changes to RTL or testbench code:

```bash
cd build
make          # Rebuild only what changed
ctest         # Run all tests
```

## Adding New Tests

To add a new test, simply add one line to `tests/CMakeLists.txt`:

```cmake
add_verilator_test(tb_my_module my_module ${RTL_DIR}/my_module.sv)
```

Where:
- `tb_my_module` is the name of your testbench C++ file (without .cpp extension)
- `my_module` is the name of the top-level SystemVerilog module
- `${RTL_DIR}/my_module.sv` is the path to your SystemVerilog file(s)

Multiple RTL files can be specified:
```cmake
add_verilator_test(tb_complex complex_top 
    ${RTL_DIR}/module1.sv 
    ${RTL_DIR}/module2.sv
    ${RTL_DIR}/module3.sv)
```

## Clean Build

```bash
rm -rf build
mkdir build
cd build
cmake ..
make
```

## CMake Options

```bash
# Build with debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..

# Specify Verilator location if not in PATH
cmake -DVERILATOR_ROOT=/path/to/verilator ..
```
