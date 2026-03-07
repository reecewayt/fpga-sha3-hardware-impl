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

    SHA3 Core Interface (maps to keccak.sv ports):
      sha3_data_out  → in[63:0]        (64-bit, paired from two 32-bit WB writes)
      sha3_num_bytes → byte_num[2:0]   (0-7 valid bytes, 0 = 8 bytes full word)
      sha3_variant   → variant[1:0]    (SHA3 variant from MODE bits)
      sha3_out_rdy   → in_ready
      sha3_is_last   → is_last
      sha3_buff_full ← buffer_full
      sha3_hash_in   ← out[511:0]
      sha3_hash_rdy  ← out_ready
      sha3_reset     → reset

    Word Pairing: WB is 32-bit but keccak expects 64-bit words. The module
    internally buffers consecutive 32-bit writes and combines them into 64-bit
    words before sending to the keccak core (little-endian: first write = low 32).

*/

import sha3_pkg::*;

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
    output logic [63:0]             sha3_data_out,   // in[63:0] - paired 32-bit words
    output logic [2:0]              sha3_num_bytes,  // byte_num[2:0] (0-7, 0=8 bytes)
    output sha3_variant_t           sha3_variant,    // variant selection
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

    // Variant mapping (MODE bits directly map to sha3_variant_t encoding)
    assign sha3_variant = sha3_variant_t'(ctrl_mode);

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
    // in_pop  : a 32-bit word will be consumed from the FIFO this clock
    // in_push : a word will be written into the FIFO this clock
    // Pop policy:
    //   - phase 0 (word_phase=0): consume first 32-bit word whenever available
    //   - phase 1 (word_phase=1): consume second 32-bit word only when >=5 bytes remain
    //     (for 1-4 byte tail we send buffered low word + zero high word, no second pop)
    wire in_pop  = (state == S_ABSORB) && !sha3_buff_full && !final_pulse &&
                   (((word_phase == 1'b0) && in_head_valid && (bytes_remaining >= 1)) ||
                    ((word_phase == 1'b1) && in_head_valid && (bytes_remaining >= 5)));
    wire in_push = wb_cyc_i && wb_stb_i && !wb_ack_o && wb_we_i &&
                   (wb_adr_i[5:2] == 4'h2) && !in_fifo_full;
    wire [IFW-1:0] in_rd_ptr_nxt = in_rd_ptr + {{(IFW-1){1'b0}}, in_pop};
    wire [IFW-1:0] in_wr_ptr_nxt = in_wr_ptr + {{(IFW-1){1'b0}}, in_push};
    wire           in_empty_nxt  = (in_rd_ptr_nxt == in_wr_ptr_nxt);

    // ================================================================
    // Word-pairing buffer: combine two 32-bit words into 64-bit
    //   word_phase = 0: waiting for/buffering high 32 bits (first word)
    //   word_phase = 1: have buffered word, current FIFO head is low 32 bits
    //   sha3_data_out = {word_buffer[31:0], in_fifo_rdata[31:0]}
    // ================================================================
    logic [31:0] word_buffer;  // holds first word (high 32 bits)
    logic        word_phase;   // 0=need high word, 1=low word ready

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
    //  bytes_ingested counts bytes sent to the keccak core (in 8-byte increments).
    //  final_pulse    is set when all message bytes have been absorbed
    //                 and msg_len is a multiple of 8 (so we need one
    //                 extra is_last / byte_num=0 clock of in_ready).
    // ================================================================
    logic [63:0] bytes_ingested;
    logic        final_pulse;        // pending flag: send is_last=1, byte_num=0

    wire [63:0] bytes_remaining = msg_len - bytes_ingested;

    // Is the current 64-bit word pair the last data word?
    //   True when bytes_remaining is 1-7 (partial final word).
    //   bytes_remaining == 8 is a full 64-bit word sent with is_last=0, followed by
    //   a zero-byte final_pulse on the next cycle.
    //   bytes_remaining == 0 is handled exclusively via final_pulse.
    wire is_last_data_word = !final_pulse &&
                             (bytes_remaining >= 64'd1) &&
                             (bytes_remaining <= 64'd7);

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
    // Paired 64-bit word: {buffered first word (high 32), current FIFO head (low 32)}
    // When no second word is available (bytes_remaining <= 4), use zeros for low 32
    wire [31:0] low_word = (in_head_valid && bytes_remaining >= 5) ? in_fifo_rdata[31:0] : 32'h0;
    assign sha3_data_out  = {word_buffer[31:0], low_word};

    // byte_num: 0-7 valid bytes (0 = 8 bytes = full 64-bit word)
    //   - final_pulse: 0 bytes (padding only)
    //   - last data: bytes_remaining % 8 (1-7 for partial, 0 for full final word)
    wire [2:0] last_byte_num = msg_len[2:0];  // 0-7: 0 = 8 bytes, 1-7 = partial
    assign sha3_num_bytes = final_pulse ? 3'b000 : last_byte_num;

    // in_ready:
    //   - phase 1, bytes 5..N: require second word valid in prefetch register
    //   - phase 1, bytes 1..4: send buffered low word + zero high word
    //   - final_pulse: zero-byte final padding beat
    assign sha3_out_rdy   = (state == S_ABSORB) && !sha3_buff_full &&
                            ((word_phase && (bytes_remaining >= 5) && in_head_valid) ||
                             (word_phase && (bytes_remaining >= 1) && (bytes_remaining <= 4)) ||
                             final_pulse);

        // is_last: assert on final partial data word or final zero-byte pulse
    assign sha3_is_last   = (state == S_ABSORB) && !sha3_buff_full &&
                                                        ((is_last_data_word && word_phase &&
                                                            ((bytes_remaining <= 4) || in_head_valid)) ||
                                                         final_pulse);

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
            word_buffer            <= '0;
            word_phase             <= 1'b0;
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
                word_buffer            <= '0;
                word_phase             <= 1'b0;
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
                    in_rd_ptr         <= in_rd_ptr + 1'b1;
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
                            word_phase     <= 1'b0;  // start with low word
                            out_wr_cnt     <= '0;
                            out_rd_ptr     <= '0;
                            // For an empty message (msg_len==0) fire the
                            // zero-byte is_last padding pulse immediately.
                            // For non-zero multiples of 8, final_pulse is set
                            // in S_ABSORB after the last full 64-bit word is consumed.
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

                            end else if (word_phase == 1'b0) begin
                                // Phase 0: buffer the high 32 bits (first word of pair)
                                if (in_head_valid && bytes_remaining >= 1) begin
                                    word_buffer <= in_fifo_rdata;
                                    word_phase  <= 1'b1;
                                    // in_rd_ptr advances automatically via in_pop signal
                                end

                            end else begin  // word_phase == 1'b1
                                // Phase 1: combine buffered high + current low → 64-bit output
                                // Check if we have enough bytes left for a low word
                                // or if this is the final partial 64-bit word (1-4 bytes)
                                if (in_head_valid && bytes_remaining >= 5) begin
                                    // Have at least 5 bytes left: normal 64-bit word with low FIFO word
                                    // (word_buffer holds high 32, in_fifo_rdata holds low 32)
                                    // in_rd_ptr advances automatically via in_pop signal
                                    word_phase <= 1'b0;  // reset for next pair

                                    if (is_last_data_word) begin
                                        // Partial final 64-bit word (5-7 valid bytes total)
                                        bytes_ingested <= msg_len;
                                        state          <= S_WAIT_HASH;
                                    end else begin
                                        // Full 8-byte word sent with is_last=0
                                        bytes_ingested <= bytes_ingested + 64'd8;

                                        // If this was the last full 64-bit word (msg_len % 8 == 0),
                                        // arm the zero-byte final padding pulse for next cycle
                                        if ((bytes_ingested + 64'd8) == msg_len) begin
                                            final_pulse <= 1'b1;
                                        end
                                    end

                                end else if (bytes_remaining >= 1 && bytes_remaining <= 4) begin
                                    // Have 1-4 bytes left: buffered high word has valid data
                                    // Low 32 bits are zeros (no second word from FIFO)
                                    // Send buffered high word + zeros as a partial 64-bit word with is_last=1
                                    word_phase     <= 1'b0;
                                    bytes_ingested <= msg_len;
                                    state          <= S_WAIT_HASH;
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





