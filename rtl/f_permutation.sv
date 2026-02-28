/*
 * Copyright 2013, Homer Hsing <homer.hsing@gmail.com>
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

/* if "ack" is 1, then current input has been used. */

import sha3_pkg::*;

// Note: This module is fully programmable - the variant is selected at runtime via variant input.
// The controller must hold the variant steady during the entire permutation.

module f_permutation (
    input  logic                    clk,
    input  logic                    reset,
    input  logic [MAX_RATE-1:0]     in,
    input  logic                    in_ready,
    input  sha3_variant_t           variant,     // SHA-3 variant: 224/256/384/512
    output logic                    ack,
    output logic [STATE_WIDTH-1:0]  out,
    output logic                    out_ready
);

    logic [4:0]            rnd_idx;      // round index (0 to 23)
    logic [STATE_WIDTH-1:0] round_in;
    logic [STATE_WIDTH-1:0] round_out;
    logic [LANE_WIDTH-1:0]  rc;         // round constant
    logic                   update;
    logic                   accept;
    logic                   calc;       // == 1: calculating rounds
    logic                   done;       // == 1: completed all 24 rounds

    assign accept = in_ready & (~calc);
    assign done = (rnd_idx == 5'd23);
    
    // Round index counter: counts from 0 to 23 during calculation
    always_ff @(posedge clk) begin
        if (reset)
            rnd_idx <= '0;
        else if (accept)
            rnd_idx <= '0;  // Start at round 0 when accepting new input
        else if (calc)
            rnd_idx <= rnd_idx + 5'd1;
    end
    
    // Calculation control: active from accept until round 23 completes
    always_ff @(posedge clk) begin
        if (reset)
            calc <= 1'b0;
        else if (accept)
            calc <= 1'b1;  // Start calculation
        else if (done)
            calc <= 1'b0;  // Stop after round 23
    end
    
    assign update = calc | accept;

    assign ack = accept;

    always_ff @(posedge clk) begin
        if (reset)
            out_ready <= 1'b0;
        else if (accept)
            out_ready <= 1'b0;
        else if (done) // Set ready after completing round 23
            out_ready <= 1'b1;
    end

    // XOR input with state (programmable rate support)
    logic [STATE_WIDTH-1:0] round_in_xor;

    always_comb begin 
        unique case(variant)
            SHA3_224: round_in_xor = {in ^ out[STATE_WIDTH-1:STATE_WIDTH-RATE_SHA3_224], out[STATE_WIDTH-RATE_SHA3_224-1:0]};
            SHA3_256: round_in_xor = {in ^ out[STATE_WIDTH-1:STATE_WIDTH-RATE_SHA3_256], out[STATE_WIDTH-RATE_SHA3_256-1:0]};
            SHA3_384: round_in_xor = {in ^ out[STATE_WIDTH-1:STATE_WIDTH-RATE_SHA3_384], out[STATE_WIDTH-RATE_SHA3_384-1:0]};
            SHA3_512: round_in_xor = {in ^ out[STATE_WIDTH-1:STATE_WIDTH-RATE_SHA3_512], out[STATE_WIDTH-RATE_SHA3_512-1:0]};
            default:  round_in_xor = {in ^ out[STATE_WIDTH-1:STATE_WIDTH-RATE_SHA3_512], out[STATE_WIDTH-RATE_SHA3_512-1:0]};
        endcase
    end
    
    assign round_in = accept ? round_in_xor : out;

    rconst_lut rconst_lut_inst (
        .rnd_idx(rnd_idx),
        .rc_out(rc)
    );

    round round_inst (
        .in(round_in),
        .round_const(rc),
        .out(round_out)
    );

    always_ff @(posedge clk) begin
        if (reset)
            out <= '0;
        else if (update)
            out <= round_out;
    end

endmodule : f_permutation
