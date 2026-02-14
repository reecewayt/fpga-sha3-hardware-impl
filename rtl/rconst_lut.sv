/*
 * Module: sha3_rc_lut
 * -------------------
 * Implementation of SHA-3 (Keccak) Round Constants using an area-optimized 
 * 7-bit Look-Up Table (LUT).
 *
 * Optimization Logic:
 * Per the Keccak specification (and highlighted by Chemejon's "SHA-3 Explained"), 
 * only 7 specific bit positions in the 64-bit Round Constant (RC) are ever 
 * non-zero. These positions follow the sequence 2^j - 1 [ for j=0 to 6]:
 * j=0: bit 0
 * j=1: bit 1
 * j=2: bit 3
 * j=3: bit 7
 * j=4: bit 15
 * j=5: bit 31
 * j=6: bit 63
 *
 * Implementation:
 * To save hardware area, we store only these 7 bits in a 24-entry ROM 
 * (168 bits total vs 1536 bits for a full 64-bit ROM). The 64-bit output 
 * is reconstructed by mapping these 7 bits to their respective positions 
 * and tying the remaining 57 bits to logic 0.
 *
 * Source: https://chemejon.wordpress.com/2021/12/06/sha-3-explained-in-plain-english/
 *
 * Note: Generated using Gemini AI
 */
module rconst_lut (
    input  logic [4:0]  rnd_idx, // 0 to 23
    output logic [63:0] rc_out
);

    logic [6:0] rc_7bit;

    // The 24x7-bit compressed ROM
    always_comb begin
        case (rnd_idx)
            5'd00: rc_7bit = 7'h01; 
            5'd01: rc_7bit = 7'h1A;
            5'd02: rc_7bit = 7'h5E; 
            5'd03: rc_7bit = 7'h70;
            5'd04: rc_7bit = 7'h1F; 
            5'd05: rc_7bit = 7'h21;
            5'd06: rc_7bit = 7'h79; 
            5'd07: rc_7bit = 7'h55;
            5'd08: rc_7bit = 7'h0E; 
            5'd09: rc_7bit = 7'h0C;
            5'd10: rc_7bit = 7'h35; 
            5'd11: rc_7bit = 7'h26;
            5'd12: rc_7bit = 7'h3F; 
            5'd13: rc_7bit = 7'h4F;
            5'd14: rc_7bit = 7'h5D; 
            5'd15: rc_7bit = 7'h53;
            5'd16: rc_7bit = 7'h52; 
            5'd17: rc_7bit = 7'h48;
            5'd18: rc_7bit = 7'h16; 
            5'd19: rc_7bit = 7'h66;
            5'd20: rc_7bit = 7'h79; 
            5'd21: rc_7bit = 7'h58;
            5'd22: rc_7bit = 7'h21; 
            5'd23: rc_7bit = 7'h74;
            default: rc_7bit = 7'h00;
        endcase
    end

    // Bit-mapping to the 64-bit word
    // SHA-3 indices: 0, 1, 3, 7, 15, 31, 63
    always_comb begin
        rc_out = 64'b0; // Default all bits to 0
        rc_out[0]  = rc_7bit[0];
        rc_out[1]  = rc_7bit[1];
        rc_out[3]  = rc_7bit[2];
        rc_out[7]  = rc_7bit[3];
        rc_out[15] = rc_7bit[4];
        rc_out[31] = rc_7bit[5];
        rc_out[63] = rc_7bit[6];
    end

endmodule