import sha3_pkg::*;

module padder (
    input  logic                clk,
    input  logic                reset,
    input  logic [31:0]         in,
    input  logic                in_ready,
    input  logic                is_last,
    input  logic [1:0]          byte_num,
    input  sha3_variant_t       variant,
    output logic                buffer_full,
    output logic [MAX_RATE-1:0] out,
    output logic                out_ready,
    input  logic                f_ack
);

    typedef enum logic {
        S_FILL = 1'b0,
        S_PAD  = 1'b1
    } state_t;

    logic [35:0]  i;
    logic         accept_user_input;
    logic         update_shift;
    logic         done;
    state_t       state;
    logic [31:0]  in_switch;
    logic         next_buffer_full;

    // --- buffer_full: one hot position depends on rate ---
    always_comb begin
        unique case (variant)
            SHA3_224: buffer_full = i[35];
            SHA3_256: buffer_full = i[33];
            SHA3_384: buffer_full = i[25];
            SHA3_512: buffer_full = i[17];
            default:  buffer_full = i[17];
        endcase
    end

    // --- next_buffer_full: will the *next* shift fill the buffer? ---
    always_comb begin
        unique case (variant)
            SHA3_224: next_buffer_full = i[34];
            SHA3_256: next_buffer_full = i[32];
            SHA3_384: next_buffer_full = i[24];
            SHA3_512: next_buffer_full = i[16];
            default:  next_buffer_full = i[16];
        endcase
    end

    assign out_ready         = buffer_full;
    assign accept_user_input = (state == S_FILL) & in_ready & ~buffer_full;
    assign update_shift      = (accept_user_input | ((state == S_PAD) & ~buffer_full)) & ~done;

    // --- padding word selection ---
    // Priority order:
    //   1. is_last asserted while still in S_FILL → insert domain suffix 0x06
    //   2. state == S_PAD                         → zero fill / closing sentinel
    //   3. normal data word
    always_comb begin
        priority if (is_last) begin
            // last data word - insert 0x06 domain suffix after valid bytes
            unique case (byte_num)
                2'b00: in_switch = 32'h0600_0000;
                2'b01: in_switch = {in[31:24], 24'h06_0000};
                2'b10: in_switch = {in[31:16], 16'h0600};
                2'b11: in_switch = {in[31:8],  8'h06};
            endcase
            // closing sentinel if this word will fill the buffer
            in_switch[7] = in_switch[7] | next_buffer_full;
        end
        else if (state == S_PAD) begin
            // zero fill, closing sentinel on last word
            in_switch    = 32'h0;
            in_switch[7] = next_buffer_full;
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
            out <= {out[MAX_RATE-33:0], in_switch};
    end

    // --- one hot fill counter ---
    always_ff @(posedge clk) begin
        if (reset)
            i <= '0;
        else if (f_ack | update_shift)
            i <= {i[34:0], 1'b1} & {36{~f_ack}};
    end

    // --- state: fill -> pad ---
    always_ff @(posedge clk) begin
        if (reset)
            state <= S_FILL;
        else if (is_last)
            state <= S_PAD;
    end

    // --- done: stop after padding block sent ---
    always_ff @(posedge clk) begin
        if (reset)
            done <= 1'b0;
        else if ((state == S_PAD) & out_ready)
            done <= 1'b1;
    end

endmodule
