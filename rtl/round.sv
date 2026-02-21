import sha3_pkg::*;

module round(
    input  logic [STATE_WIDTH-1:0] in,
    input  logic [LANE_WIDTH-1:0]  round_const,
    output logic [STATE_WIDTH-1:0] out
);

    lane_t a[GRID_SIZE][GRID_SIZE];
    lane_t b[GRID_SIZE];
    lane_t c[GRID_SIZE][GRID_SIZE];
    lane_t d[GRID_SIZE][GRID_SIZE];
    lane_t e[GRID_SIZE][GRID_SIZE];
    lane_t f[GRID_SIZE][GRID_SIZE];
    lane_t g[GRID_SIZE][GRID_SIZE];

    // Unpack input into state array
    generate
        for(genvar y=0; y<GRID_SIZE; y++) begin : gen_unpack_rows
            for(genvar x=0; x<GRID_SIZE; x++) begin : gen_unpack_cols
                assign a[x][y] = in[high_pos(x,y) : low_pos(x,y)];
            end
        end
    endgenerate

    // Theta step - column parity
    generate
        for(genvar x=0; x<GRID_SIZE; x++) begin : gen_theta_parity
            assign b[x] = a[x][0] ^ a[x][1] ^ a[x][2] ^ a[x][3] ^ a[x][4];
        end
    endgenerate

    // Theta step - mix columns
    generate
        for(genvar y=0; y<GRID_SIZE; y++) begin : gen_theta_rows
            for(genvar x=0; x<GRID_SIZE; x++) begin : gen_theta_cols
                assign c[x][y] = a[x][y] ^ b[sub_1(x)] ^ rot_up_1(b[add_1(x)]);
            end
        end
    endgenerate

    // Rho step - rotations
    // These are fixed rotations as specified in FIPS 202, Section 3.2.2
    // The specification provides raw bit offset values in the state array,
    // but since each lane is 64 bits, we take (offset % 64) for the rotation amount.
    //
    // Original table from specification (t(x,y) values):
    //      x=3  x=4  x=0  x=1  x=2
    // y=2  153  231    3   10  171
    // y=1   55  276   36  300    6
    // y=0   28   91    0    1  190
    // y=4  120   78  210   66  253
    // y=3   21  136  105   45   15
    //
    // Example derivations:
    //   d[2][0]: table value = 190, rotation = 190 % 64 = 62
    //   d[1][1]: table value = 300, rotation = 300 % 64 = 44
    //   d[4][1]: table value = 276, rotation = 276 % 64 = 20
    //   d[2][2]: table value = 171, rotation = 171 % 64 = 43
    assign d[0][0] = c[0][0];
    assign d[1][0] = rot_up_1(c[1][0]);
    assign d[2][0] = rot_up(c[2][0], 62);
    assign d[3][0] = rot_up(c[3][0], 28);
    assign d[4][0] = rot_up(c[4][0], 27);
    assign d[0][1] = rot_up(c[0][1], 36);
    assign d[1][1] = rot_up(c[1][1], 44);
    assign d[2][1] = rot_up(c[2][1], 6);
    assign d[3][1] = rot_up(c[3][1], 55);
    assign d[4][1] = rot_up(c[4][1], 20);
    assign d[0][2] = rot_up(c[0][2], 3);
    assign d[1][2] = rot_up(c[1][2], 10);
    assign d[2][2] = rot_up(c[2][2], 43);
    assign d[3][2] = rot_up(c[3][2], 25);
    assign d[4][2] = rot_up(c[4][2], 39);
    assign d[0][3] = rot_up(c[0][3], 41);
    assign d[1][3] = rot_up(c[1][3], 45);
    assign d[2][3] = rot_up(c[2][3], 15);
    assign d[3][3] = rot_up(c[3][3], 21);
    assign d[4][3] = rot_up(c[4][3], 8);
    assign d[0][4] = rot_up(c[0][4], 18);
    assign d[1][4] = rot_up(c[1][4], 2);
    assign d[2][4] = rot_up(c[2][4], 61);
    assign d[3][4] = rot_up(c[3][4], 56);
    assign d[4][4] = rot_up(c[4][4], 14);

    // Pi step - permutation
    // Lane rearrangement
    // Formula: e[x][y] = d[(x + 3y) mod 5][x]
    assign e[0][0] = d[0][0];
    assign e[0][2] = d[1][0];
    assign e[0][4] = d[2][0];
    assign e[0][1] = d[3][0];
    assign e[0][3] = d[4][0];
    assign e[1][3] = d[0][1];
    assign e[1][0] = d[1][1];
    assign e[1][2] = d[2][1];
    assign e[1][4] = d[3][1];
    assign e[1][1] = d[4][1];
    assign e[2][1] = d[0][2];
    assign e[2][3] = d[1][2];
    assign e[2][0] = d[2][2];
    assign e[2][2] = d[3][2];
    assign e[2][4] = d[4][2];
    assign e[3][4] = d[0][3];
    assign e[3][1] = d[1][3];
    assign e[3][3] = d[2][3];
    assign e[3][0] = d[3][3];
    assign e[3][2] = d[4][3];
    assign e[4][2] = d[0][4];
    assign e[4][4] = d[1][4];
    assign e[4][1] = d[2][4];
    assign e[4][3] = d[3][4];
    assign e[4][0] = d[4][4];

    // Chi step - non-linear mixing
    // This is the only non-linear operation in Keccak, providing confusion.
    // Formula: f[x][y] = e[x][y] XOR ((NOT e[x+1][y]) AND e[x+2][y])
    //
    // For each lane, XOR it with the result of:
    //   - Inverting the next lane in the row (x+1 mod 5)
    //   - ANDing with the lane two positions over (x+2 mod 5)
    //
    // This creates a non-linear S-box like operation across each row,
    // preventing linear cryptanalysis of the hash function.
    generate
        for(genvar y=0; y<GRID_SIZE; y++) begin : gen_chi_rows
            for(genvar x=0; x<GRID_SIZE; x++) begin : gen_chi_cols
                assign f[x][y] = e[x][y] ^ ((~e[add_1(x)][y]) & e[add_2(x)][y]);
            end
        end
    endgenerate

    // Iota step - add round constant to specific bits
    // Breaks symmetry by adding a round-specific constant to lane [0][0].
    // Only certain bit positions are modified: 0, 1, 3, 7, 15, 31, 63
    // These correspond to (2^j - 1) for j = 0, 1, 2, 3, 4, 5, 6
    //
    // Without this step, all rounds would be identical. The round constant
    // ensures each of the 24 rounds produces a different transformation,
    // preventing slide attacks and other symmetry-based cryptanalysis.
    //
    // Only lane [0][0] is modified; all other lanes pass through unchanged.
    generate
        for(genvar x=0; x<LANE_WIDTH; x++) begin : gen_iota_bits
            if(x==0 || x==1 || x==3 || x==7 || x==15 || x==31 || x==63)
                assign g[0][0][x] = f[0][0][x] ^ round_const[x];
            else
                assign g[0][0][x] = f[0][0][x];
        end
    endgenerate
    
    // Iota step - pass through other lanes
    generate
        for(genvar y=0; y<GRID_SIZE; y++) begin : gen_iota_passthrough_rows
            for(genvar x=0; x<GRID_SIZE; x++) begin : gen_iota_passthrough_cols
                if(x!=0 || y!=0)
                    assign g[x][y] = f[x][y];
            end
        end
    endgenerate

    // Pack output from state arraya
    generate
        for(genvar y=0; y<GRID_SIZE; y++) begin : gen_pack_rows
            for(genvar x=0; x<GRID_SIZE; x++) begin : gen_pack_cols
                assign out[high_pos(x,y) : low_pos(x,y)] = g[x][y];
            end
        end
    endgenerate

endmodule : round