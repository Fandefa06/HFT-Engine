// include/BinaryParser.hpp

#pragma once
#include "RingBuffer.hpp"
#include "Types.hpp"
#include <fstream>
#include <iostream>

// IMPORTANT: This structure is exactly 21 bytes, identical to the one in PreCompiler.cpp.
// #pragma pack ensures the compiler doesn't add hidden memory padding.
#pragma pack(push, 1)
struct RawOrder {
    uint64_t id;
    int64_t price;
    uint32_t quantity;
    uint8_t side;
};
#pragma pack(pop)

class BinaryParser {
public:
    template<size_t S>
    static void feedFromBinary(RingBuffer<MarketEvent, S>& buffer, const std::string& filename) {
        // Open the file in binary mode
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open binary file " << filename << std::endl;
            // Send termination signal to prevent the consumer thread from hanging indefinitely
            MarketEvent term; term.type = EventType::TERMINATE;
            while (!buffer.push(term)) { __builtin_ia32_pause(); }
            return;
        }

        std::cout << "Injecting pure binary data from: " << filename << std::endl;

        RawOrder ro;
        uint64_t count = 0;

        // HFT LOOP: Copies bytes directly from the SSD to the CPU structure.
        // Zero string parsing, zero string-to-double conversions. Maximum throughput.
        while (file.read(reinterpret_cast<char*>(&ro), sizeof(RawOrder))) {
            
            OrderSide side = (ro.side == 1) ? OrderSide::Ask : OrderSide::Bid;
            Order ord(ro.id, ro.price, ro.quantity, side);
            
            // Push to the lock-free RingBuffer
            while (!buffer.push(MarketEvent::newOrder(ord))) {
                __builtin_ia32_pause();
            }
            count++;
        }

        // Send termination signal to cleanly shut down the consumer thread
        MarketEvent term;
        term.type = EventType::TERMINATE;
        while (!buffer.push(term)) { __builtin_ia32_pause(); }
        
        std::cout << "Binary Injection Complete. " << count << " orders injected at lightspeed." << std::endl;
    }
};