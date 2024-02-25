#include "host.h"
#include "constants.h"
#include <vector> 
#include <random>
#include <assert.h>
#include <string.h>
#include <iomanip>
#include <cstdint>
#include <iostream>

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

    //creates and initializes a hasher object
    static XXHash64 create(uint64_t seed) {
        XXHash64 xxh;
        xxh.state[0] = seed + Prime1 + Prime2;
        xxh.state[1] = seed + Prime2;
        xxh.state[2] = seed;
        xxh.state[3] = seed - Prime1;
        xxh.bufferSize = 0;
        xxh.totalLength = 0;
        return xxh;
    }

    // adds data to hasher object
    bool add(const void* input, uint64_t length) {
        // Check for no data
        if (!input || length == 0) return false;

        totalLength += length;
        // Byte-wise access
        const unsigned char* data = (const unsigned char*)input;

        // Calculate how much space is left in the buffer
        uint64_t spaceLeft = MaxBufferSize - bufferSize;

        // If all data fits into the remaining buffer space
        if (length <= spaceLeft) {
            for (uint64_t i = 0; i < length; ++i) {
                buffer[bufferSize + i] = data[i];
            }
            bufferSize += length;
            return true;
        }

        // Fill up the buffer first if it's partially filled
        uint64_t initialCopyLength = spaceLeft;
        for (uint64_t i = 0; i < initialCopyLength; ++i) {
            buffer[bufferSize + i] = data[i];
        }
        bufferSize += initialCopyLength;

        // Process the filled buffer
        process(buffer, state[0], state[1], state[2], state[3]);
        bufferSize = 0; // Reset buffer after processing

        // Process chunks of 32 bytes directly from input data
        uint64_t processedLength = initialCopyLength;
        uint64_t remainingLength = length - processedLength;
        while (remainingLength >= 32) {
            process(&data[processedLength], state[0], state[1], state[2], state[3]);
            processedLength += 32;
            remainingLength -= 32;
        }

        // Copy any remaining bytes to the buffer
        for (uint64_t i = 0; i < remainingLength; ++i) {
            buffer[i] = data[processedLength + i];
        }
        bufferSize = remainingLength;

        return true;
    }

    // computes hash 
    uint64_t hash() const {
        uint64_t result;
        if (totalLength >= MaxBufferSize) {
            result = rotateLeft(state[0],  1) +
                    rotateLeft(state[1],  7) +
                    rotateLeft(state[2], 12) +
                    rotateLeft(state[3], 18);
            result = (result ^ processSingle(0, state[0])) * Prime1 + Prime4;
            result = (result ^ processSingle(0, state[1])) * Prime1 + Prime4;
            result = (result ^ processSingle(0, state[2])) * Prime1 + Prime4;
            result = (result ^ processSingle(0, state[3])) * Prime1 + Prime4;
        } else {
            // Internal state wasn't set in add(), therefore original seed is still stored in state2
            result = state[2] + Prime5;
        }

        result += totalLength;

        // Process remaining bytes in temporary buffer
        uint64_t dataIndex = 0;  // Use dataIndex to access buffer elements

        // At least 8 bytes left? => Process 8 bytes per step
        while (dataIndex + 8 <= bufferSize) {
            uint64_t dataValue = 0;
            for (int i = 0; i < 8; i++) {
                dataValue |= ((uint64_t)buffer[dataIndex + i]) << (i * 8);
            }
            result = rotateLeft(result ^ processSingle(0, dataValue), 27) * Prime1 + Prime4;
            dataIndex += 8;
        }

        // 4 bytes left? => Process those
        if (dataIndex + 4 <= bufferSize) {
            uint32_t dataValue = 0;
            for (int i = 0; i < 4; i++) {
                dataValue |= ((uint32_t)buffer[dataIndex + i]) << (i * 8);
            }
            result = rotateLeft(result ^ (dataValue * Prime1), 23) * Prime2 + Prime3;
            dataIndex += 4;
        }

        // Take care of remaining 0..3 bytes, process 1 byte per step
        while (dataIndex < bufferSize) {
            result = rotateLeft(result ^ (buffer[dataIndex++] * Prime5), 11) * Prime1;
        }

        // Mix bits
        result ^= result >> 33;
        result *= Prime2;
        result ^= result >> 29;
        result *= Prime3;
        result ^= result >> 32;

        return result;
    }

    // not used by ubft

    // static uint64_t hash(const void* input, uint64_t length, uint64_t seed) {
    //     XXHash64 hasher = XXHash64::create(seed);
    //     hasher.add(input, length);
    //     return hasher.hash();
    // }

    // printer function to print state of hasher object - Helpful for debugging

    // void printXXHash64(const struct XXHash64 xxh) {
    //         // Print state array
    //     printf("State values from host:\n");
    //     for (int i = 0; i < 4; ++i) {
    //         printf("state[%d] = %llu\n", i, xxh.state[i]);
    //     }

    //     // Print buffer values
    //     printf("\nBuffer values:\n");
    //     for (uint64_t i = 0; i < xxh.bufferSize; ++i) {
    //         printf("buffer[%llu] = %u\n", i, xxh.buffer[i]);
    //     }

    //     // Print bufferSize and totalLength
    //     printf("\nBuffer size: %llu\n", xxh.bufferSize);
    //     printf("Total length: %llu\n", xxh.totalLength);
    // }

    private:

        static inline uint64_t rotateLeft(uint64_t x, unsigned char bits) {
            return (x << bits) | (x >> (64 - bits));
        }

        static inline uint64_t processSingle(uint64_t previous, uint64_t input) {
            return rotateLeft(previous + input * Prime2, 31) * Prime1;
        }

        static inline void process(const void* data, uint64_t& state0, uint64_t& state1, uint64_t& state2, uint64_t& state3) {
            const uint64_t* block = (const uint64_t*) data;
            state0 = processSingle(state0, block[0]);
            state1 = processSingle(state1, block[1]);
            state2 = processSingle(state2, block[2]);
            state3 = processSingle(state3, block[3]);
        } 
};


int main(int argc, char** argv) {

    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <XCLBIN File>" << std::endl;
        return EXIT_FAILURE;
    }
    
    clock_t htod, dtoh, comp; 

    /*====================================================CL===============================================================*/

    std::string binaryFile = argv[1];
    cl_int err;
    cl::Context context;
    cl::Kernel krnl1, krnl2;
    cl::CommandQueue q;
    
    auto devices = get_xil_devices();
    auto fileBuf = read_binary_file(binaryFile);
    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    bool valid_device = false;
    for (unsigned int i = 0; i < devices.size(); i++) {
        auto device = devices[i];
        OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
        OCL_CHECK(err, q = cl::CommandQueue(context, device, 0, &err));
        std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
        cl::Program program(context, {device}, bins, nullptr, &err);
        if (err != CL_SUCCESS) {
            std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
        } else {
            std::cout << "Device[" << i << "]: program successful!\n";
            std::cout << "Setting CU(s) up..." << std::endl; 
            OCL_CHECK(err, krnl1 = cl::Kernel(program, "krnl", &err));
            valid_device = true;
            break; // we break because we found a valid device
        }
    }
    if (!valid_device) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }

    /*====================================================INIT INPUT/OUTPUT VECTORS===============================================================*/

    // xxhash config - uint64_t datatype used by ubft with xxhash algo
    // My implementation is tested on uint64_t vector of size 3
    std::vector<uint64_t, aligned_allocator<uint64_t> > input(3);
    std::vector<uint64_t, aligned_allocator<uint64_t> > hash_hw(1);
    uint64_t *hash_sw = (uint64_t*) malloc(sizeof(uint64_t) * 1);

    /*====================================================SW VERIFICATION===============================================================*/

    /* Add software xxhash*/

    uint64_t values[] = {1234567890123456, 1234567890123455, 1234567890123454};
    input = {1234567890123456, 1234567890123455, 1234567890123454};
    size_t numValues = sizeof(values) / sizeof(values[0]);
    std::cout << "Size: " << numValues << std::endl;

    // Initialize XXHash64 with a seed value - ubft is using unseeded version, which behaves the same as keeping seed = 0
    uint64_t seed = 0; 
    XXHash64 hasher = XXHash64::create(seed);

    // Hash the data
    for (size_t i = 0; i < numValues; ++i) {
        std::cout << "Ele and size: " << values[i] << " " << sizeof(values[i]) << std::endl;
        hasher.add(&values[i], sizeof(values[i]));
        // hasher.printXXHash64(hasher);                -----> use for debugging
    }

    // Compute the hash
    uint64_t hashResult = hasher.hash();

    // Print the hash result
    std::cout << "Hash from host: " << hashResult << std::endl;

    /*====================================================Setting up kernel I/O===============================================================*/

    //xxhash config
    OCL_CHECK(err, cl::Buffer buffer_input(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(uint64_t) * 3, input.data(), &err)); 

    /* OUTPUT BUFFERS */
    OCL_CHECK(err, cl::Buffer buffer_output(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, sizeof(uint64_t) * 1, hash_hw.data(), &err)); 

    /* SETTING INPUT PARAMETERS */
    OCL_CHECK(err, err = krnl1.setArg(0, buffer_input));
    OCL_CHECK(err, err = krnl1.setArg(1, buffer_output));



    /*====================================================KERNEL===============================================================*/
    /* HOST -> DEVICE DATA TRANSFER*/
    std::cout << "HOST -> DEVICE" << std::endl; 
    htod = clock(); 
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_input}, 0 /* 0 means from host*/));
    q.finish();
    htod = clock() - htod; 
    
    /*STARTING KERNEL(S)*/
    std::cout << "STARTING KERNEL(S)" << std::endl; 
    comp = clock(); 
	OCL_CHECK(err, err = q.enqueueTask(krnl1));
    q.finish(); 
    comp = clock() - comp;
    std::cout << "KERNEL(S) FINISHED" << std::endl; 

    /*DEVICE -> HOST DATA TRANSFER*/
    std::cout << "HOST <- DEVICE" << std::endl; 
    dtoh = clock();
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_output}, CL_MIGRATE_MEM_OBJECT_HOST));
    q.finish();
    dtoh = clock() - dtoh;

    /*====================================================VERIFICATION & TIMING===============================================================*/

    std::cout << "Hash from krnl: " << hash_hw[0] << std::endl;
    free(hash_sw);
}