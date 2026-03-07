/*
    sha3_wb_top.sv - Top-level wrapper connecting Wishbone SHA3 peripheral
                     to the keccak core.

    This module exposes only the Wishbone bus externally and wires the
    internal WB-side SHA3 streaming interface directly into keccak.
*/

import sha3_pkg::*;

module sha3_wb_top
#(
    parameter int DW         = 32,
    parameter int AW         = 8,
    parameter int FIFO_DEPTH = 64,
    parameter int BUFF_SIZE  = 512
)
(
    input  logic            wb_clk_i,
    input  logic            wb_rst_i,
    input  logic            wb_cyc_i,
    input  logic [AW-1:0]   wb_adr_i,
    input  logic [DW-1:0]   wb_dat_i,
    input  logic [3:0]      wb_sel_i,
    input  logic            wb_we_i,
    input  logic            wb_stb_i,
    output logic [DW-1:0]   wb_dat_o,
    output logic            wb_ack_o,
    output logic            wb_err_o
);

    // WB -> keccak stream/control
    logic [63:0]         sha3_data_out;
    logic [2:0]          sha3_num_bytes;
    sha3_variant_t       sha3_variant;
    logic                sha3_out_rdy;
    logic                sha3_is_last;
    logic                sha3_reset;

    // keccak -> WB digest/status
    logic                sha3_buff_full;
    logic [BUFF_SIZE-1:0] sha3_hash_in;
    logic                sha3_hash_rdy;

    sha3_wb #(
        .dw(DW),
        .aw(AW),
        .FIFO_DEPTH(FIFO_DEPTH),
        .BUFF_SIZE(BUFF_SIZE)
    ) u_sha3_wb (
        .wb_clk_i(wb_clk_i),
        .wb_rst_i(wb_rst_i),
        .wb_cyc_i(wb_cyc_i),
        .wb_adr_i(wb_adr_i),
        .wb_dat_i(wb_dat_i),
        .wb_sel_i(wb_sel_i),
        .wb_we_i(wb_we_i),
        .wb_stb_i(wb_stb_i),
        .wb_dat_o(wb_dat_o),
        .wb_ack_o(wb_ack_o),
        .wb_err_o(wb_err_o),
        .sha3_data_out(sha3_data_out),
        .sha3_num_bytes(sha3_num_bytes),
        .sha3_variant(sha3_variant),
        .sha3_out_rdy(sha3_out_rdy),
        .sha3_is_last(sha3_is_last),
        .sha3_reset(sha3_reset),
        .sha3_buff_full(sha3_buff_full),
        .sha3_hash_in(sha3_hash_in),
        .sha3_hash_rdy(sha3_hash_rdy)
    );

    keccak u_keccak (
        .clk(wb_clk_i),
        .reset(sha3_reset),
        .in(sha3_data_out),
        .in_ready(sha3_out_rdy),
        .is_last(sha3_is_last),
        .byte_num(sha3_num_bytes),
        .variant(sha3_variant),
        .buffer_full(sha3_buff_full),
        .out(sha3_hash_in),
        .out_ready(sha3_hash_rdy)
    );

endmodule
