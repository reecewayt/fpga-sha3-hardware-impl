package sha3_pkg;
    // Constants
    localparam int STATE_WIDTH = 1600;
    localparam int LANE_WIDTH = 64;
    localparam int GRID_SIZE = 5;
    localparam int ROUNDS = 24;
    
    // SHA-3 Variant Configuration
    // Set PROGRAMMABLE = 1 to support runtime variant selection (larger design)
    // Set PROGRAMMABLE = 0 to hardcode a single variant (smaller design)
    // When PROGRAMMABLE = 0, DEFAULT_VARIANT selects which variant to implement
    parameter bit PROGRAMMABLE = 0; // This is to provide a fallback for non-programmable designs, 
                                    // which will be smaller and might fit better in our FPGA. 
    
    // SHA-3 Variant Definitions
    typedef enum logic [1:0] {
        SHA3_224 = 2'b00,
        SHA3_256 = 2'b01,
        SHA3_384 = 2'b10,
        SHA3_512 = 2'b11
    } sha3_variant_t;
    
    // Default variant when PROGRAMMABLE = 0
    parameter sha3_variant_t DEFAULT_VARIANT = SHA3_512;
    
    // Rate (r) and Capacity (c) for each variant
    // r + c = 1600 bits, security = c/2
    localparam int RATE_SHA3_224 = 1152;  // c = 448, security = 224 bits
    localparam int RATE_SHA3_256 = 1088;  // c = 512, security = 256 bits
    localparam int RATE_SHA3_384 = 832;   // c = 768, security = 384 bits
    localparam int RATE_SHA3_512 = 576;   // c = 1024, security = 512 bits
    
    // Maximum rate needed for programmable designs
    localparam int MAX_RATE = RATE_SHA3_224;
    
    // Get rate for a given variant
    function automatic int get_rate(sha3_variant_t variant);
        case (variant)
            SHA3_224: return RATE_SHA3_224;
            SHA3_256: return RATE_SHA3_256;
            SHA3_384: return RATE_SHA3_384;
            SHA3_512: return RATE_SHA3_512;
            default:  return RATE_SHA3_512;
        endcase
    endfunction
    
    // Get capacity for a given variant
    function automatic int get_capacity(sha3_variant_t variant);
        return STATE_WIDTH - get_rate(variant);
    endfunction
    
    // Utility functions for bit indexing
    // Essential come from NIST FIPS 202 specification, section 3.1.2
    // A[x,y,z]=S[w(5y+x)+z]
    function automatic int high_pos(int x, int y);
        return STATE_WIDTH - 1 - LANE_WIDTH * (GRID_SIZE * y + x);
    endfunction
    
    function automatic int low_pos(int x, int y);
        return high_pos(x, y) - (LANE_WIDTH - 1);
    endfunction
    
    // Modular arithmetic for 5x5 grid
    function automatic int add_1(int x); //(x + 1) mod 5
        return (x == 4) ? 0 : x + 1;
    endfunction
    
    function automatic int add_2(int x); //(x + 2) mod 5
        return (x == 3) ? 0 : (x == 4) ? 1 : x + 2;
    endfunction
    
    function automatic int sub_1(int x); //(x - 1) mod 5
        return (x == 0) ? 4 : x - 1;
    endfunction
    
    // Rotation functions
    // These are shift registers with wrap-around
    function automatic logic [LANE_WIDTH-1:0] rot_up(
        logic [LANE_WIDTH-1:0] in, 
        int n
    );
        // Use shift operators for synthesizability,
        // vivado had troubles synthesizing bit slicing of variable indices.
        return (in << n) | (in >> (LANE_WIDTH - n));
    endfunction
    
    function automatic logic [LANE_WIDTH-1:0] rot_up_1(
        logic [LANE_WIDTH-1:0] in
    );
        return {in[LANE_WIDTH-2:0], in[LANE_WIDTH-1]};
    endfunction
    
    // Type definitions
    typedef logic [LANE_WIDTH-1:0] lane_t;
    typedef lane_t [GRID_SIZE-1:0][GRID_SIZE-1:0] state_array_t;
    
endpackage : sha3_pkg