import sha3_pkg::*;

module padder (
    input  logic                clk,
    input  logic                reset,
    input  logic [63:0]         in,
    input  logic                in_ready,
    input  logic                is_last,
    input  logic [2:0]          byte_num,
    input  sha3_variant_t       variant,
    output logic                buffer_full,
    output logic [MAX_RATE-1:0] out,
    output logic                out_ready,
    output logic                done,
    output logic                more_blks,
    input  logic                ack_more_blks,
    input  logic                f_ack
);

    typedef enum logic {
        S_FILL = 1'b0,
        S_PAD  = 1'b1
    } state_t;

    logic [17:0]  i;
    logic         accept_user_input;
    logic         update_shift;
    logic         more_blks_ff;
    state_t       state;
    logic [63:0]  in_switch;
    logic         next_buffer_full;
    logic         accept_last_word;

    // --- buffer_full: one hot position depends on rate ---
    // 64-bit words: SHA3-224=18 words, SHA3-256=17, SHA3-384=13, SHA3-512=9
    // i[0] is set after the 1st shift, so N words fills at i[N-1]
    always_comb begin
        unique case (variant)
            SHA3_224: buffer_full = i[17];
            SHA3_256: buffer_full = i[16];
            SHA3_384: buffer_full = i[12];
            SHA3_512: buffer_full = i[8];
            default:  buffer_full = i[8];
        endcase
    end

    // --- next_buffer_full: will the *next* shift fill the buffer? ---
    always_comb begin
        unique case (variant)
            SHA3_224: next_buffer_full = i[16];
            SHA3_256: next_buffer_full = i[15];
            SHA3_384: next_buffer_full = i[11];
            SHA3_512: next_buffer_full = i[7];
            default:  next_buffer_full = i[7];
        endcase
    end

    assign out_ready         = buffer_full;
    assign accept_user_input = (state == S_FILL) & in_ready & ~buffer_full;
    assign accept_last_word  = accept_user_input & is_last;
    assign update_shift      = (accept_user_input | ((state == S_PAD) & ~buffer_full)) & ~done;

    // --- padding word selection ---
    // Priority order:
    //   1. is_last asserted while still in S_FILL → insert domain suffix 0x06
    //   2. state == S_PAD                         → zero fill / closing sentinel
    //   3. normal data word
    always_comb begin
        priority if (accept_last_word) begin
            // last data word — input is byte-swapped by keccak.sv so valid
            // bytes occupy the LSB lanes; 0x06 suffix goes immediately above them
            unique case (byte_num)
                3'b000: in_switch = 64'h0000_0000_0000_0006;
                3'b001: in_switch = {48'h0,          8'h06, in[7:0]};
                3'b010: in_switch = {40'h0,          8'h06, in[15:0]};
                3'b011: in_switch = {32'h0,          8'h06, in[23:0]};
                3'b100: in_switch = {24'h0,          8'h06, in[31:0]};
                3'b101: in_switch = {16'h0,          8'h06, in[39:0]};
                3'b110: in_switch = { 8'h0,          8'h06, in[47:0]};
                3'b111: in_switch = {                8'h06, in[55:0]};
            endcase
            // closing sentinel (0x80) at bit 63 if this word fills the buffer
            in_switch[63] = in_switch[63] | next_buffer_full;
        end
        else if (state == S_PAD) begin
            // zero fill, closing sentinel on last word
            in_switch     = 64'h0;
            in_switch[63] = next_buffer_full;
        end
        else begin
            // normal data word
            in_switch = in;
        end
    end

    // --- shift register ---
    always_ff @(posedge clk) begin
        if (reset)
            out <= '0;
        else if (update_shift) // also shift when buffer_full to collect last word
            out <= {out[MAX_RATE-65:0], in_switch};
    end

    // --- one hot fill counter ---
    always_ff @(posedge clk) begin
        if (reset)
            i <= '0;
        else if (f_ack | update_shift)
            i <= {i[16:0], 1'b1} & {18{~f_ack}};
    end

    // --- state: fill -> pad ---
    always_ff @(posedge clk) begin
        if (reset)
            state <= S_FILL;
        else if (accept_last_word)
            state <= S_PAD;
    end

    // --- done: stop after padding block sent ---
    always_ff @(posedge clk) begin
        if (reset)
            done <= 1'b0;
        else if ((state == S_PAD) & out_ready)
            done <= 1'b1;
    end

    // --- more_blks flag ---
    // Latch high when first transfer is accepted into padder.
    // Clear when the final (S_PAD) block is accepted by f_permutation.
    // Using state instead of done avoids same-cycle done/f_ack race timing.
    always_ff @(posedge clk) begin
        if (reset)
            more_blks_ff <= 1'b0;
        else if (f_ack & (state == S_PAD))
            more_blks_ff <= 1'b0;
        else if (accept_user_input)
            more_blks_ff <= 1'b1;
    end

    assign more_blks = more_blks_ff;

endmodule
