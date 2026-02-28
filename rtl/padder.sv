/*
 * SHA-3 Padder Module
 *
 * Accumulates 32-bit input words into a rate-sized buffer and applies
 * SHA-3 pad10*1 padding with domain suffix 0x06.
 *
 * byte_num encoding (valid bytes in last word when is_last asserted with in_ready):
 *   2'b00 = 1 valid byte  → 0x06 inserted at bits[23:16]
 *   2'b01 = 2 valid bytes → 0x06 inserted at bits[15:8]
 *   2'b10 = 3 valid bytes → 0x06 inserted at bits[7:0]
 *   2'b11 = 4 valid bytes → 0x06 must start in the next word (overflow if buffer full)
 *
 * Word packing: buffer[0] is the first rate word, mapped to out[31:0].
 * Bytes are big-endian within each 32-bit word (byte 0 → bits[31:24]).
 *
 * Empty message: assert is_last=1 with in_ready=0 (no data word).
 *
 * Overflow: if byte_num=3 and write_ptr == rate_words-1, the padder first
 * outputs the full data block, then after f_ack outputs a padding-only block
 * { 0x06000000, 0x00..., 0x00000080 }.
 */

import sha3_pkg::*;

module padder (
    input  logic                clk,
    input  logic                reset,

    // User interface
    input  logic [31:0]         in,
    input  logic                in_ready,
    input  logic                is_last,     // Last word flag (pulse; or empty-message end)
    input  logic [1:0]          byte_num,    // Valid bytes in last word minus 1
    input  sha3_variant_t       variant,
    output logic                buffer_full, // Back-pressure to user

    // F-permutation interface
    output logic [MAX_RATE-1:0] out,
    output logic                out_ready,   // Rate block ready for f-permutation
    input  logic                f_ack        // F-permutation consumed the block
);

    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    localparam int MAX_WORDS = MAX_RATE / 32;  // 36 words maximum

    // -----------------------------------------------------------------------
    // Rate-word count (combinational from variant)
    // MAX_WORDS=36: valid indices 0-35 → 6-bit index; rate_words ≤ 36 fits in 6 bits.
    // -----------------------------------------------------------------------
    logic [5:0] rate_words;

    always_comb begin
        case (variant)
            SHA3_224: rate_words = 6'd36;
            SHA3_256: rate_words = 6'd34;
            SHA3_384: rate_words = 6'd26;
            SHA3_512: rate_words = 6'd18;
            default:  rate_words = 6'd18;
        endcase
    end

    // -----------------------------------------------------------------------
    // FSM
    // -----------------------------------------------------------------------
    typedef enum logic [1:0] {
        S_FILL  = 2'b00,  // Accepting input data
        S_READY = 2'b01,  // Padded block ready; out_ready=1
        S_DONE  = 2'b10   // Complete; idle until reset
    } state_t;

    state_t      state;
    logic [31:0] buffer [0:MAX_WORDS-1];
    logic [5:0]  write_ptr;        // Next buffer slot to fill (index 0 = first rate word)
    logic        overflow_pending; // Extra padding block needed after first f_ack

    // -----------------------------------------------------------------------
    // Combinational outputs
    // -----------------------------------------------------------------------
    assign buffer_full = (state == S_FILL) && (write_ptr >= rate_words);
    assign out_ready   = (state == S_READY);

    logic accept;
    assign accept = (state == S_FILL) && in_ready && !buffer_full;

    // Output packing: out[i*32 +: 32] = buffer[i]
    always_comb begin
        out = '0;
        for (int i = 0; i < MAX_WORDS; i++)
            out[i*32 +: 32] = buffer[i];
    end

    // -----------------------------------------------------------------------
    // Padded last word: insert 0x06 immediately after the final valid byte
    // -----------------------------------------------------------------------
    logic [31:0] padded_word;
    always_comb begin
        case (byte_num)
            2'b00: padded_word = {in[31:24], 8'h06, 16'h0000};  // 1 valid byte
            2'b01: padded_word = {in[31:16], 8'h06,  8'h00};    // 2 valid bytes
            2'b10: padded_word = {in[31: 8], 8'h06};            // 3 valid bytes
            2'b11: padded_word = in;                            // 4 valid bytes (special path)
        endcase
    end

    // -----------------------------------------------------------------------
    // FSM transitions and buffer updates
    // -----------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (reset) begin
            state            <= S_FILL;
            write_ptr        <= '0;
            overflow_pending <= 1'b0;
            for (int i = 0; i < MAX_WORDS; i++)
                buffer[i] <= '0;

        end else begin
            case (state)

                // -----------------------------------------------------------
                // S_FILL: accept words; finalize when is_last is seen
                // -----------------------------------------------------------
                S_FILL: begin

                    // Empty / byte-aligned end: is_last without a data word
                    if (is_last && !in_ready) begin
                        if (write_ptr == rate_words - 1) begin
                            // Rare: buffer has exactly one slot left; 0x06 and 0x80
                            // must share that word.
                            buffer[write_ptr] <= 32'h06000080;
                        end else begin
                            buffer[write_ptr]      <= 32'h06000000;
                            buffer[rate_words - 1] <= 32'h00000080;
                        end
                        state <= S_READY;

                    // Last data word present
                    end else if (accept && is_last) begin

                        // --- Overflow: full 4-byte word fills the last slot ---
                        // 0x06 has nowhere to go in this block; emit data block
                        // first, then a padding-only block after f_ack.
                        if (byte_num == 2'b11 && write_ptr == rate_words - 1) begin
                            buffer[write_ptr] <= in;
                            overflow_pending  <= 1'b1;
                            state             <= S_READY;

                        // --- Full 4-byte word; room for 0x06 in the next slot ---
                        end else if (byte_num == 2'b11) begin
                            buffer[write_ptr] <= in;
                            if (write_ptr + 1 == rate_words - 1) begin
                                // 0x06 and 0x80 share the last rate word
                                buffer[rate_words - 1] <= 32'h06000080;
                            end else begin
                                buffer[write_ptr + 1]  <= 32'h06000000;
                                buffer[rate_words - 1] <= 32'h00000080;
                            end
                            state <= S_READY;

                        // --- Partial last word (1–3 valid bytes) ---
                        end else begin
                            if (write_ptr == rate_words - 1) begin
                                // Padded word occupies the last rate slot;
                                // OR in 0x80 at bits[7:0] (0x86 if 0x06 is already there).
                                buffer[write_ptr] <= padded_word | 32'h00000080;
                            end else begin
                                buffer[write_ptr]      <= padded_word;
                                buffer[rate_words - 1] <= 32'h00000080;
                            end
                            state <= S_READY;
                        end

                    // --- Normal word (not the last) ---
                    end else if (accept) begin
                        buffer[write_ptr] <= in;
                        write_ptr         <= write_ptr + 1;
                    end
                end

                // -----------------------------------------------------------
                // S_READY: out_ready=1; wait for f_ack
                // -----------------------------------------------------------
                S_READY: begin
                    if (f_ack) begin
                        if (overflow_pending) begin
                            // Data block consumed; prepare the padding-only block.
                            // Non-blocking loop first clears all slots, then the two
                            // explicit assignments below override slots 0 and rate_words-1.
                            for (int i = 0; i < MAX_WORDS; i++)
                                buffer[i] <= '0;
                            buffer[0]              <= 32'h06000000;
                            buffer[rate_words - 1] <= 32'h00000080;
                            overflow_pending       <= 1'b0;
                            // Stay in S_READY so out_ready remains asserted.
                        end else begin
                            state <= S_DONE;
                        end
                    end
                end

                // -----------------------------------------------------------
                // S_DONE: idle until reset
                // -----------------------------------------------------------
                S_DONE: begin
                    /* wait for reset */
                end

                default: state <= S_FILL;

            endcase
        end
    end

endmodule : padder