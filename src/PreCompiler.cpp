// src/precompiler.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cmath>
#include <chrono>

// Ultra-compact structure for disk storage (21 bytes per order)
#pragma pack(push, 1)
struct RawOrder {
    uint64_t id;       // Internally translated ID
    int64_t price;     // Price in ticks (cents)
    uint32_t quantity; // Quantity in ticks
    uint8_t side;      // 0 for Bid, 1 for Ask
};
#pragma pack(pop)

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.csv> <output.bin>\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];

    std::ifstream file(inputFile);
    if (!file.is_open()) { 
        std::cerr << "Error: Could not open " << inputFile << "\n";
        return 1;
    }

    std::ofstream outBin(outputFile, std::ios::binary | std::ios::trunc);
    if (!outBin.is_open()) {
        std::cerr << "Error: Could not create " << outputFile << "\n";
        return 1;
    }

    std::cout << "Compiling " << inputFile << " to pure Binary...\n";
    auto start = std::chrono::high_resolution_clock::now();

    std::string line;
    std::unordered_map<uint64_t, uint64_t> idTranslator;
    uint64_t nextInternalId = 1;
    uint64_t count = 0;

    // Buffer for high-speed disk writing
    const size_t BATCH_SIZE = 100000;
    RawOrder* batch = new RawOrder[BATCH_SIZE];
    size_t batchIndex = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string item;
        uint64_t externalId;
        double price, qty;
        std::string isBuyerMaker;
        
        try {
            std::getline(ss, item, ','); externalId = std::stoull(item);
            std::getline(ss, item, ','); price = std::stod(item);
            std::getline(ss, item, ','); qty = std::stod(item);
            std::getline(ss, item, ','); // skip quoteQty
            std::getline(ss, item, ','); // skip timestamp
            std::getline(ss, item, ','); isBuyerMaker = item;

            // ID Translation (The Gateway)
            uint64_t internalId;
            auto it = idTranslator.find(externalId);
            if (it != idTranslator.end()) {
                internalId = it->second;
            } else { 
                internalId = nextInternalId++; 
                idTranslator[externalId] = internalId; 
            }

            uint8_t side = (isBuyerMaker == "true" || isBuyerMaker == "True") ? 1 : 0; // 1 = Ask, 0 = Bid
            int64_t tickPrice = static_cast<int64_t>(std::round(price * 100.0)); 
            uint32_t tickQty = static_cast<uint32_t>(std::round(qty * 10000.0)); 
            if(tickQty == 0) tickQty = 1;

            // Store in buffer
            batch[batchIndex++] = {internalId, tickPrice, tickQty, side};

            // Flush buffer to disk when full
            if (batchIndex == BATCH_SIZE) {
                outBin.write(reinterpret_cast<const char*>(batch), BATCH_SIZE * sizeof(RawOrder));
                batchIndex = 0;
            }

            count++;
            if (count % 10000000 == 0) {
                std::cout << "Processed " << count / 1000000 << " Million orders...\r" << std::flush;
            }
        } catch (...) { continue; }
    }

    // Flush remaining buffer
    if (batchIndex > 0) {
        outBin.write(reinterpret_cast<const char*>(batch), batchIndex * sizeof(RawOrder));
    }

    delete[] batch;
    file.close();
    outBin.close();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "\n--- COMPILATION COMPLETE ---\n";
    std::cout << "Clean Orders: " << count << "\n";
    std::cout << "Parsing Time: " << elapsed.count() << " seconds\n";
    std::cout << "Binary file ready for MotorHFT: " << outputFile << "\n";

    return 0;
}