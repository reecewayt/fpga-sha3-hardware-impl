/*
 * Copyright 2013, Homer Hsing <homer.hsing@gmail.com>
 * Modified 2026, Adapted to SystemVerilog with programmable variant support
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* "is_last" == 0 means byte number is 8 (full 64-bit word), no matter what value "byte_num" is. */
/* if "in_ready" == 0, then "is_last" should be 0. */
/* the user switch to next "in" only if "ack" == 1. */

import sha3_pkg::*;

module keccak (
    input  logic                    clk,
    input  logic                    reset,
    input  logic [63:0]             in,
    input  logic                    in_ready,
    input  logic                    is_last,
    input  logic [2:0]              byte_num,
    input  sha3_variant_t           variant,        // SHA-3 variant selection
    output logic                    buffer_full,
    output logic [511:0]     out,                  // Max output size (for SHA3-224)
    output logic                    out_ready
);

    // State machine
    logic                state;     // state == 0: user will send more input data
                                    // state == 1: user will not send any data
    
    // Padder outputs
    logic [MAX_RATE-1:0]     padder_out_ld;  // little endian representation
                                             // keccak is a little different, they put the msb in the low position, 
                                             // so (i.e.) bit 7 goes to bit 0 of each byte; essentially each byte is mirrored. 

    logic                   padder_out_ready; // indicates when the padder has valid output to feed into the F-permutation
    logic                   padder_done;      // indicates when padder has finished processing all blocks
    logic                   padder_more_blks; // indicates more blocks will arrive from padder

    
    logic [MAX_RATE-1:0]    f_in;           // input to F-permutation, which is the output of the padder << shift logic

    
    // F-permutation interface
    logic                    f_ack;
    logic [STATE_WIDTH-1:0]  f_out;
    logic                    f_out_ready;
    logic [511:0]            f_out_raw;        // before reorder bytes
                                        // need to reorganize bits in each byte for 
                                        // how computer usually represents data (msb in high position) vs how keccak expects it (msb in low position)

    // Interface signals
    logic                   ack_more_blks;

    // Bit mirror logic
    logic [63:0]  in_switch;

    genvar w, b; 
    
    assign f_out_raw = f_out[1599:1599-511]; // Only the first 512 bits are relevant for output, depending on the variant
    
    // State tracking
    always_ff @(posedge clk) begin
        if (reset)
            state <= 1'b0;
        else if (is_last)
            state <= 1'b1;
    end

    // Switch byte order 
    assign in_switch = {in[7:0], in[15:8], in[23:16], in[31:24],
                        in[39:32], in[47:40], in[55:48], in[63:56]};

    // Reverse the bit mirror for each byte to return to hexadecimal representation in the output
    generate
        for (w = 0; w < 8; w++) begin : FOUT_BSWAP_WORD
            for (b = 0; b < 8; b++) begin : FOUT_BSWAP_BYTE
                // This is a full big↔little endian conversion on each 64-bit lane.
                // The innermost loop iterates over bits within the byte, but since we're just reordering bytes, we can keep the bit order the same within each byte.
                // So we can directly assign the byte without needing to reorder bits within the byte.
                // This simplifies the code and avoids unnecessary complexity.
                assign out[w*64 + 8*b +: 8] =
                        f_out_raw[w*64 + 8*(7-b) +: 8];
            end
        end
    endgenerate

    // out_ready: latch high only when the *final* permutation completes.
    // Mirrors the reference design's shift-register gate on (state & f_ack):
    // f_out_ready pulses one cycle after the permutation finishes; we gate it
    // with 'state' so intermediate blocks (multi-block messages) are ignored.
    always_ff @(posedge clk) begin
        if (reset)
            out_ready <= 1'b0;
        else if (f_out_ready & state)
            out_ready <= 1'b1;
    end
    
    // Padder module instantiation
    // Note: The padder module needs to be ported to SystemVerilog separately
    // For now, this is a placeholder interface that matches the Verilog version
    padder padder_inst (
        .clk(clk),
        .reset(reset),
        .in(in_switch),
        .in_ready(in_ready),
        .is_last(is_last),
        .byte_num(byte_num),
        .variant(variant),
        .buffer_full(buffer_full),
        .out(padder_out_ld),
        .out_ready(padder_out_ready),
        .done(padder_done),
        .more_blks(padder_more_blks),
        .ack_more_blks(ack_more_blks),
        .f_ack(f_ack)
    );

    // Place rate into correct position for F-permutation input based on variant
    always_comb begin
        unique case(variant) 
            SHA3_224: f_in = padder_out_ld << 0; 
            SHA3_256: f_in = padder_out_ld << 64; 
            SHA3_384: f_in = padder_out_ld << 320; 
            SHA3_512: f_in = padder_out_ld << 576; 
        endcase 
    end
    
    // F-permutation (Keccak-f[1600]) instantiation
    f_permutation f_permutation_inst (
        .clk(clk),
        .reset(reset),
        .in(f_in),
        .in_ready(padder_out_ready),
        .more_blks(padder_more_blks),
        .ack_more_blks(ack_more_blks),
        .ack(f_ack),
        .out(f_out),
        .out_ready(f_out_ready)
    );

endmodule : keccak 
