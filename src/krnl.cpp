#include "constants.h"
#include <vector> 
#include <random>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ap_int.h>
#include <stdint.h> 
#include <cstdint>
#include <cstddef>

//xxhash library structs and functions

struct XXHash64 {
    
    static const uint64_t MaxBufferSize = 31 + 1;
    static const uint64_t Prime1 = 11400714785074694791ULL;
    static const uint64_t Prime2 = 14029467366897019727ULL;
    static const uint64_t Prime3 =  1609587929392839161ULL;
    static const uint64_t Prime4 =  9650029242287828579ULL;
    static const uint64_t Prime5 =  2870177450012600261ULL;

    uint64_t state[4];
    unsigned char buffer[MaxBufferSize];
    uint64_t bufferSize;
    uint64_t totalLength;


    static XXHash64 create(uint64_t seed);
    XXHash64 add(XXHash64 xxh, uint64_t input, uint64_t length);
    struct HashResult hash(XXHash64 xxh) const;

    private:
        static inline uint64_t rotateLeft(uint64_t x, unsigned char bits);
        static inline uint64_t processSingle(uint64_t previous, uint64_t input);
        static inline XXHash64 process(const unsigned char data[32], uint64_t offset, XXHash64 xxh);

};

struct HashResult {
    uint64_t hash;
    XXHash64 xxh;
};


XXHash64 XXHash64::create(uint64_t seed) {
    XXHash64 xxh;
    xxh.state[0] = seed + Prime1 + Prime2;
    xxh.state[1] = seed + Prime2;
    xxh.state[2] = seed;
    xxh.state[3] = seed - Prime1;
    xxh.bufferSize = 0;
    xxh.totalLength = 0;
    return xxh;
}

/* add function takes in uint64_t - as used by ubft (this was a void pointer but it is not supported in HLS) 
Also - it returns updated hasher object and does not deal with pointer of hasher
*/ 

XXHash64 XXHash64::add(XXHash64 xxh, uint64_t input, uint64_t length) {
    // Check for no data
    if (!input || length == 0) return xxh;

    xxh.totalLength += length;
    
    unsigned char data[sizeof(uint64_t)];

    // Copy each byte of the uint64_t value into the array
    // This is done because xxhash computes hash byte-wise
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        data[i] = (input >> (i * 8)) & 0xFF;  // Shift and mask to get each byte
    }

    // Calculate how much space is left in the buffer
    uint64_t spaceLeft = MaxBufferSize - xxh.bufferSize;

    // If all data fits into the remaining buffer space
    if (length <= spaceLeft) {
        for (uint64_t i = 0; i < length; ++i) {
            xxh.buffer[xxh.bufferSize + i] = data[i];
        }
        xxh.bufferSize += length;
        return xxh;
    }

    // Fill up the buffer first if it's partially filled
    uint64_t initialCopyLength = spaceLeft;
    for (uint64_t i = 0; i < initialCopyLength; ++i) {
        xxh.buffer[xxh.bufferSize + i] = data[i];
    }
    xxh.bufferSize += initialCopyLength;

    // Process the filled buffer
    xxh = process(xxh.buffer, 0, xxh);
    xxh.bufferSize = 0; // Reset buffer after processing

    // Process chunks of 32 bytes directly from input data
    uint64_t processedLength = initialCopyLength;
    uint64_t remainingLength = length - processedLength;
    while (remainingLength >= 32) {
        xxh = process(data, processedLength, xxh);
        processedLength += 32;
        remainingLength -= 32;
    }

    // Copy any remaining bytes to the buffer
    for (uint64_t i = 0; i < remainingLength; ++i) {
        xxh.buffer[i] = data[processedLength + i];
    }
    xxh.bufferSize = remainingLength;

    return xxh;
}

/* Returns hashResult struct and not just the resultant hash 
This is because - we need to re-initialize hasher once hash is calculated - which means
making changes to hasher object. So, it returns new hasher object and the computed hash wrapped in a struct
*/

HashResult XXHash64::hash(XXHash64 xxh) const {

    HashResult hashResult;
    uint64_t result;
    if (xxh.totalLength >= MaxBufferSize) {
        result = rotateLeft(xxh.state[0],  1) +
                rotateLeft(xxh.state[1],  7) +
                rotateLeft(xxh.state[2], 12) +
                rotateLeft(xxh.state[3], 18);
        result = (result ^ processSingle(0, xxh.state[0])) * Prime1 + Prime4;
        result = (result ^ processSingle(0, xxh.state[1])) * Prime1 + Prime4;
        result = (result ^ processSingle(0, xxh.state[2])) * Prime1 + Prime4;
        result = (result ^ processSingle(0, xxh.state[3])) * Prime1 + Prime4;
    } else {
        // Internal state wasn't set in add(), therefore original seed is still stored in state2
        result = xxh.state[2] + Prime5;
    }

    result += xxh.totalLength;

    // Process remaining bytes in temporary buffer
    uint64_t dataIndex = 0;  // Use dataIndex to access buffer elements

    // At least 8 bytes left? => Process 8 bytes per step
    while (dataIndex + 8 <= xxh.bufferSize) {
        uint64_t dataValue = 0;
        for (int i = 0; i < 8; i++) {
            dataValue |= ((uint64_t)xxh.buffer[dataIndex + i]) << (i * 8);
        }
        result = rotateLeft(result ^ processSingle(0, dataValue), 27) * Prime1 + Prime4;
        dataIndex += 8;
    }

    // 4 bytes left? => Process those
    if (dataIndex + 4 <= xxh.bufferSize) {
        uint32_t dataValue = 0;
        for (int i = 0; i < 4; i++) {
            dataValue |= ((uint32_t)xxh.buffer[dataIndex + i]) << (i * 8);
        }
        result = rotateLeft(result ^ (dataValue * Prime1), 23) * Prime2 + Prime3;
        dataIndex += 4;
    }

    // Take care of remaining 0..3 bytes, process 1 byte per step
    while (dataIndex < xxh.bufferSize) {
        result = rotateLeft(result ^ (xxh.buffer[dataIndex++] * Prime5), 11) * Prime1;
    }

    // Mix bits
    result ^= result >> 33;
    result *= Prime2;
    result ^= result >> 29;
    result *= Prime3;
    result ^= result >> 32;

    xxh = XXHash64::create(0);
    hashResult.hash = result;
    hashResult.xxh = xxh;
    return hashResult;
}


// this function not used by ubft

// static uint64_t hash(const void* input, uint64_t length, uint64_t seed) {
//     XXHash64 hasher = XXHash64::create(seed);
//     hasher.add(hasher, input, length);
//     return hasher.hash();
// }


uint64_t XXHash64::rotateLeft(uint64_t x, unsigned char bits) {
    return (x << bits) | (x >> (64 - bits));
}

uint64_t XXHash64::processSingle(uint64_t previous, uint64_t input) {
    return rotateLeft(previous + input * Prime2, 31) * Prime1;
}

/* This function processes 32 bytes of data each time
called when buffer is full or there are some unprocessed bytes in input data of add function
parameter offset added to make function pointer free. 
offset parameter is required while processing input data from a point it is unprocessed
when buffer is being processed - offset is going to be zero
To understand better - check function calls to process in the add() function above
*/

XXHash64 XXHash64::process(const unsigned char data[32], uint64_t offset, XXHash64 xxh) {
    uint64_t block[4] = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            block[i] |= ((uint64_t)data[offset + i * 8 + j]) << (j * 8);
        }
    }

    xxh.state[0] = processSingle(xxh.state[0], block[0]);
    xxh.state[1] = processSingle(xxh.state[1], block[1]);
    xxh.state[2] = processSingle(xxh.state[2], block[2]);
    xxh.state[3] = processSingle(xxh.state[3], block[3]);

    return xxh;
} 


extern "C" {

    // print function for debugging
    void printXXHash64(const struct XXHash64 xxh) {
        
        printf("State values from kernel:\n");
        for (int i = 0; i < 4; ++i) {
            printf("state[%d] = %llu\n", i, xxh.state[i]);
        }

        printf("\nBuffer values:\n");
        for (uint64_t i = 0; i < xxh.bufferSize; ++i) {
            printf("buffer[%llu] = %u\n", i, xxh.buffer[i]);
        }

        printf("\nBuffer size: %llu\n", xxh.bufferSize);
        printf("Total length: %llu\n", xxh.totalLength);
    }

    void krnl(uint64_t* input, uint64_t* output) {                     
        #pragma HLS INTERFACE m_axi port = input bundle = gmem0
        #pragma HLS INTERFACE m_axi port = output bundle = gmem1

       
        uint64_t seed = 0;  
        XXHash64 hasher = XXHash64::create(seed);

        for (size_t i = 0; i < 3; ++i) {
            printf("Ele and size: %llu %zu\n", input[i], sizeof(input[i]));
            hasher = hasher.add(hasher, input[i], sizeof(input[i]));
            // printXXHash64(hasher);
        }

        HashResult hashResult = hasher.hash(hasher);
        hasher = hashResult.xxh;
        output[0] = hashResult.hash;

        // Print the hash result
        // printf("Hash from krnl: %llu\n", output[0]);

    }
}