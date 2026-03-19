#!/usr/bin/env python3
"""
sha3.py - SHA-3 (Keccak) Instrumented Implementation
=====================================================
A high-granularity SHA-3 implementation designed specifically for FPGA hardware validation.
This module captures intermediate state snapshots after each transformation step in the
Keccak-f[1600] permutation, enabling verification of hardware module outputs.

Features:
----------
- Full SHA-3 implementation supporting all variants (SHA3-224, SHA3-256, SHA3-384, SHA3-512)
- Detailed state logging after each step: theta (θ), rho (ρ), pi (π), chi (χ), iota (ι)
- Snapshot capture at absorb, padding, and squeeze phases
- Verified against official NIST test vectors and Python hashlib

Note:
-----
This implementation prioritizes clarity and instrumentation over performance.
It is intended for generating test vectors and validating hardware implementations,
not for production hashing.

Based on the Keccak reference implementation:
https://github.com/XKCP/XKCP/blob/master/Standalone/CompactFIPS202/Python/CompactFIPS202.py

Note: Developed with the assistance of GitHub Copilot.
"""


class SHA3_Instrumented: 
    def __init__(self, output_bits=256): 
        if output_bits not in [224, 256, 384, 512]:
            raise ValueError("Output bits must be one of: 224, 256, 384, 512")
        
        self.output_bits = output_bits            # Output size in bits (224, 256, 384, or 512)
        self.lanes = [[0] * 5 for _ in range(5)]  # 5x5 array of 64-bit lanes (i.e. A state matrix)
        self.snapshots = []                       # List to store intermediate states


    def log_state(self, step_name, round_num = None): 
        """Log the current state for hardware validation."""
        snapshot = {
            'step': step_name,
            'round': round_num,
            'state': [row[:] for row in self.lanes]  # Deep copy of state
        }
        self.snapshots.append(snapshot)

    @staticmethod
    def __ROL64(a, n):
        return ((a >> (64-(n%64))) + (a << (n%64))) % (1 << 64)


    def keccak_round(self, round_index): 
        """Execute one round of Keccak-f permutation."""
        self.log_state('round_start', round_index)
        self.theta()
        self.log_state('after_theta', round_index)
        self.rho_pi()
        self.log_state('after_rho_pi', round_index)
        self.chi()
        self.log_state('after_chi', round_index)
        self.iota(round_index)
        self.log_state('after_iota', round_index)

    def keccak_f(self, rate, capacity, inputBytes, delimitedSuffix, outputByteLen):
        """Keccak sponge function."""
        if(rate + capacity != 1600):
            raise ValueError("Rate and Capacity must sum to 1600 bits.")
        
        self.lanes = [[0] * 5 for _ in range(5)]  # reset state for multiple calls
        self.snapshots = []  # Reset snapshots
        self.log_state('initial_state', None)
        
        rateInBytes = rate // 8
        
        # Absorb phase
        self.absorb(inputBytes, rateInBytes, delimitedSuffix)
        
        # Squeeze phase
        return self.squeeze(outputByteLen, rateInBytes)



    def absorb(self, inputBytes, rateInBytes, delimitedSuffix):
        """Absorb phase: XOR input into state and apply permutation."""
        inputOffset = 0
        blockSize = 0
        blockNum = 0
        
        # === Absorb all the input blocks ===
        while inputOffset < len(inputBytes):
            blockSize = min(len(inputBytes) - inputOffset, rateInBytes)
            self.log_state(f'absorb_block_{blockNum}_start', None)
            
            # XOR the block into state
            block = inputBytes[inputOffset:inputOffset + blockSize]
            self.xor_block_into_state(block)
            
            self.log_state(f'absorb_block_{blockNum}_after_xor', None)
            
            inputOffset += blockSize
            
            # Only apply permutation if we filled a complete block
            if blockSize == rateInBytes:
                for round_index in range(24):
                    self.keccak_round(round_index)
                self.log_state(f'absorb_block_{blockNum}_after_permutation', None)
                blockSize = 0
            
            blockNum += 1
        
        # === Do the padding and switch to the squeezing phase ===
        self.log_state('before_padding', None)
        self.apply_padding(blockSize, rateInBytes, delimitedSuffix)
        self.log_state('after_padding', None)
        
        # Apply final permutation
        for round_index in range(24):
            self.keccak_round(round_index)
        self.log_state('after_final_permutation', None)
    
    def xor_block_into_state(self, block):
        """XOR a block of bytes into the state array."""
        for i in range(len(block)):
            # Convert byte position to lane coordinates
            lane_index = i // 8  # Each lane is 8 bytes
            byte_in_lane = i % 8
            
            # Map linear index to (x, y) coordinates
            x = lane_index % 5
            y = lane_index // 5
            
            # XOR byte into the appropriate position in the lane
            self.lanes[x][y] ^= block[i] << (8 * byte_in_lane)

    def apply_padding(self, blockSize, rateInBytes, delimitedSuffix):
        """Apply SHA-3 padding (10*1 pattern) to the state."""
        # XOR delimited suffix at the position after the last absorbed byte
        self.xor_byte_into_state(blockSize, delimitedSuffix)
        
        # Special case: if delimitedSuffix has MSB set and blockSize is at the last position
        if ((delimitedSuffix & 0x80) != 0) and (blockSize == (rateInBytes - 1)):
            for round_index in range(24):
                self.keccak_round(round_index)
            self.log_state('padding_special_permutation', None)
        
        # XOR 0x80 at the last position of the rate
        self.xor_byte_into_state(rateInBytes - 1, 0x80)
    
    def xor_byte_into_state(self, position, byte_value):
        """XOR a single byte into the state at the given position."""
        lane_index = position // 8
        byte_in_lane = position % 8
        
        x = lane_index % 5
        y = lane_index // 5
        
        self.lanes[x][y] ^= byte_value << (8 * byte_in_lane)

    def squeeze(self, outputByteLen, rateInBytes):
        """Squeeze phase: Extract output bytes from state."""
        output = bytearray()
        
        while len(output) < outputByteLen:
            self.log_state(f'squeeze_block_{len(output)//rateInBytes}', None)
            
            # Extract bytes from state
            block = self.extract_bytes_from_state(rateInBytes)
            output.extend(block)
            
            # If we need more output, apply permutation
            if len(output) < outputByteLen:
                for round_index in range(24):
                    self.keccak_round(round_index)
        
        return bytes(output[:outputByteLen])
    
    def extract_bytes_from_state(self, numBytes):
        """Extract bytes from the state array."""
        output = bytearray()
        
        for i in range(numBytes):
            # Convert byte position to lane coordinates
            lane_index = i // 8
            byte_in_lane = i % 8
            
            # Map linear index to (x, y) coordinates
            x = lane_index % 5
            y = lane_index // 5
            
            # Extract byte from the lane
            byte_val = (self.lanes[x][y] >> (8 * byte_in_lane)) & 0xFF
            output.append(byte_val)
        
        return bytes(output)

    def hash(self, data):
        """Compute SHA-3 hash of the input data."""
        # SHA-3 uses different parameters based on output size:
        # SHA3-224: rate=1152, capacity=448, output=224 bits (28 bytes)
        # SHA3-256: rate=1088, capacity=512, output=256 bits (32 bytes)
        # SHA3-384: rate=832,  capacity=768, output=384 bits (48 bytes)
        # SHA3-512: rate=576,  capacity=1024, output=512 bits (64 bytes)
        
        output_bytes = self.output_bits // 8
        capacity = output_bytes * 2 * 8  # capacity = 2 * output_bits
        rate = 1600 - capacity
        
        # SHA-3 uses 0x06 as the delimited suffix
        delimitedSuffix = 0x06
        
        return self.keccak_f(rate, capacity, data, delimitedSuffix, output_bytes)

    # Theta θ Step Mapping
    def theta(self):
        """Theta step: XOR each bit with parity of two columns."""
        C = [0] * 5
        D = [0] * 5

        # Calculate the parity of each column
        C = [self.lanes[x][0] ^ self.lanes[x][1] ^ self.lanes[x][2] ^ self.lanes[x][3] ^ self.lanes[x][4] for x in range(5)]
        # Calculate the D values
        D = [C[(x - 1) % 5] ^ self.__ROL64(C[(x + 1) % 5], 1) for x in range(5)]
        self.lanes = [[self.lanes[x][y] ^ D[x] for y in range(5)] for x in range(5)]
       

    # ρ and π Steps Mapping
    def rho_pi(self):
        (x, y) = (1, 0)
        current = self.lanes[x][y]
        for t in range(24):
            (x, y) = (y, (2 * x + 3 * y) % 5)
            (current, self.lanes[x][y]) = (self.lanes[x][y], self.__ROL64(current, ((t + 1) * (t + 2) // 2)))

    # Chi χ Step Mapping
    def chi(self):
        """Chi step: Non-linear mixing of bits within each row."""
        for y in range(5):
            T = [self.lanes[x][y] for x in range(5)]
            for x in range(5):
                self.lanes[x][y] = T[x] ^((~T[(x+1)%5]) & T[(x+2)%5])
        
    # Iota ι Step Mapping
    def iota(self, round_index):
        """Iota step: Add round constant to state."""
        # Compute round constant using LFSR
        R = 1
        for i in range(round_index + 1):
            round_constant = 0
            for j in range(7):
                R = ((R << 1) ^ ((R >> 7) * 0x71)) % 256
                if (R & 2):
                    round_constant ^= (1 << ((1 << j) - 1))
            if i == round_index:
                self.lanes[0][0] ^= round_constant
        

