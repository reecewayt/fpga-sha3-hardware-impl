#!/usr/bin/env python3
"""
trace_abc.py - Full Keccak Trace Generator for SHA3-256
========================================================
Generates a complete trace of the Keccak-f[1600] permutation for the "abc" test vector.

Runs SHA3_Instrumented on b"abc" and dumps every intermediate state snapshot
(after each theta, rho/pi, chi, iota step inside every round, plus absorb/
padding/squeeze milestones) to a text file for hardware validation.

Expected digest: 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532

Note: Developed with the assistance of GitHub Copilot.
"""

import sys
import os

# Allow running from any directory.
sys.path.insert(0, os.path.dirname(__file__))

from sha3 import SHA3_Instrumented

OUTPUT_FILE = "abc_trace.txt"

# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------

def format_lane(value: int) -> str:
    """64-bit lane as zero-padded hex."""
    return f"{value:016x}"

def format_state(state) -> str:
    """
    Pretty-print the 5×5 Keccak state as a grid.

    The state is stored as lanes[x][y]; the conventional display is a 5-col
    table where each row is a y-index and each column is an x-index (matching
    NIST FIPS 202 Figure 1).

      State[x][y]  →  column x, row y
    """
    lines = []
    lines.append("      x=0              x=1              x=2              x=3              x=4")
    for y in range(5):
        row = "  ".join(format_lane(state[x][y]) for x in range(5))
        lines.append(f"y={y}:  {row}")
    return "\n".join(lines)

def format_snapshot(snap: dict) -> str:
    """Format one snapshot into a human-readable block."""
    step  = snap["step"]
    rnd   = snap["round"]
    state = snap["state"]

    header = f"[{step}]"
    if rnd is not None:
        header += f"  round={rnd}"

    return f"{header}\n{format_state(state)}"

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    message  = b"abc"
    expected = "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"

    sha3   = SHA3_Instrumented(output_bits=256)
    digest = sha3.hash(message)

    # Verify correctness first.
    actual_hex = digest.hex()
    match      = (actual_hex == expected)

    print(f"Message  : {message!r}")
    print(f"Expected : {expected}")
    print(f"Got      : {actual_hex}")
    print(f"Match    : {'YES ✓' if match else 'NO ✗'}")
    print(f"Snapshots: {len(sha3.snapshots)}")

    # Build the trace file.
    with open(OUTPUT_FILE, "w") as f:
        f.write("=" * 72 + "\n")
        f.write("SHA3-256 full Keccak trace — message: b\"abc\"\n")
        f.write(f"Expected digest: {expected}\n")
        f.write(f"Computed digest: {actual_hex}\n")
        f.write(f"Match: {'YES' if match else 'NO'}\n")
        f.write(f"Total snapshots: {len(sha3.snapshots)}\n")
        f.write("=" * 72 + "\n\n")

        for i, snap in enumerate(sha3.snapshots):
            f.write(f"--- snapshot {i:04d} ---\n")
            f.write(format_snapshot(snap))
            f.write("\n\n")

        f.write("=" * 72 + "\n")
        f.write("End of trace\n")

    print(f"Trace written to: {OUTPUT_FILE}")
    return 0 if match else 1


if __name__ == "__main__":
    sys.exit(main())
