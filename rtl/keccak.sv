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
    output logic [MAX_RATE-1:0]     out,            // Max output size (for SHA3-224)
    output logic                    out_ready
);

    // State machine
    logic                state;     // state == 0: user will send more input data
                                    // state == 1: user will not send any data
    
    // Padder outputs
    logic [MAX_RATE-1:0]     padder_out;
    logic                    padder_out_ready;
    
    // F-permutation interface
    logic                    f_ack;
    logic [STATE_WIDTH-1:0]  f_out;
    logic                    f_out_ready;
    
    // Output extraction - take the appropriate number of bits based on variant
    // SHA3-224 -> 224 bits, SHA3-256 -> 256 bits, SHA3-384 -> 384 bits, SHA3-512 -> 512 bits
    always_comb begin
        case (variant)
            SHA3_224: out = f_out[STATE_WIDTH-1:STATE_WIDTH-224];
            SHA3_256: out = f_out[STATE_WIDTH-1:STATE_WIDTH-256];
            SHA3_384: out = f_out[STATE_WIDTH-1:STATE_WIDTH-384];
            SHA3_512: out = f_out[STATE_WIDTH-1:STATE_WIDTH-512];
            default:  out = f_out[STATE_WIDTH-1:STATE_WIDTH-512];
        endcase
    end
    
    // State tracking
    always_ff @(posedge clk) begin
        if (reset)
            state <= 1'b0;
        else if (is_last)
            state <= 1'b1;
    end
    
    // Padder module instantiation
    // Note: The padder module needs to be ported to SystemVerilog separately
    // For now, this is a placeholder interface that matches the Verilog version
    padder padder_inst (
        .clk(clk),
        .reset(reset),
        .in(in),
        .in_ready(in_ready),
        .is_last(is_last),
        .byte_num(byte_num),
        .variant(variant),
        .buffer_full(buffer_full),
        .out(padder_out),
        .out_ready(padder_out_ready),
        .f_ack(f_ack)
    );
    
    // F-permutation (Keccak-f[1600]) instantiation
    f_permutation f_permutation_inst (
        .clk(clk),
        .reset(reset),
        .in(padder_out),
        .in_ready(padder_out_ready),
        .variant(variant),
        .ack(f_ack),
        .out(f_out),
        .out_ready(out_ready)
    );

endmodule : keccak
