package sha3_pkg;
    // Constants
    localparam int STATE_WIDTH = 1600;
    localparam int LANE_WIDTH = 64;
    localparam int GRID_SIZE = 5;
    localparam int ROUNDS = 24;
    
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