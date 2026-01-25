### SHA-3 Algorithm Summary

**Overview**
SHA-3 (Secure Hash Algorithm 3) is defined in the NIST FIPS 202 standard and is based on the Keccak cryptographic family. Unlike its predecessors (SHA-1 and SHA-2), which use the Merkle-Damgård construction, SHA-3 utilizes the **sponge construction**. Ideally suited for an AI agent's processing logic, the algorithm operates on a fixed-width state through a sequence of absorbing and squeezing phases.

#### 1. Algorithmic Steps

**Step 1: Initialization**
The internal state, denoted as $S$, is initialized to a grid of zeros. The state width ($b$) is fixed at 1600 bits for the standard SHA-3 instances. This state is conceptually organized as a $5 \times 5 \times 64$ bit 3D array (5 rows, 5 columns, 64-bit lanes).

**Step 2: Padding and Domain Separation**
The input message $M$ is padded to ensures its length is a multiple of the rate $r$.
*   **Domain Suffix:** A specific suffix is appended to the message to distinguish between functions. For SHA-3 hash functions, the suffix is `01`. For SHAKE (XOFs), the suffix is `1111`.
*   **Padding Rule:** The padding pattern **pad10*1** is applied. A '1' bit is appended, followed by a minimum number of '0' bits, and a final '1' bit is added at the end of the block.

**Step 3: Absorbing Phase**
The padded message is split into blocks ($P_i$), each having a length of $r$ bits.
*   The block is XORed with the first $r$ bits of the state (the rate part). The remaining $c$ bits (capacity) are not touched.
*   The state undergoes the **Keccak-f** permutation function (detailed below).
*   This cycle repeats until all input blocks are processed.

**Step 4: The Keccak-f Permutation**
The permutation consists of **24 rounds**. Each round applies five mapping steps in sequence to the state matrix $A$:
1.  **Theta ($\theta$):** Provides linear diffusion by computing the parity of columns and XORing the result into nearby columns.
2.  **Rho ($\rho$):** Provides inter-slice diffusion via lane-wise bit rotations using fixed offsets.
3.  **Pi ($\pi$):** Permutes the positions of the lanes within the state matrix.
4.  **Chi ($\chi$):** The only non-linear operation. It updates bits using row-wise XOR, NOT, and AND operations (conceptually an S-box).
5.  **Iota ($\iota$):** Breaks symmetry by XORing a round-dependent constant into the first lane of the state.

**Step 5: Squeezing Phase**
*   The first $r$ bits of the state are extracted as output.
*   If the required output length ($d$) exceeds $r$, the permutation function Keccak-f is applied again to the state, and additional bits are extracted.
*   The final output is truncated to the desired length $d$.

---

#### 2. Algorithm Parameters

The SHA-3 family is defined by the state width $b = 1600$. The relationship between the Rate ($r$) and Capacity ($c$) is defined as $r + c = 1600$. Higher capacity yields higher security but lower throughput.

**Global Constants**
*   **State Width ($b$):** 1600 bits.
*   **Word Size ($w$):** 64 bits.
*   **Number of Rounds ($n_r$):** 24.
*   **Padding Type:** pad10*1.

**Instance-Specific Parameters**

| Function | Output ($d$) | Rate ($r$) | Capacity ($c$) | Domain Suffix | Byte-Aligned Suffix (Hex)* | Collision Resistance (Bits) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **SHA3-224** | 224 bits | 1152 | 448 | `01` | `0x06` | 112 |
| **SHA3-256** | 256 bits | 1088 | 512 | `01` | `0x06` | 128 |
| **SHA3-384** | 384 bits | 832 | 768 | `01` | `0x06` | 192 |
| **SHA3-512** | 512 bits | 576 | 1024 | `01` | `0x06` | 256 |
| **SHAKE128** | Arbitrary | 1344 | 256 | `1111` | `0x1F` | min(d/2, 128) |
| **SHAKE256** | Arbitrary | 1088 | 512 | `1111` | `0x1F` | min(d/2, 256) |

*Table Sources:*
*\*Note: The byte-aligned suffix represents the domain bits plus the first padding bit when the message is byte-aligned.*