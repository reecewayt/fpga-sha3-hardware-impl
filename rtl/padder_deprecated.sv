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

/* "is_last" == 0 means byte number is 4, no matter what value "byte_num" is. */
/* if "in_ready" == 0, then "is_last" should be 0. */
/* the user switch to next "in" only if "ack" == 1. */

import sha3_pkg::*;

module padder (
    input  logic                clk,
    input  logic                reset,
    input  logic [31:0]         in,
    input  logic                in_ready,
    input  logic                is_last,
    input  logic [1:0]          byte_num,
    input  sha3_variant_t       variant,        // SHA-3 variant selection
    output logic                buffer_full,    // to "user" module
    output logic [MAX_RATE-1:0] out,            // to "f_permutation" module
    output logic                out_ready,      // to "f_permutation" module
    input  logic                f_ack           // from "f_permutation" module
);

    // State machine
    logic                state;       // state == 0: user will send more input data
                                      // state == 1: user will not send any data
    logic                done;        // == 1: out_ready should be 0
    
    // Buffer length counter - needs to count up to MAX_RATE/32 = 36 words
    logic [5:0]          i;           // 6 bits to count 0-36
    
    // Helper signals
    logic [31:0]         v0;          // output of module "padder1"
    logic [31:0]         v1;          // to be shifted into register "out"
    logic                accept;      // accept user input?
    logic                update;      // update the buffer?
    
    // Calculate required buffer length based on variant
    logic [5:0]          buffer_words; // number of 32-bit words needed

    
    
    always_comb begin
        case (variant)
            SHA3_224: buffer_words = 6'd36;  // 1152 bits / 32 = 36 words
            SHA3_256: buffer_words = 6'd34;  // 1088 bits / 32 = 34 words
            SHA3_384: buffer_words = 6'd26;  // 832 bits / 32 = 26 words
            SHA3_512: buffer_words = 6'd18;  // 576 bits / 32 = 18 words
            default:  buffer_words = 6'd18;
        endcase
    end
    
    // Buffer full when we've accumulated enough words for the current variant
    assign buffer_full = (i >= buffer_words);
    assign out_ready = buffer_full;
    
    // Accept user input if not in final state, input is ready, and buffer not full
    assign accept = (~state) & in_ready & (~buffer_full);
    
    // Update buffer if accepting input or filling padding in state 1
    assign update = (accept | (state & (~buffer_full))) & (~done);

    // Shift register for output buffer
    always_ff @(posedge clk) begin
        if (reset)
            out <= '0;
        else if (update)
            out <= {out[MAX_RATE-1-32:0], v1};
    end

    // Buffer length counter
    always_ff @(posedge clk) begin
        if (reset)
            i <= 6'd0;
        else if (f_ack)
            i <= 6'd0;
        else if (update)
            i <= i + 6'd1;
    end

    // State machine: transition to state 1 when last input is received
    always_ff @(posedge clk) begin
        if (reset)
            state <= 1'b0;
        else if (is_last)
            state <= 1'b1;
    end

    // Done flag: set when buffer is full in state 1
    always_ff @(posedge clk) begin
        if (reset)
            done <= 1'b0;
        else if (state & out_ready)
            done <= 1'b1;
    end

    // Padder1 submodule: handles partial byte padding
    padder1 padder1_inst (
        .in(in),
        .byte_num(byte_num),
        .out(v0)
    );
    
    // Select data to shift into buffer
    always_comb begin
        if (state) begin
            // In padding state: add final padding bit
            v1 = 32'h0;
            v1[7] = (i == buffer_words - 6'd1); // Set MSB of last byte when at last word
        end else if (is_last == 1'b0) begin
            // Normal data word (4 full bytes)
            v1 = in;
        end else begin
            // Last word with partial bytes
            v1 = v0;
            v1[7] = (i == buffer_words - 6'd1); // Set MSB if this is the last word
        end
    end

endmodule : padder


// Submodule: padder1
// Adds the 0x06 padding byte at the appropriate position based on byte_num
/*
 *     in      byte_num     out
 * 0x11223344      0    0x06000000  (0 valid bytes, pad at byte 0)
 * 0x11223344      1    0x11060000  (1 valid byte, pad at byte 1)
 * 0x11223344      2    0x11220600  (2 valid bytes, pad at byte 2)
 * 0x11223344      3    0x11223306  (3 valid bytes, pad at byte 3)
 */
module padder1 (
    input  logic [31:0] in,
    input  logic [1:0]  byte_num,
    output logic [31:0] out
);

    always_comb begin
        case (byte_num)
            2'b00: out = 32'h06000000;
            2'b01: out = {in[31:24], 24'h060000};
            2'b10: out = {in[31:16], 16'h0600};
            2'b11: out = {in[31:8],   8'h06};
        endcase
    end

endmodule : padder1
