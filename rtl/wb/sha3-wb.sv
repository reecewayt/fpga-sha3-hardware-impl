/*
    sha3-wb.sv - Wishbone interface for SHA3 (Keccak) IP Core

    Authors: Claude and Truong

    Description: This module creates a Wishbone peripheral that controls
    a SHA3 IP core. It implements input/output FIFOs to transport data from
    and to the user program. The module provides registers for control/status
    to initiate encryption requests with a selected SHA3 function.

    Address map (wb_adr_i[5:2] decodes the register):
      0x00  Control       R/W  START[0], ABORT[2], MODE[4:3]
      0x04  Status        R    IDLE[0], BUSY[1], DONE[2], IN_EMPTY[4], IN_FULL[5],
                                OUT_EMPTY[6], OUT_FULL[7], ERR_ILL[8], ERR_UF[9], ERR_OF[10]
      0x08  IN_FIFO_DATA  W    Push 32-bit word into input FIFO (stalls while full)
      0x0C  IN_FIFO_LEVEL R    Number of words in input FIFO
      0x10  OUT_FIFO_DATA R    Pop 32-bit word from output FIFO
      0x14  OUT_FIFO_LEVEL R   Number of words in output FIFO
      0x18  MSG_LEN_LO    R/W  Low 32 bits of message length (bytes)
      0x1C  MSG_LEN_HI    R/W  High 32 bits of message length (bytes)

    SHA3 Core Interface (maps to keccak.v ports):
      sha3_data_out  → in[31:0]
      sha3_num_bytes → byte_num[1:0]  (valid bytes when sha3_is_last=1; 0=none/padding)
      sha3_out_rdy   → in_ready
      sha3_is_last   → is_last
      sha3_buff_full ← buffer_full
      sha3_hash_in   ← out[511:0]
      sha3_hash_rdy  ← out_ready
      sha3_reset     → reset   (added port)

*/

module sha3_wb
#(
    parameter int dw         = 32,   // Wishbone data width (fixed 32-bit)
    parameter int aw         = 8,    // Wishbone address width
    parameter int FIFO_DEPTH = 64,   // Input FIFO depth (32-bit words)
    parameter int BUFF_SIZE  = 512   // Hash output width (bits)
)
(
    // ----------------------------------------------------------------
    // Wishbone interface
    // ----------------------------------------------------------------
    input  logic            wb_clk_i,
    input  logic            wb_rst_i,
    input  logic            wb_cyc_i,
    input  logic [aw-1:0]   wb_adr_i,
    input  logic [dw-1:0]   wb_dat_i,
    input  logic [3:0]      wb_sel_i,
    input  logic            wb_we_i,
    input  logic            wb_stb_i,
    output logic [dw-1:0]   wb_dat_o,
    output logic            wb_ack_o,
    output logic            wb_err_o,

    // ----------------------------------------------------------------
    // SHA3 / Keccak core interface
    // ----------------------------------------------------------------
    output logic [dw-1:0]           sha3_data_out,   // in[31:0]
    output logic [1:0]              sha3_num_bytes,  // byte_num
    output logic                    sha3_out_rdy,    // in_ready
    output logic                    sha3_is_last,    // is_last
    output logic                    sha3_reset,      // reset
    input  logic                    sha3_buff_full,  // buffer_full
    input  logic [BUFF_SIZE-1:0]    sha3_hash_in,    // out[511:0]
    input  logic                    sha3_hash_rdy    // out_ready
);

    // ================================================================
    // Local parameters
    // ================================================================
    localparam int IFW       = $clog2(FIFO_DEPTH) + 1; // input FIFO ptr width (extra MSB for full detect)
    localparam int OUT_DEPTH = 16;                      // max digest words (512-bit / 32)

    // Number of digest words per mode: SHA3-224=7, SHA3-256=8, SHA3-384=12, SHA3-512=16
    function automatic logic [4:0] digest_words(input logic [1:0] mode);
        case (mode)
            2'b00: digest_words = 5'd7;   // SHA3-224
            2'b01: digest_words = 5'd8;   // SHA3-256
            2'b10: digest_words = 5'd12;  // SHA3-384
            default: digest_words = 5'd16; // SHA3-512
        endcase
    endfunction

    // ================================================================
    // FSM state encoding
    // ================================================================
    typedef enum logic [1:0] {
        S_IDLE      = 2'b00,  // Waiting for START
        S_ABSORB    = 2'b01,  // Feeding data words to keccak core
        S_WAIT_HASH = 2'b10,  // Waiting for keccak to produce final digest
        S_DONE      = 2'b11   // Digest available in output FIFO
    } state_t;

    state_t state;

    // ================================================================
    // Control / config registers
    // ================================================================
    logic [31:0] ctrl_reg;    // 0x00
    logic [31:0] msg_len_lo;  // 0x18
    logic [31:0] msg_len_hi;  // 0x1C
    logic [63:0] msg_len;
    assign msg_len = {msg_len_hi, msg_len_lo};

    // Control field aliases
    wire ctrl_start = ctrl_reg[0];
    wire ctrl_abort = ctrl_reg[2];
    wire [1:0] ctrl_mode = ctrl_reg[4:3];

    // ================================================================
    // Error / status latches
    // ================================================================
    logic err_illegal_while_busy;
    logic err_fifo_underflow;
    logic err_fifo_overflow;

    // ================================================================
    // Input FIFO — backed by BRAM (Xilinx ram_style attribute).
    //   - wr_ptr and rd_ptr carry an extra MSB for full/empty detection
    //   - full  : ptrs differ in MSB but match in lower bits
    //   - empty : ptrs equal
    //
    // BRAM requires a registered (synchronous) read port, so we cannot
    // drive sha3_data_out combinatorially from the array.  Instead we
    // maintain a 1-entry prefetch register (in_fifo_rdata) and a valid
    // flag (in_head_valid).  The BRAM read address is set to in_rd_ptr_nxt
    // (one cycle ahead) so that the output register is always up-to-date
    // by the time the next word is needed — zero bubble FWFT behaviour.
    // ================================================================
    (* ram_style = "block" *) logic [31:0] in_fifo [0:FIFO_DEPTH-1];
    logic [IFW-1:0] in_wr_ptr, in_rd_ptr;

    wire in_fifo_empty = (in_wr_ptr == in_rd_ptr);
    wire in_fifo_full  = (in_wr_ptr[IFW-1] != in_rd_ptr[IFW-1]) &&
                         (in_wr_ptr[IFW-2:0] == in_rd_ptr[IFW-2:0]);
    wire [IFW-1:0] in_fifo_level_raw = in_wr_ptr - in_rd_ptr;

    // Registered prefetch buffer (BRAM output register)
    logic [31:0] in_fifo_rdata;  // current head word, captured from BRAM
    logic        in_head_valid;  // in_fifo_rdata is valid and ready to consume
    logic        in_data_available; // pipeline stage: FIFO has data (1 cycle before valid)

    // Pre-compute next-cycle pointer values so the BRAM read address
    // is issued one cycle ahead, absorbing the BRAM read latency.
    // in_pop  : a word will be consumed from the FIFO this clock
    // in_push : a word will be written into the FIFO this clock
    wire in_pop  = (state == S_ABSORB) && !sha3_buff_full && !final_pulse &&
                   in_head_valid && (bytes_remaining >= 1);
    wire in_push = wb_cyc_i && wb_stb_i && !wb_ack_o && wb_we_i &&
                   (wb_adr_i[5:2] == 4'h2) && !in_fifo_full;
    wire [IFW-1:0] in_rd_ptr_nxt = in_rd_ptr + {{(IFW-1){1'b0}}, in_pop};
    wire [IFW-1:0] in_wr_ptr_nxt = in_wr_ptr + {{(IFW-1){1'b0}}, in_push};
    wire           in_empty_nxt  = (in_rd_ptr_nxt == in_wr_ptr_nxt);

    // ================================================================
    // Output FIFO — implemented as registers (not BRAM).
    //   Only 16 words (512 bits), loaded all-at-once when digest is
    //   ready. The parallel write pattern (all 16 words simultaneously
    //   from sha3_hash_in[511:0]) prevents BRAM inference, but registers
    //   are more efficient for this small size anyway.
    //   - out_wr_cnt : number of words loaded (set when digest arrives)
    //   - out_rd_ptr : increments per WB read
    // ================================================================
    logic [31:0] out_fifo [0:OUT_DEPTH-1];
    logic [4:0]   out_wr_cnt;   // total loaded words (0..16)
    logic [4:0]   out_rd_ptr;   // next word to read

    wire out_fifo_empty = (out_rd_ptr >= out_wr_cnt);
    wire out_fifo_full  = (out_wr_cnt - out_rd_ptr == OUT_DEPTH[4:0]); // full if all slots occupied
    wire [4:0] out_fifo_level = out_wr_cnt - out_rd_ptr;

    // ================================================================
    // Byte ingestion counter & final-padding pulse logic
    //
    //  bytes_ingested counts bytes sent to the keccak core.
    //  final_pulse    is set when all message bytes have been absorbed
    //                 and msg_len is a multiple of 4 (so we need one
    //                 extra is_last / byte_num=0 clock of in_ready).
    // ================================================================
    logic [63:0] bytes_ingested;
    logic        final_pulse;        // pending flag: send is_last=1, byte_num=0

    wire [63:0] bytes_remaining = msg_len - bytes_ingested;

    // Is the current FIFO head the last data word?
    //   True when bytes_remaining is 1-3 (partial final word).
    //   bytes_remaining == 4 is a full word sent with is_last=0, followed by
    //   a zero-byte final_pulse on the next cycle.
    //   bytes_remaining == 0 is handled exclusively via final_pulse.
    wire is_last_data_word = !final_pulse &&
                             (bytes_remaining >= 64'd1) &&
                             (bytes_remaining <= 64'd3);

    // sha3_num_bytes: 0 = no data bytes (padding only), 1-3 = partial bytes.
    //   For is_last=0 the core ignores this field (all 4 bytes consumed).
    //   When bytes_remaining % 4 == 0 (and > 0) the word is sent with is_last=0,
    //   then a zero-byte final_pulse follows.
    wire [1:0] last_byte_num = msg_len[1:0]; // 0 → 0 bytes (full multiple), 1-3 → partial

    // ================================================================
    // BRAM synchronous read port for input FIFO.
    //   Simplified to pure synchronous read for clean BRAM inference.
    //   Read address is in_rd_ptr_nxt (pre-computed next pointer).
    // ================================================================
    always_ff @(posedge wb_clk_i) begin
        in_fifo_rdata <= in_fifo[in_rd_ptr_nxt[IFW-2:0]];
    end

    // ================================================================
    // SHA3 core combinational drive
    // ================================================================
    // Head word comes from the BRAM prefetch register, not the array directly
    assign sha3_data_out  = in_fifo_rdata;

    // byte_num: 0 during final_pulse (no data); last_byte_num when last data word
    assign sha3_num_bytes = final_pulse ? 2'b00 : last_byte_num;

    // in_ready: assert when BRAM prefetch holds a valid head AND there are
    //           bytes left, OR a final zero-byte padding pulse is pending
    assign sha3_out_rdy   = (state == S_ABSORB) && !sha3_buff_full &&
                            (in_head_valid && bytes_remaining >= 1 || final_pulse);

    // is_last: assert on last data word (held in prefetch reg) or on final_pulse
    assign sha3_is_last   = (state == S_ABSORB) && !sha3_buff_full &&
                            (is_last_data_word && in_head_valid || final_pulse);

    // Core reset: synchronous reset from system reset or SW ABORT bit
    assign sha3_reset     = wb_rst_i | ctrl_abort;

    // ================================================================
    // Main registered logic
    // ================================================================
    always_ff @(posedge wb_clk_i) begin
        if (wb_rst_i) begin
            state                  <= S_IDLE;
            ctrl_reg               <= '0;
            msg_len_lo             <= '0;
            msg_len_hi             <= '0;
            in_wr_ptr              <= '0;
            in_rd_ptr              <= '0;
            in_data_available      <= 1'b0;
            in_head_valid          <= 1'b0;
            out_wr_cnt             <= '0;
            out_rd_ptr             <= '0;
            bytes_ingested         <= '0;
            final_pulse            <= 1'b0;
            err_illegal_while_busy <= 1'b0;
            err_fifo_underflow     <= 1'b0;
            err_fifo_overflow      <= 1'b0;
            wb_ack_o               <= 1'b0;
            wb_dat_o               <= '0;
            wb_err_o               <= 1'b0;

        end else begin
            // --------------------------------------------------------
            // Default: de-assert single-cycle signals
            // --------------------------------------------------------
            wb_ack_o <= 1'b0;
            wb_err_o <= 1'b0;

            // Clear AUTO-CLEAR control bits (START, ABORT)
            ctrl_reg[0] <= 1'b0;  // START auto-clears
            ctrl_reg[2] <= 1'b0;  // ABORT auto-clears

            // --------------------------------------------------------
            // ABORT / RESET (SW controlled, highest priority)
            // --------------------------------------------------------
            if (ctrl_abort) begin
                state                  <= S_IDLE;
                in_wr_ptr              <= '0;
                in_rd_ptr              <= '0;
                in_data_available      <= 1'b0;
                in_head_valid          <= 1'b0;
                out_wr_cnt             <= '0;
                out_rd_ptr             <= '0;
                bytes_ingested         <= '0;
                final_pulse            <= 1'b0;
                err_illegal_while_busy <= 1'b0;
                err_fifo_underflow     <= 1'b0;
                err_fifo_overflow      <= 1'b0;

            end else begin
                // Pipeline BRAM prefetch validity to account for 1-cycle read latency:
                // After consuming a word (in_pop), clear in_data_available to insert
                // a bubble in the pipeline, giving BRAM time to fetch the next word.
                // Cycle N: in_pop=1, consume current word, in_rd_ptr advances
                // Cycle N+1: in_data_available=0 (bubble), BRAM reads new address
                // Cycle N+2: in_data_available=1 if FIFO has data
                // Cycle N+3: in_head_valid=1, new word ready in in_fifo_rdata
                if (in_pop) begin
                    in_data_available <= 1'b0;  // force pipeline bubble after pop
                    in_head_valid     <= 1'b0;
                end else begin
                    in_data_available <= !in_empty_nxt;
                    in_head_valid     <= in_data_available;
                end
                // ----------------------------------------------------
                // FSM
                // ----------------------------------------------------
                case (state)

                    // --------------------------------------------------
                    S_IDLE: begin
                        if (ctrl_start) begin
                            // Transition to absorption
                            state          <= S_ABSORB;
                            bytes_ingested <= '0;
                            out_wr_cnt     <= '0;
                            out_rd_ptr     <= '0;
                            // For an empty message (msg_len==0) fire the
                            // zero-byte is_last padding pulse immediately.
                            // For non-zero multiples of 4, final_pulse is set
                            // in S_ABSORB after the last full word is consumed.
                            final_pulse    <= (msg_len == '0);
                        end
                    end

                    // --------------------------------------------------
                    S_ABSORB: begin
                        // Keccak acceptance: core samples in/in_ready/is_last
                        // on the rising edge when buffer_full=0.
                        if (!sha3_buff_full) begin

                            if (final_pulse) begin
                                // Send the zero-byte is_last padding pulse.
                                // sha3_data_out is don't-care; byte_num=0 means
                                // "inject 0x01 padding immediately".
                                final_pulse <= 1'b0;
                                state       <= S_WAIT_HASH;

                            end else if (in_head_valid && bytes_remaining >= 1) begin
                                // Pop next data word — keccak has accepted in_fifo_rdata
                                // (driven by sha3_data_out).  Advance rd_ptr; BRAM will
                                // deliver in_fifo[new ptr] into in_fifo_rdata next cycle.
                                in_rd_ptr <= in_rd_ptr + 1'b1;

                                if (is_last_data_word) begin
                                    // Partial final word (1-3 valid bytes).
                                    // is_last=1 was already driven combinationally;
                                    // transition to waiting for the digest.
                                    bytes_ingested <= msg_len;
                                    state          <= S_WAIT_HASH;

                                end else begin
                                    // Full 4-byte word sent with is_last=0.
                                    bytes_ingested <= bytes_ingested + 64'd4;

                                    // If this was the last full word of a
                                    // multiple-of-4 message, arm the zero-byte
                                    // final padding pulse for the next cycle.
                                    if ((bytes_ingested + 64'd4) == msg_len) begin
                                        final_pulse <= 1'b1;
                                    end
                                end
                            end
                        end
                    end

                    // --------------------------------------------------
                    S_WAIT_HASH: begin
                        if (sha3_hash_rdy) begin
                            // Load digest words into output FIFO.
                            // sha3_hash_in[511:480] = word 0 (most-significant).
                            for (int i = 0; i < OUT_DEPTH; i++) begin
                                out_fifo[i] <= sha3_hash_in[511 - 32*i -: 32];
                            end
                            // Only expose the words relevant to the selected mode.
                            out_wr_cnt <= digest_words(ctrl_mode);
                            out_rd_ptr <= '0;
                            state      <= S_DONE;
                        end
                    end

                    // --------------------------------------------------
                    S_DONE: begin
                        // Auto-return to IDLE once firmware has drained the FIFO
                        if (out_fifo_empty) begin
                            state <= S_IDLE;
                        end
                    end

                endcase

                // --------------------------------------------------------
                // Wishbone transaction handler
                //   Uses classic single-cycle ACK.
                //   IN_FIFO_DATA writes stall (hold ACK low) while FIFO full.
                // --------------------------------------------------------
                if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin

                    if (wb_we_i) begin
                        // -------- Write transactions --------
                        case (wb_adr_i[5:2])

                            4'h0: begin // Control (0x00)
                                // Mode change while BUSY is illegal
                                if ((state == S_ABSORB || state == S_WAIT_HASH) &&
                                    (wb_dat_i[4:3] != ctrl_reg[4:3])) begin
                                    err_illegal_while_busy <= 1'b1;
                                end else begin
                                    ctrl_reg <= wb_dat_i;
                                end
                                wb_ack_o <= 1'b1;
                            end

                            4'h2: begin // IN_FIFO_DATA (0x08)
                                if (in_fifo_full) begin
                                    // Stall: withhold ACK until FIFO has space.
                                    // Set err_fifo_overflow every cycle a write is
                                    // attempted while full so firmware can detect
                                    // backpressure without counting pushes in SW.
                                    err_fifo_overflow <= 1'b1;
                                    wb_ack_o          <= 1'b0;
                                end else begin
                                    in_fifo[in_wr_ptr[IFW-2:0]] <= wb_dat_i;
                                    in_wr_ptr                   <= in_wr_ptr + 1'b1;
                                    wb_ack_o                    <= 1'b1;
                                end
                            end

                            4'h6: begin // MSG_LEN_LO (0x18)
                                if (state == S_IDLE) msg_len_lo <= wb_dat_i;
                                else err_illegal_while_busy <= 1'b1;
                                wb_ack_o <= 1'b1;
                            end

                            4'h7: begin // MSG_LEN_HI (0x1C)
                                if (state == S_IDLE) msg_len_hi <= wb_dat_i;
                                else err_illegal_while_busy <= 1'b1;
                                wb_ack_o <= 1'b1;
                            end

                            default: wb_ack_o <= 1'b1; // writes to R-only regs: silently ACK

                        endcase

                    end else begin
                        // -------- Read transactions --------
                        case (wb_adr_i[5:2])

                            4'h0: begin // Control (0x00)
                                wb_dat_o <= ctrl_reg;
                                wb_ack_o <= 1'b1;
                            end

                            4'h1: begin // Status (0x04)
                                wb_dat_o <= {
                                    21'b0,
                                    err_fifo_overflow,             // [10]
                                    err_fifo_underflow,            // [9]
                                    err_illegal_while_busy,        // [8]
                                    out_fifo_full,                 // [7]
                                    out_fifo_empty,                // [6]
                                    in_fifo_full,                  // [5]
                                    in_fifo_empty,                 // [4]
                                    1'b0,                          // [3] reserved (ERROR summary)
                                    (state == S_DONE),             // [2] DONE
                                    (state == S_ABSORB ||
                                     state == S_WAIT_HASH),        // [1] BUSY
                                    (state == S_IDLE)              // [0] IDLE
                                };
                                wb_ack_o <= 1'b1;
                            end

                            4'h2: begin // IN_FIFO_DATA (0x08) - write-only
                                wb_dat_o <= '0;
                                wb_ack_o <= 1'b1;
                            end

                            4'h3: begin // IN_FIFO_LEVEL (0x0C)
                                wb_dat_o <= {{(32-IFW){1'b0}}, in_fifo_level_raw};
                                wb_ack_o <= 1'b1;
                            end

                            4'h4: begin // OUT_FIFO_DATA (0x10)
                                if (out_fifo_empty) begin
                                    err_fifo_underflow <= 1'b1;
                                    wb_dat_o           <= '0;
                                end else begin
                                    wb_dat_o   <= out_fifo[out_rd_ptr[3:0]];
                                    out_rd_ptr <= out_rd_ptr + 1'b1;
                                end
                                wb_ack_o <= 1'b1;
                            end

                            4'h5: begin // OUT_FIFO_LEVEL (0x14)
                                wb_dat_o <= {27'b0, out_fifo_level};
                                wb_ack_o <= 1'b1;
                            end

                            4'h6: begin // MSG_LEN_LO (0x18)
                                wb_dat_o <= msg_len_lo;
                                wb_ack_o <= 1'b1;
                            end

                            4'h7: begin // MSG_LEN_HI (0x1C)
                                wb_dat_o <= msg_len_hi;
                                wb_ack_o <= 1'b1;
                            end

                            default: begin
                                wb_dat_o <= '0;
                                wb_ack_o <= 1'b1;
                            end

                        endcase
                    end
                end // wishbone transaction

            end // !ctrl_abort
        end // !wb_rst_i
    end // always_ff

endmodule





