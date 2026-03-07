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

module f_permutation (
    input  logic                    clk,
    input  logic                    reset,
    input  logic [MAX_RATE-1:0]     in,
    input  logic                    in_ready,
    input  logic                    more_blks,
    output logic                    ack_more_blks,
    output logic                    ack,
    output logic [STATE_WIDTH-1:0]  out,
    output logic                    out_ready
);

    logic [4:0]             rnd_idx;      // round index (0 to 23)
    logic [STATE_WIDTH-1:0] round_in;
    logic [STATE_WIDTH-1:0] round_out;
    logic [LANE_WIDTH-1:0]  rc;         // round constant
    logic                   update;
    logic                   accept;
    logic                   calc;       // == 1: calculating rounds
    logic                   done;       // == 1: completed all 24 rounds

    assign accept = in_ready & (~calc);
    assign done = (rnd_idx == 5'd23);

     // Acknowledge more blocks pending if in done state and ack_more_blks is not already set
    
    // Round index counter.
    // On the accept cycle rc[0] is applied (see rconst_lut instantiation below);
    // the register is advanced to 1 so that the first calc cycle uses rc[1].
    // If more blocks are coming after done, reset to 0 for next block.
    always_ff @(posedge clk) begin
        if (reset) begin
            rnd_idx <= '0;
        end else if (accept)
            rnd_idx <= 5'd1;  // rc[0] used this cycle; next cycle starts at rc[1]
        else if (calc & ~done)
            rnd_idx <= rnd_idx + 5'd1;
        else if (done & more_blks)
            rnd_idx <= 5'd0;  // Reset for next block
    end

    // Pulse acknowledgement only when a block has actually completed and
    // another block is pending.
    always_ff @(posedge clk) begin
        if (reset)
            ack_more_blks <= 1'b0;
        else
            ack_more_blks <= done & more_blks;
    end
    
    
    // Calculation control: active from accept until round 23 completes
    // Always reset calc on done; this creates an accept window if more blocks pending.
    always_ff @(posedge clk) begin
        if (reset)
            calc <= 1'b0;
        else if (accept)
            calc <= 1'b1;  // Start calculation
        else if (done)
            calc <= 1'b0;  // Always stop to allow next accept window
    end
    
    assign update = calc | accept;

    assign ack = accept;

    always_ff @(posedge clk) begin
        if (reset)
            out_ready <= 1'b0;
        else if (accept)
            out_ready <= 1'b0;
        else if (done & ~more_blks & ~in_ready)  // Only set ready when no more blocks pending
            out_ready <= 1'b1;
    end

    // XOR input with state — padder always drives the correct rate bits into in[MAX_RATE-1:0]
    assign round_in = accept ? {in ^ out[STATE_WIDTH-1:STATE_WIDTH-MAX_RATE], out[STATE_WIDTH-MAX_RATE-1:0]} : out;

    // Force round-constant index 0 during the accept cycle so the XOR+round-0
    // step always uses rc[0] regardless of the current rnd_idx register value.
    logic [4:0] rc_idx;
    assign rc_idx = accept ? 5'd0 : rnd_idx;

    rconst_lut rconst_lut_inst (
        .rnd_idx(rc_idx),
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
