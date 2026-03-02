import sha3_pkg::*;

module sha3_shift (
    input   logic                   clk,
    input   logic                   reset,
    input   sha3_variant_t          variant,
    input   logic [MAX_RATE-1:0]    in,
    input   logic                   in_ready,
    output  logic [MAX_RATE-1:0]    out,
    output  logic                   out_ready
);
    // Counter and state registers
    logic [5:0]          additional_shifts;
    logic [5:0]          curr_additional_shifts;
    logic                active;
    logic [MAX_RATE-1:0] shift_reg;

    // The variant determines how many 64-bit shifts to perform in order to move
    // the initial bytes to the first position of the shift register.
    always_comb begin
        unique case (variant)
            SHA3_224: additional_shifts = 0;
            SHA3_256: additional_shifts = 1;
            SHA3_384: additional_shifts = 5;
            SHA3_512: additional_shifts = 9;
            default:  additional_shifts = 9;
        endcase
    end

    // Shift register, counter, and out_ready logic
    always_ff @(posedge clk) begin
        out_ready <= 1'b0; // default: de-asserted every cycle

        if (reset) begin
            shift_reg              <= '0;
            curr_additional_shifts <= '0;
            active                 <= 1'b0;
        end else if (!active && in_ready) begin
            // Latch input and start the shifting sequence
            shift_reg              <= in;
            curr_additional_shifts <= '0;
            active                 <= 1'b1;
        end else if (active) begin
            if (curr_additional_shifts < additional_shifts) begin
                shift_reg              <= {shift_reg[MAX_RATE-65:0], 64'h0};
                curr_additional_shifts <= curr_additional_shifts + 1;
            end else begin
                // Shifting complete: pulse out_ready and go idle
                out_ready              <= 1'b1;
                active                 <= 1'b0;
                curr_additional_shifts <= '0;
            end
        end
    end

    assign out = shift_reg;

endmodule