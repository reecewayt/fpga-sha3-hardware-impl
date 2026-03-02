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

/* "is_last" == 0 means byte number is 8, no matter what value "byte_num" is. */
/* if "in_ready" == 0, then "is_last" should be 0. */
/* the user switch to next "in" only if "ack" == 1. */

import sha3_pkg::*;

module keccak (
    input  logic                    clk,
    input  logic                    reset,
    input  logic [31:0]             in,
    input  logic                    in_ready,
    input  logic                    is_last,
    input  logic [1:0]              byte_num,
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
    logic [MAX_RATE-1:0]    padder_out_raw;  // before reorder bytes

    logic                    padder_out_ready;
    
    // F-permutation interface
    logic                    f_ack;
    logic [STATE_WIDTH-1:0]  f_out;
    logic                    f_out_ready;
    logic [511:0]     f_out_raw;        // before reorder bytes
                                        // need to reorganize bits in each byte for 
                                        // how computer usually represents data (msb in high position) vs how keccak expects it (msb in low position)

    // Bit mirror logic
    logic [31:0]  in_switch;

    genvar w, b, bb; 
    
    assign f_out_raw = f_out[1599:1599-511]; // Only the first 512 bits are relevant for output, depending on the variant
    
    // State tracking
    always_ff @(posedge clk) begin
        if (reset)
            state <= 1'b0;
        else if (is_last)
            state <= 1'b1;
    end


    // Bit mirror each byte at the input of the padder
    generate
        for (b = 0; b < 4; b++) begin : IN_BSWAP_BYTE
             for (bb = 0; bb < 8; bb++) begin : BYTE_BIT
                // This is a full big↔little endian conversion on each 32-bit input word.
                // The innermost loop iterates over bits within the byte, but since we're just reordering bytes, we can keep the bit order the same within each byte.
                // So we can directly assign the byte without needing to reorder bits within the byte.
                // This simplifies the code and avoids unnecessary complexity.
                assign in_switch[8*b + bb] =
                       in[8*b + (7-bb)];
            end
        end
    endgenerate

    // Reverse the bit mirror for each byte to return to hexadecimal representation in the output
    generate
        for (w = 0; w < 8; w++) begin : FOUT_BSWAP_WORD
            for (b = 0; b < 8; b++) begin : FOUT_BSWAP_BYTE
                for (bb = 0; bb < 8; bb++) begin : BYTE_BIT
                    // This is a full big↔little endian conversion on each 64-bit lane.
                    // The innermost loop iterates over bits within the byte, but since we're just reordering bytes, we can keep the bit order the same within each byte.
                    // So we can directly assign the byte without needing to reorder bits within the byte.
                    // This simplifies the code and avoids unnecessary complexity.
                    assign out[w*64 + 8*b + bb] =
                           f_out_raw[w*64 + 8*b + (7-bb)];
                end
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
        .f_ack(f_ack)
    );
    
    // F-permutation (Keccak-f[1600]) instantiation
    f_permutation f_permutation_inst (
        .clk(clk),
        .reset(reset),
        .in(padder_out_ld),
        .in_ready(padder_out_ready),
        .ack(f_ack),
        .out(f_out),
        .out_ready(f_out_ready)
    );

endmodule : keccak
